/*
 * TLS interface functions and an internal TLS implementation
 * Copyright (c) 2004-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * This file interface functions for hostapd/wpa_supplicant to use the
 * integrated TLSv1 implementation.
 */

#include "includes.h"

#include "common.h"
#include "tls.h"
#include "tls/tlsv1_client.h"
#include "tls/tlsv1_server.h"


static int tls_ref_count = 0;

struct tls_global {
	int server;
	struct tlsv1_credentials *server_cred;
	int check_crl;

	void (*event_cb)(void *ctx, enum tls_event ev,
			 union tls_event_data *data);
	void *cb_ctx;
	int cert_in_cb;
};

struct tls_connection {
	struct tlsv1_client *client;
	struct tlsv1_server *server;
	struct tls_global *global;
};


void * tls_init(const struct tls_config *conf)
{
	struct tls_global *global;

	if (tls_ref_count == 0) {
#ifdef CONFIG_TLS_INTERNAL_CLIENT
		if (tlsv1_client_global_init())
			return NULL;
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
		if (tlsv1_server_global_init())
			return NULL;
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	}
	tls_ref_count++;

	global = os_zalloc(sizeof(*global));
	if (global == NULL)
		return NULL;
	if (conf) {
		global->event_cb = conf->event_cb;
		global->cb_ctx = conf->cb_ctx;
		global->cert_in_cb = conf->cert_in_cb;
	}

	return global;
}

void tls_deinit(void *ssl_ctx)
{
	struct tls_global *global = ssl_ctx;
	tls_ref_count--;
	if (tls_ref_count == 0) {
#ifdef CONFIG_TLS_INTERNAL_CLIENT
		tlsv1_client_global_deinit();
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
		tlsv1_server_global_deinit();
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	}
#ifdef CONFIG_TLS_INTERNAL_SERVER
	tlsv1_cred_free(global->server_cred);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	os_free(global);
}


int tls_get_errors(void *tls_ctx)
{
	return 0;
}


struct tls_connection * tls_connection_init(void *tls_ctx)
{
	struct tls_connection *conn;
	struct tls_global *global = tls_ctx;

	conn = os_zalloc(sizeof(*conn));
	if (conn == NULL)
		return NULL;
	conn->global = global;

#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (!global->server) {
		conn->client = tlsv1_client_init();
		if (conn->client == NULL) {
			os_free(conn);
			return NULL;
		}
		tlsv1_client_set_cb(conn->client, global->event_cb,
				    global->cb_ctx, global->cert_in_cb);
	}
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (global->server) {
		conn->server = tlsv1_server_init(global->server_cred);
		if (conn->server == NULL) {
			os_free(conn);
			return NULL;
		}
	}
#endif /* CONFIG_TLS_INTERNAL_SERVER */

	return conn;
}


#ifdef CONFIG_TESTING_OPTIONS
#ifdef CONFIG_TLS_INTERNAL_SERVER
void tls_connection_set_test_flags(struct tls_connection *conn, u32 flags)
{
	if (conn->server)
		tlsv1_server_set_test_flags(conn->server, flags);
}
#endif /* CONFIG_TLS_INTERNAL_SERVER */
#endif /* CONFIG_TESTING_OPTIONS */


void tls_connection_set_log_cb(struct tls_connection *conn,
			       void (*log_cb)(void *ctx, const char *msg),
			       void *ctx)
{
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server)
		tlsv1_server_set_log_cb(conn->server, log_cb, ctx);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
}


void tls_connection_deinit(void *tls_ctx, struct tls_connection *conn)
{
	if (conn == NULL)
		return;
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client)
		tlsv1_client_deinit(conn->client);
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server)
		tlsv1_server_deinit(conn->server);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	os_free(conn);
}


int tls_connection_established(void *tls_ctx, struct tls_connection *conn)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client)
		return tlsv1_client_established(conn->client);
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server)
		return tlsv1_server_established(conn->server);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	return 0;
}


char * tls_connection_peer_serial_num(void *tls_ctx,
				      struct tls_connection *conn)
{
	/* TODO */
	return NULL;
}


int tls_connection_shutdown(void *tls_ctx, struct tls_connection *conn)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client)
		return tlsv1_client_shutdown(conn->client);
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server)
		return tlsv1_server_shutdown(conn->server);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	return -1;
}


int tls_connection_set_params(void *tls_ctx, struct tls_connection *conn,
			      const struct tls_connection_params *params)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	struct tlsv1_credentials *cred;

	if (conn->client == NULL)
		return -1;

	if (params->flags & TLS_CONN_EXT_CERT_CHECK) {
		wpa_printf(MSG_INFO,
			   "TLS: tls_ext_cert_check=1 not supported");
		return -1;
	}

	cred = tlsv1_cred_alloc();
	if (cred == NULL)
		return -1;

	if (params->subject_match) {
		wpa_printf(MSG_INFO, "TLS: subject_match not supported");
		tlsv1_cred_free(cred);
		return -1;
	}

	if (params->altsubject_match) {
		wpa_printf(MSG_INFO, "TLS: altsubject_match not supported");
		tlsv1_cred_free(cred);
		return -1;
	}

	if (params->suffix_match) {
		wpa_printf(MSG_INFO, "TLS: suffix_match not supported");
		tlsv1_cred_free(cred);
		return -1;
	}

	if (params->domain_match) {
		wpa_printf(MSG_INFO, "TLS: domain_match not supported");
		tlsv1_cred_free(cred);
		return -1;
	}

	if (params->openssl_ciphers) {
		wpa_printf(MSG_INFO, "TLS: openssl_ciphers not supported");
		tlsv1_cred_free(cred);
		return -1;
	}

	if (tlsv1_set_ca_cert(cred, params->ca_cert,
			      params->ca_cert_blob, params->ca_cert_blob_len,
			      params->ca_path)) {
		wpa_printf(MSG_INFO, "TLS: Failed to configure trusted CA "
			   "certificates");
		tlsv1_cred_free(cred);
		return -1;
	}

	if (tlsv1_set_cert(cred, params->client_cert,
			   params->client_cert_blob,
			   params->client_cert_blob_len)) {
		wpa_printf(MSG_INFO, "TLS: Failed to configure client "
			   "certificate");
		tlsv1_cred_free(cred);
		return -1;
	}

	if (tlsv1_set_private_key(cred, params->private_key,
				  params->private_key_passwd,
				  params->private_key_blob,
				  params->private_key_blob_len)) {
		wpa_printf(MSG_INFO, "TLS: Failed to load private key");
		tlsv1_cred_free(cred);
		return -1;
	}

	if (tlsv1_set_dhparams(cred, params->dh_file, params->dh_blob,
			       params->dh_blob_len)) {
		wpa_printf(MSG_INFO, "TLS: Failed to load DH parameters");
		tlsv1_cred_free(cred);
		return -1;
	}

	if (tlsv1_client_set_cred(conn->client, cred) < 0) {
		tlsv1_cred_free(cred);
		return -1;
	}

	tlsv1_client_set_flags(conn->client, params->flags);

	return 0;
#else /* CONFIG_TLS_INTERNAL_CLIENT */
	return -1;
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
}


int tls_global_set_params(void *tls_ctx,
			  const struct tls_connection_params *params)
{
#ifdef CONFIG_TLS_INTERNAL_SERVER
	struct tls_global *global = tls_ctx;
	struct tlsv1_credentials *cred;

	/* Currently, global parameters are only set when running in server
	 * mode. */
	global->server = 1;
	tlsv1_cred_free(global->server_cred);
	global->server_cred = cred = tlsv1_cred_alloc();
	if (cred == NULL)
		return -1;

	if (tlsv1_set_ca_cert(cred, params->ca_cert, params->ca_cert_blob,
			      params->ca_cert_blob_len, params->ca_path)) {
		wpa_printf(MSG_INFO, "TLS: Failed to configure trusted CA "
			   "certificates");
		return -1;
	}

	if (tlsv1_set_cert(cred, params->client_cert, params->client_cert_blob,
			   params->client_cert_blob_len)) {
		wpa_printf(MSG_INFO, "TLS: Failed to configure server "
			   "certificate");
		return -1;
	}

	if (tlsv1_set_private_key(cred, params->private_key,
				  params->private_key_passwd,
				  params->private_key_blob,
				  params->private_key_blob_len)) {
		wpa_printf(MSG_INFO, "TLS: Failed to load private key");
		return -1;
	}

	if (tlsv1_set_dhparams(cred, params->dh_file, params->dh_blob,
			       params->dh_blob_len)) {
		wpa_printf(MSG_INFO, "TLS: Failed to load DH parameters");
		return -1;
	}

	if (params->ocsp_stapling_response)
		cred->ocsp_stapling_response =
			os_strdup(params->ocsp_stapling_response);
	if (params->ocsp_stapling_response_multi)
		cred->ocsp_stapling_response_multi =
			os_strdup(params->ocsp_stapling_response_multi);

	return 0;
#else /* CONFIG_TLS_INTERNAL_SERVER */
	return -1;
#endif /* CONFIG_TLS_INTERNAL_SERVER */
}


int tls_global_set_verify(void *tls_ctx, int check_crl)
{
	struct tls_global *global = tls_ctx;
	global->check_crl = check_crl;
	return 0;
}


int tls_connection_set_verify(void *tls_ctx, struct tls_connection *conn,
			      int verify_peer, unsigned int flags,
			      const u8 *session_ctx, size_t session_ctx_len)
{
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server)
		return tlsv1_server_set_verify(conn->server, verify_peer);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	return -1;
}


int tls_connection_get_random(void *tls_ctx, struct tls_connection *conn,
			      struct tls_random *data)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client)
		return tlsv1_client_get_random(conn->client, data);
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server)
		return tlsv1_server_get_random(conn->server, data);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	return -1;
}


static int tls_get_keyblock_size(struct tls_connection *conn)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client)
		return tlsv1_client_get_keyblock_size(conn->client);
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server)
		return tlsv1_server_get_keyblock_size(conn->server);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	return -1;
}


static int tls_connection_prf(void *tls_ctx, struct tls_connection *conn,
			      const char *label, int server_random_first,
			      int skip_keyblock, u8 *out, size_t out_len)
{
	int ret = -1, skip = 0;
	u8 *tmp_out = NULL;
	u8 *_out = out;

	if (skip_keyblock) {
		skip = tls_get_keyblock_size(conn);
		if (skip < 0)
			return -1;
		tmp_out = os_malloc(skip + out_len);
		if (!tmp_out)
			return -1;
		_out = tmp_out;
	}

#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client) {
		ret = tlsv1_client_prf(conn->client, label,
				       server_random_first,
				       _out, skip + out_len);
	}
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server) {
		ret = tlsv1_server_prf(conn->server, label,
				       server_random_first,
				       _out, skip + out_len);
	}
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	if (ret == 0 && skip_keyblock)
		os_memcpy(out, _out + skip, out_len);
	bin_clear_free(tmp_out, skip);

	return ret;
}


int tls_connection_export_key(void *tls_ctx, struct tls_connection *conn,
			      const char *label, u8 *out, size_t out_len)
{
	return tls_connection_prf(tls_ctx, conn, label, 0, 0, out, out_len);
}


int tls_connection_get_eap_fast_key(void *tls_ctx, struct tls_connection *conn,
				    u8 *out, size_t out_len)
{
	return tls_connection_prf(tls_ctx, conn, "key expansion", 1, 1, out,
				  out_len);
}


struct wpabuf * tls_connection_handshake(void *tls_ctx,
					 struct tls_connection *conn,
					 const struct wpabuf *in_data,
					 struct wpabuf **appl_data)
{
	return tls_connection_handshake2(tls_ctx, conn, in_data, appl_data,
					 NULL);
}


struct wpabuf * tls_connection_handshake2(void *tls_ctx,
					  struct tls_connection *conn,
					  const struct wpabuf *in_data,
					  struct wpabuf **appl_data,
					  int *need_more_data)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	u8 *res, *ad;
	size_t res_len, ad_len;
	struct wpabuf *out;

	if (conn->client == NULL)
		return NULL;

	ad = NULL;
	res = tlsv1_client_handshake(conn->client,
				     in_data ? wpabuf_head(in_data) : NULL,
				     in_data ? wpabuf_len(in_data) : 0,
				     &res_len, &ad, &ad_len, need_more_data);
	if (res == NULL)
		return NULL;
	out = wpabuf_alloc_ext_data(res, res_len);
	if (out == NULL) {
		os_free(res);
		os_free(ad);
		return NULL;
	}
	if (appl_data) {
		if (ad) {
			*appl_data = wpabuf_alloc_ext_data(ad, ad_len);
			if (*appl_data == NULL)
				os_free(ad);
		} else
			*appl_data = NULL;
	} else
		os_free(ad);

	return out;
#else /* CONFIG_TLS_INTERNAL_CLIENT */
	return NULL;
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
}


struct wpabuf * tls_connection_server_handshake(void *tls_ctx,
						struct tls_connection *conn,
						const struct wpabuf *in_data,
						struct wpabuf **appl_data)
{
#ifdef CONFIG_TLS_INTERNAL_SERVER
	u8 *res;
	size_t res_len;
	struct wpabuf *out;

	if (conn->server == NULL)
		return NULL;

	if (appl_data)
		*appl_data = NULL;

	res = tlsv1_server_handshake(conn->server, wpabuf_head(in_data),
				     wpabuf_len(in_data), &res_len);
	if (res == NULL && tlsv1_server_established(conn->server))
		return wpabuf_alloc(0);
	if (res == NULL)
		return NULL;
	out = wpabuf_alloc_ext_data(res, res_len);
	if (out == NULL) {
		os_free(res);
		return NULL;
	}

	return out;
#else /* CONFIG_TLS_INTERNAL_SERVER */
	return NULL;
#endif /* CONFIG_TLS_INTERNAL_SERVER */
}


struct wpabuf * tls_connection_encrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client) {
		struct wpabuf *buf;
		int res;
		buf = wpabuf_alloc(wpabuf_len(in_data) + 300);
		if (buf == NULL)
			return NULL;
		res = tlsv1_client_encrypt(conn->client, wpabuf_head(in_data),
					   wpabuf_len(in_data),
					   wpabuf_mhead(buf),
					   wpabuf_size(buf));
		if (res < 0) {
			wpabuf_free(buf);
			return NULL;
		}
		wpabuf_put(buf, res);
		return buf;
	}
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server) {
		struct wpabuf *buf;
		int res;
		buf = wpabuf_alloc(wpabuf_len(in_data) + 300);
		if (buf == NULL)
			return NULL;
		res = tlsv1_server_encrypt(conn->server, wpabuf_head(in_data),
					   wpabuf_len(in_data),
					   wpabuf_mhead(buf),
					   wpabuf_size(buf));
		if (res < 0) {
			wpabuf_free(buf);
			return NULL;
		}
		wpabuf_put(buf, res);
		return buf;
	}
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	return NULL;
}


struct wpabuf * tls_connection_decrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	return tls_connection_decrypt2(tls_ctx, conn, in_data, NULL);
}


struct wpabuf * tls_connection_decrypt2(void *tls_ctx,
					struct tls_connection *conn,
					const struct wpabuf *in_data,
					int *need_more_data)
{
	if (need_more_data)
		*need_more_data = 0;

#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client) {
		return tlsv1_client_decrypt(conn->client, wpabuf_head(in_data),
					    wpabuf_len(in_data),
					    need_more_data);
	}
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server) {
		struct wpabuf *buf;
		int res;
		buf = wpabuf_alloc((wpabuf_len(in_data) + 500) * 3);
		if (buf == NULL)
			return NULL;
		res = tlsv1_server_decrypt(conn->server, wpabuf_head(in_data),
					   wpabuf_len(in_data),
					   wpabuf_mhead(buf),
					   wpabuf_size(buf));
		if (res < 0) {
			wpabuf_free(buf);
			return NULL;
		}
		wpabuf_put(buf, res);
		return buf;
	}
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	return NULL;
}


int tls_connection_resumed(void *tls_ctx, struct tls_connection *conn)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client)
		return tlsv1_client_resumed(conn->client);
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server)
		return tlsv1_server_resumed(conn->server);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	return -1;
}


int tls_connection_set_cipher_list(void *tls_ctx, struct tls_connection *conn,
				   u8 *ciphers)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client)
		return tlsv1_client_set_cipher_list(conn->client, ciphers);
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server)
		return tlsv1_server_set_cipher_list(conn->server, ciphers);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	return -1;
}


int tls_get_version(void *ssl_ctx, struct tls_connection *conn,
		    char *buf, size_t buflen)
{
	if (conn == NULL)
		return -1;
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client)
		return tlsv1_client_get_version(conn->client, buf, buflen);
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
	return -1;
}


int tls_get_cipher(void *tls_ctx, struct tls_connection *conn,
		   char *buf, size_t buflen)
{
	if (conn == NULL)
		return -1;
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client)
		return tlsv1_client_get_cipher(conn->client, buf, buflen);
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server)
		return tlsv1_server_get_cipher(conn->server, buf, buflen);
#endif /* CONFIG_TLS_INTERNAL_SERVER */
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
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client) {
		return tlsv1_client_hello_ext(conn->client, ext_type,
					      data, data_len);
	}
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
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


int tls_connection_set_session_ticket_cb(void *tls_ctx,
					 struct tls_connection *conn,
					 tls_session_ticket_cb cb,
					 void *ctx)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
	if (conn->client) {
		tlsv1_client_set_session_ticket_cb(conn->client, cb, ctx);
		return 0;
	}
#endif /* CONFIG_TLS_INTERNAL_CLIENT */
#ifdef CONFIG_TLS_INTERNAL_SERVER
	if (conn->server) {
		tlsv1_server_set_session_ticket_cb(conn->server, cb, ctx);
		return 0;
	}
#endif /* CONFIG_TLS_INTERNAL_SERVER */
	return -1;
}


int tls_get_library_version(char *buf, size_t buf_len)
{
	return os_snprintf(buf, buf_len, "internal");
}


void tls_connection_set_success_data(struct tls_connection *conn,
				     struct wpabuf *data)
{
}


void tls_connection_set_success_data_resumed(struct tls_connection *conn)
{
}


const struct wpabuf *
tls_connection_get_success_data(struct tls_connection *conn)
{
	return NULL;
}


void tls_connection_remove_session(struct tls_connection *conn)
{
}
