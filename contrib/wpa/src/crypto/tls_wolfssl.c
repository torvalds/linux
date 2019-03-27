/*
 * SSL/TLS interface functions for wolfSSL TLS case
 * Copyright (c) 2004-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "tls.h"

/* wolfSSL includes */
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/error-ssl.h>
#include <wolfssl/wolfcrypt/asn.h>

#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST)
#define HAVE_AESGCM
#include <wolfssl/wolfcrypt/aes.h>
#endif

#if !defined(CONFIG_FIPS) &&                             \
    (defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) ||   \
     defined(EAP_SERVER_FAST))
#define WOLFSSL_NEED_EAP_FAST_PRF
#endif

#define SECRET_LEN          48
#define RAN_LEN             32
#define SESSION_TICKET_LEN  256

static int tls_ref_count = 0;

static int tls_ex_idx_session = 0;


/* tls input data for wolfSSL Read Callback */
struct tls_in_data {
	const struct wpabuf *in_data;
	size_t consumed; /* how many bytes have we used already */
};

/* tls output data for wolfSSL Write Callback */
struct tls_out_data {
	struct wpabuf *out_data;
};

struct tls_context {
	void (*event_cb)(void *ctx, enum tls_event ev,
			 union tls_event_data *data);
	void *cb_ctx;
	int cert_in_cb;
	char *ocsp_stapling_response;
};

static struct tls_context *tls_global = NULL;

/* wolfssl tls_connection */
struct tls_connection {
	struct tls_context *context;
	WOLFSSL *ssl;
	int read_alerts;
	int write_alerts;
	int failed;
	struct tls_in_data input;
	struct tls_out_data output;
	char *subject_match;
	char *alt_subject_match;
	char *suffix_match;
	char *domain_match;

	u8 srv_cert_hash[32];

	unsigned char client_random[RAN_LEN];
	unsigned char server_random[RAN_LEN];
	unsigned int flags;
#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST)
	tls_session_ticket_cb session_ticket_cb;
	void *session_ticket_cb_ctx;
	byte session_ticket[SESSION_TICKET_LEN];
#endif
	unsigned int ca_cert_verify:1;
	unsigned int cert_probe:1;
	unsigned int server_cert_only:1;
	unsigned int success_data:1;

	WOLFSSL_X509 *peer_cert;
	WOLFSSL_X509 *peer_issuer;
	WOLFSSL_X509 *peer_issuer_issuer;
};


static struct tls_context * tls_context_new(const struct tls_config *conf)
{
	struct tls_context *context = os_zalloc(sizeof(*context));

	if (!context)
		return NULL;

	if (conf) {
		context->event_cb = conf->event_cb;
		context->cb_ctx = conf->cb_ctx;
		context->cert_in_cb = conf->cert_in_cb;
	}

	return context;
}


static void wolfssl_reset_in_data(struct tls_in_data *in,
				  const struct wpabuf *buf)
{
	/* old one not owned by us so don't free */
	in->in_data = buf;
	in->consumed = 0;
}


static void wolfssl_reset_out_data(struct tls_out_data *out)
{
	/* old one not owned by us so don't free */
	out->out_data = wpabuf_alloc_copy("", 0);
}


/* wolfSSL I/O Receive CallBack */
static int wolfssl_receive_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
	size_t get = sz;
	struct tls_in_data *data = ctx;

	if (!data)
		return -1;

	if (get > (wpabuf_len(data->in_data) - data->consumed))
		get = wpabuf_len(data->in_data) - data->consumed;

	os_memcpy(buf, wpabuf_head(data->in_data) + data->consumed, get);
	data->consumed += get;

	if (get == 0)
		return -2; /* WANT_READ */

	return (int) get;
}


/* wolfSSL I/O Send CallBack */
static int wolfssl_send_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
	struct wpabuf *tmp;
	struct tls_out_data *data = ctx;

	if (!data)
		return -1;

	wpa_printf(MSG_DEBUG, "SSL: adding %d bytes", sz);

	tmp = wpabuf_alloc_copy(buf, sz);
	if (!tmp)
		return -1;
	data->out_data = wpabuf_concat(data->out_data, tmp);
	if (!data->out_data)
		return -1;

	return sz;
}


static void remove_session_cb(WOLFSSL_CTX *ctx, WOLFSSL_SESSION *sess)
{
	struct wpabuf *buf;

	buf = wolfSSL_SESSION_get_ex_data(sess, tls_ex_idx_session);
	if (!buf)
		return;
	wpa_printf(MSG_DEBUG,
		   "wolfSSL: Free application session data %p (sess %p)",
		   buf, sess);
	wpabuf_free(buf);

	wolfSSL_SESSION_set_ex_data(sess, tls_ex_idx_session, NULL);
}


void * tls_init(const struct tls_config *conf)
{
	WOLFSSL_CTX *ssl_ctx;
	struct tls_context *context;
	const char *ciphers;

#ifdef DEBUG_WOLFSSL
	wolfSSL_Debugging_ON();
#endif /* DEBUG_WOLFSSL */

	context = tls_context_new(conf);
	if (!context)
		return NULL;

	if (tls_ref_count == 0) {
		tls_global = context;

		if (wolfSSL_Init() < 0)
			return NULL;
		/* wolfSSL_Debugging_ON(); */
	}

	tls_ref_count++;

	/* start as client */
	ssl_ctx = wolfSSL_CTX_new(wolfSSLv23_client_method());
	if (!ssl_ctx) {
		tls_ref_count--;
		if (context != tls_global)
			os_free(context);
		if (tls_ref_count == 0) {
			os_free(tls_global);
			tls_global = NULL;
		}
	}
	wolfSSL_SetIORecv(ssl_ctx, wolfssl_receive_cb);
	wolfSSL_SetIOSend(ssl_ctx, wolfssl_send_cb);
	wolfSSL_CTX_set_ex_data(ssl_ctx, 0, context);

	if (conf->tls_session_lifetime > 0) {
		wolfSSL_CTX_set_quiet_shutdown(ssl_ctx, 1);
		wolfSSL_CTX_set_session_cache_mode(ssl_ctx,
						   SSL_SESS_CACHE_SERVER);
		wolfSSL_CTX_set_timeout(ssl_ctx, conf->tls_session_lifetime);
		wolfSSL_CTX_sess_set_remove_cb(ssl_ctx, remove_session_cb);
	} else {
		wolfSSL_CTX_set_session_cache_mode(ssl_ctx,
						   SSL_SESS_CACHE_CLIENT);
	}

	if (conf && conf->openssl_ciphers)
		ciphers = conf->openssl_ciphers;
	else
		ciphers = "ALL";
	if (wolfSSL_CTX_set_cipher_list(ssl_ctx, ciphers) != 1) {
		wpa_printf(MSG_ERROR,
			   "wolfSSL: Failed to set cipher string '%s'",
			   ciphers);
		tls_deinit(ssl_ctx);
		return NULL;
	}

	return ssl_ctx;
}


void tls_deinit(void *ssl_ctx)
{
	struct tls_context *context = wolfSSL_CTX_get_ex_data(ssl_ctx, 0);

	if (context != tls_global)
		os_free(context);

	wolfSSL_CTX_free((WOLFSSL_CTX *) ssl_ctx);

	tls_ref_count--;
	if (tls_ref_count == 0) {
		wolfSSL_Cleanup();
		os_free(tls_global);
		tls_global = NULL;
	}
}


int tls_get_errors(void *tls_ctx)
{
#ifdef DEBUG_WOLFSSL
#if 0
	unsigned long err;

	err = wolfSSL_ERR_peek_last_error_line(NULL, NULL);
	if (err != 0) {
		wpa_printf(MSG_INFO, "TLS - SSL error: %s",
			   wolfSSL_ERR_error_string(err, NULL));
		return 1;
	}
#endif
#endif /* DEBUG_WOLFSSL */
	return 0;
}


struct tls_connection * tls_connection_init(void *tls_ctx)
{
	WOLFSSL_CTX *ssl_ctx = tls_ctx;
	struct tls_connection *conn;

	wpa_printf(MSG_DEBUG, "SSL: connection init");

	conn = os_zalloc(sizeof(*conn));
	if (!conn)
		return NULL;
	conn->ssl = wolfSSL_new(ssl_ctx);
	if (!conn->ssl) {
		os_free(conn);
		return NULL;
	}

	wolfSSL_SetIOReadCtx(conn->ssl,  &conn->input);
	wolfSSL_SetIOWriteCtx(conn->ssl, &conn->output);
	wolfSSL_set_ex_data(conn->ssl, 0, conn);
	conn->context = wolfSSL_CTX_get_ex_data(ssl_ctx, 0);

	/* Need randoms post-hanshake for EAP-FAST, export key and deriving
	 * session ID in EAP methods. */
	wolfSSL_KeepArrays(conn->ssl);
	wolfSSL_KeepHandshakeResources(conn->ssl);
	wolfSSL_UseClientSuites(conn->ssl);

	return conn;
}


void tls_connection_deinit(void *tls_ctx, struct tls_connection *conn)
{
	if (!conn)
		return;

	wpa_printf(MSG_DEBUG, "SSL: connection deinit");

	/* parts */
	wolfSSL_free(conn->ssl);
	os_free(conn->subject_match);
	os_free(conn->alt_subject_match);
	os_free(conn->suffix_match);
	os_free(conn->domain_match);

	/* self */
	os_free(conn);
}


int tls_connection_established(void *tls_ctx, struct tls_connection *conn)
{
	return conn ? wolfSSL_is_init_finished(conn->ssl) : 0;
}


char * tls_connection_peer_serial_num(void *tls_ctx,
				      struct tls_connection *conn)
{
	/* TODO */
	return NULL;
}


int tls_connection_shutdown(void *tls_ctx, struct tls_connection *conn)
{
	WOLFSSL_SESSION *session;

	if (!conn)
		return -1;

	wpa_printf(MSG_DEBUG, "SSL: connection shutdown");

	/* Set quiet as OpenSSL does */
	wolfSSL_set_quiet_shutdown(conn->ssl, 1);
	wolfSSL_shutdown(conn->ssl);

	session = wolfSSL_get_session(conn->ssl);
	if (wolfSSL_clear(conn->ssl) != 1)
		return -1;
	wolfSSL_set_session(conn->ssl, session);

	return 0;
}


static int tls_connection_set_subject_match(struct tls_connection *conn,
					    const char *subject_match,
					    const char *alt_subject_match,
					    const char *suffix_match,
					    const char *domain_match)
{
	os_free(conn->subject_match);
	conn->subject_match = NULL;
	if (subject_match) {
		conn->subject_match = os_strdup(subject_match);
		if (!conn->subject_match)
			return -1;
	}

	os_free(conn->alt_subject_match);
	conn->alt_subject_match = NULL;
	if (alt_subject_match) {
		conn->alt_subject_match = os_strdup(alt_subject_match);
		if (!conn->alt_subject_match)
			return -1;
	}

	os_free(conn->suffix_match);
	conn->suffix_match = NULL;
	if (suffix_match) {
		conn->suffix_match = os_strdup(suffix_match);
		if (!conn->suffix_match)
			return -1;
	}

	os_free(conn->domain_match);
	conn->domain_match = NULL;
	if (domain_match) {
		conn->domain_match = os_strdup(domain_match);
		if (!conn->domain_match)
			return -1;
	}

	return 0;
}


static int tls_connection_dh(struct tls_connection *conn, const char *dh_file,
			     const u8 *dh_blob, size_t blob_len)
{
	if (!dh_file && !dh_blob)
		return 0;

	wolfSSL_set_accept_state(conn->ssl);

	if (dh_blob) {
		if (wolfSSL_SetTmpDH_buffer(conn->ssl, dh_blob, blob_len,
					    SSL_FILETYPE_ASN1) < 0) {
			wpa_printf(MSG_INFO, "SSL: use DH DER blob failed");
			return -1;
		}
		wpa_printf(MSG_DEBUG, "SSL: use DH blob OK");
		return 0;
	}

	if (dh_file) {
		wpa_printf(MSG_INFO, "SSL: use DH PEM file: %s", dh_file);
		if (wolfSSL_SetTmpDH_file(conn->ssl, dh_file,
					  SSL_FILETYPE_PEM) < 0) {
			wpa_printf(MSG_INFO, "SSL: use DH PEM file failed");
			if (wolfSSL_SetTmpDH_file(conn->ssl, dh_file,
						  SSL_FILETYPE_ASN1) < 0) {
				wpa_printf(MSG_INFO,
					   "SSL: use DH DER file failed");
				return -1;
			}
		}
		wpa_printf(MSG_DEBUG, "SSL: use DH file OK");
		return 0;
	}

	return 0;
}


static int tls_connection_client_cert(struct tls_connection *conn,
				      const char *client_cert,
				      const u8 *client_cert_blob,
				      size_t blob_len)
{
	if (!client_cert && !client_cert_blob)
		return 0;

	if (client_cert_blob) {
		if (wolfSSL_use_certificate_chain_buffer_format(
			    conn->ssl, client_cert_blob, blob_len,
			    SSL_FILETYPE_ASN1) < 0) {
			wpa_printf(MSG_INFO,
				   "SSL: use client cert DER blob failed");
			return -1;
		}
		wpa_printf(MSG_DEBUG, "SSL: use client cert blob OK");
		return 0;
	}

	if (client_cert) {
		if (wolfSSL_use_certificate_chain_file(conn->ssl,
						       client_cert) < 0) {
			wpa_printf(MSG_INFO,
				   "SSL: use client cert PEM file failed");
			if (wolfSSL_use_certificate_chain_file_format(
				    conn->ssl, client_cert,
				    SSL_FILETYPE_ASN1) < 0) {
				wpa_printf(MSG_INFO,
					   "SSL: use client cert DER file failed");
				return -1;
			}
		}
		wpa_printf(MSG_DEBUG, "SSL: use client cert file OK");
		return 0;
	}

	return 0;
}


static int tls_passwd_cb(char *buf, int size, int rwflag, void *password)
{
	if (!password)
		return 0;
	os_strlcpy(buf, (char *) password, size);
	return os_strlen(buf);
}


static int tls_connection_private_key(void *tls_ctx,
				      struct tls_connection *conn,
				      const char *private_key,
				      const char *private_key_passwd,
				      const u8 *private_key_blob,
				      size_t blob_len)
{
	WOLFSSL_CTX *ctx = tls_ctx;
	char *passwd = NULL;
	int ok = 0;

	if (!private_key && !private_key_blob)
		return 0;

	if (private_key_passwd) {
		passwd = os_strdup(private_key_passwd);
		if (!passwd)
			return -1;
	}

	wolfSSL_CTX_set_default_passwd_cb(ctx, tls_passwd_cb);
	wolfSSL_CTX_set_default_passwd_cb_userdata(ctx, passwd);

	if (private_key_blob) {
		if (wolfSSL_use_PrivateKey_buffer(conn->ssl,
						  private_key_blob, blob_len,
						  SSL_FILETYPE_ASN1) < 0) {
			wpa_printf(MSG_INFO,
				   "SSL: use private DER blob failed");
		} else {
			wpa_printf(MSG_DEBUG, "SSL: use private key blob OK");
			ok = 1;
		}
	}

	if (!ok && private_key) {
		if (wolfSSL_use_PrivateKey_file(conn->ssl, private_key,
						SSL_FILETYPE_PEM) < 0) {
			wpa_printf(MSG_INFO,
				   "SSL: use private key PEM file failed");
			if (wolfSSL_use_PrivateKey_file(conn->ssl, private_key,
							SSL_FILETYPE_ASN1) < 0)
			{
				wpa_printf(MSG_INFO,
					   "SSL: use private key DER file failed");
			} else {
				ok = 1;
			}
		} else {
			ok = 1;
		}

		if (ok)
			wpa_printf(MSG_DEBUG, "SSL: use private key file OK");
	}

	wolfSSL_CTX_set_default_passwd_cb(ctx, NULL);
	os_free(passwd);

	if (!ok)
		return -1;

	return 0;
}


static int tls_match_alt_subject_component(WOLFSSL_X509 *cert, int type,
					   const char *value, size_t len)
{
	WOLFSSL_ASN1_OBJECT *gen;
	void *ext;
	int found = 0;
	int i;

	ext = wolfSSL_X509_get_ext_d2i(cert, ALT_NAMES_OID, NULL, NULL);

	for (i = 0; ext && i < wolfSSL_sk_num(ext); i++) {
		gen = wolfSSL_sk_value(ext, i);
		if (gen->type != type)
			continue;
		if (os_strlen((char *) gen->obj) == len &&
		    os_memcmp(value, gen->obj, len) == 0)
			found++;
	}

	wolfSSL_sk_ASN1_OBJECT_free(ext);

	return found;
}


static int tls_match_alt_subject(WOLFSSL_X509 *cert, const char *match)
{
	int type;
	const char *pos, *end;
	size_t len;

	pos = match;
	do {
		if (os_strncmp(pos, "EMAIL:", 6) == 0) {
			type = GEN_EMAIL;
			pos += 6;
		} else if (os_strncmp(pos, "DNS:", 4) == 0) {
			type = GEN_DNS;
			pos += 4;
		} else if (os_strncmp(pos, "URI:", 4) == 0) {
			type = GEN_URI;
			pos += 4;
		} else {
			wpa_printf(MSG_INFO,
				   "TLS: Invalid altSubjectName match '%s'",
				   pos);
			return 0;
		}
		end = os_strchr(pos, ';');
		while (end) {
			if (os_strncmp(end + 1, "EMAIL:", 6) == 0 ||
			    os_strncmp(end + 1, "DNS:", 4) == 0 ||
			    os_strncmp(end + 1, "URI:", 4) == 0)
				break;
			end = os_strchr(end + 1, ';');
		}
		if (end)
			len = end - pos;
		else
			len = os_strlen(pos);
		if (tls_match_alt_subject_component(cert, type, pos, len) > 0)
			return 1;
		pos = end + 1;
	} while (end);

	return 0;
}


static int domain_suffix_match(const char *val, size_t len, const char *match,
			       int full)
{
	size_t i, match_len;

	/* Check for embedded nuls that could mess up suffix matching */
	for (i = 0; i < len; i++) {
		if (val[i] == '\0') {
			wpa_printf(MSG_DEBUG,
				   "TLS: Embedded null in a string - reject");
			return 0;
		}
	}

	match_len = os_strlen(match);
	if (match_len > len || (full && match_len != len))
		return 0;

	if (os_strncasecmp(val + len - match_len, match, match_len) != 0)
		return 0; /* no match */

	if (match_len == len)
		return 1; /* exact match */

	if (val[len - match_len - 1] == '.')
		return 1; /* full label match completes suffix match */

	wpa_printf(MSG_DEBUG, "TLS: Reject due to incomplete label match");
	return 0;
}


static int tls_match_suffix(WOLFSSL_X509 *cert, const char *match, int full)
{
	WOLFSSL_ASN1_OBJECT *gen;
	void *ext;
	int i;
	int j;
	int dns_name = 0;
	WOLFSSL_X509_NAME *name;

	wpa_printf(MSG_DEBUG, "TLS: Match domain against %s%s",
		   full ? "" : "suffix ", match);

	ext = wolfSSL_X509_get_ext_d2i(cert, ALT_NAMES_OID, NULL, NULL);

	for (j = 0; ext && j < wolfSSL_sk_num(ext); j++) {
		gen = wolfSSL_sk_value(ext, j);
		if (gen->type != ALT_NAMES_OID)
			continue;
		dns_name++;
		wpa_hexdump_ascii(MSG_DEBUG, "TLS: Certificate dNSName",
				  gen->obj, os_strlen((char *)gen->obj));
		if (domain_suffix_match((const char *) gen->obj,
					os_strlen((char *) gen->obj), match,
					full) == 1) {
			wpa_printf(MSG_DEBUG, "TLS: %s in dNSName found",
				   full ? "Match" : "Suffix match");
			wolfSSL_sk_ASN1_OBJECT_free(ext);
			return 1;
		}
	}
	wolfSSL_sk_ASN1_OBJECT_free(ext);

	if (dns_name) {
		wpa_printf(MSG_DEBUG, "TLS: None of the dNSName(s) matched");
		return 0;
	}

	name = wolfSSL_X509_get_subject_name(cert);
	i = -1;
	for (;;) {
		WOLFSSL_X509_NAME_ENTRY *e;
		WOLFSSL_ASN1_STRING *cn;

		i = wolfSSL_X509_NAME_get_index_by_NID(name, ASN_COMMON_NAME,
						       i);
		if (i == -1)
			break;
		e = wolfSSL_X509_NAME_get_entry(name, i);
		if (!e)
			continue;
		cn = wolfSSL_X509_NAME_ENTRY_get_data(e);
		if (!cn)
			continue;
		wpa_hexdump_ascii(MSG_DEBUG, "TLS: Certificate commonName",
				  cn->data, cn->length);
		if (domain_suffix_match(cn->data, cn->length, match, full) == 1)
		{
			wpa_printf(MSG_DEBUG, "TLS: %s in commonName found",
				   full ? "Match" : "Suffix match");
			return 1;
		}
	}

	wpa_printf(MSG_DEBUG, "TLS: No CommonName %smatch found",
		   full ? "" : "suffix ");
	return 0;
}


static enum tls_fail_reason wolfssl_tls_fail_reason(int err)
{
	switch (err) {
	case X509_V_ERR_CERT_REVOKED:
		return TLS_FAIL_REVOKED;
	case ASN_BEFORE_DATE_E:
	case X509_V_ERR_CERT_NOT_YET_VALID:
	case X509_V_ERR_CRL_NOT_YET_VALID:
		return TLS_FAIL_NOT_YET_VALID;
	case ASN_AFTER_DATE_E:
	case X509_V_ERR_CERT_HAS_EXPIRED:
	case X509_V_ERR_CRL_HAS_EXPIRED:
		return TLS_FAIL_EXPIRED;
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
	case X509_V_ERR_UNABLE_TO_GET_CRL:
	case X509_V_ERR_UNABLE_TO_GET_CRL_ISSUER:
	case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
	case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
	case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
	case X509_V_ERR_CERT_CHAIN_TOO_LONG:
	case X509_V_ERR_PATH_LENGTH_EXCEEDED:
	case X509_V_ERR_INVALID_CA:
		return TLS_FAIL_UNTRUSTED;
	case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE:
	case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
	case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
	case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
	case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
	case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD:
	case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
	case X509_V_ERR_CERT_UNTRUSTED:
	case X509_V_ERR_CERT_REJECTED:
		return TLS_FAIL_BAD_CERTIFICATE;
	default:
		return TLS_FAIL_UNSPECIFIED;
	}
}


static const char * wolfssl_tls_err_string(int err, const char *err_str)
{
	switch (err) {
	case ASN_BEFORE_DATE_E:
		return "certificate is not yet valid";
	case ASN_AFTER_DATE_E:
		return "certificate has expired";
	default:
		return err_str;
	}
}


static struct wpabuf * get_x509_cert(WOLFSSL_X509 *cert)
{
	struct wpabuf *buf = NULL;
	const u8 *data;
	int cert_len;

	data = wolfSSL_X509_get_der(cert, &cert_len);
	if (!data)
		buf = wpabuf_alloc_copy(data, cert_len);

	return buf;
}


static void wolfssl_tls_fail_event(struct tls_connection *conn,
				   WOLFSSL_X509 *err_cert, int err, int depth,
				   const char *subject, const char *err_str,
				   enum tls_fail_reason reason)
{
	union tls_event_data ev;
	struct wpabuf *cert = NULL;
	struct tls_context *context = conn->context;

	if (!context->event_cb)
		return;

	cert = get_x509_cert(err_cert);
	os_memset(&ev, 0, sizeof(ev));
	ev.cert_fail.reason = reason != TLS_FAIL_UNSPECIFIED ?
		reason : wolfssl_tls_fail_reason(err);
	ev.cert_fail.depth = depth;
	ev.cert_fail.subject = subject;
	ev.cert_fail.reason_txt = wolfssl_tls_err_string(err, err_str);
	ev.cert_fail.cert = cert;
	context->event_cb(context->cb_ctx, TLS_CERT_CHAIN_FAILURE, &ev);
	wpabuf_free(cert);
}


static void wolfssl_tls_cert_event(struct tls_connection *conn,
				   WOLFSSL_X509 *err_cert, int depth,
				   const char *subject)
{
	struct wpabuf *cert = NULL;
	union tls_event_data ev;
	struct tls_context *context = conn->context;
	char *alt_subject[TLS_MAX_ALT_SUBJECT];
	int alt, num_alt_subject = 0;
	WOLFSSL_ASN1_OBJECT *gen;
	void *ext;
	int i;
#ifdef CONFIG_SHA256
	u8 hash[32];
#endif /* CONFIG_SHA256 */

	if (!context->event_cb)
		return;

	os_memset(&ev, 0, sizeof(ev));
	if (conn->cert_probe || (conn->flags & TLS_CONN_EXT_CERT_CHECK) ||
	    context->cert_in_cb) {
		cert = get_x509_cert(err_cert);
		ev.peer_cert.cert = cert;
	}

#ifdef CONFIG_SHA256
	if (cert) {
		const u8 *addr[1];
		size_t len[1];

		addr[0] = wpabuf_head(cert);
		len[0] = wpabuf_len(cert);
		if (sha256_vector(1, addr, len, hash) == 0) {
			ev.peer_cert.hash = hash;
			ev.peer_cert.hash_len = sizeof(hash);
		}
	}
#endif /* CONFIG_SHA256 */

	ev.peer_cert.depth = depth;
	ev.peer_cert.subject = subject;

	ext = wolfSSL_X509_get_ext_d2i(err_cert, ALT_NAMES_OID, NULL, NULL);
	for (i = 0; ext && i < wolfSSL_sk_num(ext); i++) {
		char *pos;

		if (num_alt_subject == TLS_MAX_ALT_SUBJECT)
			break;
		gen = wolfSSL_sk_value((void *) ext, i);
		if (gen->type != GEN_EMAIL &&
		    gen->type != GEN_DNS &&
		    gen->type != GEN_URI)
			continue;

		pos = os_malloc(10 + os_strlen((char *) gen->obj) + 1);
		if (!pos)
			break;
		alt_subject[num_alt_subject++] = pos;

		switch (gen->type) {
		case GEN_EMAIL:
			os_memcpy(pos, "EMAIL:", 6);
			pos += 6;
			break;
		case GEN_DNS:
			os_memcpy(pos, "DNS:", 4);
			pos += 4;
			break;
		case GEN_URI:
			os_memcpy(pos, "URI:", 4);
			pos += 4;
			break;
		}

		os_memcpy(pos, gen->obj, os_strlen((char *)gen->obj));
		pos += os_strlen((char *)gen->obj);
		*pos = '\0';
	}
	wolfSSL_sk_ASN1_OBJECT_free(ext);

	for (alt = 0; alt < num_alt_subject; alt++)
		ev.peer_cert.altsubject[alt] = alt_subject[alt];
	ev.peer_cert.num_altsubject = num_alt_subject;

	context->event_cb(context->cb_ctx, TLS_PEER_CERTIFICATE, &ev);
	wpabuf_free(cert);
	for (alt = 0; alt < num_alt_subject; alt++)
		os_free(alt_subject[alt]);
}


static int tls_verify_cb(int preverify_ok, WOLFSSL_X509_STORE_CTX *x509_ctx)
{
	char buf[256];
	WOLFSSL_X509 *err_cert;
	int err, depth;
	WOLFSSL *ssl;
	struct tls_connection *conn;
	struct tls_context *context;
	char *match, *altmatch, *suffix_match, *domain_match;
	const char *err_str;

	err_cert = wolfSSL_X509_STORE_CTX_get_current_cert(x509_ctx);
	if (!err_cert) {
		wpa_printf(MSG_DEBUG, "wolfSSL: No Cert");
		return 0;
	}

	err = wolfSSL_X509_STORE_CTX_get_error(x509_ctx);
	depth = wolfSSL_X509_STORE_CTX_get_error_depth(x509_ctx);
	ssl = wolfSSL_X509_STORE_CTX_get_ex_data(
		x509_ctx, wolfSSL_get_ex_data_X509_STORE_CTX_idx());
	wolfSSL_X509_NAME_oneline(wolfSSL_X509_get_subject_name(err_cert), buf,
				  sizeof(buf));

	conn = wolfSSL_get_ex_data(ssl, 0);
	if (!conn) {
		wpa_printf(MSG_DEBUG, "wolfSSL: No ex_data");
		return 0;
	}

	if (depth == 0)
		conn->peer_cert = err_cert;
	else if (depth == 1)
		conn->peer_issuer = err_cert;
	else if (depth == 2)
		conn->peer_issuer_issuer = err_cert;

	context = conn->context;
	match = conn->subject_match;
	altmatch = conn->alt_subject_match;
	suffix_match = conn->suffix_match;
	domain_match = conn->domain_match;

	if (!preverify_ok && !conn->ca_cert_verify)
		preverify_ok = 1;
	if (!preverify_ok && depth > 0 && conn->server_cert_only)
		preverify_ok = 1;
	if (!preverify_ok && (conn->flags & TLS_CONN_DISABLE_TIME_CHECKS) &&
	    (err == X509_V_ERR_CERT_HAS_EXPIRED ||
	     err == ASN_AFTER_DATE_E || err == ASN_BEFORE_DATE_E ||
	     err == X509_V_ERR_CERT_NOT_YET_VALID)) {
		wpa_printf(MSG_DEBUG,
			   "wolfSSL: Ignore certificate validity time mismatch");
		preverify_ok = 1;
	}

	err_str = wolfSSL_X509_verify_cert_error_string(err);

#ifdef CONFIG_SHA256
	/*
	 * Do not require preverify_ok so we can explicity allow otherwise
	 * invalid pinned server certificates.
	 */
	if (depth == 0 && conn->server_cert_only) {
		struct wpabuf *cert;

		cert = get_x509_cert(err_cert);
		if (!cert) {
			wpa_printf(MSG_DEBUG,
				   "wolfSSL: Could not fetch server certificate data");
			preverify_ok = 0;
		} else {
			u8 hash[32];
			const u8 *addr[1];
			size_t len[1];

			addr[0] = wpabuf_head(cert);
			len[0] = wpabuf_len(cert);
			if (sha256_vector(1, addr, len, hash) < 0 ||
			    os_memcmp(conn->srv_cert_hash, hash, 32) != 0) {
				err_str = "Server certificate mismatch";
				err = X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN;
				preverify_ok = 0;
			} else if (!preverify_ok) {
				/*
				 * Certificate matches pinned certificate, allow
				 * regardless of other problems.
				 */
				wpa_printf(MSG_DEBUG,
					   "wolfSSL: Ignore validation issues for a pinned server certificate");
				preverify_ok = 1;
			}
			wpabuf_free(cert);
		}
	}
#endif /* CONFIG_SHA256 */

	if (!preverify_ok) {
		wpa_printf(MSG_WARNING,
			   "TLS: Certificate verification failed, error %d (%s) depth %d for '%s'",
			   err, err_str, depth, buf);
		wolfssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       err_str, TLS_FAIL_UNSPECIFIED);
		return preverify_ok;
	}

	wpa_printf(MSG_DEBUG,
		   "TLS: %s - preverify_ok=%d err=%d (%s) ca_cert_verify=%d depth=%d buf='%s'",
		   __func__, preverify_ok, err, err_str,
		   conn->ca_cert_verify, depth, buf);
	if (depth == 0 && match && os_strstr(buf, match) == NULL) {
		wpa_printf(MSG_WARNING,
			   "TLS: Subject '%s' did not match with '%s'",
			   buf, match);
		preverify_ok = 0;
		wolfssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       "Subject mismatch",
				       TLS_FAIL_SUBJECT_MISMATCH);
	} else if (depth == 0 && altmatch &&
		   !tls_match_alt_subject(err_cert, altmatch)) {
		wpa_printf(MSG_WARNING,
			   "TLS: altSubjectName match '%s' not found",
			   altmatch);
		preverify_ok = 0;
		wolfssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       "AltSubject mismatch",
				       TLS_FAIL_ALTSUBJECT_MISMATCH);
	} else if (depth == 0 && suffix_match &&
		   !tls_match_suffix(err_cert, suffix_match, 0)) {
		wpa_printf(MSG_WARNING,
			   "TLS: Domain suffix match '%s' not found",
			   suffix_match);
		preverify_ok = 0;
		wolfssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       "Domain suffix mismatch",
				       TLS_FAIL_DOMAIN_SUFFIX_MISMATCH);
	} else if (depth == 0 && domain_match &&
		   !tls_match_suffix(err_cert, domain_match, 1)) {
		wpa_printf(MSG_WARNING, "TLS: Domain match '%s' not found",
			   domain_match);
		preverify_ok = 0;
		wolfssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       "Domain mismatch",
				       TLS_FAIL_DOMAIN_MISMATCH);
	} else {
		wolfssl_tls_cert_event(conn, err_cert, depth, buf);
	}

	if (conn->cert_probe && preverify_ok && depth == 0) {
		wpa_printf(MSG_DEBUG,
			   "wolfSSL: Reject server certificate on probe-only run");
		preverify_ok = 0;
		wolfssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       "Server certificate chain probe",
				       TLS_FAIL_SERVER_CHAIN_PROBE);
	}

#ifdef HAVE_OCSP_WOLFSSL
	if (depth == 0 && (conn->flags & TLS_CONN_REQUEST_OCSP) &&
	    preverify_ok) {
		enum ocsp_result res;

		res = check_ocsp_resp(conn->ssl_ctx, conn->ssl, err_cert,
				      conn->peer_issuer,
				      conn->peer_issuer_issuer);
		if (res == OCSP_REVOKED) {
			preverify_ok = 0;
			wolfssl_tls_fail_event(conn, err_cert, err, depth, buf,
					       "certificate revoked",
					       TLS_FAIL_REVOKED);
			if (err == X509_V_OK)
				X509_STORE_CTX_set_error(
					x509_ctx, X509_V_ERR_CERT_REVOKED);
		} else if (res != OCSP_GOOD &&
			   (conn->flags & TLS_CONN_REQUIRE_OCSP)) {
			preverify_ok = 0;
			wolfssl_tls_fail_event(conn, err_cert, err, depth, buf,
					       "bad certificate status response",
					       TLS_FAIL_UNSPECIFIED);
		}
	}
#endif /* HAVE_OCSP_WOLFSSL */
	if (depth == 0 && preverify_ok && context->event_cb != NULL)
		context->event_cb(context->cb_ctx,
				  TLS_CERT_CHAIN_SUCCESS, NULL);

	return preverify_ok;
}


static int tls_connection_ca_cert(void *tls_ctx, struct tls_connection *conn,
				  const char *ca_cert,
				  const u8 *ca_cert_blob, size_t blob_len,
				  const char *ca_path)
{
	WOLFSSL_CTX *ctx = tls_ctx;

	wolfSSL_set_verify(conn->ssl, SSL_VERIFY_PEER, tls_verify_cb);
	conn->ca_cert_verify = 1;

	if (ca_cert && os_strncmp(ca_cert, "probe://", 8) == 0) {
		wpa_printf(MSG_DEBUG,
			   "wolfSSL: Probe for server certificate chain");
		conn->cert_probe = 1;
		conn->ca_cert_verify = 0;
		return 0;
	}

	if (ca_cert && os_strncmp(ca_cert, "hash://", 7) == 0) {
#ifdef CONFIG_SHA256
		const char *pos = ca_cert + 7;

		if (os_strncmp(pos, "server/sha256/", 14) != 0) {
			wpa_printf(MSG_DEBUG,
				   "wolfSSL: Unsupported ca_cert hash value '%s'",
				   ca_cert);
			return -1;
		}
		pos += 14;
		if (os_strlen(pos) != 32 * 2) {
			wpa_printf(MSG_DEBUG,
				   "wolfSSL: Unexpected SHA256 hash length in ca_cert '%s'",
				   ca_cert);
			return -1;
		}
		if (hexstr2bin(pos, conn->srv_cert_hash, 32) < 0) {
			wpa_printf(MSG_DEBUG,
				   "wolfSSL: Invalid SHA256 hash value in ca_cert '%s'",
				   ca_cert);
			return -1;
		}
		conn->server_cert_only = 1;
		wpa_printf(MSG_DEBUG,
			   "wolfSSL: Checking only server certificate match");
		return 0;
#else /* CONFIG_SHA256 */
		wpa_printf(MSG_INFO,
			   "No SHA256 included in the build - cannot validate server certificate hash");
		return -1;
#endif /* CONFIG_SHA256 */
	}

	if (ca_cert_blob) {
		if (wolfSSL_CTX_load_verify_buffer(ctx, ca_cert_blob, blob_len,
						   SSL_FILETYPE_ASN1) !=
		    SSL_SUCCESS) {
			wpa_printf(MSG_INFO, "SSL: failed to load CA blob");
			return -1;
		}
		wpa_printf(MSG_DEBUG, "SSL: use CA cert blob OK");
		return 0;
	}

	if (ca_cert || ca_path) {
		WOLFSSL_X509_STORE *cm = wolfSSL_X509_STORE_new();

		if (!cm) {
			wpa_printf(MSG_INFO,
				   "SSL: failed to create certificate store");
			return -1;
		}
		wolfSSL_CTX_set_cert_store(ctx, cm);

		if (wolfSSL_CTX_load_verify_locations(ctx, ca_cert, ca_path) !=
		    SSL_SUCCESS) {
			wpa_printf(MSG_INFO,
				   "SSL: failed to load ca_cert as PEM");

			if (!ca_cert)
				return -1;

			if (wolfSSL_CTX_der_load_verify_locations(
				    ctx, ca_cert, SSL_FILETYPE_ASN1) !=
			    SSL_SUCCESS) {
				wpa_printf(MSG_INFO,
					   "SSL: failed to load ca_cert as DER");
				return -1;
			}
		}
		return 0;
	}

	conn->ca_cert_verify = 0;
	return 0;
}


static void tls_set_conn_flags(WOLFSSL *ssl, unsigned int flags)
{
#ifdef HAVE_SESSION_TICKET
#if 0
	if (!(flags & TLS_CONN_DISABLE_SESSION_TICKET))
		wolfSSL_UseSessionTicket(ssl);
#endif
#endif /* HAVE_SESSION_TICKET */

	if (flags & TLS_CONN_DISABLE_TLSv1_0)
		wolfSSL_set_options(ssl, SSL_OP_NO_TLSv1);
	if (flags & TLS_CONN_DISABLE_TLSv1_1)
		wolfSSL_set_options(ssl, SSL_OP_NO_TLSv1_1);
	if (flags & TLS_CONN_DISABLE_TLSv1_2)
		wolfSSL_set_options(ssl, SSL_OP_NO_TLSv1_2);
}


int tls_connection_set_params(void *tls_ctx, struct tls_connection *conn,
			      const struct tls_connection_params *params)
{
	wpa_printf(MSG_DEBUG, "SSL: set params");

	if (tls_connection_set_subject_match(conn, params->subject_match,
					     params->altsubject_match,
					     params->suffix_match,
					     params->domain_match) < 0) {
		wpa_printf(MSG_INFO, "Error setting subject match");
		return -1;
	}

	if (tls_connection_ca_cert(tls_ctx, conn, params->ca_cert,
				   params->ca_cert_blob,
				   params->ca_cert_blob_len,
				   params->ca_path) < 0) {
		wpa_printf(MSG_INFO, "Error setting CA cert");
		return -1;
	}

	if (tls_connection_client_cert(conn, params->client_cert,
				       params->client_cert_blob,
				       params->client_cert_blob_len) < 0) {
		wpa_printf(MSG_INFO, "Error setting client cert");
		return -1;
	}

	if (tls_connection_private_key(tls_ctx, conn, params->private_key,
				       params->private_key_passwd,
				       params->private_key_blob,
				       params->private_key_blob_len) < 0) {
		wpa_printf(MSG_INFO, "Error setting private key");
		return -1;
	}

	if (tls_connection_dh(conn, params->dh_file, params->dh_blob,
			      params->dh_blob_len) < 0) {
		wpa_printf(MSG_INFO, "Error setting DH");
		return -1;
	}

	if (params->openssl_ciphers &&
	    wolfSSL_set_cipher_list(conn->ssl, params->openssl_ciphers) != 1) {
		wpa_printf(MSG_INFO,
			   "wolfSSL: Failed to set cipher string '%s'",
			   params->openssl_ciphers);
		return -1;
	}

	tls_set_conn_flags(conn->ssl, params->flags);

#ifdef HAVE_CERTIFICATE_STATUS_REQUEST
	if (params->flags & TLS_CONN_REQUEST_OCSP) {
		if (wolfSSL_UseOCSPStapling(conn->ssl, WOLFSSL_CSR_OCSP,
					    WOLFSSL_CSR_OCSP_USE_NONCE) !=
		    SSL_SUCCESS)
			return -1;
		wolfSSL_CTX_EnableOCSP(tls_ctx, 0);
	}
#endif /* HAVE_CERTIFICATE_STATUS_REQUEST */
#ifdef HAVE_CERTIFICATE_STATUS_REQUEST_V2
	if (params->flags & TLS_CONN_REQUEST_OCSP) {
		if (wolfSSL_UseOCSPStaplingV2(conn->ssl,
					      WOLFSSL_CSR2_OCSP_MULTI, 0) !=
		    SSL_SUCCESS)
			return -1;
		wolfSSL_CTX_EnableOCSP(tls_ctx, 0);
	}
#endif /* HAVE_CERTIFICATE_STATUS_REQUEST_V2 */
#if !defined(HAVE_CERTIFICATE_STATUS_REQUEST) && \
    !defined(HAVE_CERTIFICATE_STATUS_REQUEST_V2)
#ifdef HAVE_OCSP
	if (params->flags & TLS_CONN_REQUEST_OCSP)
		wolfSSL_CTX_EnableOCSP(ctx, 0);
#else /* HAVE_OCSP */
	if (params->flags & TLS_CONN_REQUIRE_OCSP) {
		wpa_printf(MSG_INFO,
			   "wolfSSL: No OCSP support included - reject configuration");
		return -1;
	}
	if (params->flags & TLS_CONN_REQUEST_OCSP) {
		wpa_printf(MSG_DEBUG,
			   "wolfSSL: No OCSP support included - allow optional OCSP case to continue");
	}
#endif /* HAVE_OCSP */
#endif /* !HAVE_CERTIFICATE_STATUS_REQUEST &&
	* !HAVE_CERTIFICATE_STATUS_REQUEST_V2 */

	conn->flags = params->flags;

	return 0;
}


static int tls_global_ca_cert(void *ssl_ctx, const char *ca_cert)
{
	WOLFSSL_CTX *ctx = ssl_ctx;

	if (ca_cert) {
		if (wolfSSL_CTX_load_verify_locations(ctx, ca_cert, NULL) != 1)
		{
			wpa_printf(MSG_WARNING,
				   "Failed to load root certificates");
			return -1;
		}

		wpa_printf(MSG_DEBUG,
			   "TLS: Trusted root certificate(s) loaded");
	}

	return 0;
}


static int tls_global_client_cert(void *ssl_ctx, const char *client_cert)
{
	WOLFSSL_CTX *ctx = ssl_ctx;

	if (!client_cert)
		return 0;

	if (wolfSSL_CTX_use_certificate_chain_file_format(ctx, client_cert,
							  SSL_FILETYPE_ASN1) !=
	    SSL_SUCCESS &&
	    wolfSSL_CTX_use_certificate_chain_file(ctx, client_cert) !=
	    SSL_SUCCESS) {
		wpa_printf(MSG_INFO, "Failed to load client certificate");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "SSL: Loaded global client certificate: %s",
		   client_cert);

	return 0;
}


static int tls_global_private_key(void *ssl_ctx, const char *private_key,
				  const char *private_key_passwd)
{
	WOLFSSL_CTX *ctx = ssl_ctx;
	char *passwd = NULL;
	int ret = 0;

	if (!private_key)
		return 0;

	if (private_key_passwd) {
		passwd = os_strdup(private_key_passwd);
		if (!passwd)
			return -1;
	}

	wolfSSL_CTX_set_default_passwd_cb(ctx, tls_passwd_cb);
	wolfSSL_CTX_set_default_passwd_cb_userdata(ctx, passwd);

	if (wolfSSL_CTX_use_PrivateKey_file(ctx, private_key,
					    SSL_FILETYPE_ASN1) != 1 &&
	    wolfSSL_CTX_use_PrivateKey_file(ctx, private_key,
					    SSL_FILETYPE_PEM) != 1) {
		wpa_printf(MSG_INFO, "Failed to load private key");
		ret = -1;
	}

	wpa_printf(MSG_DEBUG, "SSL: Loaded global private key");

	os_free(passwd);
	wolfSSL_CTX_set_default_passwd_cb(ctx, NULL);

	return ret;
}


static int tls_global_dh(void *ssl_ctx, const char *dh_file,
			 const u8 *dh_blob, size_t blob_len)
{
	WOLFSSL_CTX *ctx = ssl_ctx;

	if (!dh_file && !dh_blob)
		return 0;

	if (dh_blob) {
		if (wolfSSL_CTX_SetTmpDH_buffer(ctx, dh_blob, blob_len,
						SSL_FILETYPE_ASN1) < 0) {
			wpa_printf(MSG_INFO,
				   "SSL: global use DH DER blob failed");
			return -1;
		}
		wpa_printf(MSG_DEBUG, "SSL: global use DH blob OK");
		return 0;
	}

	if (dh_file) {
		if (wolfSSL_CTX_SetTmpDH_file(ctx, dh_file, SSL_FILETYPE_PEM) <
		    0) {
			wpa_printf(MSG_INFO,
				   "SSL: global use DH PEM file failed");
			if (wolfSSL_CTX_SetTmpDH_file(ctx, dh_file,
						      SSL_FILETYPE_ASN1) < 0) {
				wpa_printf(MSG_INFO,
					   "SSL: global use DH DER file failed");
				return -1;
			}
		}
		wpa_printf(MSG_DEBUG, "SSL: global use DH file OK");
		return 0;
	}

	return 0;
}


#ifdef HAVE_OCSP

int ocsp_status_cb(void *unused, const char *url, int url_sz,
		   unsigned char *request, int request_sz,
		   unsigned char **response)
{
	size_t len;

	(void) unused;

	if (!url) {
		wpa_printf(MSG_DEBUG,
			   "wolfSSL: OCSP status callback - no response configured");
		*response = NULL;
		return 0;
	}

	*response = (unsigned char *) os_readfile(url, &len);
	if (!*response) {
		wpa_printf(MSG_DEBUG,
			   "wolfSSL: OCSP status callback - could not read response file");
		return -1;
	}
	wpa_printf(MSG_DEBUG,
		   "wolfSSL: OCSP status callback - send cached response");
	return len;
}


void ocsp_resp_free_cb(void *ocsp_stapling_response, unsigned char *response)
{
	os_free(response);
}

#endif /* HAVE_OCSP */


int tls_global_set_params(void *tls_ctx,
			  const struct tls_connection_params *params)
{
	wpa_printf(MSG_DEBUG, "SSL: global set params");

	if (tls_global_ca_cert(tls_ctx, params->ca_cert) < 0) {
		wpa_printf(MSG_INFO, "SSL: Failed to load ca cert file '%s'",
			   params->ca_cert);
		return -1;
	}

	if (tls_global_client_cert(tls_ctx, params->client_cert) < 0) {
		wpa_printf(MSG_INFO,
			   "SSL: Failed to load client cert file '%s'",
			   params->client_cert);
		return -1;
	}

	if (tls_global_private_key(tls_ctx, params->private_key,
				   params->private_key_passwd) < 0) {
		wpa_printf(MSG_INFO,
			   "SSL: Failed to load private key file '%s'",
			   params->private_key);
		return -1;
	}

	if (tls_global_dh(tls_ctx, params->dh_file, params->dh_blob,
			  params->dh_blob_len) < 0) {
		wpa_printf(MSG_INFO, "SSL: Failed to load DH file '%s'",
			   params->dh_file);
		return -1;
	}

	if (params->openssl_ciphers &&
	    wolfSSL_CTX_set_cipher_list(tls_ctx,
					params->openssl_ciphers) != 1) {
		wpa_printf(MSG_INFO,
			   "wolfSSL: Failed to set cipher string '%s'",
			   params->openssl_ciphers);
		return -1;
	}

#ifdef HAVE_SESSION_TICKET
	/* Session ticket is off by default - can't disable once on. */
	if (!(params->flags & TLS_CONN_DISABLE_SESSION_TICKET))
		wolfSSL_CTX_UseSessionTicket(tls_ctx);
#endif /* HAVE_SESSION_TICKET */

#ifdef HAVE_OCSP
	if (params->ocsp_stapling_response) {
		wolfSSL_CTX_SetOCSP_OverrideURL(tls_ctx,
						params->ocsp_stapling_response);
		wolfSSL_CTX_SetOCSP_Cb(tls_ctx, ocsp_status_cb,
				       ocsp_resp_free_cb, NULL);
	}
#endif /* HAVE_OCSP */

	return 0;
}


int tls_global_set_verify(void *tls_ctx, int check_crl)
{
	wpa_printf(MSG_DEBUG, "SSL: global set verify: %d", check_crl);

	if (check_crl) {
		/* Hack to Enable CRLs. */
		wolfSSL_CTX_LoadCRLBuffer(tls_ctx, NULL, 0, SSL_FILETYPE_PEM);
	}

	return 0;
}


int tls_connection_set_verify(void *ssl_ctx, struct tls_connection *conn,
			      int verify_peer, unsigned int flags,
			      const u8 *session_ctx, size_t session_ctx_len)
{
	if (!conn)
		return -1;

	wpa_printf(MSG_DEBUG, "SSL: set verify: %d", verify_peer);

	if (verify_peer) {
		conn->ca_cert_verify = 1;
		wolfSSL_set_verify(conn->ssl, SSL_VERIFY_PEER |
				   SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
				   tls_verify_cb);
	} else {
		conn->ca_cert_verify = 0;
		wolfSSL_set_verify(conn->ssl, SSL_VERIFY_NONE, NULL);
	}

	wolfSSL_set_accept_state(conn->ssl);

	/* TODO: do we need to fake a session like OpenSSL does here? */

	return 0;
}


static struct wpabuf * wolfssl_handshake(struct tls_connection *conn,
					 const struct wpabuf *in_data,
					 int server)
{
	int res;

	wolfssl_reset_out_data(&conn->output);

	/* Initiate TLS handshake or continue the existing handshake */
	if (server) {
		wolfSSL_set_accept_state(conn->ssl);
		res = wolfSSL_accept(conn->ssl);
		wpa_printf(MSG_DEBUG, "SSL: wolfSSL_accept: %d", res);
	} else {
		wolfSSL_set_connect_state(conn->ssl);
		res = wolfSSL_connect(conn->ssl);
		wpa_printf(MSG_DEBUG, "SSL: wolfSSL_connect: %d", res);
	}

	if (res != 1) {
		int err = wolfSSL_get_error(conn->ssl, res);

		if (err == SSL_ERROR_WANT_READ) {
			wpa_printf(MSG_DEBUG,
				   "SSL: wolfSSL_connect - want more data");
		} else if (err == SSL_ERROR_WANT_WRITE) {
			wpa_printf(MSG_DEBUG,
				   "SSL: wolfSSL_connect - want to write");
		} else {
			char msg[80];

			wpa_printf(MSG_DEBUG,
				   "SSL: wolfSSL_connect - failed %s",
				   wolfSSL_ERR_error_string(err, msg));
			conn->failed++;
		}
	}

	return conn->output.out_data;
}


static struct wpabuf * wolfssl_get_appl_data(struct tls_connection *conn,
					     size_t max_len)
{
	int res;
	struct wpabuf *appl_data = wpabuf_alloc(max_len + 100);

	if (!appl_data)
		return NULL;

	res = wolfSSL_read(conn->ssl, wpabuf_mhead(appl_data),
			   wpabuf_size(appl_data));
	if (res < 0) {
		int err = wolfSSL_get_error(conn->ssl, res);

		if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
			wpa_printf(MSG_DEBUG,
				   "SSL: No Application Data included");
		} else {
			char msg[80];

			wpa_printf(MSG_DEBUG,
				   "Failed to read possible Application Data %s",
				   wolfSSL_ERR_error_string(err, msg));
		}

		wpabuf_free(appl_data);
		return NULL;
	}

	wpabuf_put(appl_data, res);
	wpa_hexdump_buf_key(MSG_MSGDUMP,
			    "SSL: Application Data in Finished message",
			    appl_data);
	return appl_data;
}


static struct wpabuf *
wolfssl_connection_handshake(struct tls_connection *conn,
			     const struct wpabuf *in_data,
			     struct wpabuf **appl_data, int server)
{
	struct wpabuf *out_data;

	wolfssl_reset_in_data(&conn->input, in_data);

	if (appl_data)
		*appl_data = NULL;

	out_data = wolfssl_handshake(conn, in_data, server);
	if (!out_data)
		return NULL;

	if (wolfSSL_is_init_finished(conn->ssl)) {
		wpa_printf(MSG_DEBUG,
			   "wolfSSL: Handshake finished - resumed=%d",
			   tls_connection_resumed(NULL, conn));
		if (appl_data && in_data)
			*appl_data = wolfssl_get_appl_data(conn,
							   wpabuf_len(in_data));
	}

	return out_data;
}


struct wpabuf * tls_connection_handshake(void *tls_ctx,
					 struct tls_connection *conn,
					 const struct wpabuf *in_data,
					 struct wpabuf **appl_data)
{
	return wolfssl_connection_handshake(conn, in_data, appl_data, 0);
}


struct wpabuf * tls_connection_server_handshake(void *tls_ctx,
						struct tls_connection *conn,
						const struct wpabuf *in_data,
						struct wpabuf **appl_data)
{
	return wolfssl_connection_handshake(conn, in_data, appl_data, 1);
}


struct wpabuf * tls_connection_encrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	int res;

	if (!conn)
		return NULL;

	wpa_printf(MSG_DEBUG, "SSL: encrypt: %ld bytes", wpabuf_len(in_data));

	wolfssl_reset_out_data(&conn->output);

	res = wolfSSL_write(conn->ssl, wpabuf_head(in_data),
			    wpabuf_len(in_data));
	if (res < 0) {
		int  err = wolfSSL_get_error(conn->ssl, res);
		char msg[80];

		wpa_printf(MSG_INFO, "Encryption failed - SSL_write: %s",
			   wolfSSL_ERR_error_string(err, msg));
		return NULL;
	}

	return conn->output.out_data;
}


struct wpabuf * tls_connection_decrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	int res;
	struct wpabuf *buf;

	if (!conn)
		return NULL;

	wpa_printf(MSG_DEBUG, "SSL: decrypt");

	wolfssl_reset_in_data(&conn->input, in_data);

	/* Read decrypted data for further processing */
	/*
	 * Even though we try to disable TLS compression, it is possible that
	 * this cannot be done with all TLS libraries. Add extra buffer space
	 * to handle the possibility of the decrypted data being longer than
	 * input data.
	 */
	buf = wpabuf_alloc((wpabuf_len(in_data) + 500) * 3);
	if (!buf)
		return NULL;
	res = wolfSSL_read(conn->ssl, wpabuf_mhead(buf), wpabuf_size(buf));
	if (res < 0) {
		wpa_printf(MSG_INFO, "Decryption failed - SSL_read");
		wpabuf_free(buf);
		return NULL;
	}
	wpabuf_put(buf, res);

	wpa_printf(MSG_DEBUG, "SSL: decrypt: %ld bytes", wpabuf_len(buf));

	return buf;
}


int tls_connection_resumed(void *tls_ctx, struct tls_connection *conn)
{
	return conn ? wolfSSL_session_reused(conn->ssl) : 0;
}


int tls_connection_set_cipher_list(void *tls_ctx, struct tls_connection *conn,
				   u8 *ciphers)
{
	char buf[128], *pos, *end;
	u8 *c;
	int ret;

	if (!conn || !conn->ssl || !ciphers)
		return -1;

	buf[0] = '\0';
	pos = buf;
	end = pos + sizeof(buf);

	c = ciphers;
	while (*c != TLS_CIPHER_NONE) {
		const char *suite;

		switch (*c) {
		case TLS_CIPHER_RC4_SHA:
			suite = "RC4-SHA";
			break;
		case TLS_CIPHER_AES128_SHA:
			suite = "AES128-SHA";
			break;
		case TLS_CIPHER_RSA_DHE_AES128_SHA:
			suite = "DHE-RSA-AES128-SHA";
			break;
		case TLS_CIPHER_ANON_DH_AES128_SHA:
			suite = "ADH-AES128-SHA";
			break;
		case TLS_CIPHER_RSA_DHE_AES256_SHA:
			suite = "DHE-RSA-AES256-SHA";
			break;
		case TLS_CIPHER_AES256_SHA:
			suite = "AES256-SHA";
			break;
		default:
			wpa_printf(MSG_DEBUG,
				   "TLS: Unsupported cipher selection: %d", *c);
			return -1;
		}
		ret = os_snprintf(pos, end - pos, ":%s", suite);
		if (os_snprintf_error(end - pos, ret))
			break;
		pos += ret;

		c++;
	}

	wpa_printf(MSG_DEBUG, "wolfSSL: cipher suites: %s", buf + 1);

	if (wolfSSL_set_cipher_list(conn->ssl, buf + 1) != 1) {
		wpa_printf(MSG_DEBUG, "Cipher suite configuration failed");
		return -1;
	}

	return 0;
}


int tls_get_cipher(void *tls_ctx, struct tls_connection *conn,
		   char *buf, size_t buflen)
{
	WOLFSSL_CIPHER *cipher;
	const char *name;

	if (!conn || !conn->ssl)
		return -1;

	cipher = wolfSSL_get_current_cipher(conn->ssl);
	if (!cipher)
		return -1;

	name = wolfSSL_CIPHER_get_name(cipher);
	if (!name)
		return -1;

	if (os_strcmp(name, "SSL_RSA_WITH_RC4_128_SHA") == 0)
		os_strlcpy(buf, "RC4-SHA", buflen);
	else if (os_strcmp(name, "TLS_RSA_WITH_AES_128_CBC_SHA") == 0)
		os_strlcpy(buf, "AES128-SHA", buflen);
	else if (os_strcmp(name, "TLS_DHE_RSA_WITH_AES_128_CBC_SHA") == 0)
		os_strlcpy(buf, "DHE-RSA-AES128-SHA", buflen);
	else if (os_strcmp(name, "TLS_DH_anon_WITH_AES_128_CBC_SHA") == 0)
		os_strlcpy(buf, "ADH-AES128-SHA", buflen);
	else if (os_strcmp(name, "TLS_DHE_RSA_WITH_AES_256_CBC_SHA") == 0)
		os_strlcpy(buf, "DHE-RSA-AES256-SHA", buflen);
	else if (os_strcmp(name, "TLS_RSA_WITH_AES_256_CBC_SHA") == 0)
		os_strlcpy(buf, "AES256-SHA", buflen);
	else
		os_strlcpy(buf, name, buflen);

	return 0;
}


int tls_connection_enable_workaround(void *tls_ctx,
				     struct tls_connection *conn)
{
	/* no empty fragments in wolfSSL for now */
	return 0;
}


int tls_connection_get_failed(void *tls_ctx, struct tls_connection *conn)
{
	if (!conn)
		return -1;

	return conn->failed;
}


int tls_connection_get_read_alerts(void *tls_ctx, struct tls_connection *conn)
{
	if (!conn)
		return -1;

	/* TODO: this is not incremented anywhere */
	return conn->read_alerts;
}


int tls_connection_get_write_alerts(void *tls_ctx,
				    struct tls_connection *conn)
{
	if (!conn)
		return -1;

	/* TODO: this is not incremented anywhere */
	return conn->write_alerts;
}



int tls_get_library_version(char *buf, size_t buf_len)
{
	return os_snprintf(buf, buf_len, "wolfSSL build=%s run=%s",
			   WOLFSSL_VERSION, wolfSSL_lib_version());
}

int tls_get_version(void *ssl_ctx, struct tls_connection *conn,
		    char *buf, size_t buflen)
{
	const char *name;

	if (!conn || !conn->ssl)
		return -1;

	name = wolfSSL_get_version(conn->ssl);
	if (!name)
		return -1;

	os_strlcpy(buf, name, buflen);
	return 0;
}


int tls_connection_get_random(void *ssl_ctx, struct tls_connection *conn,
			      struct tls_random *keys)
{
	WOLFSSL *ssl;

	if (!conn || !keys)
		return -1;
	ssl = conn->ssl;
	if (!ssl)
		return -1;

	os_memset(keys, 0, sizeof(*keys));
	keys->client_random = conn->client_random;
	keys->client_random_len = wolfSSL_get_client_random(
		ssl, conn->client_random, sizeof(conn->client_random));
	keys->server_random = conn->server_random;
	keys->server_random_len = wolfSSL_get_server_random(
		ssl, conn->server_random, sizeof(conn->server_random));

	return 0;
}


int tls_connection_export_key(void *tls_ctx, struct tls_connection *conn,
			      const char *label, u8 *out, size_t out_len)
{
	if (!conn || wolfSSL_make_eap_keys(conn->ssl, out, out_len, label) != 0)
		return -1;
	return 0;
}


#define SEED_LEN	(RAN_LEN + RAN_LEN)

int tls_connection_get_eap_fast_key(void *tls_ctx, struct tls_connection *conn,
				    u8 *out, size_t out_len)
{
	byte seed[SEED_LEN];
	int ret = -1;
	WOLFSSL *ssl;
	byte *tmp_out;
	byte *_out;
	int skip = 0;
	byte *master_key;
	unsigned int master_key_len;
	byte *server_random;
	unsigned int server_len;
	byte *client_random;
	unsigned int client_len;

	if (!conn || !conn->ssl)
		return -1;
	ssl = conn->ssl;

	skip = 2 * (wolfSSL_GetKeySize(ssl) + wolfSSL_GetHmacSize(ssl) +
		    wolfSSL_GetIVSize(ssl));

	tmp_out = os_malloc(skip + out_len);
	if (!tmp_out)
		return -1;
	_out = tmp_out;

	wolfSSL_get_keys(ssl, &master_key, &master_key_len, &server_random,
			 &server_len, &client_random, &client_len);
	os_memcpy(seed, server_random, RAN_LEN);
	os_memcpy(seed + RAN_LEN, client_random, RAN_LEN);

	if (wolfSSL_GetVersion(ssl) == WOLFSSL_TLSV1_2) {
		tls_prf_sha256(master_key, master_key_len,
			       "key expansion", seed, sizeof(seed),
			       _out, skip + out_len);
		ret = 0;
	} else {
		ret = tls_prf_sha1_md5(master_key, master_key_len,
				       "key expansion", seed, sizeof(seed),
				       _out, skip + out_len);
	}

	os_memset(master_key, 0, master_key_len);
	if (ret == 0)
		os_memcpy(out, _out + skip, out_len);
	bin_clear_free(tmp_out, skip + out_len);

	return ret;
}


#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST)

int tls_connection_client_hello_ext(void *ssl_ctx, struct tls_connection *conn,
				    int ext_type, const u8 *data,
				    size_t data_len)
{
	(void) ssl_ctx;

	if (!conn || !conn->ssl || ext_type != 35)
		return -1;

	if (wolfSSL_set_SessionTicket(conn->ssl, data,
				      (unsigned int) data_len) != 1)
		return -1;

	return 0;
}


static int tls_sess_sec_cb(WOLFSSL *s, void *secret, int *secret_len, void *arg)
{
	struct tls_connection *conn = arg;
	int ret;
	unsigned char client_random[RAN_LEN];
	unsigned char server_random[RAN_LEN];
	word32 ticket_len = sizeof(conn->session_ticket);

	if (!conn || !conn->session_ticket_cb)
		return 1;

	if (wolfSSL_get_client_random(s, client_random,
				      sizeof(client_random)) == 0 ||
	    wolfSSL_get_server_random(s, server_random,
				      sizeof(server_random)) == 0 ||
	    wolfSSL_get_SessionTicket(s, conn->session_ticket,
				      &ticket_len) != 1)
		return 1;

	if (ticket_len == 0)
		return 0;

	ret = conn->session_ticket_cb(conn->session_ticket_cb_ctx,
				      conn->session_ticket, ticket_len,
				      client_random, server_random, secret);
	if (ret <= 0)
		return 1;

	*secret_len = SECRET_LEN;
	return 0;
}

#endif /* EAP_FAST || EAP_FAST_DYNAMIC || EAP_SERVER_FAST */


int tls_connection_set_session_ticket_cb(void *tls_ctx,
					 struct tls_connection *conn,
					 tls_session_ticket_cb cb,
					 void *ctx)
{
#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST)
	conn->session_ticket_cb = cb;
	conn->session_ticket_cb_ctx = ctx;

	if (cb) {
		if (wolfSSL_set_session_secret_cb(conn->ssl, tls_sess_sec_cb,
						  conn) != 1)
			return -1;
	} else {
		if (wolfSSL_set_session_secret_cb(conn->ssl, NULL, NULL) != 1)
			return -1;
	}

	return 0;
#else /* EAP_FAST || EAP_FAST_DYNAMIC || EAP_SERVER_FAST */
	return -1;
#endif /* EAP_FAST || EAP_FAST_DYNAMIC || EAP_SERVER_FAST */
}


void tls_connection_set_success_data_resumed(struct tls_connection *conn)
{
	wpa_printf(MSG_DEBUG,
		   "wolfSSL: Success data accepted for resumed session");
}


void tls_connection_remove_session(struct tls_connection *conn)
{
	WOLFSSL_SESSION *sess;

	sess = wolfSSL_get_session(conn->ssl);
	if (!sess)
		return;

	wolfSSL_SSL_SESSION_set_timeout(sess, 0);
	wpa_printf(MSG_DEBUG,
		   "wolfSSL: Removed cached session to disable session resumption");
}


void tls_connection_set_success_data(struct tls_connection *conn,
				     struct wpabuf *data)
{
	WOLFSSL_SESSION *sess;
	struct wpabuf *old;

	wpa_printf(MSG_DEBUG, "wolfSSL: Set success data");

	sess = wolfSSL_get_session(conn->ssl);
	if (!sess) {
		wpa_printf(MSG_DEBUG,
			   "wolfSSL: No session found for success data");
		goto fail;
	}

	old = wolfSSL_SESSION_get_ex_data(sess, tls_ex_idx_session);
	if (old) {
		wpa_printf(MSG_DEBUG, "wolfSSL: Replacing old success data %p",
			   old);
		wpabuf_free(old);
	}
	if (wolfSSL_SESSION_set_ex_data(sess, tls_ex_idx_session, data) != 1)
		goto fail;

	wpa_printf(MSG_DEBUG, "wolfSSL: Stored success data %p", data);
	conn->success_data = 1;
	return;

fail:
	wpa_printf(MSG_INFO, "wolfSSL: Failed to store success data");
	wpabuf_free(data);
}


const struct wpabuf *
tls_connection_get_success_data(struct tls_connection *conn)
{
	WOLFSSL_SESSION *sess;

	wpa_printf(MSG_DEBUG, "wolfSSL: Get success data");

	sess = wolfSSL_get_session(conn->ssl);
	if (!sess)
		return NULL;
	return wolfSSL_SESSION_get_ex_data(sess, tls_ex_idx_session);
}
