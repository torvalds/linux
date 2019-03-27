/*
 * HTTP wrapper
 * Copyright (c) 2012-2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H

struct http_ctx;

struct http_othername {
	char *oid;
	u8 *data;
	size_t len;
};

#define HTTP_MAX_CERT_LOGO_HASH 32

struct http_logo {
	char *alg_oid;
	u8 *hash;
	size_t hash_len;
	char *uri;
};

struct http_cert {
	char **dnsname;
	unsigned int num_dnsname;
	struct http_othername *othername;
	unsigned int num_othername;
	struct http_logo *logo;
	unsigned int num_logo;
};

int soap_init_client(struct http_ctx *ctx, const char *address,
		     const char *ca_fname, const char *username,
		     const char *password, const char *client_cert,
		     const char *client_key);
int soap_reinit_client(struct http_ctx *ctx);
xml_node_t * soap_send_receive(struct http_ctx *ctx, xml_node_t *node);

struct http_ctx * http_init_ctx(void *upper_ctx, struct xml_node_ctx *xml_ctx);
void http_ocsp_set(struct http_ctx *ctx, int val);
void http_deinit_ctx(struct http_ctx *ctx);

int http_download_file(struct http_ctx *ctx, const char *url,
		       const char *fname, const char *ca_fname);
char * http_post(struct http_ctx *ctx, const char *url, const char *data,
		 const char *content_type, const char *ext_hdr,
		 const char *ca_fname,
		 const char *username, const char *password,
		 const char *client_cert, const char *client_key,
		 size_t *resp_len);
void http_set_cert_cb(struct http_ctx *ctx,
		      int (*cb)(void *ctx, struct http_cert *cert),
		      void *cb_ctx);
const char * http_get_err(struct http_ctx *ctx);
void http_parse_x509_certificate(struct http_ctx *ctx, const char *fname);

#endif /* HTTP_UTILS_H */
