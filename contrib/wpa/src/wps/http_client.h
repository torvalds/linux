/*
 * http_client - HTTP client
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

struct http_client;

enum http_client_event {
	HTTP_CLIENT_FAILED,
	HTTP_CLIENT_TIMEOUT,
	HTTP_CLIENT_OK,
	HTTP_CLIENT_INVALID_REPLY,
};

char * http_client_url_parse(const char *url, struct sockaddr_in *dst,
			     char **path);
struct http_client * http_client_addr(struct sockaddr_in *dst,
				      struct wpabuf *req, size_t max_response,
				      void (*cb)(void *ctx,
						 struct http_client *c,
						 enum http_client_event event),
				      void *cb_ctx);
struct http_client * http_client_url(const char *url,
				     struct wpabuf *req, size_t max_response,
				     void (*cb)(void *ctx,
						struct http_client *c,
						enum http_client_event event),
				     void *cb_ctx);
void http_client_free(struct http_client *c);
struct wpabuf * http_client_get_body(struct http_client *c);
char * http_client_get_hdr_line(struct http_client *c, const char *tag);
char * http_link_update(char *url, const char *base);

#endif /* HTTP_CLIENT_H */
