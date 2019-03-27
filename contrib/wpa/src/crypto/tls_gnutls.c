/*
 * SSL/TLS interface functions for GnuTLS
 * Copyright (c) 2004-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#ifdef PKCS12_FUNCS
#include <gnutls/pkcs12.h>
#endif /* PKCS12_FUNCS */
#if GNUTLS_VERSION_NUMBER >= 0x030103
#include <gnutls/ocsp.h>
#endif /* 3.1.3 */

#include "common.h"
#include "crypto/crypto.h"
#include "tls.h"


static int tls_gnutls_ref_count = 0;

struct tls_global {
	/* Data for session resumption */
	void *session_data;
	size_t session_data_size;

	int server;

	int params_set;
	gnutls_certificate_credentials_t xcred;

	void (*event_cb)(void *ctx, enum tls_event ev,
			 union tls_event_data *data);
	void *cb_ctx;
	int cert_in_cb;

	char *ocsp_stapling_response;
};

struct tls_connection {
	struct tls_global *global;
	gnutls_session_t session;
	int read_alerts, write_alerts, failed;

	u8 *pre_shared_secret;
	size_t pre_shared_secret_len;
	int established;
	int verify_peer;
	unsigned int disable_time_checks:1;

	struct wpabuf *push_buf;
	struct wpabuf *pull_buf;
	const u8 *pull_buf_offset;

	int params_set;
	gnutls_certificate_credentials_t xcred;

	char *suffix_match;
	char *domain_match;
	unsigned int flags;
};


static int tls_connection_verify_peer(gnutls_session_t session);


static void tls_log_func(int level, const char *msg)
{
	char *s, *pos;
	if (level == 6 || level == 7) {
		/* These levels seem to be mostly I/O debug and msg dumps */
		return;
	}

	s = os_strdup(msg);
	if (s == NULL)
		return;

	pos = s;
	while (*pos != '\0') {
		if (*pos == '\n') {
			*pos = '\0';
			break;
		}
		pos++;
	}
	wpa_printf(level > 3 ? MSG_MSGDUMP : MSG_DEBUG,
		   "gnutls<%d> %s", level, s);
	os_free(s);
}


void * tls_init(const struct tls_config *conf)
{
	struct tls_global *global;

	if (tls_gnutls_ref_count == 0) {
		wpa_printf(MSG_DEBUG,
			   "GnuTLS: Library version %s (runtime) - %s (build)",
			   gnutls_check_version(NULL), GNUTLS_VERSION);
	}

	global = os_zalloc(sizeof(*global));
	if (global == NULL)
		return NULL;

	if (tls_gnutls_ref_count == 0 && gnutls_global_init() < 0) {
		os_free(global);
		return NULL;
	}
	tls_gnutls_ref_count++;

	gnutls_global_set_log_function(tls_log_func);
	if (wpa_debug_show_keys)
		gnutls_global_set_log_level(11);

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
	if (global) {
		if (global->params_set)
			gnutls_certificate_free_credentials(global->xcred);
		os_free(global->session_data);
		os_free(global->ocsp_stapling_response);
		os_free(global);
	}

	tls_gnutls_ref_count--;
	if (tls_gnutls_ref_count == 0)
		gnutls_global_deinit();
}


int tls_get_errors(void *ssl_ctx)
{
	return 0;
}


static ssize_t tls_pull_func(gnutls_transport_ptr_t ptr, void *buf,
			     size_t len)
{
	struct tls_connection *conn = (struct tls_connection *) ptr;
	const u8 *end;
	if (conn->pull_buf == NULL) {
		errno = EWOULDBLOCK;
		return -1;
	}

	end = wpabuf_head_u8(conn->pull_buf) + wpabuf_len(conn->pull_buf);
	if ((size_t) (end - conn->pull_buf_offset) < len)
		len = end - conn->pull_buf_offset;
	os_memcpy(buf, conn->pull_buf_offset, len);
	conn->pull_buf_offset += len;
	if (conn->pull_buf_offset == end) {
		wpa_printf(MSG_DEBUG, "%s - pull_buf consumed", __func__);
		wpabuf_free(conn->pull_buf);
		conn->pull_buf = NULL;
		conn->pull_buf_offset = NULL;
	} else {
		wpa_printf(MSG_DEBUG, "%s - %lu bytes remaining in pull_buf",
			   __func__,
			   (unsigned long) (end - conn->pull_buf_offset));
	}
	return len;
}


static ssize_t tls_push_func(gnutls_transport_ptr_t ptr, const void *buf,
			     size_t len)
{
	struct tls_connection *conn = (struct tls_connection *) ptr;

	if (wpabuf_resize(&conn->push_buf, len) < 0) {
		errno = ENOMEM;
		return -1;
	}
	wpabuf_put_data(conn->push_buf, buf, len);

	return len;
}


static int tls_gnutls_init_session(struct tls_global *global,
				   struct tls_connection *conn)
{
	const char *err;
	int ret;

	ret = gnutls_init(&conn->session,
			  global->server ? GNUTLS_SERVER : GNUTLS_CLIENT);
	if (ret < 0) {
		wpa_printf(MSG_INFO, "TLS: Failed to initialize new TLS "
			   "connection: %s", gnutls_strerror(ret));
		return -1;
	}

	ret = gnutls_set_default_priority(conn->session);
	if (ret < 0)
		goto fail;

	ret = gnutls_priority_set_direct(conn->session, "NORMAL:-VERS-SSL3.0",
					 &err);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "GnuTLS: Priority string failure at "
			   "'%s'", err);
		goto fail;
	}

	gnutls_transport_set_pull_function(conn->session, tls_pull_func);
	gnutls_transport_set_push_function(conn->session, tls_push_func);
	gnutls_transport_set_ptr(conn->session, (gnutls_transport_ptr_t) conn);
	gnutls_session_set_ptr(conn->session, conn);

	return 0;

fail:
	wpa_printf(MSG_INFO, "TLS: Failed to setup new TLS connection: %s",
		   gnutls_strerror(ret));
	gnutls_deinit(conn->session);
	return -1;
}


struct tls_connection * tls_connection_init(void *ssl_ctx)
{
	struct tls_global *global = ssl_ctx;
	struct tls_connection *conn;
	int ret;

	conn = os_zalloc(sizeof(*conn));
	if (conn == NULL)
		return NULL;
	conn->global = global;

	if (tls_gnutls_init_session(global, conn)) {
		os_free(conn);
		return NULL;
	}

	if (global->params_set) {
		ret = gnutls_credentials_set(conn->session,
					     GNUTLS_CRD_CERTIFICATE,
					     global->xcred);
		if (ret < 0) {
			wpa_printf(MSG_INFO, "Failed to configure "
				   "credentials: %s", gnutls_strerror(ret));
			os_free(conn);
			return NULL;
		}
	}

	if (gnutls_certificate_allocate_credentials(&conn->xcred)) {
		os_free(conn);
		return NULL;
	}

	return conn;
}


void tls_connection_deinit(void *ssl_ctx, struct tls_connection *conn)
{
	if (conn == NULL)
		return;

	gnutls_certificate_free_credentials(conn->xcred);
	gnutls_deinit(conn->session);
	os_free(conn->pre_shared_secret);
	wpabuf_free(conn->push_buf);
	wpabuf_free(conn->pull_buf);
	os_free(conn->suffix_match);
	os_free(conn->domain_match);
	os_free(conn);
}


int tls_connection_established(void *ssl_ctx, struct tls_connection *conn)
{
	return conn ? conn->established : 0;
}


char * tls_connection_peer_serial_num(void *tls_ctx,
				      struct tls_connection *conn)
{
	/* TODO */
	return NULL;
}


int tls_connection_shutdown(void *ssl_ctx, struct tls_connection *conn)
{
	struct tls_global *global = ssl_ctx;
	int ret;

	if (conn == NULL)
		return -1;

	/* Shutdown previous TLS connection without notifying the peer
	 * because the connection was already terminated in practice
	 * and "close notify" shutdown alert would confuse AS. */
	gnutls_bye(conn->session, GNUTLS_SHUT_RDWR);
	wpabuf_free(conn->push_buf);
	conn->push_buf = NULL;
	conn->established = 0;

	gnutls_deinit(conn->session);
	if (tls_gnutls_init_session(global, conn)) {
		wpa_printf(MSG_INFO, "GnuTLS: Failed to preparare new session "
			   "for session resumption use");
		return -1;
	}

	ret = gnutls_credentials_set(conn->session, GNUTLS_CRD_CERTIFICATE,
				     conn->params_set ? conn->xcred :
				     global->xcred);
	if (ret < 0) {
		wpa_printf(MSG_INFO, "GnuTLS: Failed to configure credentials "
			   "for session resumption: %s", gnutls_strerror(ret));
		return -1;
	}

	if (global->session_data) {
		ret = gnutls_session_set_data(conn->session,
					      global->session_data,
					      global->session_data_size);
		if (ret < 0) {
			wpa_printf(MSG_INFO, "GnuTLS: Failed to set session "
				   "data: %s", gnutls_strerror(ret));
			return -1;
		}
	}

	return 0;
}


int tls_connection_set_params(void *tls_ctx, struct tls_connection *conn,
			      const struct tls_connection_params *params)
{
	int ret;
	const char *err;
	char prio_buf[100];
	const char *prio = NULL;

	if (conn == NULL || params == NULL)
		return -1;

	if (params->flags & TLS_CONN_REQUIRE_OCSP_ALL) {
		wpa_printf(MSG_INFO,
			   "GnuTLS: ocsp=3 not supported");
		return -1;
	}

	if (params->flags & TLS_CONN_EXT_CERT_CHECK) {
		wpa_printf(MSG_INFO,
			   "GnuTLS: tls_ext_cert_check=1 not supported");
		return -1;
	}

	if (params->subject_match) {
		wpa_printf(MSG_INFO, "GnuTLS: subject_match not supported");
		return -1;
	}

	if (params->altsubject_match) {
		wpa_printf(MSG_INFO, "GnuTLS: altsubject_match not supported");
		return -1;
	}

	os_free(conn->suffix_match);
	conn->suffix_match = NULL;
	if (params->suffix_match) {
		conn->suffix_match = os_strdup(params->suffix_match);
		if (conn->suffix_match == NULL)
			return -1;
	}

#if GNUTLS_VERSION_NUMBER >= 0x030300
	os_free(conn->domain_match);
	conn->domain_match = NULL;
	if (params->domain_match) {
		conn->domain_match = os_strdup(params->domain_match);
		if (conn->domain_match == NULL)
			return -1;
	}
#else /* < 3.3.0 */
	if (params->domain_match) {
		wpa_printf(MSG_INFO, "GnuTLS: domain_match not supported");
		return -1;
	}
#endif /* >= 3.3.0 */

	conn->flags = params->flags;

	if (params->flags & (TLS_CONN_DISABLE_TLSv1_0 |
			     TLS_CONN_DISABLE_TLSv1_1 |
			     TLS_CONN_DISABLE_TLSv1_2)) {
		os_snprintf(prio_buf, sizeof(prio_buf),
			    "NORMAL:-VERS-SSL3.0%s%s%s",
			    params->flags & TLS_CONN_DISABLE_TLSv1_0 ?
			    ":-VERS-TLS1.0" : "",
			    params->flags & TLS_CONN_DISABLE_TLSv1_1 ?
			    ":-VERS-TLS1.1" : "",
			    params->flags & TLS_CONN_DISABLE_TLSv1_2 ?
			    ":-VERS-TLS1.2" : "");
		prio = prio_buf;
	}

	if (params->openssl_ciphers) {
		if (os_strcmp(params->openssl_ciphers, "SUITEB128") == 0) {
			prio = "SUITEB128";
		} else if (os_strcmp(params->openssl_ciphers,
				     "SUITEB192") == 0) {
			prio = "SUITEB192";
		} else if ((params->flags & TLS_CONN_SUITEB) &&
			   os_strcmp(params->openssl_ciphers,
				     "ECDHE-RSA-AES256-GCM-SHA384") == 0) {
			prio = "NONE:+VERS-TLS1.2:+AEAD:+ECDHE-RSA:+AES-256-GCM:+SIGN-RSA-SHA384:+CURVE-SECP384R1:+COMP-NULL";
		} else if (os_strcmp(params->openssl_ciphers,
				     "ECDHE-RSA-AES256-GCM-SHA384") == 0) {
			prio = "NONE:+VERS-TLS1.2:+AEAD:+ECDHE-RSA:+AES-256-GCM:+SIGN-RSA-SHA384:+CURVE-SECP384R1:+COMP-NULL";
		} else if (os_strcmp(params->openssl_ciphers,
				     "DHE-RSA-AES256-GCM-SHA384") == 0) {
			prio = "NONE:+VERS-TLS1.2:+AEAD:+DHE-RSA:+AES-256-GCM:+SIGN-RSA-SHA384:+CURVE-SECP384R1:+COMP-NULL:%PROFILE_HIGH";
		} else if (os_strcmp(params->openssl_ciphers,
				     "ECDHE-ECDSA-AES256-GCM-SHA384") == 0) {
			prio = "NONE:+VERS-TLS1.2:+AEAD:+ECDHE-ECDSA:+AES-256-GCM:+SIGN-RSA-SHA384:+CURVE-SECP384R1:+COMP-NULL";
		} else {
			wpa_printf(MSG_INFO,
				   "GnuTLS: openssl_ciphers not supported");
			return -1;
		}
	} else if (params->flags & TLS_CONN_SUITEB) {
		prio = "NONE:+VERS-TLS1.2:+AEAD:+ECDHE-ECDSA:+ECDHE-RSA:+DHE-RSA:+AES-256-GCM:+SIGN-RSA-SHA384:+CURVE-SECP384R1:+COMP-NULL:%PROFILE_HIGH";
	}

	if (prio) {
		wpa_printf(MSG_DEBUG, "GnuTLS: Set priority string: %s", prio);
		ret = gnutls_priority_set_direct(conn->session, prio, &err);
		if (ret < 0) {
			wpa_printf(MSG_ERROR,
				   "GnuTLS: Priority string failure at '%s'",
				   err);
			return -1;
		}
	}

	/* TODO: gnutls_certificate_set_verify_flags(xcred, flags);
	 * to force peer validation(?) */

	if (params->ca_cert) {
		wpa_printf(MSG_DEBUG, "GnuTLS: Try to parse %s in DER format",
			   params->ca_cert);
		ret = gnutls_certificate_set_x509_trust_file(
			conn->xcred, params->ca_cert, GNUTLS_X509_FMT_DER);
		if (ret < 0) {
			wpa_printf(MSG_DEBUG,
				   "GnuTLS: Failed to read CA cert '%s' in DER format (%s) - try in PEM format",
				   params->ca_cert,
				   gnutls_strerror(ret));
			ret = gnutls_certificate_set_x509_trust_file(
				conn->xcred, params->ca_cert,
				GNUTLS_X509_FMT_PEM);
			if (ret < 0) {
				wpa_printf(MSG_DEBUG,
					   "Failed to read CA cert '%s' in PEM format: %s",
					   params->ca_cert,
					   gnutls_strerror(ret));
				return -1;
			}
			wpa_printf(MSG_DEBUG,
				   "GnuTLS: Successfully read CA cert '%s' in PEM format",
				   params->ca_cert);
		} else {
			wpa_printf(MSG_DEBUG,
				   "GnuTLS: Successfully read CA cert '%s' in DER format",
				   params->ca_cert);
		}
	} else if (params->ca_cert_blob) {
		gnutls_datum_t ca;

		ca.data = (unsigned char *) params->ca_cert_blob;
		ca.size = params->ca_cert_blob_len;

		ret = gnutls_certificate_set_x509_trust_mem(
			conn->xcred, &ca, GNUTLS_X509_FMT_DER);
		if (ret < 0) {
			wpa_printf(MSG_DEBUG,
				   "Failed to parse CA cert in DER format: %s",
				   gnutls_strerror(ret));
			ret = gnutls_certificate_set_x509_trust_mem(
				conn->xcred, &ca, GNUTLS_X509_FMT_PEM);
			if (ret < 0) {
				wpa_printf(MSG_DEBUG,
					   "Failed to parse CA cert in PEM format: %s",
					   gnutls_strerror(ret));
				return -1;
			}
		}
	} else if (params->ca_path) {
		wpa_printf(MSG_INFO, "GnuTLS: ca_path not supported");
		return -1;
	}

	conn->disable_time_checks = 0;
	if (params->ca_cert || params->ca_cert_blob) {
		conn->verify_peer = 1;
		gnutls_certificate_set_verify_function(
			conn->xcred, tls_connection_verify_peer);

		if (params->flags & TLS_CONN_ALLOW_SIGN_RSA_MD5) {
			gnutls_certificate_set_verify_flags(
				conn->xcred, GNUTLS_VERIFY_ALLOW_SIGN_RSA_MD5);
		}

		if (params->flags & TLS_CONN_DISABLE_TIME_CHECKS) {
			conn->disable_time_checks = 1;
			gnutls_certificate_set_verify_flags(
				conn->xcred,
				GNUTLS_VERIFY_DISABLE_TIME_CHECKS);
		}
	}

	if (params->client_cert && params->private_key) {
		wpa_printf(MSG_DEBUG,
			   "GnuTLS: Try to parse client cert '%s' and key '%s' in DER format",
			   params->client_cert, params->private_key);
#if GNUTLS_VERSION_NUMBER >= 0x03010b
		ret = gnutls_certificate_set_x509_key_file2(
			conn->xcred, params->client_cert, params->private_key,
			GNUTLS_X509_FMT_DER, params->private_key_passwd, 0);
#else
		/* private_key_passwd not (easily) supported here */
		ret = gnutls_certificate_set_x509_key_file(
			conn->xcred, params->client_cert, params->private_key,
			GNUTLS_X509_FMT_DER);
#endif
		if (ret < 0) {
			wpa_printf(MSG_DEBUG,
				   "GnuTLS: Failed to read client cert/key in DER format (%s) - try in PEM format",
				   gnutls_strerror(ret));
#if GNUTLS_VERSION_NUMBER >= 0x03010b
			ret = gnutls_certificate_set_x509_key_file2(
				conn->xcred, params->client_cert,
				params->private_key, GNUTLS_X509_FMT_PEM,
				params->private_key_passwd, 0);
#else
			ret = gnutls_certificate_set_x509_key_file(
				conn->xcred, params->client_cert,
				params->private_key, GNUTLS_X509_FMT_PEM);
#endif
			if (ret < 0) {
				wpa_printf(MSG_DEBUG, "Failed to read client "
					   "cert/key in PEM format: %s",
					   gnutls_strerror(ret));
				return ret;
			}
			wpa_printf(MSG_DEBUG,
				   "GnuTLS: Successfully read client cert/key in PEM format");
		} else {
			wpa_printf(MSG_DEBUG,
				   "GnuTLS: Successfully read client cert/key in DER format");
		}
	} else if (params->private_key) {
		int pkcs12_ok = 0;
#ifdef PKCS12_FUNCS
		/* Try to load in PKCS#12 format */
		wpa_printf(MSG_DEBUG,
			   "GnuTLS: Try to parse client cert/key '%s'in PKCS#12 DER format",
			   params->private_key);
		ret = gnutls_certificate_set_x509_simple_pkcs12_file(
			conn->xcred, params->private_key, GNUTLS_X509_FMT_DER,
			params->private_key_passwd);
		if (ret != 0) {
			wpa_printf(MSG_DEBUG, "Failed to load private_key in "
				   "PKCS#12 format: %s", gnutls_strerror(ret));
			return -1;
		} else
			pkcs12_ok = 1;
#endif /* PKCS12_FUNCS */

		if (!pkcs12_ok) {
			wpa_printf(MSG_DEBUG, "GnuTLS: PKCS#12 support not "
				   "included");
			return -1;
		}
	} else if (params->client_cert_blob && params->private_key_blob) {
		gnutls_datum_t cert, key;

		cert.data = (unsigned char *) params->client_cert_blob;
		cert.size = params->client_cert_blob_len;
		key.data = (unsigned char *) params->private_key_blob;
		key.size = params->private_key_blob_len;

#if GNUTLS_VERSION_NUMBER >= 0x03010b
		ret = gnutls_certificate_set_x509_key_mem2(
			conn->xcred, &cert, &key, GNUTLS_X509_FMT_DER,
			params->private_key_passwd, 0);
#else
		/* private_key_passwd not (easily) supported here */
		ret = gnutls_certificate_set_x509_key_mem(
			conn->xcred, &cert, &key, GNUTLS_X509_FMT_DER);
#endif
		if (ret < 0) {
			wpa_printf(MSG_DEBUG, "Failed to read client cert/key "
				   "in DER format: %s", gnutls_strerror(ret));
#if GNUTLS_VERSION_NUMBER >= 0x03010b
			ret = gnutls_certificate_set_x509_key_mem2(
				conn->xcred, &cert, &key, GNUTLS_X509_FMT_PEM,
				params->private_key_passwd, 0);
#else
			/* private_key_passwd not (easily) supported here */
			ret = gnutls_certificate_set_x509_key_mem(
				conn->xcred, &cert, &key, GNUTLS_X509_FMT_PEM);
#endif
			if (ret < 0) {
				wpa_printf(MSG_DEBUG, "Failed to read client "
					   "cert/key in PEM format: %s",
					   gnutls_strerror(ret));
				return ret;
			}
		}
	} else if (params->private_key_blob) {
#ifdef PKCS12_FUNCS
		gnutls_datum_t key;

		key.data = (unsigned char *) params->private_key_blob;
		key.size = params->private_key_blob_len;

		/* Try to load in PKCS#12 format */
		ret = gnutls_certificate_set_x509_simple_pkcs12_mem(
			conn->xcred, &key, GNUTLS_X509_FMT_DER,
			params->private_key_passwd);
		if (ret != 0) {
			wpa_printf(MSG_DEBUG, "Failed to load private_key in "
				   "PKCS#12 format: %s", gnutls_strerror(ret));
			return -1;
		}
#else /* PKCS12_FUNCS */
		wpa_printf(MSG_DEBUG, "GnuTLS: PKCS#12 support not included");
		return -1;
#endif /* PKCS12_FUNCS */
	}

#if GNUTLS_VERSION_NUMBER >= 0x030103
	if (params->flags & (TLS_CONN_REQUEST_OCSP | TLS_CONN_REQUIRE_OCSP)) {
		ret = gnutls_ocsp_status_request_enable_client(conn->session,
							       NULL, 0, NULL);
		if (ret != GNUTLS_E_SUCCESS) {
			wpa_printf(MSG_INFO,
				   "GnuTLS: Failed to enable OCSP client");
			return -1;
		}
	}
#else /* 3.1.3 */
	if (params->flags & TLS_CONN_REQUIRE_OCSP) {
		wpa_printf(MSG_INFO,
			   "GnuTLS: OCSP not supported by this version of GnuTLS");
		return -1;
	}
#endif /* 3.1.3 */

	conn->params_set = 1;

	ret = gnutls_credentials_set(conn->session, GNUTLS_CRD_CERTIFICATE,
				     conn->xcred);
	if (ret < 0) {
		wpa_printf(MSG_INFO, "Failed to configure credentials: %s",
			   gnutls_strerror(ret));
	}

	return ret;
}


#if GNUTLS_VERSION_NUMBER >= 0x030103
static int server_ocsp_status_req(gnutls_session_t session, void *ptr,
				  gnutls_datum_t *resp)
{
	struct tls_global *global = ptr;
	char *cached;
	size_t len;

	if (!global->ocsp_stapling_response) {
		wpa_printf(MSG_DEBUG, "GnuTLS: OCSP status callback - no response configured");
		return GNUTLS_E_NO_CERTIFICATE_STATUS;
	}

	cached = os_readfile(global->ocsp_stapling_response, &len);
	if (!cached) {
		wpa_printf(MSG_DEBUG,
			   "GnuTLS: OCSP status callback - could not read response file (%s)",
			   global->ocsp_stapling_response);
		return GNUTLS_E_NO_CERTIFICATE_STATUS;
	}

	wpa_printf(MSG_DEBUG,
		   "GnuTLS: OCSP status callback - send cached response");
	resp->data = gnutls_malloc(len);
	if (!resp->data) {
		os_free(resp);
		return GNUTLS_E_MEMORY_ERROR;
	}

	os_memcpy(resp->data, cached, len);
	resp->size = len;
	os_free(cached);

	return GNUTLS_E_SUCCESS;
}
#endif /* 3.1.3 */


int tls_global_set_params(void *tls_ctx,
			  const struct tls_connection_params *params)
{
	struct tls_global *global = tls_ctx;
	int ret;

	/* Currently, global parameters are only set when running in server
	 * mode. */
	global->server = 1;

	if (global->params_set) {
		gnutls_certificate_free_credentials(global->xcred);
		global->params_set = 0;
	}

	ret = gnutls_certificate_allocate_credentials(&global->xcred);
	if (ret) {
		wpa_printf(MSG_DEBUG, "Failed to allocate global credentials "
			   "%s", gnutls_strerror(ret));
		return -1;
	}

	if (params->ca_cert) {
		ret = gnutls_certificate_set_x509_trust_file(
			global->xcred, params->ca_cert, GNUTLS_X509_FMT_DER);
		if (ret < 0) {
			wpa_printf(MSG_DEBUG, "Failed to read CA cert '%s' "
				   "in DER format: %s", params->ca_cert,
				   gnutls_strerror(ret));
			ret = gnutls_certificate_set_x509_trust_file(
				global->xcred, params->ca_cert,
				GNUTLS_X509_FMT_PEM);
			if (ret < 0) {
				wpa_printf(MSG_DEBUG, "Failed to read CA cert "
					   "'%s' in PEM format: %s",
					   params->ca_cert,
					   gnutls_strerror(ret));
				goto fail;
			}
		}

		if (params->flags & TLS_CONN_ALLOW_SIGN_RSA_MD5) {
			gnutls_certificate_set_verify_flags(
				global->xcred,
				GNUTLS_VERIFY_ALLOW_SIGN_RSA_MD5);
		}

		if (params->flags & TLS_CONN_DISABLE_TIME_CHECKS) {
			gnutls_certificate_set_verify_flags(
				global->xcred,
				GNUTLS_VERIFY_DISABLE_TIME_CHECKS);
		}
	}

	if (params->client_cert && params->private_key) {
		/* TODO: private_key_passwd? */
		ret = gnutls_certificate_set_x509_key_file(
			global->xcred, params->client_cert,
			params->private_key, GNUTLS_X509_FMT_DER);
		if (ret < 0) {
			wpa_printf(MSG_DEBUG, "Failed to read client cert/key "
				   "in DER format: %s", gnutls_strerror(ret));
			ret = gnutls_certificate_set_x509_key_file(
				global->xcred, params->client_cert,
				params->private_key, GNUTLS_X509_FMT_PEM);
			if (ret < 0) {
				wpa_printf(MSG_DEBUG, "Failed to read client "
					   "cert/key in PEM format: %s",
					   gnutls_strerror(ret));
				goto fail;
			}
		}
	} else if (params->private_key) {
		int pkcs12_ok = 0;
#ifdef PKCS12_FUNCS
		/* Try to load in PKCS#12 format */
		ret = gnutls_certificate_set_x509_simple_pkcs12_file(
			global->xcred, params->private_key,
			GNUTLS_X509_FMT_DER, params->private_key_passwd);
		if (ret != 0) {
			wpa_printf(MSG_DEBUG, "Failed to load private_key in "
				   "PKCS#12 format: %s", gnutls_strerror(ret));
			goto fail;
		} else
			pkcs12_ok = 1;
#endif /* PKCS12_FUNCS */

		if (!pkcs12_ok) {
			wpa_printf(MSG_DEBUG, "GnuTLS: PKCS#12 support not "
				   "included");
			goto fail;
		}
	}

#if GNUTLS_VERSION_NUMBER >= 0x030103
	os_free(global->ocsp_stapling_response);
	if (params->ocsp_stapling_response)
		global->ocsp_stapling_response =
			os_strdup(params->ocsp_stapling_response);
	else
		global->ocsp_stapling_response = NULL;
	gnutls_certificate_set_ocsp_status_request_function(
		global->xcred, server_ocsp_status_req, global);
#endif /* 3.1.3 */

	global->params_set = 1;

	return 0;

fail:
	gnutls_certificate_free_credentials(global->xcred);
	return -1;
}


int tls_global_set_verify(void *ssl_ctx, int check_crl)
{
	/* TODO */
	return 0;
}


int tls_connection_set_verify(void *ssl_ctx, struct tls_connection *conn,
			      int verify_peer, unsigned int flags,
			      const u8 *session_ctx, size_t session_ctx_len)
{
	if (conn == NULL || conn->session == NULL)
		return -1;

	conn->verify_peer = verify_peer;
	gnutls_certificate_server_set_request(conn->session,
					      verify_peer ? GNUTLS_CERT_REQUIRE
					      : GNUTLS_CERT_REQUEST);

	return 0;
}


int tls_connection_get_random(void *ssl_ctx, struct tls_connection *conn,
			    struct tls_random *keys)
{
#if GNUTLS_VERSION_NUMBER >= 0x030012
	gnutls_datum_t client, server;

	if (conn == NULL || conn->session == NULL || keys == NULL)
		return -1;

	os_memset(keys, 0, sizeof(*keys));
	gnutls_session_get_random(conn->session, &client, &server);
	keys->client_random = client.data;
	keys->server_random = server.data;
	keys->client_random_len = client.size;
	keys->server_random_len = client.size;

	return 0;
#else /* 3.0.18 */
	return -1;
#endif /* 3.0.18 */
}


int tls_connection_export_key(void *tls_ctx, struct tls_connection *conn,
			      const char *label, u8 *out, size_t out_len)
{
	if (conn == NULL || conn->session == NULL)
		return -1;

	return gnutls_prf(conn->session, os_strlen(label), label,
			  0 /* client_random first */, 0, NULL, out_len,
			  (char *) out);
}


int tls_connection_get_eap_fast_key(void *tls_ctx, struct tls_connection *conn,
				    u8 *out, size_t out_len)
{
	return -1;
}


static void gnutls_tls_fail_event(struct tls_connection *conn,
				  const gnutls_datum_t *cert, int depth,
				  const char *subject, const char *err_str,
				  enum tls_fail_reason reason)
{
	union tls_event_data ev;
	struct tls_global *global = conn->global;
	struct wpabuf *cert_buf = NULL;

	if (global->event_cb == NULL)
		return;

	os_memset(&ev, 0, sizeof(ev));
	ev.cert_fail.depth = depth;
	ev.cert_fail.subject = subject ? subject : "";
	ev.cert_fail.reason = reason;
	ev.cert_fail.reason_txt = err_str;
	if (cert) {
		cert_buf = wpabuf_alloc_copy(cert->data, cert->size);
		ev.cert_fail.cert = cert_buf;
	}
	global->event_cb(global->cb_ctx, TLS_CERT_CHAIN_FAILURE, &ev);
	wpabuf_free(cert_buf);
}


#if GNUTLS_VERSION_NUMBER < 0x030300
static int server_eku_purpose(gnutls_x509_crt_t cert)
{
	unsigned int i;

	for (i = 0; ; i++) {
		char oid[128];
		size_t oid_size = sizeof(oid);
		int res;

		res = gnutls_x509_crt_get_key_purpose_oid(cert, i, oid,
							  &oid_size, NULL);
		if (res == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) {
			if (i == 0) {
				/* No EKU - assume any use allowed */
				return 1;
			}
			break;
		}

		if (res < 0) {
			wpa_printf(MSG_INFO, "GnuTLS: Failed to get EKU");
			return 0;
		}

		wpa_printf(MSG_DEBUG, "GnuTLS: Certificate purpose: %s", oid);
		if (os_strcmp(oid, GNUTLS_KP_TLS_WWW_SERVER) == 0 ||
		    os_strcmp(oid, GNUTLS_KP_ANY) == 0)
			return 1;
	}

	return 0;
}
#endif /* < 3.3.0 */


static int check_ocsp(struct tls_connection *conn, gnutls_session_t session,
		      gnutls_alert_description_t *err)
{
#if GNUTLS_VERSION_NUMBER >= 0x030103
	gnutls_datum_t response, buf;
	gnutls_ocsp_resp_t resp;
	unsigned int cert_status;
	int res;

	if (!(conn->flags & (TLS_CONN_REQUEST_OCSP | TLS_CONN_REQUIRE_OCSP)))
		return 0;

	if (!gnutls_ocsp_status_request_is_checked(session, 0)) {
		if (conn->flags & TLS_CONN_REQUIRE_OCSP) {
			wpa_printf(MSG_INFO,
				   "GnuTLS: No valid OCSP response received");
			goto ocsp_error;
		}

		wpa_printf(MSG_DEBUG,
			   "GnuTLS: Valid OCSP response was not received - continue since OCSP was not required");
		return 0;
	}

	/*
	 * GnuTLS has already verified the OCSP response in
	 * check_ocsp_response() and rejected handshake if the certificate was
	 * found to be revoked. However, if the response indicates that the
	 * status is unknown, handshake continues and reaches here. We need to
	 * re-import the OCSP response to check for unknown certificate status,
	 * but we do not need to repeat gnutls_ocsp_resp_check_crt() and
	 * gnutls_ocsp_resp_verify_direct() calls.
	 */

	res = gnutls_ocsp_status_request_get(session, &response);
	if (res != GNUTLS_E_SUCCESS) {
		wpa_printf(MSG_INFO,
			   "GnuTLS: OCSP response was received, but it was not valid");
		goto ocsp_error;
	}

	if (gnutls_ocsp_resp_init(&resp) != GNUTLS_E_SUCCESS)
		goto ocsp_error;

	res = gnutls_ocsp_resp_import(resp, &response);
	if (res != GNUTLS_E_SUCCESS) {
		wpa_printf(MSG_INFO,
			   "GnuTLS: Could not parse received OCSP response: %s",
			   gnutls_strerror(res));
		gnutls_ocsp_resp_deinit(resp);
		goto ocsp_error;
	}

	res = gnutls_ocsp_resp_print(resp, GNUTLS_OCSP_PRINT_FULL, &buf);
	if (res == GNUTLS_E_SUCCESS) {
		wpa_printf(MSG_DEBUG, "GnuTLS: %s", buf.data);
		gnutls_free(buf.data);
	}

	res = gnutls_ocsp_resp_get_single(resp, 0, NULL, NULL, NULL,
					  NULL, &cert_status, NULL,
					  NULL, NULL, NULL);
	gnutls_ocsp_resp_deinit(resp);
	if (res != GNUTLS_E_SUCCESS) {
		wpa_printf(MSG_INFO,
			   "GnuTLS: Failed to extract OCSP information: %s",
			   gnutls_strerror(res));
		goto ocsp_error;
	}

	if (cert_status == GNUTLS_OCSP_CERT_GOOD) {
		wpa_printf(MSG_DEBUG, "GnuTLS: OCSP cert status: good");
	} else if (cert_status == GNUTLS_OCSP_CERT_REVOKED) {
		wpa_printf(MSG_DEBUG,
			   "GnuTLS: OCSP cert status: revoked");
		goto ocsp_error;
	} else {
		wpa_printf(MSG_DEBUG,
			   "GnuTLS: OCSP cert status: unknown");
		if (conn->flags & TLS_CONN_REQUIRE_OCSP)
			goto ocsp_error;
		wpa_printf(MSG_DEBUG,
			   "GnuTLS: OCSP was not required, so allow connection to continue");
	}

	return 0;

ocsp_error:
	gnutls_tls_fail_event(conn, NULL, 0, NULL,
			      "bad certificate status response",
			      TLS_FAIL_REVOKED);
	*err = GNUTLS_A_CERTIFICATE_REVOKED;
	return -1;
#else /* GnuTLS 3.1.3 or newer */
	return 0;
#endif /* GnuTLS 3.1.3 or newer */
}


static int tls_connection_verify_peer(gnutls_session_t session)
{
	struct tls_connection *conn;
	unsigned int status, num_certs, i;
	struct os_time now;
	const gnutls_datum_t *certs;
	gnutls_x509_crt_t cert;
	gnutls_alert_description_t err;
	int res;

	conn = gnutls_session_get_ptr(session);
	if (!conn->verify_peer) {
		wpa_printf(MSG_DEBUG,
			   "GnuTLS: No peer certificate verification enabled");
		return 0;
	}

	wpa_printf(MSG_DEBUG, "GnuTSL: Verifying peer certificate");

#if GNUTLS_VERSION_NUMBER >= 0x030300
	{
		gnutls_typed_vdata_st data[1];
		unsigned int elements = 0;

		os_memset(data, 0, sizeof(data));
		if (!conn->global->server) {
			data[elements].type = GNUTLS_DT_KEY_PURPOSE_OID;
			data[elements].data = (void *) GNUTLS_KP_TLS_WWW_SERVER;
			elements++;
		}
		res = gnutls_certificate_verify_peers(session, data, 1,
						      &status);
	}
#else /* < 3.3.0 */
	res = gnutls_certificate_verify_peers2(session, &status);
#endif
	if (res < 0) {
		wpa_printf(MSG_INFO, "TLS: Failed to verify peer "
			   "certificate chain");
		err = GNUTLS_A_INTERNAL_ERROR;
		goto out;
	}

#if GNUTLS_VERSION_NUMBER >= 0x030104
	{
		gnutls_datum_t info;
		int ret, type;

		type = gnutls_certificate_type_get(session);
		ret = gnutls_certificate_verification_status_print(status, type,
								   &info, 0);
		if (ret < 0) {
			wpa_printf(MSG_DEBUG,
				   "GnuTLS: Failed to print verification status");
			err = GNUTLS_A_INTERNAL_ERROR;
			goto out;
		}
		wpa_printf(MSG_DEBUG, "GnuTLS: %s", info.data);
		gnutls_free(info.data);
	}
#endif /* GnuTLS 3.1.4 or newer */

	certs = gnutls_certificate_get_peers(session, &num_certs);
	if (certs == NULL || num_certs == 0) {
		wpa_printf(MSG_INFO, "TLS: No peer certificate chain received");
		err = GNUTLS_A_UNKNOWN_CA;
		goto out;
	}

	if (conn->verify_peer && (status & GNUTLS_CERT_INVALID)) {
		wpa_printf(MSG_INFO, "TLS: Peer certificate not trusted");
		if (status & GNUTLS_CERT_INSECURE_ALGORITHM) {
			wpa_printf(MSG_INFO, "TLS: Certificate uses insecure "
				   "algorithm");
			gnutls_tls_fail_event(conn, NULL, 0, NULL,
					      "certificate uses insecure algorithm",
					      TLS_FAIL_BAD_CERTIFICATE);
			err = GNUTLS_A_INSUFFICIENT_SECURITY;
			goto out;
		}
		if (status & GNUTLS_CERT_NOT_ACTIVATED) {
			wpa_printf(MSG_INFO, "TLS: Certificate not yet "
				   "activated");
			gnutls_tls_fail_event(conn, NULL, 0, NULL,
					      "certificate not yet valid",
					      TLS_FAIL_NOT_YET_VALID);
			err = GNUTLS_A_CERTIFICATE_EXPIRED;
			goto out;
		}
		if (status & GNUTLS_CERT_EXPIRED) {
			wpa_printf(MSG_INFO, "TLS: Certificate expired");
			gnutls_tls_fail_event(conn, NULL, 0, NULL,
					      "certificate has expired",
					      TLS_FAIL_EXPIRED);
			err = GNUTLS_A_CERTIFICATE_EXPIRED;
			goto out;
		}
		gnutls_tls_fail_event(conn, NULL, 0, NULL,
				      "untrusted certificate",
				      TLS_FAIL_UNTRUSTED);
		err = GNUTLS_A_INTERNAL_ERROR;
		goto out;
	}

	if (status & GNUTLS_CERT_SIGNER_NOT_FOUND) {
		wpa_printf(MSG_INFO, "TLS: Peer certificate does not have a "
			   "known issuer");
		gnutls_tls_fail_event(conn, NULL, 0, NULL, "signed not found",
				      TLS_FAIL_UNTRUSTED);
		err = GNUTLS_A_UNKNOWN_CA;
		goto out;
	}

	if (status & GNUTLS_CERT_REVOKED) {
		wpa_printf(MSG_INFO, "TLS: Peer certificate has been revoked");
		gnutls_tls_fail_event(conn, NULL, 0, NULL,
				      "certificate revoked",
				      TLS_FAIL_REVOKED);
		err = GNUTLS_A_CERTIFICATE_REVOKED;
		goto out;
	}

	if (status != 0) {
		wpa_printf(MSG_INFO, "TLS: Unknown verification status: %d",
			   status);
		err = GNUTLS_A_INTERNAL_ERROR;
		goto out;
	}

	if (check_ocsp(conn, session, &err))
		goto out;

	os_get_time(&now);

	for (i = 0; i < num_certs; i++) {
		char *buf;
		size_t len;
		if (gnutls_x509_crt_init(&cert) < 0) {
			wpa_printf(MSG_INFO, "TLS: Certificate initialization "
				   "failed");
			err = GNUTLS_A_BAD_CERTIFICATE;
			goto out;
		}

		if (gnutls_x509_crt_import(cert, &certs[i],
					   GNUTLS_X509_FMT_DER) < 0) {
			wpa_printf(MSG_INFO, "TLS: Could not parse peer "
				   "certificate %d/%d", i + 1, num_certs);
			gnutls_x509_crt_deinit(cert);
			err = GNUTLS_A_BAD_CERTIFICATE;
			goto out;
		}

		gnutls_x509_crt_get_dn(cert, NULL, &len);
		len++;
		buf = os_malloc(len + 1);
		if (buf) {
			buf[0] = buf[len] = '\0';
			gnutls_x509_crt_get_dn(cert, buf, &len);
		}
		wpa_printf(MSG_DEBUG, "TLS: Peer cert chain %d/%d: %s",
			   i + 1, num_certs, buf);

		if (conn->global->event_cb) {
			struct wpabuf *cert_buf = NULL;
			union tls_event_data ev;
#ifdef CONFIG_SHA256
			u8 hash[32];
			const u8 *_addr[1];
			size_t _len[1];
#endif /* CONFIG_SHA256 */

			os_memset(&ev, 0, sizeof(ev));
			if (conn->global->cert_in_cb) {
				cert_buf = wpabuf_alloc_copy(certs[i].data,
							     certs[i].size);
				ev.peer_cert.cert = cert_buf;
			}
#ifdef CONFIG_SHA256
			_addr[0] = certs[i].data;
			_len[0] = certs[i].size;
			if (sha256_vector(1, _addr, _len, hash) == 0) {
				ev.peer_cert.hash = hash;
				ev.peer_cert.hash_len = sizeof(hash);
			}
#endif /* CONFIG_SHA256 */
			ev.peer_cert.depth = i;
			ev.peer_cert.subject = buf;
			conn->global->event_cb(conn->global->cb_ctx,
					       TLS_PEER_CERTIFICATE, &ev);
			wpabuf_free(cert_buf);
		}

		if (i == 0) {
			if (conn->suffix_match &&
			    !gnutls_x509_crt_check_hostname(
				    cert, conn->suffix_match)) {
				wpa_printf(MSG_WARNING,
					   "TLS: Domain suffix match '%s' not found",
					   conn->suffix_match);
				gnutls_tls_fail_event(
					conn, &certs[i], i, buf,
					"Domain suffix mismatch",
					TLS_FAIL_DOMAIN_SUFFIX_MISMATCH);
				err = GNUTLS_A_BAD_CERTIFICATE;
				gnutls_x509_crt_deinit(cert);
				os_free(buf);
				goto out;
			}

#if GNUTLS_VERSION_NUMBER >= 0x030300
			if (conn->domain_match &&
			    !gnutls_x509_crt_check_hostname2(
				    cert, conn->domain_match,
				    GNUTLS_VERIFY_DO_NOT_ALLOW_WILDCARDS)) {
				wpa_printf(MSG_WARNING,
					   "TLS: Domain match '%s' not found",
					   conn->domain_match);
				gnutls_tls_fail_event(
					conn, &certs[i], i, buf,
					"Domain mismatch",
					TLS_FAIL_DOMAIN_MISMATCH);
				err = GNUTLS_A_BAD_CERTIFICATE;
				gnutls_x509_crt_deinit(cert);
				os_free(buf);
				goto out;
			}
#endif /* >= 3.3.0 */

			/* TODO: validate altsubject_match.
			 * For now, any such configuration is rejected in
			 * tls_connection_set_params() */

#if GNUTLS_VERSION_NUMBER < 0x030300
			/*
			 * gnutls_certificate_verify_peers() not available, so
			 * need to check EKU separately.
			 */
			if (!conn->global->server &&
			    !server_eku_purpose(cert)) {
				wpa_printf(MSG_WARNING,
					   "GnuTLS: No server EKU");
				gnutls_tls_fail_event(
					conn, &certs[i], i, buf,
					"No server EKU",
					TLS_FAIL_BAD_CERTIFICATE);
				err = GNUTLS_A_BAD_CERTIFICATE;
				gnutls_x509_crt_deinit(cert);
				os_free(buf);
				goto out;
			}
#endif /* < 3.3.0 */
		}

		if (!conn->disable_time_checks &&
		    (gnutls_x509_crt_get_expiration_time(cert) < now.sec ||
		     gnutls_x509_crt_get_activation_time(cert) > now.sec)) {
			wpa_printf(MSG_INFO, "TLS: Peer certificate %d/%d is "
				   "not valid at this time",
				   i + 1, num_certs);
			gnutls_tls_fail_event(
				conn, &certs[i], i, buf,
				"Certificate is not valid at this time",
				TLS_FAIL_EXPIRED);
			gnutls_x509_crt_deinit(cert);
			os_free(buf);
			err = GNUTLS_A_CERTIFICATE_EXPIRED;
			goto out;
		}

		os_free(buf);

		gnutls_x509_crt_deinit(cert);
	}

	if (conn->global->event_cb != NULL)
		conn->global->event_cb(conn->global->cb_ctx,
				       TLS_CERT_CHAIN_SUCCESS, NULL);

	return 0;

out:
	conn->failed++;
	gnutls_alert_send(session, GNUTLS_AL_FATAL, err);
	return GNUTLS_E_CERTIFICATE_ERROR;
}


static struct wpabuf * gnutls_get_appl_data(struct tls_connection *conn)
{
	int res;
	struct wpabuf *ad;
	wpa_printf(MSG_DEBUG, "GnuTLS: Check for possible Application Data");
	ad = wpabuf_alloc((wpabuf_len(conn->pull_buf) + 500) * 3);
	if (ad == NULL)
		return NULL;

	res = gnutls_record_recv(conn->session, wpabuf_mhead(ad),
				 wpabuf_size(ad));
	wpa_printf(MSG_DEBUG, "GnuTLS: gnutls_record_recv: %d", res);
	if (res < 0) {
		wpa_printf(MSG_DEBUG, "%s - gnutls_record_recv failed: %d "
			   "(%s)", __func__, (int) res,
			   gnutls_strerror(res));
		wpabuf_free(ad);
		return NULL;
	}

	wpabuf_put(ad, res);
	wpa_printf(MSG_DEBUG, "GnuTLS: Received %d bytes of Application Data",
		   res);
	return ad;
}


struct wpabuf * tls_connection_handshake(void *tls_ctx,
					 struct tls_connection *conn,
					 const struct wpabuf *in_data,
					 struct wpabuf **appl_data)
{
	struct tls_global *global = tls_ctx;
	struct wpabuf *out_data;
	int ret;

	if (appl_data)
		*appl_data = NULL;

	if (in_data && wpabuf_len(in_data) > 0) {
		if (conn->pull_buf) {
			wpa_printf(MSG_DEBUG, "%s - %lu bytes remaining in "
				   "pull_buf", __func__,
				   (unsigned long) wpabuf_len(conn->pull_buf));
			wpabuf_free(conn->pull_buf);
		}
		conn->pull_buf = wpabuf_dup(in_data);
		if (conn->pull_buf == NULL)
			return NULL;
		conn->pull_buf_offset = wpabuf_head(conn->pull_buf);
	}

	ret = gnutls_handshake(conn->session);
	if (ret < 0) {
		gnutls_alert_description_t alert;
		union tls_event_data ev;

		switch (ret) {
		case GNUTLS_E_AGAIN:
			if (global->server && conn->established &&
			    conn->push_buf == NULL) {
				/* Need to return something to trigger
				 * completion of EAP-TLS. */
				conn->push_buf = wpabuf_alloc(0);
			}
			break;
		case GNUTLS_E_DH_PRIME_UNACCEPTABLE:
			wpa_printf(MSG_DEBUG, "GnuTLS: Unacceptable DH prime");
			if (conn->global->event_cb) {
				os_memset(&ev, 0, sizeof(ev));
				ev.alert.is_local = 1;
				ev.alert.type = "fatal";
				ev.alert.description = "insufficient security";
				conn->global->event_cb(conn->global->cb_ctx,
						       TLS_ALERT, &ev);
			}
			/*
			 * Could send a TLS Alert to the server, but for now,
			 * simply terminate handshake.
			 */
			conn->failed++;
			conn->write_alerts++;
			break;
		case GNUTLS_E_FATAL_ALERT_RECEIVED:
			alert = gnutls_alert_get(conn->session);
			wpa_printf(MSG_DEBUG, "%s - received fatal '%s' alert",
				   __func__, gnutls_alert_get_name(alert));
			conn->read_alerts++;
			if (conn->global->event_cb != NULL) {
				os_memset(&ev, 0, sizeof(ev));
				ev.alert.is_local = 0;
				ev.alert.type = gnutls_alert_get_name(alert);
				ev.alert.description = ev.alert.type;
				conn->global->event_cb(conn->global->cb_ctx,
						       TLS_ALERT, &ev);
			}
			/* continue */
		default:
			wpa_printf(MSG_DEBUG, "%s - gnutls_handshake failed "
				   "-> %s", __func__, gnutls_strerror(ret));
			conn->failed++;
		}
	} else {
		size_t size;

		wpa_printf(MSG_DEBUG, "TLS: Handshake completed successfully");

#if GNUTLS_VERSION_NUMBER >= 0x03010a
		{
			char *desc;

			desc = gnutls_session_get_desc(conn->session);
			if (desc) {
				wpa_printf(MSG_DEBUG, "GnuTLS: %s", desc);
				gnutls_free(desc);
			}
		}
#endif /* GnuTLS 3.1.10 or newer */

		conn->established = 1;
		if (conn->push_buf == NULL) {
			/* Need to return something to get final TLS ACK. */
			conn->push_buf = wpabuf_alloc(0);
		}

		gnutls_session_get_data(conn->session, NULL, &size);
		if (global->session_data == NULL ||
		    global->session_data_size < size) {
			os_free(global->session_data);
			global->session_data = os_malloc(size);
		}
		if (global->session_data) {
			global->session_data_size = size;
			gnutls_session_get_data(conn->session,
						global->session_data,
						&global->session_data_size);
		}

		if (conn->pull_buf && appl_data)
			*appl_data = gnutls_get_appl_data(conn);
	}

	out_data = conn->push_buf;
	conn->push_buf = NULL;
	return out_data;
}


struct wpabuf * tls_connection_server_handshake(void *tls_ctx,
						struct tls_connection *conn,
						const struct wpabuf *in_data,
						struct wpabuf **appl_data)
{
	return tls_connection_handshake(tls_ctx, conn, in_data, appl_data);
}


struct wpabuf * tls_connection_encrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	ssize_t res;
	struct wpabuf *buf;

	res = gnutls_record_send(conn->session, wpabuf_head(in_data),
				 wpabuf_len(in_data));
	if (res < 0) {
		wpa_printf(MSG_INFO, "%s: Encryption failed: %s",
			   __func__, gnutls_strerror(res));
		return NULL;
	}

	buf = conn->push_buf;
	conn->push_buf = NULL;
	return buf;
}


struct wpabuf * tls_connection_decrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	ssize_t res;
	struct wpabuf *out;

	if (conn->pull_buf) {
		wpa_printf(MSG_DEBUG, "%s - %lu bytes remaining in "
			   "pull_buf", __func__,
			   (unsigned long) wpabuf_len(conn->pull_buf));
		wpabuf_free(conn->pull_buf);
	}
	conn->pull_buf = wpabuf_dup(in_data);
	if (conn->pull_buf == NULL)
		return NULL;
	conn->pull_buf_offset = wpabuf_head(conn->pull_buf);

	/*
	 * Even though we try to disable TLS compression, it is possible that
	 * this cannot be done with all TLS libraries. Add extra buffer space
	 * to handle the possibility of the decrypted data being longer than
	 * input data.
	 */
	out = wpabuf_alloc((wpabuf_len(in_data) + 500) * 3);
	if (out == NULL)
		return NULL;

	res = gnutls_record_recv(conn->session, wpabuf_mhead(out),
				 wpabuf_size(out));
	if (res < 0) {
		wpa_printf(MSG_DEBUG, "%s - gnutls_record_recv failed: %d "
			   "(%s)", __func__, (int) res, gnutls_strerror(res));
		wpabuf_free(out);
		return NULL;
	}
	wpabuf_put(out, res);

	return out;
}


int tls_connection_resumed(void *ssl_ctx, struct tls_connection *conn)
{
	if (conn == NULL)
		return 0;
	return gnutls_session_is_resumed(conn->session);
}


int tls_connection_set_cipher_list(void *tls_ctx, struct tls_connection *conn,
				   u8 *ciphers)
{
	/* TODO */
	return -1;
}


int tls_get_version(void *ssl_ctx, struct tls_connection *conn,
		    char *buf, size_t buflen)
{
	gnutls_protocol_t ver;

	ver = gnutls_protocol_get_version(conn->session);
	if (ver == GNUTLS_TLS1_0)
		os_strlcpy(buf, "TLSv1", buflen);
	else if (ver == GNUTLS_TLS1_1)
		os_strlcpy(buf, "TLSv1.1", buflen);
	else if (ver == GNUTLS_TLS1_2)
		os_strlcpy(buf, "TLSv1.2", buflen);
	else
		return -1;
	return 0;
}


int tls_get_cipher(void *ssl_ctx, struct tls_connection *conn,
		   char *buf, size_t buflen)
{
	gnutls_cipher_algorithm_t cipher;
	gnutls_kx_algorithm_t kx;
	gnutls_mac_algorithm_t mac;
	const char *kx_str, *cipher_str, *mac_str;
	int res;

	cipher = gnutls_cipher_get(conn->session);
	cipher_str = gnutls_cipher_get_name(cipher);
	if (!cipher_str)
		cipher_str = "";

	kx = gnutls_kx_get(conn->session);
	kx_str = gnutls_kx_get_name(kx);
	if (!kx_str)
		kx_str = "";

	mac = gnutls_mac_get(conn->session);
	mac_str = gnutls_mac_get_name(mac);
	if (!mac_str)
		mac_str = "";

	if (kx == GNUTLS_KX_RSA)
		res = os_snprintf(buf, buflen, "%s-%s", cipher_str, mac_str);
	else
		res = os_snprintf(buf, buflen, "%s-%s-%s",
				  kx_str, cipher_str, mac_str);
	if (os_snprintf_error(buflen, res))
		return -1;

	return 0;
}


int tls_connection_enable_workaround(void *ssl_ctx,
				     struct tls_connection *conn)
{
	gnutls_record_disable_padding(conn->session);
	return 0;
}


int tls_connection_client_hello_ext(void *ssl_ctx, struct tls_connection *conn,
				    int ext_type, const u8 *data,
				    size_t data_len)
{
	/* TODO */
	return -1;
}


int tls_connection_get_failed(void *ssl_ctx, struct tls_connection *conn)
{
	if (conn == NULL)
		return -1;
	return conn->failed;
}


int tls_connection_get_read_alerts(void *ssl_ctx, struct tls_connection *conn)
{
	if (conn == NULL)
		return -1;
	return conn->read_alerts;
}


int tls_connection_get_write_alerts(void *ssl_ctx, struct tls_connection *conn)
{
	if (conn == NULL)
		return -1;
	return conn->write_alerts;
}


int tls_connection_set_session_ticket_cb(void *tls_ctx,
					 struct tls_connection *conn,
					 tls_session_ticket_cb cb, void *ctx)
{
	return -1;
}


int tls_get_library_version(char *buf, size_t buf_len)
{
	return os_snprintf(buf, buf_len, "GnuTLS build=%s run=%s",
			   GNUTLS_VERSION, gnutls_check_version(NULL));
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
