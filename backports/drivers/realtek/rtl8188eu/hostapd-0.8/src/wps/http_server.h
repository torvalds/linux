/*
 * http_server - HTTP server
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

struct http_server;
struct http_request;

void http_request_deinit(struct http_request *req);
void http_request_send(struct http_request *req, struct wpabuf *resp);
void http_request_send_and_deinit(struct http_request *req,
				  struct wpabuf *resp);
enum httpread_hdr_type http_request_get_type(struct http_request *req);
char * http_request_get_uri(struct http_request *req);
char * http_request_get_hdr(struct http_request *req);
char * http_request_get_data(struct http_request *req);
char * http_request_get_hdr_line(struct http_request *req, const char *tag);
struct sockaddr_in * http_request_get_cli_addr(struct http_request *req);

struct http_server * http_server_init(struct in_addr *addr, int port,
				      void (*cb)(void *ctx,
						 struct http_request *req),
				      void *cb_ctx);
void http_server_deinit(struct http_server *srv);
int http_server_get_port(struct http_server *srv);

#endif /* HTTP_SERVER_H */
