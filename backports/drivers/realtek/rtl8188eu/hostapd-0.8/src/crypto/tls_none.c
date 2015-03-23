/*
 * SSL/TLS interface functions for no TLS case
 * Copyright (c) 2004-2009, Jouni Malinen <j@w1.fi>
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

#include "includes.h"

#include "common.h"
#include "tls.h"

void * tls_init(const struct tls_config *conf)
{
	return (void *) 1;
}


void tls_deinit(void *ssl_ctx)
{
}


int tls_get_errors(void *tls_ctx)
{
	return 0;
}


struct tls_connection * tls_connection_init(void *tls_ctx)
{
	return NULL;
}


void tls_connection_deinit(void *tls_ctx, struct tls_connection *conn)
{
}


int tls_connection_established(void *tls_ctx, struct tls_connection *conn)
{
	return -1;
}


int tls_connection_shutdown(void *tls_ctx, struct tls_connection *conn)
{
	return -1;
}


int tls_connection_set_params(void *tls_ctx, struct tls_connection *conn,
			      const struct tls_connection_params *params)
{
	return -1;
}


int tls_global_set_params(void *tls_ctx,
			  const struct tls_connection_params *params)
{
	return -1;
}


int tls_global_set_verify(void *tls_ctx, int check_crl)
{
	return -1;
}


int tls_connection_set_verify(void *tls_ctx, struct tls_connection *conn,
			      int verify_peer)
{
	return -1;
}


int tls_connection_set_ia(void *tls_ctx, struct tls_connection *conn,
			  int tls_ia)
{
	return -1;
}


int tls_connection_get_keys(void *tls_ctx, struct tls_connection *conn,
			    struct tls_keys *keys)
{
	return -1;
}


int tls_connection_prf(void *tls_ctx, struct tls_connection *conn,
		       const char *label, int server_random_first,
		       u8 *out, size_t out_len)
{
	return -1;
}


struct wpabuf * tls_connection_handshake(void *tls_ctx,
					 struct tls_connection *conn,
					 const struct wpabuf *in_data,
					 struct wpabuf **appl_data)
{
	return NULL;
}


struct wpabuf * tls_connection_server_handshake(void *tls_ctx,
						struct tls_connection *conn,
						const struct wpabuf *in_data,
						struct wpabuf **appl_data)
{
	return NULL;
}


struct wpabuf * tls_connection_encrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	return NULL;
}


struct wpabuf * tls_connection_decrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	return NULL;
}


int tls_connection_resumed(void *tls_ctx, struct tls_connection *conn)
{
	return 0;
}


int tls_connection_set_cipher_list(void *tls_ctx, struct tls_connection *conn,
				   u8 *ciphers)
{
	return -1;
}


int tls_get_cipher(void *tls_ctx, struct tls_connection *conn,
		   char *buf, size_t buflen)
{
	return -1;
}


int tls_connection_enable_workaround(void *tls_ctx,
				     struct tls_connection *conn)
{
	return -1;
}


int tls_connection_client_hello_ext(void *tls_ctx, struct tls_connection *conn,
				    int ext_type, const u8 *data,
				    size_t data_len)
{
	return -1;
}


int tls_connection_get_failed(void *tls_ctx, struct tls_connection *conn)
{
	return 0;
}


int tls_connection_get_read_alerts(void *tls_ctx, struct tls_connection *conn)
{
	return 0;
}


int tls_connection_get_write_alerts(void *tls_ctx,
				    struct tls_connection *conn)
{
	return 0;
}


int tls_connection_get_keyblock_size(void *tls_ctx,
				     struct tls_connection *conn)
{
	return -1;
}


unsigned int tls_capabilities(void *tls_ctx)
{
	return 0;
}


struct wpabuf * tls_connection_ia_send_phase_finished(
	void *tls_ctx, struct tls_connection *conn, int final)
{
	return NULL;
}


int tls_connection_ia_final_phase_finished(void *tls_ctx,
					   struct tls_connection *conn)
{
	return -1;
}


int tls_connection_ia_permute_inner_secret(void *tls_ctx,
					   struct tls_connection *conn,
					   const u8 *key, size_t key_len)
{
	return -1;
}
