#include "uwsgi.h"

extern struct uwsgi_server uwsgi;

static size_t get_content_length(char *buf, uint16_t size) {
	int i;
	size_t val = 0;
	for(i=0;i<size;i++) {
		if (buf[i] >= '0' && buf[i] <= '9') {
			val = (val*10) + (buf[i] - '0');
			continue;
		}
		break;
	}

	return val;
}

#ifdef UWSGI_UDP
ssize_t send_udp_message(uint8_t modifier1, char *host, char *message, uint16_t message_size) {

	int fd;
	struct sockaddr_in udp_addr;
	char *udp_port;
	ssize_t ret;
	char udpbuff[1024];

	if (message_size + 4 > 1024)
		return -1;

	udp_port = strchr(host, ':');
	if (udp_port == NULL) {
		return -1;
	}

	udp_port[0] = 0; 

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		uwsgi_error("socket()");
		return -1;
	}

	memset(&udp_addr, 0, sizeof(struct sockaddr_in));
	udp_addr.sin_family = AF_INET;
	udp_addr.sin_port = htons(atoi(udp_port));
	udp_addr.sin_addr.s_addr = inet_addr(host);

	udpbuff[0] = modifier1;
#ifdef __BIG_ENDIAN__
	message_size = uwsgi_swap16(message_size);
#endif

	memcpy(udpbuff+1, &message_size, 2);

	udpbuff[3] = 0;

#ifdef __BIG_ENDIAN__
	message_size = uwsgi_swap16(message_size);
#endif

	memcpy(udpbuff+4, message, message_size);

	ret = sendto(fd, udpbuff, message_size+4, 0, (struct sockaddr *) &udp_addr, sizeof(udp_addr));
	if (ret < 0) {
		uwsgi_error("sendto()");
	}
	close(fd);

	return ret;
	
}
#endif

int uwsgi_enqueue_message(char *host, int port, uint8_t modifier1, uint8_t modifier2, char *message, int size, int timeout) {

	struct pollfd uwsgi_poll;
	struct sockaddr_in uws_addr;
	int cnt;
	struct uwsgi_header uh;

	if (!timeout)
		timeout = 1;

	if (size > 0xFFFF) {
		uwsgi_log( "invalid object (marshalled) size\n");
		return -1;
	}

	uwsgi_poll.fd = socket(AF_INET, SOCK_STREAM, 0);
	if (uwsgi_poll.fd < 0) {
		uwsgi_error("socket()");
		return -1;
	}

	memset(&uws_addr, 0, sizeof(struct sockaddr_in));
	uws_addr.sin_family = AF_INET;
	uws_addr.sin_port = htons(port);
	uws_addr.sin_addr.s_addr = inet_addr(host);

	uwsgi_poll.events = POLLIN;

	if (timed_connect(&uwsgi_poll, (const struct sockaddr *) &uws_addr, sizeof(struct sockaddr_in), timeout)) {
		uwsgi_error("connect()");
		close(uwsgi_poll.fd);
		return -1;
	}

	uh.modifier1 = modifier1;
	uh.pktsize = (uint16_t) size;
	uh.modifier2 = modifier2;

	cnt = write(uwsgi_poll.fd, &uh, 4);
	if (cnt != 4) {
		uwsgi_error("write()");
		close(uwsgi_poll.fd);
		return -1;
	}

	cnt = write(uwsgi_poll.fd, message, size);
	if (cnt != size) {
		uwsgi_error("write()");
		close(uwsgi_poll.fd);
		return -1;
	}

	return uwsgi_poll.fd;
}

/*
PyObject *uwsgi_send_message(const char *host, int port, uint8_t modifier1, uint8_t modifier2, char *message, int size, int timeout) {

	struct pollfd uwsgi_mpoll;
	struct sockaddr_in uws_addr;
	int cnt;
	struct uwsgi_header uh;
	char buffer[0xFFFF];



	if (!timeout)
		timeout = 1;

	if (size > 0xFFFF) {
		uwsgi_log( "invalid object (marshalled) size\n");
		Py_INCREF(Py_None);
		return Py_None;
	}

	uwsgi_mpoll.events = POLLIN;

	uwsgi_mpoll.fd = socket(AF_INET, SOCK_STREAM, 0);
	if (uwsgi_mpoll.fd < 0) {
		uwsgi_error("socket()");
		Py_INCREF(Py_None);
		return Py_None;
	}

	memset(&uws_addr, 0, sizeof(struct sockaddr_in));
	uws_addr.sin_family = AF_INET;
	uws_addr.sin_port = htons(port);
	uws_addr.sin_addr.s_addr = inet_addr(host);

	UWSGI_SET_BLOCKING;

	if (timed_connect(&uwsgi_mpoll, (const struct sockaddr *) &uws_addr, sizeof(struct sockaddr_in), timeout)) {
		uwsgi_error("connect()");
		close(uwsgi_mpoll.fd);
		Py_INCREF(Py_None);
		return Py_None;
	}

	uh.modifier1 = modifier1;
	uh.pktsize = (uint16_t) size;
	uh.modifier2 = modifier2;

	cnt = write(uwsgi_mpoll.fd, &uh, 4);
	if (cnt != 4) {
		uwsgi_error("write()");
		close(uwsgi_mpoll.fd);
		Py_INCREF(Py_None);
		return Py_None;
	}

	cnt = write(uwsgi_mpoll.fd, message, size);
	if (cnt != size) {
		uwsgi_error("write()");
		close(uwsgi_mpoll.fd);
		Py_INCREF(Py_None);
		return Py_None;
	}


	if (!uwsgi_parse_response(&uwsgi_mpoll, timeout, &uh, buffer)) {
		UWSGI_UNSET_BLOCKING;
		Py_INCREF(Py_None);
		return Py_None;
	}

	UWSGI_UNSET_BLOCKING;

	close(uwsgi_mpoll.fd);

	if (uh.modifier1 == UWSGI_MODIFIER_RESPONSE) {
		if (!uh.modifier2) {
			Py_INCREF(Py_None);
			return Py_None;
		}
		else {
			Py_INCREF(Py_True);
			return Py_True;
		}
	}

	return PyMarshal_ReadObjectFromString(buffer, uh.pktsize);
}

*/

int uwsgi_parse_response(struct pollfd *upoll, int timeout, struct uwsgi_header *uh, char *buffer) {
	int rlen, i;

	if (!timeout)
		timeout = 1;
	/* first 4 byte header */
	rlen = poll(upoll, 1, timeout * 1000);
	if (rlen < 0) {
		uwsgi_error("poll()");
		exit(1);
	}
	else if (rlen == 0) {
		uwsgi_log( "timeout. skip request\n");
		close(upoll->fd);
		return 0;
	}
	rlen = read(upoll->fd, uh, 4);
	if (rlen > 0 && rlen < 4) {
		i = rlen;
		while (i < 4) {
			rlen = poll(upoll, 1, timeout * 1000);
			if (rlen < 0) {
				uwsgi_error("poll()");
				exit(1);
			}
			else if (rlen == 0) {
				uwsgi_log( "timeout waiting for header. skip request.\n");
				close(upoll->fd);
				break;
			}
			rlen = read(upoll->fd, (char *) (uh) + i, 4 - i);
			if (rlen <= 0) {
				uwsgi_log( "broken header. skip request.\n");
				close(upoll->fd);
				break;
			}
			i += rlen;
		}
		if (i < 4) {
			return 0;
		}
	}
	else if (rlen <= 0) {
		uwsgi_log( "invalid request header size: %d...skip\n", rlen);
		close(upoll->fd);
		return 0;
	}
	/* big endian ? */
#ifdef __BIG_ENDIAN__
	uh->pktsize = uwsgi_swap16(uh->pktsize);
#endif

#ifdef UWSGI_DEBUG
	uwsgi_debug("uwsgi payload size: %d (0x%X) modifier1: %d modifier2: %d\n", uh->pktsize, uh->pktsize, uh->modifier1, uh->modifier2);
#endif

	/* check for max buffer size */
	if (uh->pktsize > uwsgi.buffer_size) {
		uwsgi_log( "invalid request block size: %d...skip\n", uh->pktsize);
		close(upoll->fd);
		return 0;
	}

	//uwsgi_log("ready for reading %d bytes\n", wsgi_req.size);

	i = 0;
	while (i < uh->pktsize) {
		rlen = poll(upoll, 1, timeout * 1000);
		if (rlen < 0) {
			uwsgi_error("poll()");
			exit(1);
		}
		else if (rlen == 0) {
			uwsgi_log( "timeout. skip request. (expecting %d bytes, got %d)\n", uh->pktsize, i);
			close(upoll->fd);
			break;
		}
		rlen = read(upoll->fd, buffer + i, uh->pktsize - i);
		if (rlen <= 0) {
			uwsgi_log( "broken vars. skip request.\n");
			close(upoll->fd);
			break;
		}
		i += rlen;
	}


	if (i < uh->pktsize) {
		return 0;
	}

	return 1;
}

int uwsgi_parse_vars(struct wsgi_request *wsgi_req) {

	char *buffer = wsgi_req->buffer;

	char *ptrbuf, *bufferend;

	uint16_t strsize = 0;

	ptrbuf = buffer;
	bufferend = ptrbuf + wsgi_req->uh.pktsize;
	int i, script_name= -1, path_info= -1;

	/* set an HTTP 500 status as default */
	wsgi_req->status = 500;

	while (ptrbuf < bufferend) {
		if (ptrbuf + 2 < bufferend) {
			memcpy(&strsize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
			strsize = uwsgi_swap16(strsize);
#endif
			/* key cannot be null */
                        if (!strsize) {
                                uwsgi_log( "uwsgi key cannot be null. skip this request.\n");
                                return -1;
                        }
			
			ptrbuf += 2;
			if (ptrbuf + strsize < bufferend) {
				// var key
				wsgi_req->hvec[wsgi_req->var_cnt].iov_base = ptrbuf;
				wsgi_req->hvec[wsgi_req->var_cnt].iov_len = strsize;
				ptrbuf += strsize;
				// value can be null (even at the end) so use <=
				if (ptrbuf + 2 <= bufferend) {
					memcpy(&strsize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
					strsize = uwsgi_swap16(strsize);
#endif
					ptrbuf += 2;
					if (ptrbuf + strsize <= bufferend) {
						//uwsgi_log("uwsgi %.*s = %.*s\n", wsgi_req->hvec[wsgi_req->var_cnt].iov_len, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, strsize, ptrbuf);
						if (!uwsgi_strncmp("SCRIPT_NAME", 11, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->script_name = ptrbuf;
							wsgi_req->script_name_len = strsize;
							script_name = wsgi_req->var_cnt + 1;
#ifdef UWSGI_DEBUG
							uwsgi_debug("SCRIPT_NAME=%.*s\n", wsgi_req->script_name_len, wsgi_req->script_name);
#endif
						}
						else if (!uwsgi_strncmp("PATH_INFO", 9, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->path_info = ptrbuf;
							wsgi_req->path_info_len = strsize;
							path_info = wsgi_req->var_cnt + 1;
#ifdef UWSGI_DEBUG
							uwsgi_debug("PATH_INFO=%.*s\n", wsgi_req->path_info_len, wsgi_req->path_info);
#endif
						}
						else if (!uwsgi_strncmp("SERVER_PROTOCOL", 15, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->protocol = ptrbuf;
							wsgi_req->protocol_len = strsize;
						}
						else if (!uwsgi_strncmp("REQUEST_URI", 11, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->uri = ptrbuf;
							wsgi_req->uri_len = strsize;
						}
						else if (!uwsgi_strncmp("QUERY_STRING", 12, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->query_string = ptrbuf;
							wsgi_req->query_string_len = strsize;
						}
						else if (!uwsgi_strncmp("REQUEST_METHOD", 14, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->method = ptrbuf;
							wsgi_req->method_len = strsize;
						}
						else if (!uwsgi_strncmp("REMOTE_ADDR", 11, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->remote_addr = ptrbuf;
							wsgi_req->remote_addr_len = strsize;
						}
						else if (!uwsgi_strncmp("REMOTE_USER", 11, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->remote_user = ptrbuf;
							wsgi_req->remote_user_len = strsize;
						}
						else if (!uwsgi_strncmp("UWSGI_SCHEME", 12, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->scheme = ptrbuf;
							wsgi_req->scheme_len = strsize;
						}
						else if (!uwsgi_strncmp("UWSGI_SCRIPT", 12, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len )) {
							wsgi_req->script = ptrbuf;
							wsgi_req->script_len = strsize;
						}
						else if (!uwsgi_strncmp("UWSGI_MODULE", 12, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->module = ptrbuf;
							wsgi_req->module_len = strsize;
						}
						else if (!uwsgi_strncmp("UWSGI_CALLABLE", 14, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->callable = ptrbuf;
							wsgi_req->callable_len = strsize;
						}
						else if (!uwsgi_strncmp("UWSGI_PYHOME", 12, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->pyhome = ptrbuf;
							wsgi_req->pyhome_len = strsize;
						}
						else if (!uwsgi_strncmp("UWSGI_CHDIR", 11, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->chdir = ptrbuf;
							wsgi_req->chdir_len = strsize;
						}
						else if (!uwsgi_strncmp("SERVER_NAME", 11, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len) && !uwsgi.vhost_host) {
							wsgi_req->host = ptrbuf;
							wsgi_req->host_len = strsize;
#ifdef UWSGI_DEBUG
							uwsgi_debug("SERVER_NAME=%.*s\n", wsgi_req->host_len, wsgi_req->host);
#endif
						}
						else if (!uwsgi_strncmp("HTTP_HOST", 9, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len) && uwsgi.vhost_host) {
							wsgi_req->host = ptrbuf;
							wsgi_req->host_len = strsize;
#ifdef UWSGI_DEBUG
							uwsgi_debug("HTTP_HOST=%.*s\n", wsgi_req->host_len, wsgi_req->host);
#endif
						}
						else if (!uwsgi_strncmp("HTTPS", 5, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->https = ptrbuf;
							wsgi_req->https_len = strsize;
						}
						else if (!uwsgi_strncmp("CONTENT_LENGTH", 14, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len)) {
							wsgi_req->post_cl = get_content_length(ptrbuf, strsize);
						}

						if (wsgi_req->var_cnt < uwsgi.vec_size - (4 + 1)) {
							wsgi_req->var_cnt++;
						}
						else {
							uwsgi_log( "max vec size reached. skip this header.\n");
							return -1;
						}
						// var value
						wsgi_req->hvec[wsgi_req->var_cnt].iov_base = ptrbuf;
						wsgi_req->hvec[wsgi_req->var_cnt].iov_len = strsize;
						//uwsgi_log("%.*s = %.*s\n", wsgi_req->hvec[wsgi_req->var_cnt-1].iov_len, wsgi_req->hvec[wsgi_req->var_cnt-1].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len, wsgi_req->hvec[wsgi_req->var_cnt].iov_base);
						if (wsgi_req->var_cnt < uwsgi.vec_size - (4 + 1)) {
							wsgi_req->var_cnt++;
						}
						else {
							uwsgi_log( "max vec size reached. skip this header.\n");
							return -1;
						}
						ptrbuf += strsize;
					}
					else {
						return -1;
					}
				}
				else {
					return -1;
				}
			}
		}
		else {
			return -1;
		}
	}


	if (uwsgi.manage_script_name) {
		if (uwsgi.apps_cnt > 0 && wsgi_req->path_info_len > 1) {
			// starts with 1 as the 0 app is the default (/) one
			int best_found = 0;
			char *orig_path_info = wsgi_req->path_info;
			int orig_path_info_len = wsgi_req->path_info_len;
			// if SCRIPT_NAME is not allocated, add a slot for it
			if (script_name == -1) {
				if (wsgi_req->var_cnt >= uwsgi.vec_size - (4 + 2)) {
					uwsgi_log( "max vec size reached. skip this header.\n");
                                        return -1;
				}
				wsgi_req->var_cnt++;
				wsgi_req->hvec[wsgi_req->var_cnt].iov_base = "SCRIPT_NAME";
                                wsgi_req->hvec[wsgi_req->var_cnt].iov_len = 11;
				wsgi_req->var_cnt++;
				script_name = wsgi_req->var_cnt;
			}
			for(i=1;i<uwsgi.apps_cnt;i++) {
				uwsgi_log("app mountpoint = %.*s\n", uwsgi.apps[i].mountpoint_len, uwsgi.apps[i].mountpoint);
				if (orig_path_info_len >= uwsgi.apps[i].mountpoint_len) {
					if (!uwsgi_startswith(orig_path_info, uwsgi.apps[i].mountpoint, uwsgi.apps[i].mountpoint_len) && uwsgi.apps[i].mountpoint_len > best_found) {
						best_found = uwsgi.apps[i].mountpoint_len;
						wsgi_req->script_name = uwsgi.apps[i].mountpoint;
						wsgi_req->script_name_len = uwsgi.apps[i].mountpoint_len;
						wsgi_req->path_info = orig_path_info+wsgi_req->script_name_len;
						wsgi_req->path_info_len = orig_path_info_len-wsgi_req->script_name_len;

						wsgi_req->hvec[script_name].iov_base = wsgi_req->script_name;
						wsgi_req->hvec[script_name].iov_len = wsgi_req->script_name_len;

						wsgi_req->hvec[path_info].iov_base = wsgi_req->path_info;
						wsgi_req->hvec[path_info].iov_len = wsgi_req->path_info_len;
						uwsgi_log("managed SCRIPT_NAME = %.*s PATH_INFO = %.*s\n", wsgi_req->script_name_len, wsgi_req->script_name, wsgi_req->path_info_len, wsgi_req->path_info);
					} 
				}
			}
		}
	}

	return 0;
}

int uwsgi_ping_node(int node, struct wsgi_request *wsgi_req) {


	struct pollfd uwsgi_poll;

	struct uwsgi_cluster_node *ucn = &uwsgi.shared->nodes[node];

	if (ucn->name[0] == 0) {
		return 0;
	}

	if (ucn->status == UWSGI_NODE_OK) {
		return 0;
	}

	uwsgi_poll.fd = socket(AF_INET, SOCK_STREAM, 0);
	if (uwsgi_poll.fd < 0) {
		uwsgi_error("socket()");
		return -1;
	}

	if (timed_connect(&uwsgi_poll, (const struct sockaddr *) &ucn->ucn_addr, sizeof(struct sockaddr_in), uwsgi.shared->options[UWSGI_OPTION_SOCKET_TIMEOUT])) {
		close(uwsgi_poll.fd);
		return -1;
	}

	wsgi_req->uh.modifier1 = UWSGI_MODIFIER_PING;
	wsgi_req->uh.pktsize = 0;
	wsgi_req->uh.modifier2 = 0;
	if (write(uwsgi_poll.fd, wsgi_req, 4) != 4) {
		uwsgi_error("write()");
		return -1;
	}

	uwsgi_poll.events = POLLIN;
	if (!uwsgi_parse_response(&uwsgi_poll, uwsgi.shared->options[UWSGI_OPTION_SOCKET_TIMEOUT], (struct uwsgi_header *) wsgi_req, wsgi_req->buffer)) {
		return -1;
	}

	return 0;
}

ssize_t uwsgi_send_empty_pkt(int fd, char *socket_name, uint8_t modifier1, uint8_t modifier2) {

	struct uwsgi_header uh;
	char *port;
	uint16_t s_port;
	struct sockaddr_in uaddr;
	int ret;

	uh.modifier1 = modifier1;
	uh.pktsize = 0;
	uh.modifier2 = modifier2;

	if (socket_name) {
		port = strchr(socket_name, ':');
		if (!port) return -1;
		s_port = atoi(port+1);
		port[0] = 0;
		memset(&uaddr, 0, sizeof(struct sockaddr_in));
		uaddr.sin_family = AF_INET;
		uaddr.sin_addr.s_addr = inet_addr(socket_name);
		uaddr.sin_port = htons(s_port);

		port[0] = ':';

		ret = sendto(fd, &uh, 4, 0, (struct sockaddr *) &uaddr, sizeof(struct sockaddr_in));
	}
	else {
		ret = send(fd, &uh, 4, 0);
	}

	if (ret < 0) {
		uwsgi_error("sendto()");
	}

	return ret;
}

int uwsgi_get_dgram(int fd, struct wsgi_request *wsgi_req) {

	ssize_t rlen;
	struct uwsgi_header *uh;
	static char *buffer = NULL;

	if (!buffer) {
		buffer = malloc(uwsgi.buffer_size + 4);
		if (!buffer) {
			uwsgi_error("malloc()");
			exit(1);
		}
	}
		

	rlen = read(fd, buffer, uwsgi.buffer_size + 4);

        if (rlen < 0) {
                uwsgi_error("read()");
                return -1;
        }

        if (rlen < 4) {
                uwsgi_log("invalid uwsgi packet\n");
                return -1;
        }

	uh = (struct uwsgi_header *) buffer;

	wsgi_req->uh.modifier1 = uh->modifier1;
	wsgi_req->uh.pktsize = uh->pktsize;
	wsgi_req->uh.modifier2 = uh->modifier2;

	if (wsgi_req->uh.pktsize > uwsgi.buffer_size) {
		uwsgi_log("invalid uwsgi packet size, probably you need to increase buffer size\n");
		return -1;
	}

	wsgi_req->buffer = buffer+4;

	uwsgi_log("request received %d %d\n", wsgi_req->uh.modifier1, wsgi_req->uh.modifier2);

	return 0;

}

int uwsgi_hooked_parse_dict_dgram(int fd, char *buffer, size_t len, uint8_t modifier1, uint8_t modifier2, void (*hook)()) {

	struct uwsgi_header *uh;
	ssize_t rlen;

	char *ptrbuf, *bufferend;
        uint16_t keysize = 0, valsize = 0;
	char *key;


	rlen = read(fd, buffer, len);

	if (rlen < 0) {
		uwsgi_error("read()");
		return -1;
	}

	uwsgi_log("RLEN: %d\n", rlen);

	// check for valid dict 4(header) 2(non-zero key)+1 2(value)
	if (rlen < (4+2+1+2)) {
		uwsgi_log("invalid uwsgi dictionary\n");
		return -1;
	}
	
	uh = (struct uwsgi_header *) buffer;

	if (uh->modifier1 != modifier1 || uh->modifier2 != modifier2) {
		uwsgi_log("invalid uwsgi dictionary received, modifier1: %d modifier2: %d\n", uh->modifier1, uh->modifier2);
		return -1;
	}

        ptrbuf = buffer + 4;

	if (uh->pktsize > len) {
		uwsgi_log("* WARNING * the uwsgi dictionary received is too big, data will be truncated\n");
		bufferend = ptrbuf + len;
	}
	else {
        	bufferend = ptrbuf + uh->pktsize;
	}

	
	uwsgi_log("%p %p %d\n", ptrbuf, bufferend, bufferend-ptrbuf);
        while (ptrbuf < bufferend) {
                if (ptrbuf + 2 >= bufferend) return -1;
                memcpy(&keysize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
                keysize = uwsgi_swap16(keysize);
#endif
                /* key cannot be null */
                if (!keysize)  return -1;

                ptrbuf += 2;
                if (ptrbuf + keysize > bufferend) return -1;

                // key
                key = ptrbuf;
                ptrbuf += keysize;
                // value can be null
                if (ptrbuf + 2 > bufferend) return -1;

                memcpy(&valsize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
		valsize = uwsgi_swap16(valsize);
#endif
		ptrbuf += 2;
                if (ptrbuf + valsize > bufferend) return -1;

		// now call the hook
		hook(key, keysize, ptrbuf, valsize);
                ptrbuf += valsize;
	}

	return 0;

}
