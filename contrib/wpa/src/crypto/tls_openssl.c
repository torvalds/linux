/*
 * SSL/TLS interface functions for OpenSSL
 * Copyright (c) 2004-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#ifndef CONFIG_SMARTCARD
#ifndef OPENSSL_NO_ENGINE
#ifndef ANDROID
#define OPENSSL_NO_ENGINE
#endif
#endif
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>
#include <openssl/pkcs12.h>
#include <openssl/x509v3.h>
#ifndef OPENSSL_NO_ENGINE
#include <openssl/engine.h>
#endif /* OPENSSL_NO_ENGINE */
#ifndef OPENSSL_NO_DSA
#include <openssl/dsa.h>
#endif
#ifndef OPENSSL_NO_DH
#include <openssl/dh.h>
#endif

#include "common.h"
#include "crypto.h"
#include "sha1.h"
#include "sha256.h"
#include "tls.h"
#include "tls_openssl.h"

#if !defined(CONFIG_FIPS) &&                             \
    (defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) ||   \
     defined(EAP_SERVER_FAST))
#define OPENSSL_NEED_EAP_FAST_PRF
#endif

#if defined(OPENSSL_IS_BORINGSSL)
/* stack_index_t is the return type of OpenSSL's sk_XXX_num() functions. */
typedef size_t stack_index_t;
#else
typedef int stack_index_t;
#endif

#ifdef SSL_set_tlsext_status_type
#ifndef OPENSSL_NO_TLSEXT
#define HAVE_OCSP
#include <openssl/ocsp.h>
#endif /* OPENSSL_NO_TLSEXT */
#endif /* SSL_set_tlsext_status_type */

#if (OPENSSL_VERSION_NUMBER < 0x10100000L || \
     (defined(LIBRESSL_VERSION_NUMBER) && \
      LIBRESSL_VERSION_NUMBER < 0x20700000L)) && \
    !defined(BORINGSSL_API_VERSION)
/*
 * SSL_get_client_random() and SSL_get_server_random() were added in OpenSSL
 * 1.1.0 and newer BoringSSL revisions. Provide compatibility wrappers for
 * older versions.
 */

static size_t SSL_get_client_random(const SSL *ssl, unsigned char *out,
				    size_t outlen)
{
	if (!ssl->s3 || outlen < SSL3_RANDOM_SIZE)
		return 0;
	os_memcpy(out, ssl->s3->client_random, SSL3_RANDOM_SIZE);
	return SSL3_RANDOM_SIZE;
}


static size_t SSL_get_server_random(const SSL *ssl, unsigned char *out,
				    size_t outlen)
{
	if (!ssl->s3 || outlen < SSL3_RANDOM_SIZE)
		return 0;
	os_memcpy(out, ssl->s3->server_random, SSL3_RANDOM_SIZE);
	return SSL3_RANDOM_SIZE;
}


#ifdef OPENSSL_NEED_EAP_FAST_PRF
static size_t SSL_SESSION_get_master_key(const SSL_SESSION *session,
					 unsigned char *out, size_t outlen)
{
	if (!session || session->master_key_length < 0 ||
	    (size_t) session->master_key_length > outlen)
		return 0;
	if ((size_t) session->master_key_length < outlen)
		outlen = session->master_key_length;
	os_memcpy(out, session->master_key, outlen);
	return outlen;
}
#endif /* OPENSSL_NEED_EAP_FAST_PRF */

#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#ifdef CONFIG_SUITEB
static int RSA_bits(const RSA *r)
{
	return BN_num_bits(r->n);
}
#endif /* CONFIG_SUITEB */


static const unsigned char * ASN1_STRING_get0_data(const ASN1_STRING *x)
{
	return ASN1_STRING_data((ASN1_STRING *) x);
}
#endif

#ifdef ANDROID
#include <openssl/pem.h>
#include <keystore/keystore_get.h>

static BIO * BIO_from_keystore(const char *key)
{
	BIO *bio = NULL;
	uint8_t *value = NULL;
	int length = keystore_get(key, strlen(key), &value);
	if (length != -1 && (bio = BIO_new(BIO_s_mem())) != NULL)
		BIO_write(bio, value, length);
	free(value);
	return bio;
}


static int tls_add_ca_from_keystore(X509_STORE *ctx, const char *key_alias)
{
	BIO *bio = BIO_from_keystore(key_alias);
	STACK_OF(X509_INFO) *stack = NULL;
	stack_index_t i;

	if (bio) {
		stack = PEM_X509_INFO_read_bio(bio, NULL, NULL, NULL);
		BIO_free(bio);
	}

	if (!stack) {
		wpa_printf(MSG_WARNING, "TLS: Failed to parse certificate: %s",
			   key_alias);
		return -1;
	}

	for (i = 0; i < sk_X509_INFO_num(stack); ++i) {
		X509_INFO *info = sk_X509_INFO_value(stack, i);

		if (info->x509)
			X509_STORE_add_cert(ctx, info->x509);
		if (info->crl)
			X509_STORE_add_crl(ctx, info->crl);
	}

	sk_X509_INFO_pop_free(stack, X509_INFO_free);

	return 0;
}


static int tls_add_ca_from_keystore_encoded(X509_STORE *ctx,
					    const char *encoded_key_alias)
{
	int rc = -1;
	int len = os_strlen(encoded_key_alias);
	unsigned char *decoded_alias;

	if (len & 1) {
		wpa_printf(MSG_WARNING, "Invalid hex-encoded alias: %s",
			   encoded_key_alias);
		return rc;
	}

	decoded_alias = os_malloc(len / 2 + 1);
	if (decoded_alias) {
		if (!hexstr2bin(encoded_key_alias, decoded_alias, len / 2)) {
			decoded_alias[len / 2] = '\0';
			rc = tls_add_ca_from_keystore(
				ctx, (const char *) decoded_alias);
		}
		os_free(decoded_alias);
	}

	return rc;
}

#endif /* ANDROID */

static int tls_openssl_ref_count = 0;
static int tls_ex_idx_session = -1;

struct tls_context {
	void (*event_cb)(void *ctx, enum tls_event ev,
			 union tls_event_data *data);
	void *cb_ctx;
	int cert_in_cb;
	char *ocsp_stapling_response;
};

static struct tls_context *tls_global = NULL;


struct tls_data {
	SSL_CTX *ssl;
	unsigned int tls_session_lifetime;
};

struct tls_connection {
	struct tls_context *context;
	SSL_CTX *ssl_ctx;
	SSL *ssl;
	BIO *ssl_in, *ssl_out;
#if defined(ANDROID) || !defined(OPENSSL_NO_ENGINE)
	ENGINE *engine;        /* functional reference to the engine */
	EVP_PKEY *private_key; /* the private key if using engine */
#endif /* OPENSSL_NO_ENGINE */
	char *subject_match, *altsubject_match, *suffix_match, *domain_match;
	int read_alerts, write_alerts, failed;

	tls_session_ticket_cb session_ticket_cb;
	void *session_ticket_cb_ctx;

	/* SessionTicket received from OpenSSL hello_extension_cb (server) */
	u8 *session_ticket;
	size_t session_ticket_len;

	unsigned int ca_cert_verify:1;
	unsigned int cert_probe:1;
	unsigned int server_cert_only:1;
	unsigned int invalid_hb_used:1;
	unsigned int success_data:1;
	unsigned int client_hello_generated:1;
	unsigned int server:1;

	u8 srv_cert_hash[32];

	unsigned int flags;

	X509 *peer_cert;
	X509 *peer_issuer;
	X509 *peer_issuer_issuer;

	unsigned char client_random[SSL3_RANDOM_SIZE];
	unsigned char server_random[SSL3_RANDOM_SIZE];

	u16 cipher_suite;
	int server_dh_prime_len;
};


static struct tls_context * tls_context_new(const struct tls_config *conf)
{
	struct tls_context *context = os_zalloc(sizeof(*context));
	if (context == NULL)
		return NULL;
	if (conf) {
		context->event_cb = conf->event_cb;
		context->cb_ctx = conf->cb_ctx;
		context->cert_in_cb = conf->cert_in_cb;
	}
	return context;
}


#ifdef CONFIG_NO_STDOUT_DEBUG

static void _tls_show_errors(void)
{
	unsigned long err;

	while ((err = ERR_get_error())) {
		/* Just ignore the errors, since stdout is disabled */
	}
}
#define tls_show_errors(l, f, t) _tls_show_errors()

#else /* CONFIG_NO_STDOUT_DEBUG */

static void tls_show_errors(int level, const char *func, const char *txt)
{
	unsigned long err;

	wpa_printf(level, "OpenSSL: %s - %s %s",
		   func, txt, ERR_error_string(ERR_get_error(), NULL));

	while ((err = ERR_get_error())) {
		wpa_printf(MSG_INFO, "OpenSSL: pending error: %s",
			   ERR_error_string(err, NULL));
	}
}

#endif /* CONFIG_NO_STDOUT_DEBUG */


#ifdef CONFIG_NATIVE_WINDOWS

/* Windows CryptoAPI and access to certificate stores */
#include <wincrypt.h>

#ifdef __MINGW32_VERSION
/*
 * MinGW does not yet include all the needed definitions for CryptoAPI, so
 * define here whatever extra is needed.
 */
#define CERT_SYSTEM_STORE_CURRENT_USER (1 << 16)
#define CERT_STORE_READONLY_FLAG 0x00008000
#define CERT_STORE_OPEN_EXISTING_FLAG 0x00004000

#endif /* __MINGW32_VERSION */


struct cryptoapi_rsa_data {
	const CERT_CONTEXT *cert;
	HCRYPTPROV crypt_prov;
	DWORD key_spec;
	BOOL free_crypt_prov;
};


static void cryptoapi_error(const char *msg)
{
	wpa_printf(MSG_INFO, "CryptoAPI: %s; err=%u",
		   msg, (unsigned int) GetLastError());
}


static int cryptoapi_rsa_pub_enc(int flen, const unsigned char *from,
				 unsigned char *to, RSA *rsa, int padding)
{
	wpa_printf(MSG_DEBUG, "%s - not implemented", __func__);
	return 0;
}


static int cryptoapi_rsa_pub_dec(int flen, const unsigned char *from,
				 unsigned char *to, RSA *rsa, int padding)
{
	wpa_printf(MSG_DEBUG, "%s - not implemented", __func__);
	return 0;
}


static int cryptoapi_rsa_priv_enc(int flen, const unsigned char *from,
				  unsigned char *to, RSA *rsa, int padding)
{
	struct cryptoapi_rsa_data *priv =
		(struct cryptoapi_rsa_data *) rsa->meth->app_data;
	HCRYPTHASH hash;
	DWORD hash_size, len, i;
	unsigned char *buf = NULL;
	int ret = 0;

	if (priv == NULL) {
		RSAerr(RSA_F_RSA_EAY_PRIVATE_ENCRYPT,
		       ERR_R_PASSED_NULL_PARAMETER);
		return 0;
	}

	if (padding != RSA_PKCS1_PADDING) {
		RSAerr(RSA_F_RSA_EAY_PRIVATE_ENCRYPT,
		       RSA_R_UNKNOWN_PADDING_TYPE);
		return 0;
	}

	if (flen != 16 /* MD5 */ + 20 /* SHA-1 */) {
		wpa_printf(MSG_INFO, "%s - only MD5-SHA1 hash supported",
			   __func__);
		RSAerr(RSA_F_RSA_EAY_PRIVATE_ENCRYPT,
		       RSA_R_INVALID_MESSAGE_LENGTH);
		return 0;
	}

	if (!CryptCreateHash(priv->crypt_prov, CALG_SSL3_SHAMD5, 0, 0, &hash))
	{
		cryptoapi_error("CryptCreateHash failed");
		return 0;
	}

	len = sizeof(hash_size);
	if (!CryptGetHashParam(hash, HP_HASHSIZE, (BYTE *) &hash_size, &len,
			       0)) {
		cryptoapi_error("CryptGetHashParam failed");
		goto err;
	}

	if ((int) hash_size != flen) {
		wpa_printf(MSG_INFO, "CryptoAPI: Invalid hash size (%u != %d)",
			   (unsigned) hash_size, flen);
		RSAerr(RSA_F_RSA_EAY_PRIVATE_ENCRYPT,
		       RSA_R_INVALID_MESSAGE_LENGTH);
		goto err;
	}
	if (!CryptSetHashParam(hash, HP_HASHVAL, (BYTE * ) from, 0)) {
		cryptoapi_error("CryptSetHashParam failed");
		goto err;
	}

	len = RSA_size(rsa);
	buf = os_malloc(len);
	if (buf == NULL) {
		RSAerr(RSA_F_RSA_EAY_PRIVATE_ENCRYPT, ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!CryptSignHash(hash, priv->key_spec, NULL, 0, buf, &len)) {
		cryptoapi_error("CryptSignHash failed");
		goto err;
	}

	for (i = 0; i < len; i++)
		to[i] = buf[len - i - 1];
	ret = len;

err:
	os_free(buf);
	CryptDestroyHash(hash);

	return ret;
}


static int cryptoapi_rsa_priv_dec(int flen, const unsigned char *from,
				  unsigned char *to, RSA *rsa, int padding)
{
	wpa_printf(MSG_DEBUG, "%s - not implemented", __func__);
	return 0;
}


static void cryptoapi_free_data(struct cryptoapi_rsa_data *priv)
{
	if (priv == NULL)
		return;
	if (priv->crypt_prov && priv->free_crypt_prov)
		CryptReleaseContext(priv->crypt_prov, 0);
	if (priv->cert)
		CertFreeCertificateContext(priv->cert);
	os_free(priv);
}


static int cryptoapi_finish(RSA *rsa)
{
	cryptoapi_free_data((struct cryptoapi_rsa_data *) rsa->meth->app_data);
	os_free((void *) rsa->meth);
	rsa->meth = NULL;
	return 1;
}


static const CERT_CONTEXT * cryptoapi_find_cert(const char *name, DWORD store)
{
	HCERTSTORE cs;
	const CERT_CONTEXT *ret = NULL;

	cs = CertOpenStore((LPCSTR) CERT_STORE_PROV_SYSTEM, 0, 0,
			   store | CERT_STORE_OPEN_EXISTING_FLAG |
			   CERT_STORE_READONLY_FLAG, L"MY");
	if (cs == NULL) {
		cryptoapi_error("Failed to open 'My system store'");
		return NULL;
	}

	if (strncmp(name, "cert://", 7) == 0) {
		unsigned short wbuf[255];
		MultiByteToWideChar(CP_ACP, 0, name + 7, -1, wbuf, 255);
		ret = CertFindCertificateInStore(cs, X509_ASN_ENCODING |
						 PKCS_7_ASN_ENCODING,
						 0, CERT_FIND_SUBJECT_STR,
						 wbuf, NULL);
	} else if (strncmp(name, "hash://", 7) == 0) {
		CRYPT_HASH_BLOB blob;
		int len;
		const char *hash = name + 7;
		unsigned char *buf;

		len = os_strlen(hash) / 2;
		buf = os_malloc(len);
		if (buf && hexstr2bin(hash, buf, len) == 0) {
			blob.cbData = len;
			blob.pbData = buf;
			ret = CertFindCertificateInStore(cs,
							 X509_ASN_ENCODING |
							 PKCS_7_ASN_ENCODING,
							 0, CERT_FIND_HASH,
							 &blob, NULL);
		}
		os_free(buf);
	}

	CertCloseStore(cs, 0);

	return ret;
}


static int tls_cryptoapi_cert(SSL *ssl, const char *name)
{
	X509 *cert = NULL;
	RSA *rsa = NULL, *pub_rsa;
	struct cryptoapi_rsa_data *priv;
	RSA_METHOD *rsa_meth;

	if (name == NULL ||
	    (strncmp(name, "cert://", 7) != 0 &&
	     strncmp(name, "hash://", 7) != 0))
		return -1;

	priv = os_zalloc(sizeof(*priv));
	rsa_meth = os_zalloc(sizeof(*rsa_meth));
	if (priv == NULL || rsa_meth == NULL) {
		wpa_printf(MSG_WARNING, "CryptoAPI: Failed to allocate memory "
			   "for CryptoAPI RSA method");
		os_free(priv);
		os_free(rsa_meth);
		return -1;
	}

	priv->cert = cryptoapi_find_cert(name, CERT_SYSTEM_STORE_CURRENT_USER);
	if (priv->cert == NULL) {
		priv->cert = cryptoapi_find_cert(
			name, CERT_SYSTEM_STORE_LOCAL_MACHINE);
	}
	if (priv->cert == NULL) {
		wpa_printf(MSG_INFO, "CryptoAPI: Could not find certificate "
			   "'%s'", name);
		goto err;
	}

	cert = d2i_X509(NULL,
			(const unsigned char **) &priv->cert->pbCertEncoded,
			priv->cert->cbCertEncoded);
	if (cert == NULL) {
		wpa_printf(MSG_INFO, "CryptoAPI: Could not process X509 DER "
			   "encoding");
		goto err;
	}

	if (!CryptAcquireCertificatePrivateKey(priv->cert,
					       CRYPT_ACQUIRE_COMPARE_KEY_FLAG,
					       NULL, &priv->crypt_prov,
					       &priv->key_spec,
					       &priv->free_crypt_prov)) {
		cryptoapi_error("Failed to acquire a private key for the "
				"certificate");
		goto err;
	}

	rsa_meth->name = "Microsoft CryptoAPI RSA Method";
	rsa_meth->rsa_pub_enc = cryptoapi_rsa_pub_enc;
	rsa_meth->rsa_pub_dec = cryptoapi_rsa_pub_dec;
	rsa_meth->rsa_priv_enc = cryptoapi_rsa_priv_enc;
	rsa_meth->rsa_priv_dec = cryptoapi_rsa_priv_dec;
	rsa_meth->finish = cryptoapi_finish;
	rsa_meth->flags = RSA_METHOD_FLAG_NO_CHECK;
	rsa_meth->app_data = (char *) priv;

	rsa = RSA_new();
	if (rsa == NULL) {
		SSLerr(SSL_F_SSL_CTX_USE_CERTIFICATE_FILE,
		       ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!SSL_use_certificate(ssl, cert)) {
		RSA_free(rsa);
		rsa = NULL;
		goto err;
	}
	pub_rsa = cert->cert_info->key->pkey->pkey.rsa;
	X509_free(cert);
	cert = NULL;

	rsa->n = BN_dup(pub_rsa->n);
	rsa->e = BN_dup(pub_rsa->e);
	if (!RSA_set_method(rsa, rsa_meth))
		goto err;

	if (!SSL_use_RSAPrivateKey(ssl, rsa))
		goto err;
	RSA_free(rsa);

	return 0;

err:
	if (cert)
		X509_free(cert);
	if (rsa)
		RSA_free(rsa);
	else {
		os_free(rsa_meth);
		cryptoapi_free_data(priv);
	}
	return -1;
}


static int tls_cryptoapi_ca_cert(SSL_CTX *ssl_ctx, SSL *ssl, const char *name)
{
	HCERTSTORE cs;
	PCCERT_CONTEXT ctx = NULL;
	X509 *cert;
	char buf[128];
	const char *store;
#ifdef UNICODE
	WCHAR *wstore;
#endif /* UNICODE */

	if (name == NULL || strncmp(name, "cert_store://", 13) != 0)
		return -1;

	store = name + 13;
#ifdef UNICODE
	wstore = os_malloc((os_strlen(store) + 1) * sizeof(WCHAR));
	if (wstore == NULL)
		return -1;
	wsprintf(wstore, L"%S", store);
	cs = CertOpenSystemStore(0, wstore);
	os_free(wstore);
#else /* UNICODE */
	cs = CertOpenSystemStore(0, store);
#endif /* UNICODE */
	if (cs == NULL) {
		wpa_printf(MSG_DEBUG, "%s: failed to open system cert store "
			   "'%s': error=%d", __func__, store,
			   (int) GetLastError());
		return -1;
	}

	while ((ctx = CertEnumCertificatesInStore(cs, ctx))) {
		cert = d2i_X509(NULL,
				(const unsigned char **) &ctx->pbCertEncoded,
				ctx->cbCertEncoded);
		if (cert == NULL) {
			wpa_printf(MSG_INFO, "CryptoAPI: Could not process "
				   "X509 DER encoding for CA cert");
			continue;
		}

		X509_NAME_oneline(X509_get_subject_name(cert), buf,
				  sizeof(buf));
		wpa_printf(MSG_DEBUG, "OpenSSL: Loaded CA certificate for "
			   "system certificate store: subject='%s'", buf);

		if (!X509_STORE_add_cert(SSL_CTX_get_cert_store(ssl_ctx),
					 cert)) {
			tls_show_errors(MSG_WARNING, __func__,
					"Failed to add ca_cert to OpenSSL "
					"certificate store");
		}

		X509_free(cert);
	}

	if (!CertCloseStore(cs, 0)) {
		wpa_printf(MSG_DEBUG, "%s: failed to close system cert store "
			   "'%s': error=%d", __func__, name + 13,
			   (int) GetLastError());
	}

	return 0;
}


#else /* CONFIG_NATIVE_WINDOWS */

static int tls_cryptoapi_cert(SSL *ssl, const char *name)
{
	return -1;
}

#endif /* CONFIG_NATIVE_WINDOWS */


static void ssl_info_cb(const SSL *ssl, int where, int ret)
{
	const char *str;
	int w;

	wpa_printf(MSG_DEBUG, "SSL: (where=0x%x ret=0x%x)", where, ret);
	w = where & ~SSL_ST_MASK;
	if (w & SSL_ST_CONNECT)
		str = "SSL_connect";
	else if (w & SSL_ST_ACCEPT)
		str = "SSL_accept";
	else
		str = "undefined";

	if (where & SSL_CB_LOOP) {
		wpa_printf(MSG_DEBUG, "SSL: %s:%s",
			   str, SSL_state_string_long(ssl));
	} else if (where & SSL_CB_ALERT) {
		struct tls_connection *conn = SSL_get_app_data((SSL *) ssl);
		wpa_printf(MSG_INFO, "SSL: SSL3 alert: %s:%s:%s",
			   where & SSL_CB_READ ?
			   "read (remote end reported an error)" :
			   "write (local SSL3 detected an error)",
			   SSL_alert_type_string_long(ret),
			   SSL_alert_desc_string_long(ret));
		if ((ret >> 8) == SSL3_AL_FATAL) {
			if (where & SSL_CB_READ)
				conn->read_alerts++;
			else
				conn->write_alerts++;
		}
		if (conn->context->event_cb != NULL) {
			union tls_event_data ev;
			struct tls_context *context = conn->context;
			os_memset(&ev, 0, sizeof(ev));
			ev.alert.is_local = !(where & SSL_CB_READ);
			ev.alert.type = SSL_alert_type_string_long(ret);
			ev.alert.description = SSL_alert_desc_string_long(ret);
			context->event_cb(context->cb_ctx, TLS_ALERT, &ev);
		}
	} else if (where & SSL_CB_EXIT && ret <= 0) {
		wpa_printf(MSG_DEBUG, "SSL: %s:%s in %s",
			   str, ret == 0 ? "failed" : "error",
			   SSL_state_string_long(ssl));
	}
}


#ifndef OPENSSL_NO_ENGINE
/**
 * tls_engine_load_dynamic_generic - load any openssl engine
 * @pre: an array of commands and values that load an engine initialized
 *       in the engine specific function
 * @post: an array of commands and values that initialize an already loaded
 *        engine (or %NULL if not required)
 * @id: the engine id of the engine to load (only required if post is not %NULL
 *
 * This function is a generic function that loads any openssl engine.
 *
 * Returns: 0 on success, -1 on failure
 */
static int tls_engine_load_dynamic_generic(const char *pre[],
					   const char *post[], const char *id)
{
	ENGINE *engine;
	const char *dynamic_id = "dynamic";

	engine = ENGINE_by_id(id);
	if (engine) {
		wpa_printf(MSG_DEBUG, "ENGINE: engine '%s' is already "
			   "available", id);
		/*
		 * If it was auto-loaded by ENGINE_by_id() we might still
		 * need to tell it which PKCS#11 module to use in legacy
		 * (non-p11-kit) environments. Do so now; even if it was
		 * properly initialised before, setting it again will be
		 * harmless.
		 */
		goto found;
	}
	ERR_clear_error();

	engine = ENGINE_by_id(dynamic_id);
	if (engine == NULL) {
		wpa_printf(MSG_INFO, "ENGINE: Can't find engine %s [%s]",
			   dynamic_id,
			   ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}

	/* Perform the pre commands. This will load the engine. */
	while (pre && pre[0]) {
		wpa_printf(MSG_DEBUG, "ENGINE: '%s' '%s'", pre[0], pre[1]);
		if (ENGINE_ctrl_cmd_string(engine, pre[0], pre[1], 0) == 0) {
			wpa_printf(MSG_INFO, "ENGINE: ctrl cmd_string failed: "
				   "%s %s [%s]", pre[0], pre[1],
				   ERR_error_string(ERR_get_error(), NULL));
			ENGINE_free(engine);
			return -1;
		}
		pre += 2;
	}

	/*
	 * Free the reference to the "dynamic" engine. The loaded engine can
	 * now be looked up using ENGINE_by_id().
	 */
	ENGINE_free(engine);

	engine = ENGINE_by_id(id);
	if (engine == NULL) {
		wpa_printf(MSG_INFO, "ENGINE: Can't find engine %s [%s]",
			   id, ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}
 found:
	while (post && post[0]) {
		wpa_printf(MSG_DEBUG, "ENGINE: '%s' '%s'", post[0], post[1]);
		if (ENGINE_ctrl_cmd_string(engine, post[0], post[1], 0) == 0) {
			wpa_printf(MSG_DEBUG, "ENGINE: ctrl cmd_string failed:"
				" %s %s [%s]", post[0], post[1],
				   ERR_error_string(ERR_get_error(), NULL));
			ENGINE_remove(engine);
			ENGINE_free(engine);
			return -1;
		}
		post += 2;
	}
	ENGINE_free(engine);

	return 0;
}


/**
 * tls_engine_load_dynamic_pkcs11 - load the pkcs11 engine provided by opensc
 * @pkcs11_so_path: pksc11_so_path from the configuration
 * @pcks11_module_path: pkcs11_module_path from the configuration
 */
static int tls_engine_load_dynamic_pkcs11(const char *pkcs11_so_path,
					  const char *pkcs11_module_path)
{
	char *engine_id = "pkcs11";
	const char *pre_cmd[] = {
		"SO_PATH", NULL /* pkcs11_so_path */,
		"ID", NULL /* engine_id */,
		"LIST_ADD", "1",
		/* "NO_VCHECK", "1", */
		"LOAD", NULL,
		NULL, NULL
	};
	const char *post_cmd[] = {
		"MODULE_PATH", NULL /* pkcs11_module_path */,
		NULL, NULL
	};

	if (!pkcs11_so_path)
		return 0;

	pre_cmd[1] = pkcs11_so_path;
	pre_cmd[3] = engine_id;
	if (pkcs11_module_path)
		post_cmd[1] = pkcs11_module_path;
	else
		post_cmd[0] = NULL;

	wpa_printf(MSG_DEBUG, "ENGINE: Loading pkcs11 Engine from %s",
		   pkcs11_so_path);

	return tls_engine_load_dynamic_generic(pre_cmd, post_cmd, engine_id);
}


/**
 * tls_engine_load_dynamic_opensc - load the opensc engine provided by opensc
 * @opensc_so_path: opensc_so_path from the configuration
 */
static int tls_engine_load_dynamic_opensc(const char *opensc_so_path)
{
	char *engine_id = "opensc";
	const char *pre_cmd[] = {
		"SO_PATH", NULL /* opensc_so_path */,
		"ID", NULL /* engine_id */,
		"LIST_ADD", "1",
		"LOAD", NULL,
		NULL, NULL
	};

	if (!opensc_so_path)
		return 0;

	pre_cmd[1] = opensc_so_path;
	pre_cmd[3] = engine_id;

	wpa_printf(MSG_DEBUG, "ENGINE: Loading OpenSC Engine from %s",
		   opensc_so_path);

	return tls_engine_load_dynamic_generic(pre_cmd, NULL, engine_id);
}
#endif /* OPENSSL_NO_ENGINE */


static void remove_session_cb(SSL_CTX *ctx, SSL_SESSION *sess)
{
	struct wpabuf *buf;

	if (tls_ex_idx_session < 0)
		return;
	buf = SSL_SESSION_get_ex_data(sess, tls_ex_idx_session);
	if (!buf)
		return;
	wpa_printf(MSG_DEBUG,
		   "OpenSSL: Free application session data %p (sess %p)",
		   buf, sess);
	wpabuf_free(buf);

	SSL_SESSION_set_ex_data(sess, tls_ex_idx_session, NULL);
}


void * tls_init(const struct tls_config *conf)
{
	struct tls_data *data;
	SSL_CTX *ssl;
	struct tls_context *context;
	const char *ciphers;

	if (tls_openssl_ref_count == 0) {
		tls_global = context = tls_context_new(conf);
		if (context == NULL)
			return NULL;
#ifdef CONFIG_FIPS
#ifdef OPENSSL_FIPS
		if (conf && conf->fips_mode) {
			static int fips_enabled = 0;

			if (!fips_enabled && !FIPS_mode_set(1)) {
				wpa_printf(MSG_ERROR, "Failed to enable FIPS "
					   "mode");
				ERR_load_crypto_strings();
				ERR_print_errors_fp(stderr);
				os_free(tls_global);
				tls_global = NULL;
				return NULL;
			} else {
				wpa_printf(MSG_INFO, "Running in FIPS mode");
				fips_enabled = 1;
			}
		}
#else /* OPENSSL_FIPS */
		if (conf && conf->fips_mode) {
			wpa_printf(MSG_ERROR, "FIPS mode requested, but not "
				   "supported");
			os_free(tls_global);
			tls_global = NULL;
			return NULL;
		}
#endif /* OPENSSL_FIPS */
#endif /* CONFIG_FIPS */
#if OPENSSL_VERSION_NUMBER < 0x10100000L || \
	(defined(LIBRESSL_VERSION_NUMBER) && \
	 LIBRESSL_VERSION_NUMBER < 0x20700000L)
		SSL_load_error_strings();
		SSL_library_init();
#ifndef OPENSSL_NO_SHA256
		EVP_add_digest(EVP_sha256());
#endif /* OPENSSL_NO_SHA256 */
		/* TODO: if /dev/urandom is available, PRNG is seeded
		 * automatically. If this is not the case, random data should
		 * be added here. */

#ifdef PKCS12_FUNCS
#ifndef OPENSSL_NO_RC2
		/*
		 * 40-bit RC2 is commonly used in PKCS#12 files, so enable it.
		 * This is enabled by PKCS12_PBE_add() in OpenSSL 0.9.8
		 * versions, but it looks like OpenSSL 1.0.0 does not do that
		 * anymore.
		 */
		EVP_add_cipher(EVP_rc2_40_cbc());
#endif /* OPENSSL_NO_RC2 */
		PKCS12_PBE_add();
#endif  /* PKCS12_FUNCS */
#endif /* < 1.1.0 */
	} else {
		context = tls_context_new(conf);
		if (context == NULL)
			return NULL;
	}
	tls_openssl_ref_count++;

	data = os_zalloc(sizeof(*data));
	if (data)
		ssl = SSL_CTX_new(SSLv23_method());
	else
		ssl = NULL;
	if (ssl == NULL) {
		tls_openssl_ref_count--;
		if (context != tls_global)
			os_free(context);
		if (tls_openssl_ref_count == 0) {
			os_free(tls_global);
			tls_global = NULL;
		}
		os_free(data);
		return NULL;
	}
	data->ssl = ssl;
	if (conf)
		data->tls_session_lifetime = conf->tls_session_lifetime;

	SSL_CTX_set_options(ssl, SSL_OP_NO_SSLv2);
	SSL_CTX_set_options(ssl, SSL_OP_NO_SSLv3);

#ifdef SSL_MODE_NO_AUTO_CHAIN
	/* Number of deployed use cases assume the default OpenSSL behavior of
	 * auto chaining the local certificate is in use. BoringSSL removed this
	 * functionality by default, so we need to restore it here to avoid
	 * breaking existing use cases. */
	SSL_CTX_clear_mode(ssl, SSL_MODE_NO_AUTO_CHAIN);
#endif /* SSL_MODE_NO_AUTO_CHAIN */

	SSL_CTX_set_info_callback(ssl, ssl_info_cb);
	SSL_CTX_set_app_data(ssl, context);
	if (data->tls_session_lifetime > 0) {
		SSL_CTX_set_quiet_shutdown(ssl, 1);
		/*
		 * Set default context here. In practice, this will be replaced
		 * by the per-EAP method context in tls_connection_set_verify().
		 */
		SSL_CTX_set_session_id_context(ssl, (u8 *) "hostapd", 7);
		SSL_CTX_set_session_cache_mode(ssl, SSL_SESS_CACHE_SERVER);
		SSL_CTX_set_timeout(ssl, data->tls_session_lifetime);
		SSL_CTX_sess_set_remove_cb(ssl, remove_session_cb);
	} else {
		SSL_CTX_set_session_cache_mode(ssl, SSL_SESS_CACHE_OFF);
	}

	if (tls_ex_idx_session < 0) {
		tls_ex_idx_session = SSL_SESSION_get_ex_new_index(
			0, NULL, NULL, NULL, NULL);
		if (tls_ex_idx_session < 0) {
			tls_deinit(data);
			return NULL;
		}
	}

#ifndef OPENSSL_NO_ENGINE
	wpa_printf(MSG_DEBUG, "ENGINE: Loading dynamic engine");
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	ERR_load_ENGINE_strings();
	ENGINE_load_dynamic();
#endif /* OPENSSL_VERSION_NUMBER */

	if (conf &&
	    (conf->opensc_engine_path || conf->pkcs11_engine_path ||
	     conf->pkcs11_module_path)) {
		if (tls_engine_load_dynamic_opensc(conf->opensc_engine_path) ||
		    tls_engine_load_dynamic_pkcs11(conf->pkcs11_engine_path,
						   conf->pkcs11_module_path)) {
			tls_deinit(data);
			return NULL;
		}
	}
#endif /* OPENSSL_NO_ENGINE */

	if (conf && conf->openssl_ciphers)
		ciphers = conf->openssl_ciphers;
	else
		ciphers = TLS_DEFAULT_CIPHERS;
	if (SSL_CTX_set_cipher_list(ssl, ciphers) != 1) {
		wpa_printf(MSG_ERROR,
			   "OpenSSL: Failed to set cipher string '%s'",
			   ciphers);
		tls_deinit(data);
		return NULL;
	}

	return data;
}


void tls_deinit(void *ssl_ctx)
{
	struct tls_data *data = ssl_ctx;
	SSL_CTX *ssl = data->ssl;
	struct tls_context *context = SSL_CTX_get_app_data(ssl);
	if (context != tls_global)
		os_free(context);
	if (data->tls_session_lifetime > 0)
		SSL_CTX_flush_sessions(ssl, 0);
	SSL_CTX_free(ssl);

	tls_openssl_ref_count--;
	if (tls_openssl_ref_count == 0) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L || \
	(defined(LIBRESSL_VERSION_NUMBER) && \
	 LIBRESSL_VERSION_NUMBER < 0x20700000L)
#ifndef OPENSSL_NO_ENGINE
		ENGINE_cleanup();
#endif /* OPENSSL_NO_ENGINE */
		CRYPTO_cleanup_all_ex_data();
		ERR_remove_thread_state(NULL);
		ERR_free_strings();
		EVP_cleanup();
#endif /* < 1.1.0 */
		os_free(tls_global->ocsp_stapling_response);
		tls_global->ocsp_stapling_response = NULL;
		os_free(tls_global);
		tls_global = NULL;
	}

	os_free(data);
}


#ifndef OPENSSL_NO_ENGINE

/* Cryptoki return values */
#define CKR_PIN_INCORRECT 0x000000a0
#define CKR_PIN_INVALID 0x000000a1
#define CKR_PIN_LEN_RANGE 0x000000a2

/* libp11 */
#define ERR_LIB_PKCS11	ERR_LIB_USER

static int tls_is_pin_error(unsigned int err)
{
	return ERR_GET_LIB(err) == ERR_LIB_PKCS11 &&
		(ERR_GET_REASON(err) == CKR_PIN_INCORRECT ||
		 ERR_GET_REASON(err) == CKR_PIN_INVALID ||
		 ERR_GET_REASON(err) == CKR_PIN_LEN_RANGE);
}

#endif /* OPENSSL_NO_ENGINE */


#ifdef ANDROID
/* EVP_PKEY_from_keystore comes from system/security/keystore-engine. */
EVP_PKEY * EVP_PKEY_from_keystore(const char *key_id);
#endif /* ANDROID */

static int tls_engine_init(struct tls_connection *conn, const char *engine_id,
			   const char *pin, const char *key_id,
			   const char *cert_id, const char *ca_cert_id)
{
#if defined(ANDROID) && defined(OPENSSL_IS_BORINGSSL)
#if !defined(OPENSSL_NO_ENGINE)
#error "This code depends on OPENSSL_NO_ENGINE being defined by BoringSSL."
#endif
	if (!key_id)
		return TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED;
	conn->engine = NULL;
	conn->private_key = EVP_PKEY_from_keystore(key_id);
	if (!conn->private_key) {
		wpa_printf(MSG_ERROR,
			   "ENGINE: cannot load private key with id '%s' [%s]",
			   key_id,
			   ERR_error_string(ERR_get_error(), NULL));
		return TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED;
	}
#endif /* ANDROID && OPENSSL_IS_BORINGSSL */

#ifndef OPENSSL_NO_ENGINE
	int ret = -1;
	if (engine_id == NULL) {
		wpa_printf(MSG_ERROR, "ENGINE: Engine ID not set");
		return -1;
	}

	ERR_clear_error();
#ifdef ANDROID
	ENGINE_load_dynamic();
#endif
	conn->engine = ENGINE_by_id(engine_id);
	if (!conn->engine) {
		wpa_printf(MSG_ERROR, "ENGINE: engine %s not available [%s]",
			   engine_id, ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	if (ENGINE_init(conn->engine) != 1) {
		wpa_printf(MSG_ERROR, "ENGINE: engine init failed "
			   "(engine: %s) [%s]", engine_id,
			   ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	wpa_printf(MSG_DEBUG, "ENGINE: engine initialized");

#ifndef ANDROID
	if (pin && ENGINE_ctrl_cmd_string(conn->engine, "PIN", pin, 0) == 0) {
		wpa_printf(MSG_ERROR, "ENGINE: cannot set pin [%s]",
			   ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
#endif
	if (key_id) {
		/*
		 * Ensure that the ENGINE does not attempt to use the OpenSSL
		 * UI system to obtain a PIN, if we didn't provide one.
		 */
		struct {
			const void *password;
			const char *prompt_info;
		} key_cb = { "", NULL };

		/* load private key first in-case PIN is required for cert */
		conn->private_key = ENGINE_load_private_key(conn->engine,
							    key_id, NULL,
							    &key_cb);
		if (!conn->private_key) {
			unsigned long err = ERR_get_error();

			wpa_printf(MSG_ERROR,
				   "ENGINE: cannot load private key with id '%s' [%s]",
				   key_id,
				   ERR_error_string(err, NULL));
			if (tls_is_pin_error(err))
				ret = TLS_SET_PARAMS_ENGINE_PRV_BAD_PIN;
			else
				ret = TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED;
			goto err;
		}
	}

	/* handle a certificate and/or CA certificate */
	if (cert_id || ca_cert_id) {
		const char *cmd_name = "LOAD_CERT_CTRL";

		/* test if the engine supports a LOAD_CERT_CTRL */
		if (!ENGINE_ctrl(conn->engine, ENGINE_CTRL_GET_CMD_FROM_NAME,
				 0, (void *)cmd_name, NULL)) {
			wpa_printf(MSG_ERROR, "ENGINE: engine does not support"
				   " loading certificates");
			ret = TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED;
			goto err;
		}
	}

	return 0;

err:
	if (conn->engine) {
		ENGINE_free(conn->engine);
		conn->engine = NULL;
	}

	if (conn->private_key) {
		EVP_PKEY_free(conn->private_key);
		conn->private_key = NULL;
	}

	return ret;
#else /* OPENSSL_NO_ENGINE */
	return 0;
#endif /* OPENSSL_NO_ENGINE */
}


static void tls_engine_deinit(struct tls_connection *conn)
{
#if defined(ANDROID) || !defined(OPENSSL_NO_ENGINE)
	wpa_printf(MSG_DEBUG, "ENGINE: engine deinit");
	if (conn->private_key) {
		EVP_PKEY_free(conn->private_key);
		conn->private_key = NULL;
	}
	if (conn->engine) {
#if !defined(OPENSSL_IS_BORINGSSL)
		ENGINE_finish(conn->engine);
#endif /* !OPENSSL_IS_BORINGSSL */
		conn->engine = NULL;
	}
#endif /* ANDROID || !OPENSSL_NO_ENGINE */
}


int tls_get_errors(void *ssl_ctx)
{
	int count = 0;
	unsigned long err;

	while ((err = ERR_get_error())) {
		wpa_printf(MSG_INFO, "TLS - SSL error: %s",
			   ERR_error_string(err, NULL));
		count++;
	}

	return count;
}


static const char * openssl_content_type(int content_type)
{
	switch (content_type) {
	case 20:
		return "change cipher spec";
	case 21:
		return "alert";
	case 22:
		return "handshake";
	case 23:
		return "application data";
	case 24:
		return "heartbeat";
	case 256:
		return "TLS header info"; /* pseudo content type */
	default:
		return "?";
	}
}


static const char * openssl_handshake_type(int content_type, const u8 *buf,
					   size_t len)
{
	if (content_type != 22 || !buf || len == 0)
		return "";
	switch (buf[0]) {
	case 0:
		return "hello request";
	case 1:
		return "client hello";
	case 2:
		return "server hello";
	case 4:
		return "new session ticket";
	case 11:
		return "certificate";
	case 12:
		return "server key exchange";
	case 13:
		return "certificate request";
	case 14:
		return "server hello done";
	case 15:
		return "certificate verify";
	case 16:
		return "client key exchange";
	case 20:
		return "finished";
	case 21:
		return "certificate url";
	case 22:
		return "certificate status";
	default:
		return "?";
	}
}


#ifdef CONFIG_SUITEB

static void check_server_hello(struct tls_connection *conn,
			       const u8 *pos, const u8 *end)
{
	size_t payload_len, id_len;

	/*
	 * Parse ServerHello to get the selected cipher suite since OpenSSL does
	 * not make it cleanly available during handshake and we need to know
	 * whether DHE was selected.
	 */

	if (end - pos < 3)
		return;
	payload_len = WPA_GET_BE24(pos);
	pos += 3;

	if ((size_t) (end - pos) < payload_len)
		return;
	end = pos + payload_len;

	/* Skip Version and Random */
	if (end - pos < 2 + SSL3_RANDOM_SIZE)
		return;
	pos += 2 + SSL3_RANDOM_SIZE;

	/* Skip Session ID */
	if (end - pos < 1)
		return;
	id_len = *pos++;
	if ((size_t) (end - pos) < id_len)
		return;
	pos += id_len;

	if (end - pos < 2)
		return;
	conn->cipher_suite = WPA_GET_BE16(pos);
	wpa_printf(MSG_DEBUG, "OpenSSL: Server selected cipher suite 0x%x",
		   conn->cipher_suite);
}


static void check_server_key_exchange(SSL *ssl, struct tls_connection *conn,
				      const u8 *pos, const u8 *end)
{
	size_t payload_len;
	u16 dh_len;
	BIGNUM *p;
	int bits;

	if (!(conn->flags & TLS_CONN_SUITEB))
		return;

	/* DHE is enabled only with DHE-RSA-AES256-GCM-SHA384 */
	if (conn->cipher_suite != 0x9f)
		return;

	if (end - pos < 3)
		return;
	payload_len = WPA_GET_BE24(pos);
	pos += 3;

	if ((size_t) (end - pos) < payload_len)
		return;
	end = pos + payload_len;

	if (end - pos < 2)
		return;
	dh_len = WPA_GET_BE16(pos);
	pos += 2;

	if ((size_t) (end - pos) < dh_len)
		return;
	p = BN_bin2bn(pos, dh_len, NULL);
	if (!p)
		return;

	bits = BN_num_bits(p);
	BN_free(p);

	conn->server_dh_prime_len = bits;
	wpa_printf(MSG_DEBUG, "OpenSSL: Server DH prime length: %d bits",
		   conn->server_dh_prime_len);
}

#endif /* CONFIG_SUITEB */


static void tls_msg_cb(int write_p, int version, int content_type,
		       const void *buf, size_t len, SSL *ssl, void *arg)
{
	struct tls_connection *conn = arg;
	const u8 *pos = buf;

	if (write_p == 2) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: session ver=0x%x content_type=%d",
			   version, content_type);
		wpa_hexdump_key(MSG_MSGDUMP, "OpenSSL: Data", buf, len);
		return;
	}

	wpa_printf(MSG_DEBUG, "OpenSSL: %s ver=0x%x content_type=%d (%s/%s)",
		   write_p ? "TX" : "RX", version, content_type,
		   openssl_content_type(content_type),
		   openssl_handshake_type(content_type, buf, len));
	wpa_hexdump_key(MSG_MSGDUMP, "OpenSSL: Message", buf, len);
	if (content_type == 24 && len >= 3 && pos[0] == 1) {
		size_t payload_len = WPA_GET_BE16(pos + 1);
		if (payload_len + 3 > len) {
			wpa_printf(MSG_ERROR, "OpenSSL: Heartbeat attack detected");
			conn->invalid_hb_used = 1;
		}
	}

#ifdef CONFIG_SUITEB
	/*
	 * Need to parse these handshake messages to be able to check DH prime
	 * length since OpenSSL does not expose the new cipher suite and DH
	 * parameters during handshake (e.g., for cert_cb() callback).
	 */
	if (content_type == 22 && pos && len > 0 && pos[0] == 2)
		check_server_hello(conn, pos + 1, pos + len);
	if (content_type == 22 && pos && len > 0 && pos[0] == 12)
		check_server_key_exchange(ssl, conn, pos + 1, pos + len);
#endif /* CONFIG_SUITEB */
}


struct tls_connection * tls_connection_init(void *ssl_ctx)
{
	struct tls_data *data = ssl_ctx;
	SSL_CTX *ssl = data->ssl;
	struct tls_connection *conn;
	long options;
	struct tls_context *context = SSL_CTX_get_app_data(ssl);

	conn = os_zalloc(sizeof(*conn));
	if (conn == NULL)
		return NULL;
	conn->ssl_ctx = ssl;
	conn->ssl = SSL_new(ssl);
	if (conn->ssl == NULL) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to initialize new SSL connection");
		os_free(conn);
		return NULL;
	}

	conn->context = context;
	SSL_set_app_data(conn->ssl, conn);
	SSL_set_msg_callback(conn->ssl, tls_msg_cb);
	SSL_set_msg_callback_arg(conn->ssl, conn);
	options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
		SSL_OP_SINGLE_DH_USE;
#ifdef SSL_OP_NO_COMPRESSION
	options |= SSL_OP_NO_COMPRESSION;
#endif /* SSL_OP_NO_COMPRESSION */
	SSL_set_options(conn->ssl, options);

	conn->ssl_in = BIO_new(BIO_s_mem());
	if (!conn->ssl_in) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to create a new BIO for ssl_in");
		SSL_free(conn->ssl);
		os_free(conn);
		return NULL;
	}

	conn->ssl_out = BIO_new(BIO_s_mem());
	if (!conn->ssl_out) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to create a new BIO for ssl_out");
		SSL_free(conn->ssl);
		BIO_free(conn->ssl_in);
		os_free(conn);
		return NULL;
	}

	SSL_set_bio(conn->ssl, conn->ssl_in, conn->ssl_out);

	return conn;
}


void tls_connection_deinit(void *ssl_ctx, struct tls_connection *conn)
{
	if (conn == NULL)
		return;
	if (conn->success_data) {
		/*
		 * Make sure ssl_clear_bad_session() does not remove this
		 * session.
		 */
		SSL_set_quiet_shutdown(conn->ssl, 1);
		SSL_shutdown(conn->ssl);
	}
	SSL_free(conn->ssl);
	tls_engine_deinit(conn);
	os_free(conn->subject_match);
	os_free(conn->altsubject_match);
	os_free(conn->suffix_match);
	os_free(conn->domain_match);
	os_free(conn->session_ticket);
	os_free(conn);
}


int tls_connection_established(void *ssl_ctx, struct tls_connection *conn)
{
	return conn ? SSL_is_init_finished(conn->ssl) : 0;
}


char * tls_connection_peer_serial_num(void *tls_ctx,
				      struct tls_connection *conn)
{
	ASN1_INTEGER *ser;
	char *serial_num;
	size_t len;

	if (!conn->peer_cert)
		return NULL;

	ser = X509_get_serialNumber(conn->peer_cert);
	if (!ser)
		return NULL;

	len = ASN1_STRING_length(ser) * 2 + 1;
	serial_num = os_malloc(len);
	if (!serial_num)
		return NULL;
	wpa_snprintf_hex_uppercase(serial_num, len,
				   ASN1_STRING_get0_data(ser),
				   ASN1_STRING_length(ser));
	return serial_num;
}


int tls_connection_shutdown(void *ssl_ctx, struct tls_connection *conn)
{
	if (conn == NULL)
		return -1;

	/* Shutdown previous TLS connection without notifying the peer
	 * because the connection was already terminated in practice
	 * and "close notify" shutdown alert would confuse AS. */
	SSL_set_quiet_shutdown(conn->ssl, 1);
	SSL_shutdown(conn->ssl);
	return SSL_clear(conn->ssl) == 1 ? 0 : -1;
}


static int tls_match_altsubject_component(X509 *cert, int type,
					  const char *value, size_t len)
{
	GENERAL_NAME *gen;
	void *ext;
	int found = 0;
	stack_index_t i;

	ext = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);

	for (i = 0; ext && i < sk_GENERAL_NAME_num(ext); i++) {
		gen = sk_GENERAL_NAME_value(ext, i);
		if (gen->type != type)
			continue;
		if (os_strlen((char *) gen->d.ia5->data) == len &&
		    os_memcmp(value, gen->d.ia5->data, len) == 0)
			found++;
	}

	sk_GENERAL_NAME_pop_free(ext, GENERAL_NAME_free);

	return found;
}


static int tls_match_altsubject(X509 *cert, const char *match)
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
			wpa_printf(MSG_INFO, "TLS: Invalid altSubjectName "
				   "match '%s'", pos);
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
		if (tls_match_altsubject_component(cert, type, pos, len) > 0)
			return 1;
		pos = end + 1;
	} while (end);

	return 0;
}


#ifndef CONFIG_NATIVE_WINDOWS
static int domain_suffix_match(const u8 *val, size_t len, const char *match,
			       int full)
{
	size_t i, match_len;

	/* Check for embedded nuls that could mess up suffix matching */
	for (i = 0; i < len; i++) {
		if (val[i] == '\0') {
			wpa_printf(MSG_DEBUG, "TLS: Embedded null in a string - reject");
			return 0;
		}
	}

	match_len = os_strlen(match);
	if (match_len > len || (full && match_len != len))
		return 0;

	if (os_strncasecmp((const char *) val + len - match_len, match,
			   match_len) != 0)
		return 0; /* no match */

	if (match_len == len)
		return 1; /* exact match */

	if (val[len - match_len - 1] == '.')
		return 1; /* full label match completes suffix match */

	wpa_printf(MSG_DEBUG, "TLS: Reject due to incomplete label match");
	return 0;
}
#endif /* CONFIG_NATIVE_WINDOWS */


static int tls_match_suffix(X509 *cert, const char *match, int full)
{
#ifdef CONFIG_NATIVE_WINDOWS
	/* wincrypt.h has conflicting X509_NAME definition */
	return -1;
#else /* CONFIG_NATIVE_WINDOWS */
	GENERAL_NAME *gen;
	void *ext;
	int i;
	stack_index_t j;
	int dns_name = 0;
	X509_NAME *name;

	wpa_printf(MSG_DEBUG, "TLS: Match domain against %s%s",
		   full ? "": "suffix ", match);

	ext = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);

	for (j = 0; ext && j < sk_GENERAL_NAME_num(ext); j++) {
		gen = sk_GENERAL_NAME_value(ext, j);
		if (gen->type != GEN_DNS)
			continue;
		dns_name++;
		wpa_hexdump_ascii(MSG_DEBUG, "TLS: Certificate dNSName",
				  gen->d.dNSName->data,
				  gen->d.dNSName->length);
		if (domain_suffix_match(gen->d.dNSName->data,
					gen->d.dNSName->length, match, full) ==
		    1) {
			wpa_printf(MSG_DEBUG, "TLS: %s in dNSName found",
				   full ? "Match" : "Suffix match");
			sk_GENERAL_NAME_pop_free(ext, GENERAL_NAME_free);
			return 1;
		}
	}
	sk_GENERAL_NAME_pop_free(ext, GENERAL_NAME_free);

	if (dns_name) {
		wpa_printf(MSG_DEBUG, "TLS: None of the dNSName(s) matched");
		return 0;
	}

	name = X509_get_subject_name(cert);
	i = -1;
	for (;;) {
		X509_NAME_ENTRY *e;
		ASN1_STRING *cn;

		i = X509_NAME_get_index_by_NID(name, NID_commonName, i);
		if (i == -1)
			break;
		e = X509_NAME_get_entry(name, i);
		if (e == NULL)
			continue;
		cn = X509_NAME_ENTRY_get_data(e);
		if (cn == NULL)
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
		   full ? "": "suffix ");
	return 0;
#endif /* CONFIG_NATIVE_WINDOWS */
}


static enum tls_fail_reason openssl_tls_fail_reason(int err)
{
	switch (err) {
	case X509_V_ERR_CERT_REVOKED:
		return TLS_FAIL_REVOKED;
	case X509_V_ERR_CERT_NOT_YET_VALID:
	case X509_V_ERR_CRL_NOT_YET_VALID:
		return TLS_FAIL_NOT_YET_VALID;
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


static struct wpabuf * get_x509_cert(X509 *cert)
{
	struct wpabuf *buf;
	u8 *tmp;

	int cert_len = i2d_X509(cert, NULL);
	if (cert_len <= 0)
		return NULL;

	buf = wpabuf_alloc(cert_len);
	if (buf == NULL)
		return NULL;

	tmp = wpabuf_put(buf, cert_len);
	i2d_X509(cert, &tmp);
	return buf;
}


static void openssl_tls_fail_event(struct tls_connection *conn,
				   X509 *err_cert, int err, int depth,
				   const char *subject, const char *err_str,
				   enum tls_fail_reason reason)
{
	union tls_event_data ev;
	struct wpabuf *cert = NULL;
	struct tls_context *context = conn->context;

	if (context->event_cb == NULL)
		return;

	cert = get_x509_cert(err_cert);
	os_memset(&ev, 0, sizeof(ev));
	ev.cert_fail.reason = reason != TLS_FAIL_UNSPECIFIED ?
		reason : openssl_tls_fail_reason(err);
	ev.cert_fail.depth = depth;
	ev.cert_fail.subject = subject;
	ev.cert_fail.reason_txt = err_str;
	ev.cert_fail.cert = cert;
	context->event_cb(context->cb_ctx, TLS_CERT_CHAIN_FAILURE, &ev);
	wpabuf_free(cert);
}


static void openssl_tls_cert_event(struct tls_connection *conn,
				   X509 *err_cert, int depth,
				   const char *subject)
{
	struct wpabuf *cert = NULL;
	union tls_event_data ev;
	struct tls_context *context = conn->context;
	char *altsubject[TLS_MAX_ALT_SUBJECT];
	int alt, num_altsubject = 0;
	GENERAL_NAME *gen;
	void *ext;
	stack_index_t i;
	ASN1_INTEGER *ser;
	char serial_num[128];
#ifdef CONFIG_SHA256
	u8 hash[32];
#endif /* CONFIG_SHA256 */

	if (context->event_cb == NULL)
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

	ser = X509_get_serialNumber(err_cert);
	if (ser) {
		wpa_snprintf_hex_uppercase(serial_num, sizeof(serial_num),
					   ASN1_STRING_get0_data(ser),
					   ASN1_STRING_length(ser));
		ev.peer_cert.serial_num = serial_num;
	}

	ext = X509_get_ext_d2i(err_cert, NID_subject_alt_name, NULL, NULL);
	for (i = 0; ext && i < sk_GENERAL_NAME_num(ext); i++) {
		char *pos;

		if (num_altsubject == TLS_MAX_ALT_SUBJECT)
			break;
		gen = sk_GENERAL_NAME_value(ext, i);
		if (gen->type != GEN_EMAIL &&
		    gen->type != GEN_DNS &&
		    gen->type != GEN_URI)
			continue;

		pos = os_malloc(10 + gen->d.ia5->length + 1);
		if (pos == NULL)
			break;
		altsubject[num_altsubject++] = pos;

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

		os_memcpy(pos, gen->d.ia5->data, gen->d.ia5->length);
		pos += gen->d.ia5->length;
		*pos = '\0';
	}
	sk_GENERAL_NAME_pop_free(ext, GENERAL_NAME_free);

	for (alt = 0; alt < num_altsubject; alt++)
		ev.peer_cert.altsubject[alt] = altsubject[alt];
	ev.peer_cert.num_altsubject = num_altsubject;

	context->event_cb(context->cb_ctx, TLS_PEER_CERTIFICATE, &ev);
	wpabuf_free(cert);
	for (alt = 0; alt < num_altsubject; alt++)
		os_free(altsubject[alt]);
}


static int tls_verify_cb(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
	char buf[256];
	X509 *err_cert;
	int err, depth;
	SSL *ssl;
	struct tls_connection *conn;
	struct tls_context *context;
	char *match, *altmatch, *suffix_match, *domain_match;
	const char *err_str;

	err_cert = X509_STORE_CTX_get_current_cert(x509_ctx);
	if (!err_cert)
		return 0;

	err = X509_STORE_CTX_get_error(x509_ctx);
	depth = X509_STORE_CTX_get_error_depth(x509_ctx);
	ssl = X509_STORE_CTX_get_ex_data(x509_ctx,
					 SSL_get_ex_data_X509_STORE_CTX_idx());
	X509_NAME_oneline(X509_get_subject_name(err_cert), buf, sizeof(buf));

	conn = SSL_get_app_data(ssl);
	if (conn == NULL)
		return 0;

	if (depth == 0)
		conn->peer_cert = err_cert;
	else if (depth == 1)
		conn->peer_issuer = err_cert;
	else if (depth == 2)
		conn->peer_issuer_issuer = err_cert;

	context = conn->context;
	match = conn->subject_match;
	altmatch = conn->altsubject_match;
	suffix_match = conn->suffix_match;
	domain_match = conn->domain_match;

	if (!preverify_ok && !conn->ca_cert_verify)
		preverify_ok = 1;
	if (!preverify_ok && depth > 0 && conn->server_cert_only)
		preverify_ok = 1;
	if (!preverify_ok && (conn->flags & TLS_CONN_DISABLE_TIME_CHECKS) &&
	    (err == X509_V_ERR_CERT_HAS_EXPIRED ||
	     err == X509_V_ERR_CERT_NOT_YET_VALID)) {
		wpa_printf(MSG_DEBUG, "OpenSSL: Ignore certificate validity "
			   "time mismatch");
		preverify_ok = 1;
	}

	err_str = X509_verify_cert_error_string(err);

#ifdef CONFIG_SHA256
	/*
	 * Do not require preverify_ok so we can explicity allow otherwise
	 * invalid pinned server certificates.
	 */
	if (depth == 0 && conn->server_cert_only) {
		struct wpabuf *cert;
		cert = get_x509_cert(err_cert);
		if (!cert) {
			wpa_printf(MSG_DEBUG, "OpenSSL: Could not fetch "
				   "server certificate data");
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
					   "OpenSSL: Ignore validation issues for a pinned server certificate");
				preverify_ok = 1;
			}
			wpabuf_free(cert);
		}
	}
#endif /* CONFIG_SHA256 */

	if (!preverify_ok) {
		wpa_printf(MSG_WARNING, "TLS: Certificate verification failed,"
			   " error %d (%s) depth %d for '%s'", err, err_str,
			   depth, buf);
		openssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       err_str, TLS_FAIL_UNSPECIFIED);
		return preverify_ok;
	}

	wpa_printf(MSG_DEBUG, "TLS: tls_verify_cb - preverify_ok=%d "
		   "err=%d (%s) ca_cert_verify=%d depth=%d buf='%s'",
		   preverify_ok, err, err_str,
		   conn->ca_cert_verify, depth, buf);
	if (depth == 0 && match && os_strstr(buf, match) == NULL) {
		wpa_printf(MSG_WARNING, "TLS: Subject '%s' did not "
			   "match with '%s'", buf, match);
		preverify_ok = 0;
		openssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       "Subject mismatch",
				       TLS_FAIL_SUBJECT_MISMATCH);
	} else if (depth == 0 && altmatch &&
		   !tls_match_altsubject(err_cert, altmatch)) {
		wpa_printf(MSG_WARNING, "TLS: altSubjectName match "
			   "'%s' not found", altmatch);
		preverify_ok = 0;
		openssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       "AltSubject mismatch",
				       TLS_FAIL_ALTSUBJECT_MISMATCH);
	} else if (depth == 0 && suffix_match &&
		   !tls_match_suffix(err_cert, suffix_match, 0)) {
		wpa_printf(MSG_WARNING, "TLS: Domain suffix match '%s' not found",
			   suffix_match);
		preverify_ok = 0;
		openssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       "Domain suffix mismatch",
				       TLS_FAIL_DOMAIN_SUFFIX_MISMATCH);
	} else if (depth == 0 && domain_match &&
		   !tls_match_suffix(err_cert, domain_match, 1)) {
		wpa_printf(MSG_WARNING, "TLS: Domain match '%s' not found",
			   domain_match);
		preverify_ok = 0;
		openssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       "Domain mismatch",
				       TLS_FAIL_DOMAIN_MISMATCH);
	} else
		openssl_tls_cert_event(conn, err_cert, depth, buf);

	if (conn->cert_probe && preverify_ok && depth == 0) {
		wpa_printf(MSG_DEBUG, "OpenSSL: Reject server certificate "
			   "on probe-only run");
		preverify_ok = 0;
		openssl_tls_fail_event(conn, err_cert, err, depth, buf,
				       "Server certificate chain probe",
				       TLS_FAIL_SERVER_CHAIN_PROBE);
	}

#ifdef CONFIG_SUITEB
	if (conn->flags & TLS_CONN_SUITEB) {
		EVP_PKEY *pk;
		RSA *rsa;
		int len = -1;

		pk = X509_get_pubkey(err_cert);
		if (pk) {
			rsa = EVP_PKEY_get1_RSA(pk);
			if (rsa) {
				len = RSA_bits(rsa);
				RSA_free(rsa);
			}
			EVP_PKEY_free(pk);
		}

		if (len >= 0) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: RSA modulus size: %d bits", len);
			if (len < 3072) {
				preverify_ok = 0;
				openssl_tls_fail_event(
					conn, err_cert, err,
					depth, buf,
					"Insufficient RSA modulus size",
					TLS_FAIL_INSUFFICIENT_KEY_LEN);
			}
		}
	}
#endif /* CONFIG_SUITEB */

#ifdef OPENSSL_IS_BORINGSSL
	if (depth == 0 && (conn->flags & TLS_CONN_REQUEST_OCSP) &&
	    preverify_ok) {
		enum ocsp_result res;

		res = check_ocsp_resp(conn->ssl_ctx, conn->ssl, err_cert,
				      conn->peer_issuer,
				      conn->peer_issuer_issuer);
		if (res == OCSP_REVOKED) {
			preverify_ok = 0;
			openssl_tls_fail_event(conn, err_cert, err, depth, buf,
					       "certificate revoked",
					       TLS_FAIL_REVOKED);
			if (err == X509_V_OK)
				X509_STORE_CTX_set_error(
					x509_ctx, X509_V_ERR_CERT_REVOKED);
		} else if (res != OCSP_GOOD &&
			   (conn->flags & TLS_CONN_REQUIRE_OCSP)) {
			preverify_ok = 0;
			openssl_tls_fail_event(conn, err_cert, err, depth, buf,
					       "bad certificate status response",
					       TLS_FAIL_UNSPECIFIED);
		}
	}
#endif /* OPENSSL_IS_BORINGSSL */

	if (depth == 0 && preverify_ok && context->event_cb != NULL)
		context->event_cb(context->cb_ctx,
				  TLS_CERT_CHAIN_SUCCESS, NULL);

	return preverify_ok;
}


#ifndef OPENSSL_NO_STDIO
static int tls_load_ca_der(struct tls_data *data, const char *ca_cert)
{
	SSL_CTX *ssl_ctx = data->ssl;
	X509_LOOKUP *lookup;
	int ret = 0;

	lookup = X509_STORE_add_lookup(SSL_CTX_get_cert_store(ssl_ctx),
				       X509_LOOKUP_file());
	if (lookup == NULL) {
		tls_show_errors(MSG_WARNING, __func__,
				"Failed add lookup for X509 store");
		return -1;
	}

	if (!X509_LOOKUP_load_file(lookup, ca_cert, X509_FILETYPE_ASN1)) {
		unsigned long err = ERR_peek_error();
		tls_show_errors(MSG_WARNING, __func__,
				"Failed load CA in DER format");
		if (ERR_GET_LIB(err) == ERR_LIB_X509 &&
		    ERR_GET_REASON(err) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
			wpa_printf(MSG_DEBUG, "OpenSSL: %s - ignoring "
				   "cert already in hash table error",
				   __func__);
		} else
			ret = -1;
	}

	return ret;
}
#endif /* OPENSSL_NO_STDIO */


static int tls_connection_ca_cert(struct tls_data *data,
				  struct tls_connection *conn,
				  const char *ca_cert, const u8 *ca_cert_blob,
				  size_t ca_cert_blob_len, const char *ca_path)
{
	SSL_CTX *ssl_ctx = data->ssl;
	X509_STORE *store;

	/*
	 * Remove previously configured trusted CA certificates before adding
	 * new ones.
	 */
	store = X509_STORE_new();
	if (store == NULL) {
		wpa_printf(MSG_DEBUG, "OpenSSL: %s - failed to allocate new "
			   "certificate store", __func__);
		return -1;
	}
	SSL_CTX_set_cert_store(ssl_ctx, store);

	SSL_set_verify(conn->ssl, SSL_VERIFY_PEER, tls_verify_cb);
	conn->ca_cert_verify = 1;

	if (ca_cert && os_strncmp(ca_cert, "probe://", 8) == 0) {
		wpa_printf(MSG_DEBUG, "OpenSSL: Probe for server certificate "
			   "chain");
		conn->cert_probe = 1;
		conn->ca_cert_verify = 0;
		return 0;
	}

	if (ca_cert && os_strncmp(ca_cert, "hash://", 7) == 0) {
#ifdef CONFIG_SHA256
		const char *pos = ca_cert + 7;
		if (os_strncmp(pos, "server/sha256/", 14) != 0) {
			wpa_printf(MSG_DEBUG, "OpenSSL: Unsupported ca_cert "
				   "hash value '%s'", ca_cert);
			return -1;
		}
		pos += 14;
		if (os_strlen(pos) != 32 * 2) {
			wpa_printf(MSG_DEBUG, "OpenSSL: Unexpected SHA256 "
				   "hash length in ca_cert '%s'", ca_cert);
			return -1;
		}
		if (hexstr2bin(pos, conn->srv_cert_hash, 32) < 0) {
			wpa_printf(MSG_DEBUG, "OpenSSL: Invalid SHA256 hash "
				   "value in ca_cert '%s'", ca_cert);
			return -1;
		}
		conn->server_cert_only = 1;
		wpa_printf(MSG_DEBUG, "OpenSSL: Checking only server "
			   "certificate match");
		return 0;
#else /* CONFIG_SHA256 */
		wpa_printf(MSG_INFO, "No SHA256 included in the build - "
			   "cannot validate server certificate hash");
		return -1;
#endif /* CONFIG_SHA256 */
	}

	if (ca_cert_blob) {
		X509 *cert = d2i_X509(NULL,
				      (const unsigned char **) &ca_cert_blob,
				      ca_cert_blob_len);
		if (cert == NULL) {
			tls_show_errors(MSG_WARNING, __func__,
					"Failed to parse ca_cert_blob");
			return -1;
		}

		if (!X509_STORE_add_cert(SSL_CTX_get_cert_store(ssl_ctx),
					 cert)) {
			unsigned long err = ERR_peek_error();
			tls_show_errors(MSG_WARNING, __func__,
					"Failed to add ca_cert_blob to "
					"certificate store");
			if (ERR_GET_LIB(err) == ERR_LIB_X509 &&
			    ERR_GET_REASON(err) ==
			    X509_R_CERT_ALREADY_IN_HASH_TABLE) {
				wpa_printf(MSG_DEBUG, "OpenSSL: %s - ignoring "
					   "cert already in hash table error",
					   __func__);
			} else {
				X509_free(cert);
				return -1;
			}
		}
		X509_free(cert);
		wpa_printf(MSG_DEBUG, "OpenSSL: %s - added ca_cert_blob "
			   "to certificate store", __func__);
		return 0;
	}

#ifdef ANDROID
	/* Single alias */
	if (ca_cert && os_strncmp("keystore://", ca_cert, 11) == 0) {
		if (tls_add_ca_from_keystore(SSL_CTX_get_cert_store(ssl_ctx),
					     &ca_cert[11]) < 0)
			return -1;
		SSL_set_verify(conn->ssl, SSL_VERIFY_PEER, tls_verify_cb);
		return 0;
	}

	/* Multiple aliases separated by space */
	if (ca_cert && os_strncmp("keystores://", ca_cert, 12) == 0) {
		char *aliases = os_strdup(&ca_cert[12]);
		const char *delim = " ";
		int rc = 0;
		char *savedptr;
		char *alias;

		if (!aliases)
			return -1;
		alias = strtok_r(aliases, delim, &savedptr);
		for (; alias; alias = strtok_r(NULL, delim, &savedptr)) {
			if (tls_add_ca_from_keystore_encoded(
				    SSL_CTX_get_cert_store(ssl_ctx), alias)) {
				wpa_printf(MSG_WARNING,
					   "OpenSSL: %s - Failed to add ca_cert %s from keystore",
					   __func__, alias);
				rc = -1;
				break;
			}
		}
		os_free(aliases);
		if (rc)
			return rc;

		SSL_set_verify(conn->ssl, SSL_VERIFY_PEER, tls_verify_cb);
		return 0;
	}
#endif /* ANDROID */

#ifdef CONFIG_NATIVE_WINDOWS
	if (ca_cert && tls_cryptoapi_ca_cert(ssl_ctx, conn->ssl, ca_cert) ==
	    0) {
		wpa_printf(MSG_DEBUG, "OpenSSL: Added CA certificates from "
			   "system certificate store");
		return 0;
	}
#endif /* CONFIG_NATIVE_WINDOWS */

	if (ca_cert || ca_path) {
#ifndef OPENSSL_NO_STDIO
		if (SSL_CTX_load_verify_locations(ssl_ctx, ca_cert, ca_path) !=
		    1) {
			tls_show_errors(MSG_WARNING, __func__,
					"Failed to load root certificates");
			if (ca_cert &&
			    tls_load_ca_der(data, ca_cert) == 0) {
				wpa_printf(MSG_DEBUG, "OpenSSL: %s - loaded "
					   "DER format CA certificate",
					   __func__);
			} else
				return -1;
		} else {
			wpa_printf(MSG_DEBUG, "TLS: Trusted root "
				   "certificate(s) loaded");
			tls_get_errors(data);
		}
#else /* OPENSSL_NO_STDIO */
		wpa_printf(MSG_DEBUG, "OpenSSL: %s - OPENSSL_NO_STDIO",
			   __func__);
		return -1;
#endif /* OPENSSL_NO_STDIO */
	} else {
		/* No ca_cert configured - do not try to verify server
		 * certificate */
		conn->ca_cert_verify = 0;
	}

	return 0;
}


static int tls_global_ca_cert(struct tls_data *data, const char *ca_cert)
{
	SSL_CTX *ssl_ctx = data->ssl;

	if (ca_cert) {
		if (SSL_CTX_load_verify_locations(ssl_ctx, ca_cert, NULL) != 1)
		{
			tls_show_errors(MSG_WARNING, __func__,
					"Failed to load root certificates");
			return -1;
		}

		wpa_printf(MSG_DEBUG, "TLS: Trusted root "
			   "certificate(s) loaded");

#ifndef OPENSSL_NO_STDIO
		/* Add the same CAs to the client certificate requests */
		SSL_CTX_set_client_CA_list(ssl_ctx,
					   SSL_load_client_CA_file(ca_cert));
#endif /* OPENSSL_NO_STDIO */
	}

	return 0;
}


int tls_global_set_verify(void *ssl_ctx, int check_crl)
{
	int flags;

	if (check_crl) {
		struct tls_data *data = ssl_ctx;
		X509_STORE *cs = SSL_CTX_get_cert_store(data->ssl);
		if (cs == NULL) {
			tls_show_errors(MSG_INFO, __func__, "Failed to get "
					"certificate store when enabling "
					"check_crl");
			return -1;
		}
		flags = X509_V_FLAG_CRL_CHECK;
		if (check_crl == 2)
			flags |= X509_V_FLAG_CRL_CHECK_ALL;
		X509_STORE_set_flags(cs, flags);
	}
	return 0;
}


static int tls_connection_set_subject_match(struct tls_connection *conn,
					    const char *subject_match,
					    const char *altsubject_match,
					    const char *suffix_match,
					    const char *domain_match)
{
	os_free(conn->subject_match);
	conn->subject_match = NULL;
	if (subject_match) {
		conn->subject_match = os_strdup(subject_match);
		if (conn->subject_match == NULL)
			return -1;
	}

	os_free(conn->altsubject_match);
	conn->altsubject_match = NULL;
	if (altsubject_match) {
		conn->altsubject_match = os_strdup(altsubject_match);
		if (conn->altsubject_match == NULL)
			return -1;
	}

	os_free(conn->suffix_match);
	conn->suffix_match = NULL;
	if (suffix_match) {
		conn->suffix_match = os_strdup(suffix_match);
		if (conn->suffix_match == NULL)
			return -1;
	}

	os_free(conn->domain_match);
	conn->domain_match = NULL;
	if (domain_match) {
		conn->domain_match = os_strdup(domain_match);
		if (conn->domain_match == NULL)
			return -1;
	}

	return 0;
}


#ifdef CONFIG_SUITEB
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
static int suiteb_cert_cb(SSL *ssl, void *arg)
{
	struct tls_connection *conn = arg;

	/*
	 * This cert_cb() is not really the best location for doing a
	 * constraint check for the ServerKeyExchange message, but this seems to
	 * be the only place where the current OpenSSL sequence can be
	 * terminated cleanly with an TLS alert going out to the server.
	 */

	if (!(conn->flags & TLS_CONN_SUITEB))
		return 1;

	/* DHE is enabled only with DHE-RSA-AES256-GCM-SHA384 */
	if (conn->cipher_suite != 0x9f)
		return 1;

	if (conn->server_dh_prime_len >= 3072)
		return 1;

	wpa_printf(MSG_DEBUG,
		   "OpenSSL: Server DH prime length (%d bits) not sufficient for Suite B RSA - reject handshake",
		   conn->server_dh_prime_len);
	return 0;
}
#endif /* OPENSSL_VERSION_NUMBER */
#endif /* CONFIG_SUITEB */


static int tls_set_conn_flags(struct tls_connection *conn, unsigned int flags,
			      const char *openssl_ciphers)
{
	SSL *ssl = conn->ssl;

#ifdef SSL_OP_NO_TICKET
	if (flags & TLS_CONN_DISABLE_SESSION_TICKET)
		SSL_set_options(ssl, SSL_OP_NO_TICKET);
	else
		SSL_clear_options(ssl, SSL_OP_NO_TICKET);
#endif /* SSL_OP_NO_TICKET */

#ifdef SSL_OP_NO_TLSv1
	if (flags & TLS_CONN_DISABLE_TLSv1_0)
		SSL_set_options(ssl, SSL_OP_NO_TLSv1);
	else
		SSL_clear_options(ssl, SSL_OP_NO_TLSv1);
#endif /* SSL_OP_NO_TLSv1 */
#ifdef SSL_OP_NO_TLSv1_1
	if (flags & TLS_CONN_DISABLE_TLSv1_1)
		SSL_set_options(ssl, SSL_OP_NO_TLSv1_1);
	else
		SSL_clear_options(ssl, SSL_OP_NO_TLSv1_1);
#endif /* SSL_OP_NO_TLSv1_1 */
#ifdef SSL_OP_NO_TLSv1_2
	if (flags & TLS_CONN_DISABLE_TLSv1_2)
		SSL_set_options(ssl, SSL_OP_NO_TLSv1_2);
	else
		SSL_clear_options(ssl, SSL_OP_NO_TLSv1_2);
#endif /* SSL_OP_NO_TLSv1_2 */
#ifdef SSL_OP_NO_TLSv1_3
	if (flags & TLS_CONN_DISABLE_TLSv1_3)
		SSL_set_options(ssl, SSL_OP_NO_TLSv1_3);
	else
		SSL_clear_options(ssl, SSL_OP_NO_TLSv1_3);
#endif /* SSL_OP_NO_TLSv1_3 */
#ifdef CONFIG_SUITEB
#ifdef OPENSSL_IS_BORINGSSL
	/* Start with defaults from BoringSSL */
	SSL_CTX_set_verify_algorithm_prefs(conn->ssl_ctx, NULL, 0);
#endif /* OPENSSL_IS_BORINGSSL */
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
	if (flags & TLS_CONN_SUITEB_NO_ECDH) {
		const char *ciphers = "DHE-RSA-AES256-GCM-SHA384";

		if (openssl_ciphers) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: Override ciphers for Suite B (no ECDH): %s",
				   openssl_ciphers);
			ciphers = openssl_ciphers;
		}
		if (SSL_set_cipher_list(ssl, ciphers) != 1) {
			wpa_printf(MSG_INFO,
				   "OpenSSL: Failed to set Suite B ciphers");
			return -1;
		}
	} else if (flags & TLS_CONN_SUITEB) {
		EC_KEY *ecdh;
		const char *ciphers =
			"ECDHE-RSA-AES256-GCM-SHA384:DHE-RSA-AES256-GCM-SHA384";
		int nid[1] = { NID_secp384r1 };

		if (openssl_ciphers) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: Override ciphers for Suite B: %s",
				   openssl_ciphers);
			ciphers = openssl_ciphers;
		}
		if (SSL_set_cipher_list(ssl, ciphers) != 1) {
			wpa_printf(MSG_INFO,
				   "OpenSSL: Failed to set Suite B ciphers");
			return -1;
		}

		if (SSL_set1_curves(ssl, nid, 1) != 1) {
			wpa_printf(MSG_INFO,
				   "OpenSSL: Failed to set Suite B curves");
			return -1;
		}

		ecdh = EC_KEY_new_by_curve_name(NID_secp384r1);
		if (!ecdh || SSL_set_tmp_ecdh(ssl, ecdh) != 1) {
			EC_KEY_free(ecdh);
			wpa_printf(MSG_INFO,
				   "OpenSSL: Failed to set ECDH parameter");
			return -1;
		}
		EC_KEY_free(ecdh);
	}
	if (flags & (TLS_CONN_SUITEB | TLS_CONN_SUITEB_NO_ECDH)) {
#ifdef OPENSSL_IS_BORINGSSL
		uint16_t sigalgs[1] = { SSL_SIGN_RSA_PKCS1_SHA384 };

		if (SSL_CTX_set_verify_algorithm_prefs(conn->ssl_ctx, sigalgs,
						       1) != 1) {
			wpa_printf(MSG_INFO,
				   "OpenSSL: Failed to set Suite B sigalgs");
			return -1;
		}
#else /* OPENSSL_IS_BORINGSSL */
		/* ECDSA+SHA384 if need to add EC support here */
		if (SSL_set1_sigalgs_list(ssl, "RSA+SHA384") != 1) {
			wpa_printf(MSG_INFO,
				   "OpenSSL: Failed to set Suite B sigalgs");
			return -1;
		}
#endif /* OPENSSL_IS_BORINGSSL */

		SSL_set_options(ssl, SSL_OP_NO_TLSv1);
		SSL_set_options(ssl, SSL_OP_NO_TLSv1_1);
		SSL_set_cert_cb(ssl, suiteb_cert_cb, conn);
	}
#else /* OPENSSL_VERSION_NUMBER < 0x10002000L */
	if (flags & (TLS_CONN_SUITEB | TLS_CONN_SUITEB_NO_ECDH)) {
		wpa_printf(MSG_ERROR,
			   "OpenSSL: Suite B RSA case not supported with this OpenSSL version");
		return -1;
	}
#endif /* OPENSSL_VERSION_NUMBER */

#ifdef OPENSSL_IS_BORINGSSL
	if (openssl_ciphers && os_strcmp(openssl_ciphers, "SUITEB192") == 0) {
		uint16_t sigalgs[1] = { SSL_SIGN_ECDSA_SECP384R1_SHA384 };
		int nid[1] = { NID_secp384r1 };

		if (SSL_set1_curves(ssl, nid, 1) != 1) {
			wpa_printf(MSG_INFO,
				   "OpenSSL: Failed to set Suite B curves");
			return -1;
		}

		if (SSL_CTX_set_verify_algorithm_prefs(conn->ssl_ctx, sigalgs,
						       1) != 1) {
			wpa_printf(MSG_INFO,
				   "OpenSSL: Failed to set Suite B sigalgs");
			return -1;
		}
	}
#endif /* OPENSSL_IS_BORINGSSL */
#endif /* CONFIG_SUITEB */

	return 0;
}


int tls_connection_set_verify(void *ssl_ctx, struct tls_connection *conn,
			      int verify_peer, unsigned int flags,
			      const u8 *session_ctx, size_t session_ctx_len)
{
	static int counter = 0;
	struct tls_data *data = ssl_ctx;

	if (conn == NULL)
		return -1;

	if (verify_peer) {
		conn->ca_cert_verify = 1;
		SSL_set_verify(conn->ssl, SSL_VERIFY_PEER |
			       SSL_VERIFY_FAIL_IF_NO_PEER_CERT |
			       SSL_VERIFY_CLIENT_ONCE, tls_verify_cb);
	} else {
		conn->ca_cert_verify = 0;
		SSL_set_verify(conn->ssl, SSL_VERIFY_NONE, NULL);
	}

	if (tls_set_conn_flags(conn, flags, NULL) < 0)
		return -1;
	conn->flags = flags;

	SSL_set_accept_state(conn->ssl);

	if (data->tls_session_lifetime == 0) {
		/*
		 * Set session id context to a unique value to make sure
		 * session resumption cannot be used either through session
		 * caching or TLS ticket extension.
		 */
		counter++;
		SSL_set_session_id_context(conn->ssl,
					   (const unsigned char *) &counter,
					   sizeof(counter));
	} else if (session_ctx) {
		SSL_set_session_id_context(conn->ssl, session_ctx,
					   session_ctx_len);
	}

	return 0;
}


static int tls_connection_client_cert(struct tls_connection *conn,
				      const char *client_cert,
				      const u8 *client_cert_blob,
				      size_t client_cert_blob_len)
{
	if (client_cert == NULL && client_cert_blob == NULL)
		return 0;

#ifdef PKCS12_FUNCS
#if OPENSSL_VERSION_NUMBER < 0x10002000L || defined(LIBRESSL_VERSION_NUMBER)
	/*
	 * Clear previously set extra chain certificates, if any, from PKCS#12
	 * processing in tls_parse_pkcs12() to allow OpenSSL to build a new
	 * chain properly.
	 */
	SSL_CTX_clear_extra_chain_certs(conn->ssl_ctx);
#endif /* OPENSSL_VERSION_NUMBER < 0x10002000L */
#endif /* PKCS12_FUNCS */

	if (client_cert_blob &&
	    SSL_use_certificate_ASN1(conn->ssl, (u8 *) client_cert_blob,
				     client_cert_blob_len) == 1) {
		wpa_printf(MSG_DEBUG, "OpenSSL: SSL_use_certificate_ASN1 --> "
			   "OK");
		return 0;
	} else if (client_cert_blob) {
		tls_show_errors(MSG_DEBUG, __func__,
				"SSL_use_certificate_ASN1 failed");
	}

	if (client_cert == NULL)
		return -1;

#ifdef ANDROID
	if (os_strncmp("keystore://", client_cert, 11) == 0) {
		BIO *bio = BIO_from_keystore(&client_cert[11]);
		X509 *x509 = NULL;
		int ret = -1;
		if (bio) {
			x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL);
		}
		if (x509) {
			if (SSL_use_certificate(conn->ssl, x509) == 1)
				ret = 0;
			X509_free(x509);
		}

		/* Read additional certificates into the chain. */
		while (bio) {
			x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL);
			if (x509) {
				/* Takes ownership of x509 */
				SSL_add0_chain_cert(conn->ssl, x509);
			} else {
				BIO_free(bio);
				bio = NULL;
			}
		}
		return ret;
	}
#endif /* ANDROID */

#ifndef OPENSSL_NO_STDIO
	if (SSL_use_certificate_file(conn->ssl, client_cert,
				     SSL_FILETYPE_ASN1) == 1) {
		wpa_printf(MSG_DEBUG, "OpenSSL: SSL_use_certificate_file (DER)"
			   " --> OK");
		return 0;
	}

	if (SSL_use_certificate_file(conn->ssl, client_cert,
				     SSL_FILETYPE_PEM) == 1) {
		ERR_clear_error();
		wpa_printf(MSG_DEBUG, "OpenSSL: SSL_use_certificate_file (PEM)"
			   " --> OK");
		return 0;
	}

	tls_show_errors(MSG_DEBUG, __func__,
			"SSL_use_certificate_file failed");
#else /* OPENSSL_NO_STDIO */
	wpa_printf(MSG_DEBUG, "OpenSSL: %s - OPENSSL_NO_STDIO", __func__);
#endif /* OPENSSL_NO_STDIO */

	return -1;
}


static int tls_global_client_cert(struct tls_data *data,
				  const char *client_cert)
{
#ifndef OPENSSL_NO_STDIO
	SSL_CTX *ssl_ctx = data->ssl;

	if (client_cert == NULL)
		return 0;

	if (SSL_CTX_use_certificate_file(ssl_ctx, client_cert,
					 SSL_FILETYPE_ASN1) != 1 &&
	    SSL_CTX_use_certificate_chain_file(ssl_ctx, client_cert) != 1 &&
	    SSL_CTX_use_certificate_file(ssl_ctx, client_cert,
					 SSL_FILETYPE_PEM) != 1) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to load client certificate");
		return -1;
	}
	return 0;
#else /* OPENSSL_NO_STDIO */
	if (client_cert == NULL)
		return 0;
	wpa_printf(MSG_DEBUG, "OpenSSL: %s - OPENSSL_NO_STDIO", __func__);
	return -1;
#endif /* OPENSSL_NO_STDIO */
}


#ifdef PKCS12_FUNCS
static int tls_parse_pkcs12(struct tls_data *data, SSL *ssl, PKCS12 *p12,
			    const char *passwd)
{
	EVP_PKEY *pkey;
	X509 *cert;
	STACK_OF(X509) *certs;
	int res = 0;
	char buf[256];

	pkey = NULL;
	cert = NULL;
	certs = NULL;
	if (!passwd)
		passwd = "";
	if (!PKCS12_parse(p12, passwd, &pkey, &cert, &certs)) {
		tls_show_errors(MSG_DEBUG, __func__,
				"Failed to parse PKCS12 file");
		PKCS12_free(p12);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "TLS: Successfully parsed PKCS12 data");

	if (cert) {
		X509_NAME_oneline(X509_get_subject_name(cert), buf,
				  sizeof(buf));
		wpa_printf(MSG_DEBUG, "TLS: Got certificate from PKCS12: "
			   "subject='%s'", buf);
		if (ssl) {
			if (SSL_use_certificate(ssl, cert) != 1)
				res = -1;
		} else {
			if (SSL_CTX_use_certificate(data->ssl, cert) != 1)
				res = -1;
		}
		X509_free(cert);
	}

	if (pkey) {
		wpa_printf(MSG_DEBUG, "TLS: Got private key from PKCS12");
		if (ssl) {
			if (SSL_use_PrivateKey(ssl, pkey) != 1)
				res = -1;
		} else {
			if (SSL_CTX_use_PrivateKey(data->ssl, pkey) != 1)
				res = -1;
		}
		EVP_PKEY_free(pkey);
	}

	if (certs) {
#if OPENSSL_VERSION_NUMBER >= 0x10002000L && !defined(LIBRESSL_VERSION_NUMBER)
		if (ssl)
			SSL_clear_chain_certs(ssl);
		else
			SSL_CTX_clear_chain_certs(data->ssl);
		while ((cert = sk_X509_pop(certs)) != NULL) {
			X509_NAME_oneline(X509_get_subject_name(cert), buf,
					  sizeof(buf));
			wpa_printf(MSG_DEBUG, "TLS: additional certificate"
				   " from PKCS12: subject='%s'", buf);
			if ((ssl && SSL_add1_chain_cert(ssl, cert) != 1) ||
			    (!ssl && SSL_CTX_add1_chain_cert(data->ssl,
							     cert) != 1)) {
				tls_show_errors(MSG_DEBUG, __func__,
						"Failed to add additional certificate");
				res = -1;
				X509_free(cert);
				break;
			}
			X509_free(cert);
		}
		if (!res) {
			/* Try to continue anyway */
		}
		sk_X509_pop_free(certs, X509_free);
#ifndef OPENSSL_IS_BORINGSSL
		if (ssl)
			res = SSL_build_cert_chain(
				ssl,
				SSL_BUILD_CHAIN_FLAG_CHECK |
				SSL_BUILD_CHAIN_FLAG_IGNORE_ERROR);
		else
			res = SSL_CTX_build_cert_chain(
				data->ssl,
				SSL_BUILD_CHAIN_FLAG_CHECK |
				SSL_BUILD_CHAIN_FLAG_IGNORE_ERROR);
		if (!res) {
			tls_show_errors(MSG_DEBUG, __func__,
					"Failed to build certificate chain");
		} else if (res == 2) {
			wpa_printf(MSG_DEBUG,
				   "TLS: Ignore certificate chain verification error when building chain with PKCS#12 extra certificates");
		}
#endif /* OPENSSL_IS_BORINGSSL */
		/*
		 * Try to continue regardless of result since it is possible for
		 * the extra certificates not to be required.
		 */
		res = 0;
#else /* OPENSSL_VERSION_NUMBER >= 0x10002000L */
		SSL_CTX_clear_extra_chain_certs(data->ssl);
		while ((cert = sk_X509_pop(certs)) != NULL) {
			X509_NAME_oneline(X509_get_subject_name(cert), buf,
					  sizeof(buf));
			wpa_printf(MSG_DEBUG, "TLS: additional certificate"
				   " from PKCS12: subject='%s'", buf);
			/*
			 * There is no SSL equivalent for the chain cert - so
			 * always add it to the context...
			 */
			if (SSL_CTX_add_extra_chain_cert(data->ssl, cert) != 1)
			{
				X509_free(cert);
				res = -1;
				break;
			}
		}
		sk_X509_pop_free(certs, X509_free);
#endif /* OPENSSL_VERSION_NUMBER >= 0x10002000L */
	}

	PKCS12_free(p12);

	if (res < 0)
		tls_get_errors(data);

	return res;
}
#endif  /* PKCS12_FUNCS */


static int tls_read_pkcs12(struct tls_data *data, SSL *ssl,
			   const char *private_key, const char *passwd)
{
#ifdef PKCS12_FUNCS
	FILE *f;
	PKCS12 *p12;

	f = fopen(private_key, "rb");
	if (f == NULL)
		return -1;

	p12 = d2i_PKCS12_fp(f, NULL);
	fclose(f);

	if (p12 == NULL) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to use PKCS#12 file");
		return -1;
	}

	return tls_parse_pkcs12(data, ssl, p12, passwd);

#else /* PKCS12_FUNCS */
	wpa_printf(MSG_INFO, "TLS: PKCS12 support disabled - cannot read "
		   "p12/pfx files");
	return -1;
#endif  /* PKCS12_FUNCS */
}


static int tls_read_pkcs12_blob(struct tls_data *data, SSL *ssl,
				const u8 *blob, size_t len, const char *passwd)
{
#ifdef PKCS12_FUNCS
	PKCS12 *p12;

	p12 = d2i_PKCS12(NULL, (const unsigned char **) &blob, len);
	if (p12 == NULL) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to use PKCS#12 blob");
		return -1;
	}

	return tls_parse_pkcs12(data, ssl, p12, passwd);

#else /* PKCS12_FUNCS */
	wpa_printf(MSG_INFO, "TLS: PKCS12 support disabled - cannot parse "
		   "p12/pfx blobs");
	return -1;
#endif  /* PKCS12_FUNCS */
}


#ifndef OPENSSL_NO_ENGINE
static int tls_engine_get_cert(struct tls_connection *conn,
			       const char *cert_id,
			       X509 **cert)
{
	/* this runs after the private key is loaded so no PIN is required */
	struct {
		const char *cert_id;
		X509 *cert;
	} params;
	params.cert_id = cert_id;
	params.cert = NULL;

	if (!ENGINE_ctrl_cmd(conn->engine, "LOAD_CERT_CTRL",
			     0, &params, NULL, 1)) {
		unsigned long err = ERR_get_error();

		wpa_printf(MSG_ERROR, "ENGINE: cannot load client cert with id"
			   " '%s' [%s]", cert_id,
			   ERR_error_string(err, NULL));
		if (tls_is_pin_error(err))
			return TLS_SET_PARAMS_ENGINE_PRV_BAD_PIN;
		return TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED;
	}
	if (!params.cert) {
		wpa_printf(MSG_ERROR, "ENGINE: did not properly cert with id"
			   " '%s'", cert_id);
		return TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED;
	}
	*cert = params.cert;
	return 0;
}
#endif /* OPENSSL_NO_ENGINE */


static int tls_connection_engine_client_cert(struct tls_connection *conn,
					     const char *cert_id)
{
#ifndef OPENSSL_NO_ENGINE
	X509 *cert;

	if (tls_engine_get_cert(conn, cert_id, &cert))
		return -1;

	if (!SSL_use_certificate(conn->ssl, cert)) {
		tls_show_errors(MSG_ERROR, __func__,
				"SSL_use_certificate failed");
                X509_free(cert);
		return -1;
	}
	X509_free(cert);
	wpa_printf(MSG_DEBUG, "ENGINE: SSL_use_certificate --> "
		   "OK");
	return 0;

#else /* OPENSSL_NO_ENGINE */
	return -1;
#endif /* OPENSSL_NO_ENGINE */
}


static int tls_connection_engine_ca_cert(struct tls_data *data,
					 struct tls_connection *conn,
					 const char *ca_cert_id)
{
#ifndef OPENSSL_NO_ENGINE
	X509 *cert;
	SSL_CTX *ssl_ctx = data->ssl;
	X509_STORE *store;

	if (tls_engine_get_cert(conn, ca_cert_id, &cert))
		return -1;

	/* start off the same as tls_connection_ca_cert */
	store = X509_STORE_new();
	if (store == NULL) {
		wpa_printf(MSG_DEBUG, "OpenSSL: %s - failed to allocate new "
			   "certificate store", __func__);
		X509_free(cert);
		return -1;
	}
	SSL_CTX_set_cert_store(ssl_ctx, store);
	if (!X509_STORE_add_cert(store, cert)) {
		unsigned long err = ERR_peek_error();
		tls_show_errors(MSG_WARNING, __func__,
				"Failed to add CA certificate from engine "
				"to certificate store");
		if (ERR_GET_LIB(err) == ERR_LIB_X509 &&
		    ERR_GET_REASON(err) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
			wpa_printf(MSG_DEBUG, "OpenSSL: %s - ignoring cert"
				   " already in hash table error",
				   __func__);
		} else {
			X509_free(cert);
			return -1;
		}
	}
	X509_free(cert);
	wpa_printf(MSG_DEBUG, "OpenSSL: %s - added CA certificate from engine "
		   "to certificate store", __func__);
	SSL_set_verify(conn->ssl, SSL_VERIFY_PEER, tls_verify_cb);
	conn->ca_cert_verify = 1;

	return 0;

#else /* OPENSSL_NO_ENGINE */
	return -1;
#endif /* OPENSSL_NO_ENGINE */
}


static int tls_connection_engine_private_key(struct tls_connection *conn)
{
#if defined(ANDROID) || !defined(OPENSSL_NO_ENGINE)
	if (SSL_use_PrivateKey(conn->ssl, conn->private_key) != 1) {
		tls_show_errors(MSG_ERROR, __func__,
				"ENGINE: cannot use private key for TLS");
		return -1;
	}
	if (!SSL_check_private_key(conn->ssl)) {
		tls_show_errors(MSG_INFO, __func__,
				"Private key failed verification");
		return -1;
	}
	return 0;
#else /* OPENSSL_NO_ENGINE */
	wpa_printf(MSG_ERROR, "SSL: Configuration uses engine, but "
		   "engine support was not compiled in");
	return -1;
#endif /* OPENSSL_NO_ENGINE */
}


#ifndef OPENSSL_NO_STDIO
static int tls_passwd_cb(char *buf, int size, int rwflag, void *password)
{
	if (!password)
		return 0;
	os_strlcpy(buf, (const char *) password, size);
	return os_strlen(buf);
}
#endif /* OPENSSL_NO_STDIO */


static int tls_use_private_key_file(struct tls_data *data, SSL *ssl,
				    const char *private_key,
				    const char *private_key_passwd)
{
#ifndef OPENSSL_NO_STDIO
	BIO *bio;
	EVP_PKEY *pkey;
	int ret;

	/* First try ASN.1 (DER). */
	bio = BIO_new_file(private_key, "r");
	if (!bio)
		return -1;
	pkey = d2i_PrivateKey_bio(bio, NULL);
	BIO_free(bio);

	if (pkey) {
		wpa_printf(MSG_DEBUG, "OpenSSL: %s (DER) --> loaded", __func__);
	} else {
		/* Try PEM with the provided password. */
		bio = BIO_new_file(private_key, "r");
		if (!bio)
			return -1;
		pkey = PEM_read_bio_PrivateKey(bio, NULL, tls_passwd_cb,
					       (void *) private_key_passwd);
		BIO_free(bio);
		if (!pkey)
			return -1;
		wpa_printf(MSG_DEBUG, "OpenSSL: %s (PEM) --> loaded", __func__);
		/* Clear errors from the previous failed load. */
		ERR_clear_error();
	}

	if (ssl)
		ret = SSL_use_PrivateKey(ssl, pkey);
	else
		ret = SSL_CTX_use_PrivateKey(data->ssl, pkey);

	EVP_PKEY_free(pkey);
	return ret == 1 ? 0 : -1;
#else /* OPENSSL_NO_STDIO */
	wpa_printf(MSG_DEBUG, "OpenSSL: %s - OPENSSL_NO_STDIO", __func__);
	return -1;
#endif /* OPENSSL_NO_STDIO */
}


static int tls_connection_private_key(struct tls_data *data,
				      struct tls_connection *conn,
				      const char *private_key,
				      const char *private_key_passwd,
				      const u8 *private_key_blob,
				      size_t private_key_blob_len)
{
	int ok;

	if (private_key == NULL && private_key_blob == NULL)
		return 0;

	ok = 0;
	while (private_key_blob) {
		if (SSL_use_PrivateKey_ASN1(EVP_PKEY_RSA, conn->ssl,
					    (u8 *) private_key_blob,
					    private_key_blob_len) == 1) {
			wpa_printf(MSG_DEBUG, "OpenSSL: SSL_use_PrivateKey_"
				   "ASN1(EVP_PKEY_RSA) --> OK");
			ok = 1;
			break;
		}

		if (SSL_use_PrivateKey_ASN1(EVP_PKEY_DSA, conn->ssl,
					    (u8 *) private_key_blob,
					    private_key_blob_len) == 1) {
			wpa_printf(MSG_DEBUG, "OpenSSL: SSL_use_PrivateKey_"
				   "ASN1(EVP_PKEY_DSA) --> OK");
			ok = 1;
			break;
		}

		if (SSL_use_RSAPrivateKey_ASN1(conn->ssl,
					       (u8 *) private_key_blob,
					       private_key_blob_len) == 1) {
			wpa_printf(MSG_DEBUG, "OpenSSL: "
				   "SSL_use_RSAPrivateKey_ASN1 --> OK");
			ok = 1;
			break;
		}

		if (tls_read_pkcs12_blob(data, conn->ssl, private_key_blob,
					 private_key_blob_len,
					 private_key_passwd) == 0) {
			wpa_printf(MSG_DEBUG, "OpenSSL: PKCS#12 as blob --> "
				   "OK");
			ok = 1;
			break;
		}

		break;
	}

	while (!ok && private_key) {
		if (tls_use_private_key_file(data, conn->ssl, private_key,
					     private_key_passwd) == 0) {
			ok = 1;
			break;
		}

		if (tls_read_pkcs12(data, conn->ssl, private_key,
				    private_key_passwd) == 0) {
			wpa_printf(MSG_DEBUG, "OpenSSL: Reading PKCS#12 file "
				   "--> OK");
			ok = 1;
			break;
		}

		if (tls_cryptoapi_cert(conn->ssl, private_key) == 0) {
			wpa_printf(MSG_DEBUG, "OpenSSL: Using CryptoAPI to "
				   "access certificate store --> OK");
			ok = 1;
			break;
		}

		break;
	}

	if (!ok) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to load private key");
		return -1;
	}
	ERR_clear_error();

	if (!SSL_check_private_key(conn->ssl)) {
		tls_show_errors(MSG_INFO, __func__, "Private key failed "
				"verification");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "SSL: Private key loaded successfully");
	return 0;
}


static int tls_global_private_key(struct tls_data *data,
				  const char *private_key,
				  const char *private_key_passwd)
{
	SSL_CTX *ssl_ctx = data->ssl;

	if (private_key == NULL)
		return 0;

	if (tls_use_private_key_file(data, NULL, private_key,
				     private_key_passwd) &&
	    tls_read_pkcs12(data, NULL, private_key, private_key_passwd)) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to load private key");
		ERR_clear_error();
		return -1;
	}
	ERR_clear_error();

	if (!SSL_CTX_check_private_key(ssl_ctx)) {
		tls_show_errors(MSG_INFO, __func__,
				"Private key failed verification");
		return -1;
	}

	return 0;
}


static int tls_connection_dh(struct tls_connection *conn, const char *dh_file)
{
#ifdef OPENSSL_NO_DH
	if (dh_file == NULL)
		return 0;
	wpa_printf(MSG_ERROR, "TLS: openssl does not include DH support, but "
		   "dh_file specified");
	return -1;
#else /* OPENSSL_NO_DH */
	DH *dh;
	BIO *bio;

	/* TODO: add support for dh_blob */
	if (dh_file == NULL)
		return 0;
	if (conn == NULL)
		return -1;

	bio = BIO_new_file(dh_file, "r");
	if (bio == NULL) {
		wpa_printf(MSG_INFO, "TLS: Failed to open DH file '%s': %s",
			   dh_file, ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}
	dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
	BIO_free(bio);
#ifndef OPENSSL_NO_DSA
	while (dh == NULL) {
		DSA *dsa;
		wpa_printf(MSG_DEBUG, "TLS: Failed to parse DH file '%s': %s -"
			   " trying to parse as DSA params", dh_file,
			   ERR_error_string(ERR_get_error(), NULL));
		bio = BIO_new_file(dh_file, "r");
		if (bio == NULL)
			break;
		dsa = PEM_read_bio_DSAparams(bio, NULL, NULL, NULL);
		BIO_free(bio);
		if (!dsa) {
			wpa_printf(MSG_DEBUG, "TLS: Failed to parse DSA file "
				   "'%s': %s", dh_file,
				   ERR_error_string(ERR_get_error(), NULL));
			break;
		}

		wpa_printf(MSG_DEBUG, "TLS: DH file in DSA param format");
		dh = DSA_dup_DH(dsa);
		DSA_free(dsa);
		if (dh == NULL) {
			wpa_printf(MSG_INFO, "TLS: Failed to convert DSA "
				   "params into DH params");
			break;
		}
		break;
	}
#endif /* !OPENSSL_NO_DSA */
	if (dh == NULL) {
		wpa_printf(MSG_INFO, "TLS: Failed to read/parse DH/DSA file "
			   "'%s'", dh_file);
		return -1;
	}

	if (SSL_set_tmp_dh(conn->ssl, dh) != 1) {
		wpa_printf(MSG_INFO, "TLS: Failed to set DH params from '%s': "
			   "%s", dh_file,
			   ERR_error_string(ERR_get_error(), NULL));
		DH_free(dh);
		return -1;
	}
	DH_free(dh);
	return 0;
#endif /* OPENSSL_NO_DH */
}


static int tls_global_dh(struct tls_data *data, const char *dh_file)
{
#ifdef OPENSSL_NO_DH
	if (dh_file == NULL)
		return 0;
	wpa_printf(MSG_ERROR, "TLS: openssl does not include DH support, but "
		   "dh_file specified");
	return -1;
#else /* OPENSSL_NO_DH */
	SSL_CTX *ssl_ctx = data->ssl;
	DH *dh;
	BIO *bio;

	/* TODO: add support for dh_blob */
	if (dh_file == NULL)
		return 0;
	if (ssl_ctx == NULL)
		return -1;

	bio = BIO_new_file(dh_file, "r");
	if (bio == NULL) {
		wpa_printf(MSG_INFO, "TLS: Failed to open DH file '%s': %s",
			   dh_file, ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}
	dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
	BIO_free(bio);
#ifndef OPENSSL_NO_DSA
	while (dh == NULL) {
		DSA *dsa;
		wpa_printf(MSG_DEBUG, "TLS: Failed to parse DH file '%s': %s -"
			   " trying to parse as DSA params", dh_file,
			   ERR_error_string(ERR_get_error(), NULL));
		bio = BIO_new_file(dh_file, "r");
		if (bio == NULL)
			break;
		dsa = PEM_read_bio_DSAparams(bio, NULL, NULL, NULL);
		BIO_free(bio);
		if (!dsa) {
			wpa_printf(MSG_DEBUG, "TLS: Failed to parse DSA file "
				   "'%s': %s", dh_file,
				   ERR_error_string(ERR_get_error(), NULL));
			break;
		}

		wpa_printf(MSG_DEBUG, "TLS: DH file in DSA param format");
		dh = DSA_dup_DH(dsa);
		DSA_free(dsa);
		if (dh == NULL) {
			wpa_printf(MSG_INFO, "TLS: Failed to convert DSA "
				   "params into DH params");
			break;
		}
		break;
	}
#endif /* !OPENSSL_NO_DSA */
	if (dh == NULL) {
		wpa_printf(MSG_INFO, "TLS: Failed to read/parse DH/DSA file "
			   "'%s'", dh_file);
		return -1;
	}

	if (SSL_CTX_set_tmp_dh(ssl_ctx, dh) != 1) {
		wpa_printf(MSG_INFO, "TLS: Failed to set DH params from '%s': "
			   "%s", dh_file,
			   ERR_error_string(ERR_get_error(), NULL));
		DH_free(dh);
		return -1;
	}
	DH_free(dh);
	return 0;
#endif /* OPENSSL_NO_DH */
}


int tls_connection_get_random(void *ssl_ctx, struct tls_connection *conn,
			      struct tls_random *keys)
{
	SSL *ssl;

	if (conn == NULL || keys == NULL)
		return -1;
	ssl = conn->ssl;
	if (ssl == NULL)
		return -1;

	os_memset(keys, 0, sizeof(*keys));
	keys->client_random = conn->client_random;
	keys->client_random_len = SSL_get_client_random(
		ssl, conn->client_random, sizeof(conn->client_random));
	keys->server_random = conn->server_random;
	keys->server_random_len = SSL_get_server_random(
		ssl, conn->server_random, sizeof(conn->server_random));

	return 0;
}


#ifdef OPENSSL_NEED_EAP_FAST_PRF
static int openssl_get_keyblock_size(SSL *ssl)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L || \
	(defined(LIBRESSL_VERSION_NUMBER) && \
	 LIBRESSL_VERSION_NUMBER < 0x20700000L)
	const EVP_CIPHER *c;
	const EVP_MD *h;
	int md_size;

	if (ssl->enc_read_ctx == NULL || ssl->enc_read_ctx->cipher == NULL ||
	    ssl->read_hash == NULL)
		return -1;

	c = ssl->enc_read_ctx->cipher;
	h = EVP_MD_CTX_md(ssl->read_hash);
	if (h)
		md_size = EVP_MD_size(h);
	else if (ssl->s3)
		md_size = ssl->s3->tmp.new_mac_secret_size;
	else
		return -1;

	wpa_printf(MSG_DEBUG, "OpenSSL: keyblock size: key_len=%d MD_size=%d "
		   "IV_len=%d", EVP_CIPHER_key_length(c), md_size,
		   EVP_CIPHER_iv_length(c));
	return 2 * (EVP_CIPHER_key_length(c) +
		    md_size +
		    EVP_CIPHER_iv_length(c));
#else
	const SSL_CIPHER *ssl_cipher;
	int cipher, digest;
	const EVP_CIPHER *c;
	const EVP_MD *h;

	ssl_cipher = SSL_get_current_cipher(ssl);
	if (!ssl_cipher)
		return -1;
	cipher = SSL_CIPHER_get_cipher_nid(ssl_cipher);
	digest = SSL_CIPHER_get_digest_nid(ssl_cipher);
	wpa_printf(MSG_DEBUG, "OpenSSL: cipher nid %d digest nid %d",
		   cipher, digest);
	if (cipher < 0 || digest < 0)
		return -1;
	c = EVP_get_cipherbynid(cipher);
	h = EVP_get_digestbynid(digest);
	if (!c || !h)
		return -1;

	wpa_printf(MSG_DEBUG,
		   "OpenSSL: keyblock size: key_len=%d MD_size=%d IV_len=%d",
		   EVP_CIPHER_key_length(c), EVP_MD_size(h),
		   EVP_CIPHER_iv_length(c));
	return 2 * (EVP_CIPHER_key_length(c) + EVP_MD_size(h) +
		    EVP_CIPHER_iv_length(c));
#endif
}
#endif /* OPENSSL_NEED_EAP_FAST_PRF */


int tls_connection_export_key(void *tls_ctx, struct tls_connection *conn,
			      const char *label, u8 *out, size_t out_len)
{
	if (!conn ||
	    SSL_export_keying_material(conn->ssl, out, out_len, label,
				       os_strlen(label), NULL, 0, 0) != 1)
		return -1;
	return 0;
}


int tls_connection_get_eap_fast_key(void *tls_ctx, struct tls_connection *conn,
				    u8 *out, size_t out_len)
{
#ifdef OPENSSL_NEED_EAP_FAST_PRF
	SSL *ssl;
	SSL_SESSION *sess;
	u8 *rnd;
	int ret = -1;
	int skip = 0;
	u8 *tmp_out = NULL;
	u8 *_out = out;
	unsigned char client_random[SSL3_RANDOM_SIZE];
	unsigned char server_random[SSL3_RANDOM_SIZE];
	unsigned char master_key[64];
	size_t master_key_len;
	const char *ver;

	/*
	 * TLS library did not support EAP-FAST key generation, so get the
	 * needed TLS session parameters and use an internal implementation of
	 * TLS PRF to derive the key.
	 */

	if (conn == NULL)
		return -1;
	ssl = conn->ssl;
	if (ssl == NULL)
		return -1;
	ver = SSL_get_version(ssl);
	sess = SSL_get_session(ssl);
	if (!ver || !sess)
		return -1;

	skip = openssl_get_keyblock_size(ssl);
	if (skip < 0)
		return -1;
	tmp_out = os_malloc(skip + out_len);
	if (!tmp_out)
		return -1;
	_out = tmp_out;

	rnd = os_malloc(2 * SSL3_RANDOM_SIZE);
	if (!rnd) {
		os_free(tmp_out);
		return -1;
	}

	SSL_get_client_random(ssl, client_random, sizeof(client_random));
	SSL_get_server_random(ssl, server_random, sizeof(server_random));
	master_key_len = SSL_SESSION_get_master_key(sess, master_key,
						    sizeof(master_key));

	os_memcpy(rnd, server_random, SSL3_RANDOM_SIZE);
	os_memcpy(rnd + SSL3_RANDOM_SIZE, client_random, SSL3_RANDOM_SIZE);

	if (os_strcmp(ver, "TLSv1.2") == 0) {
		tls_prf_sha256(master_key, master_key_len,
			       "key expansion", rnd, 2 * SSL3_RANDOM_SIZE,
			       _out, skip + out_len);
		ret = 0;
	} else if (tls_prf_sha1_md5(master_key, master_key_len,
				    "key expansion", rnd, 2 * SSL3_RANDOM_SIZE,
				    _out, skip + out_len) == 0) {
		ret = 0;
	}
	os_memset(master_key, 0, sizeof(master_key));
	os_free(rnd);
	if (ret == 0)
		os_memcpy(out, _out + skip, out_len);
	bin_clear_free(tmp_out, skip);

	return ret;
#else /* OPENSSL_NEED_EAP_FAST_PRF */
	wpa_printf(MSG_ERROR,
		   "OpenSSL: EAP-FAST keys cannot be exported in FIPS mode");
	return -1;
#endif /* OPENSSL_NEED_EAP_FAST_PRF */
}


static struct wpabuf *
openssl_handshake(struct tls_connection *conn, const struct wpabuf *in_data)
{
	int res;
	struct wpabuf *out_data;

	/*
	 * Give TLS handshake data from the server (if available) to OpenSSL
	 * for processing.
	 */
	if (in_data && wpabuf_len(in_data) > 0 &&
	    BIO_write(conn->ssl_in, wpabuf_head(in_data), wpabuf_len(in_data))
	    < 0) {
		tls_show_errors(MSG_INFO, __func__,
				"Handshake failed - BIO_write");
		return NULL;
	}

	/* Initiate TLS handshake or continue the existing handshake */
	if (conn->server)
		res = SSL_accept(conn->ssl);
	else
		res = SSL_connect(conn->ssl);
	if (res != 1) {
		int err = SSL_get_error(conn->ssl, res);
		if (err == SSL_ERROR_WANT_READ)
			wpa_printf(MSG_DEBUG, "SSL: SSL_connect - want "
				   "more data");
		else if (err == SSL_ERROR_WANT_WRITE)
			wpa_printf(MSG_DEBUG, "SSL: SSL_connect - want to "
				   "write");
		else {
			tls_show_errors(MSG_INFO, __func__, "SSL_connect");
			conn->failed++;
			if (!conn->server && !conn->client_hello_generated) {
				/* The server would not understand TLS Alert
				 * before ClientHello, so simply terminate
				 * handshake on this type of error case caused
				 * by a likely internal error like no ciphers
				 * available. */
				wpa_printf(MSG_DEBUG,
					   "OpenSSL: Could not generate ClientHello");
				conn->write_alerts++;
				return NULL;
			}
		}
	}

	if (!conn->server && !conn->failed)
		conn->client_hello_generated = 1;

#ifdef CONFIG_SUITEB
	if ((conn->flags & TLS_CONN_SUITEB) && !conn->server &&
	    os_strncmp(SSL_get_cipher(conn->ssl), "DHE-", 4) == 0 &&
	    conn->server_dh_prime_len < 3072) {
		struct tls_context *context = conn->context;

		/*
		 * This should not be reached since earlier cert_cb should have
		 * terminated the handshake. Keep this check here for extra
		 * protection if anything goes wrong with the more low-level
		 * checks based on having to parse the TLS handshake messages.
		 */
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Server DH prime length: %d bits",
			   conn->server_dh_prime_len);

		if (context->event_cb) {
			union tls_event_data ev;

			os_memset(&ev, 0, sizeof(ev));
			ev.alert.is_local = 1;
			ev.alert.type = "fatal";
			ev.alert.description = "insufficient security";
			context->event_cb(context->cb_ctx, TLS_ALERT, &ev);
		}
		/*
		 * Could send a TLS Alert to the server, but for now, simply
		 * terminate handshake.
		 */
		conn->failed++;
		conn->write_alerts++;
		return NULL;
	}
#endif /* CONFIG_SUITEB */

	/* Get the TLS handshake data to be sent to the server */
	res = BIO_ctrl_pending(conn->ssl_out);
	wpa_printf(MSG_DEBUG, "SSL: %d bytes pending from ssl_out", res);
	out_data = wpabuf_alloc(res);
	if (out_data == NULL) {
		wpa_printf(MSG_DEBUG, "SSL: Failed to allocate memory for "
			   "handshake output (%d bytes)", res);
		if (BIO_reset(conn->ssl_out) < 0) {
			tls_show_errors(MSG_INFO, __func__,
					"BIO_reset failed");
		}
		return NULL;
	}
	res = res == 0 ? 0 : BIO_read(conn->ssl_out, wpabuf_mhead(out_data),
				      res);
	if (res < 0) {
		tls_show_errors(MSG_INFO, __func__,
				"Handshake failed - BIO_read");
		if (BIO_reset(conn->ssl_out) < 0) {
			tls_show_errors(MSG_INFO, __func__,
					"BIO_reset failed");
		}
		wpabuf_free(out_data);
		return NULL;
	}
	wpabuf_put(out_data, res);

	return out_data;
}


static struct wpabuf *
openssl_get_appl_data(struct tls_connection *conn, size_t max_len)
{
	struct wpabuf *appl_data;
	int res;

	appl_data = wpabuf_alloc(max_len + 100);
	if (appl_data == NULL)
		return NULL;

	res = SSL_read(conn->ssl, wpabuf_mhead(appl_data),
		       wpabuf_size(appl_data));
	if (res < 0) {
		int err = SSL_get_error(conn->ssl, res);
		if (err == SSL_ERROR_WANT_READ ||
		    err == SSL_ERROR_WANT_WRITE) {
			wpa_printf(MSG_DEBUG, "SSL: No Application Data "
				   "included");
		} else {
			tls_show_errors(MSG_INFO, __func__,
					"Failed to read possible "
					"Application Data");
		}
		wpabuf_free(appl_data);
		return NULL;
	}

	wpabuf_put(appl_data, res);
	wpa_hexdump_buf_key(MSG_MSGDUMP, "SSL: Application Data in Finished "
			    "message", appl_data);

	return appl_data;
}


static struct wpabuf *
openssl_connection_handshake(struct tls_connection *conn,
			     const struct wpabuf *in_data,
			     struct wpabuf **appl_data)
{
	struct wpabuf *out_data;

	if (appl_data)
		*appl_data = NULL;

	out_data = openssl_handshake(conn, in_data);
	if (out_data == NULL)
		return NULL;
	if (conn->invalid_hb_used) {
		wpa_printf(MSG_INFO, "TLS: Heartbeat attack detected - do not send response");
		wpabuf_free(out_data);
		return NULL;
	}

	if (SSL_is_init_finished(conn->ssl)) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Handshake finished - resumed=%d",
			   tls_connection_resumed(conn->ssl_ctx, conn));
		if (appl_data && in_data)
			*appl_data = openssl_get_appl_data(conn,
							   wpabuf_len(in_data));
	}

	if (conn->invalid_hb_used) {
		wpa_printf(MSG_INFO, "TLS: Heartbeat attack detected - do not send response");
		if (appl_data) {
			wpabuf_free(*appl_data);
			*appl_data = NULL;
		}
		wpabuf_free(out_data);
		return NULL;
	}

	return out_data;
}


struct wpabuf *
tls_connection_handshake(void *ssl_ctx, struct tls_connection *conn,
			 const struct wpabuf *in_data,
			 struct wpabuf **appl_data)
{
	return openssl_connection_handshake(conn, in_data, appl_data);
}


struct wpabuf * tls_connection_server_handshake(void *tls_ctx,
						struct tls_connection *conn,
						const struct wpabuf *in_data,
						struct wpabuf **appl_data)
{
	conn->server = 1;
	return openssl_connection_handshake(conn, in_data, appl_data);
}


struct wpabuf * tls_connection_encrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	int res;
	struct wpabuf *buf;

	if (conn == NULL)
		return NULL;

	/* Give plaintext data for OpenSSL to encrypt into the TLS tunnel. */
	if ((res = BIO_reset(conn->ssl_in)) < 0 ||
	    (res = BIO_reset(conn->ssl_out)) < 0) {
		tls_show_errors(MSG_INFO, __func__, "BIO_reset failed");
		return NULL;
	}
	res = SSL_write(conn->ssl, wpabuf_head(in_data), wpabuf_len(in_data));
	if (res < 0) {
		tls_show_errors(MSG_INFO, __func__,
				"Encryption failed - SSL_write");
		return NULL;
	}

	/* Read encrypted data to be sent to the server */
	buf = wpabuf_alloc(wpabuf_len(in_data) + 300);
	if (buf == NULL)
		return NULL;
	res = BIO_read(conn->ssl_out, wpabuf_mhead(buf), wpabuf_size(buf));
	if (res < 0) {
		tls_show_errors(MSG_INFO, __func__,
				"Encryption failed - BIO_read");
		wpabuf_free(buf);
		return NULL;
	}
	wpabuf_put(buf, res);

	return buf;
}


struct wpabuf * tls_connection_decrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data)
{
	int res;
	struct wpabuf *buf;

	/* Give encrypted data from TLS tunnel for OpenSSL to decrypt. */
	res = BIO_write(conn->ssl_in, wpabuf_head(in_data),
			wpabuf_len(in_data));
	if (res < 0) {
		tls_show_errors(MSG_INFO, __func__,
				"Decryption failed - BIO_write");
		return NULL;
	}
	if (BIO_reset(conn->ssl_out) < 0) {
		tls_show_errors(MSG_INFO, __func__, "BIO_reset failed");
		return NULL;
	}

	/* Read decrypted data for further processing */
	/*
	 * Even though we try to disable TLS compression, it is possible that
	 * this cannot be done with all TLS libraries. Add extra buffer space
	 * to handle the possibility of the decrypted data being longer than
	 * input data.
	 */
	buf = wpabuf_alloc((wpabuf_len(in_data) + 500) * 3);
	if (buf == NULL)
		return NULL;
	res = SSL_read(conn->ssl, wpabuf_mhead(buf), wpabuf_size(buf));
	if (res < 0) {
		tls_show_errors(MSG_INFO, __func__,
				"Decryption failed - SSL_read");
		wpabuf_free(buf);
		return NULL;
	}
	wpabuf_put(buf, res);

	if (conn->invalid_hb_used) {
		wpa_printf(MSG_INFO, "TLS: Heartbeat attack detected - do not send response");
		wpabuf_free(buf);
		return NULL;
	}

	return buf;
}


int tls_connection_resumed(void *ssl_ctx, struct tls_connection *conn)
{
	return conn ? SSL_session_reused(conn->ssl) : 0;
}


int tls_connection_set_cipher_list(void *tls_ctx, struct tls_connection *conn,
				   u8 *ciphers)
{
	char buf[500], *pos, *end;
	u8 *c;
	int ret;

	if (conn == NULL || conn->ssl == NULL || ciphers == NULL)
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
			wpa_printf(MSG_DEBUG, "TLS: Unsupported "
				   "cipher selection: %d", *c);
			return -1;
		}
		ret = os_snprintf(pos, end - pos, ":%s", suite);
		if (os_snprintf_error(end - pos, ret))
			break;
		pos += ret;

		c++;
	}

	wpa_printf(MSG_DEBUG, "OpenSSL: cipher suites: %s", buf + 1);

#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST)
	if (os_strstr(buf, ":ADH-")) {
		/*
		 * Need to drop to security level 0 to allow anonymous
		 * cipher suites for EAP-FAST.
		 */
		SSL_set_security_level(conn->ssl, 0);
	} else if (SSL_get_security_level(conn->ssl) == 0) {
		/* Force at least security level 1 */
		SSL_set_security_level(conn->ssl, 1);
	}
#endif /* EAP_FAST || EAP_FAST_DYNAMIC || EAP_SERVER_FAST */
#endif

	if (SSL_set_cipher_list(conn->ssl, buf + 1) != 1) {
		tls_show_errors(MSG_INFO, __func__,
				"Cipher suite configuration failed");
		return -1;
	}

	return 0;
}


int tls_get_version(void *ssl_ctx, struct tls_connection *conn,
		    char *buf, size_t buflen)
{
	const char *name;
	if (conn == NULL || conn->ssl == NULL)
		return -1;

	name = SSL_get_version(conn->ssl);
	if (name == NULL)
		return -1;

	os_strlcpy(buf, name, buflen);
	return 0;
}


int tls_get_cipher(void *ssl_ctx, struct tls_connection *conn,
		   char *buf, size_t buflen)
{
	const char *name;
	if (conn == NULL || conn->ssl == NULL)
		return -1;

	name = SSL_get_cipher(conn->ssl);
	if (name == NULL)
		return -1;

	os_strlcpy(buf, name, buflen);
	return 0;
}


int tls_connection_enable_workaround(void *ssl_ctx,
				     struct tls_connection *conn)
{
	SSL_set_options(conn->ssl, SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS);

	return 0;
}


#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST)
/* ClientHello TLS extensions require a patch to openssl, so this function is
 * commented out unless explicitly needed for EAP-FAST in order to be able to
 * build this file with unmodified openssl. */
int tls_connection_client_hello_ext(void *ssl_ctx, struct tls_connection *conn,
				    int ext_type, const u8 *data,
				    size_t data_len)
{
	if (conn == NULL || conn->ssl == NULL || ext_type != 35)
		return -1;

	if (SSL_set_session_ticket_ext(conn->ssl, (void *) data,
				       data_len) != 1)
		return -1;

	return 0;
}
#endif /* EAP_FAST || EAP_FAST_DYNAMIC || EAP_SERVER_FAST */


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


#ifdef HAVE_OCSP

static void ocsp_debug_print_resp(OCSP_RESPONSE *rsp)
{
#ifndef CONFIG_NO_STDOUT_DEBUG
	BIO *out;
	size_t rlen;
	char *txt;
	int res;

	if (wpa_debug_level > MSG_DEBUG)
		return;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return;

	OCSP_RESPONSE_print(out, rsp, 0);
	rlen = BIO_ctrl_pending(out);
	txt = os_malloc(rlen + 1);
	if (!txt) {
		BIO_free(out);
		return;
	}

	res = BIO_read(out, txt, rlen);
	if (res > 0) {
		txt[res] = '\0';
		wpa_printf(MSG_DEBUG, "OpenSSL: OCSP Response\n%s", txt);
	}
	os_free(txt);
	BIO_free(out);
#endif /* CONFIG_NO_STDOUT_DEBUG */
}


static void debug_print_cert(X509 *cert, const char *title)
{
#ifndef CONFIG_NO_STDOUT_DEBUG
	BIO *out;
	size_t rlen;
	char *txt;
	int res;

	if (wpa_debug_level > MSG_DEBUG)
		return;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return;

	X509_print(out, cert);
	rlen = BIO_ctrl_pending(out);
	txt = os_malloc(rlen + 1);
	if (!txt) {
		BIO_free(out);
		return;
	}

	res = BIO_read(out, txt, rlen);
	if (res > 0) {
		txt[res] = '\0';
		wpa_printf(MSG_DEBUG, "OpenSSL: %s\n%s", title, txt);
	}
	os_free(txt);

	BIO_free(out);
#endif /* CONFIG_NO_STDOUT_DEBUG */
}


static int ocsp_resp_cb(SSL *s, void *arg)
{
	struct tls_connection *conn = arg;
	const unsigned char *p;
	int len, status, reason, res;
	OCSP_RESPONSE *rsp;
	OCSP_BASICRESP *basic;
	OCSP_CERTID *id;
	ASN1_GENERALIZEDTIME *produced_at, *this_update, *next_update;
	X509_STORE *store;
	STACK_OF(X509) *certs = NULL;

	len = SSL_get_tlsext_status_ocsp_resp(s, &p);
	if (!p) {
		wpa_printf(MSG_DEBUG, "OpenSSL: No OCSP response received");
		return (conn->flags & TLS_CONN_REQUIRE_OCSP) ? 0 : 1;
	}

	wpa_hexdump(MSG_DEBUG, "OpenSSL: OCSP response", p, len);

	rsp = d2i_OCSP_RESPONSE(NULL, &p, len);
	if (!rsp) {
		wpa_printf(MSG_INFO, "OpenSSL: Failed to parse OCSP response");
		return 0;
	}

	ocsp_debug_print_resp(rsp);

	status = OCSP_response_status(rsp);
	if (status != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
		wpa_printf(MSG_INFO, "OpenSSL: OCSP responder error %d (%s)",
			   status, OCSP_response_status_str(status));
		return 0;
	}

	basic = OCSP_response_get1_basic(rsp);
	if (!basic) {
		wpa_printf(MSG_INFO, "OpenSSL: Could not find BasicOCSPResponse");
		return 0;
	}

	store = SSL_CTX_get_cert_store(conn->ssl_ctx);
	if (conn->peer_issuer) {
		debug_print_cert(conn->peer_issuer, "Add OCSP issuer");

		if (X509_STORE_add_cert(store, conn->peer_issuer) != 1) {
			tls_show_errors(MSG_INFO, __func__,
					"OpenSSL: Could not add issuer to certificate store");
		}
		certs = sk_X509_new_null();
		if (certs) {
			X509 *cert;
			cert = X509_dup(conn->peer_issuer);
			if (cert && !sk_X509_push(certs, cert)) {
				tls_show_errors(
					MSG_INFO, __func__,
					"OpenSSL: Could not add issuer to OCSP responder trust store");
				X509_free(cert);
				sk_X509_free(certs);
				certs = NULL;
			}
			if (certs && conn->peer_issuer_issuer) {
				cert = X509_dup(conn->peer_issuer_issuer);
				if (cert && !sk_X509_push(certs, cert)) {
					tls_show_errors(
						MSG_INFO, __func__,
						"OpenSSL: Could not add issuer's issuer to OCSP responder trust store");
					X509_free(cert);
				}
			}
		}
	}

	status = OCSP_basic_verify(basic, certs, store, OCSP_TRUSTOTHER);
	sk_X509_pop_free(certs, X509_free);
	if (status <= 0) {
		tls_show_errors(MSG_INFO, __func__,
				"OpenSSL: OCSP response failed verification");
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		return 0;
	}

	wpa_printf(MSG_DEBUG, "OpenSSL: OCSP response verification succeeded");

	if (!conn->peer_cert) {
		wpa_printf(MSG_DEBUG, "OpenSSL: Peer certificate not available for OCSP status check");
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		return 0;
	}

	if (!conn->peer_issuer) {
		wpa_printf(MSG_DEBUG, "OpenSSL: Peer issuer certificate not available for OCSP status check");
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		return 0;
	}

	id = OCSP_cert_to_id(EVP_sha256(), conn->peer_cert, conn->peer_issuer);
	if (!id) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Could not create OCSP certificate identifier (SHA256)");
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		return 0;
	}

	res = OCSP_resp_find_status(basic, id, &status, &reason, &produced_at,
				    &this_update, &next_update);
	if (!res) {
		id = OCSP_cert_to_id(NULL, conn->peer_cert, conn->peer_issuer);
		if (!id) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: Could not create OCSP certificate identifier (SHA1)");
			OCSP_BASICRESP_free(basic);
			OCSP_RESPONSE_free(rsp);
			return 0;
		}

		res = OCSP_resp_find_status(basic, id, &status, &reason,
					    &produced_at, &this_update,
					    &next_update);
	}

	if (!res) {
		wpa_printf(MSG_INFO, "OpenSSL: Could not find current server certificate from OCSP response%s",
			   (conn->flags & TLS_CONN_REQUIRE_OCSP) ? "" :
			   " (OCSP not required)");
		OCSP_CERTID_free(id);
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		return (conn->flags & TLS_CONN_REQUIRE_OCSP) ? 0 : 1;
	}
	OCSP_CERTID_free(id);

	if (!OCSP_check_validity(this_update, next_update, 5 * 60, -1)) {
		tls_show_errors(MSG_INFO, __func__,
				"OpenSSL: OCSP status times invalid");
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		return 0;
	}

	OCSP_BASICRESP_free(basic);
	OCSP_RESPONSE_free(rsp);

	wpa_printf(MSG_DEBUG, "OpenSSL: OCSP status for server certificate: %s",
		   OCSP_cert_status_str(status));

	if (status == V_OCSP_CERTSTATUS_GOOD)
		return 1;
	if (status == V_OCSP_CERTSTATUS_REVOKED)
		return 0;
	if (conn->flags & TLS_CONN_REQUIRE_OCSP) {
		wpa_printf(MSG_DEBUG, "OpenSSL: OCSP status unknown, but OCSP required");
		return 0;
	}
	wpa_printf(MSG_DEBUG, "OpenSSL: OCSP status unknown, but OCSP was not required, so allow connection to continue");
	return 1;
}


static int ocsp_status_cb(SSL *s, void *arg)
{
	char *tmp;
	char *resp;
	size_t len;

	if (tls_global->ocsp_stapling_response == NULL) {
		wpa_printf(MSG_DEBUG, "OpenSSL: OCSP status callback - no response configured");
		return SSL_TLSEXT_ERR_OK;
	}

	resp = os_readfile(tls_global->ocsp_stapling_response, &len);
	if (resp == NULL) {
		wpa_printf(MSG_DEBUG, "OpenSSL: OCSP status callback - could not read response file");
		/* TODO: Build OCSPResponse with responseStatus = internalError
		 */
		return SSL_TLSEXT_ERR_OK;
	}
	wpa_printf(MSG_DEBUG, "OpenSSL: OCSP status callback - send cached response");
	tmp = OPENSSL_malloc(len);
	if (tmp == NULL) {
		os_free(resp);
		return SSL_TLSEXT_ERR_ALERT_FATAL;
	}

	os_memcpy(tmp, resp, len);
	os_free(resp);
	SSL_set_tlsext_status_ocsp_resp(s, tmp, len);

	return SSL_TLSEXT_ERR_OK;
}

#endif /* HAVE_OCSP */


int tls_connection_set_params(void *tls_ctx, struct tls_connection *conn,
			      const struct tls_connection_params *params)
{
	struct tls_data *data = tls_ctx;
	int ret;
	unsigned long err;
	int can_pkcs11 = 0;
	const char *key_id = params->key_id;
	const char *cert_id = params->cert_id;
	const char *ca_cert_id = params->ca_cert_id;
	const char *engine_id = params->engine ? params->engine_id : NULL;
	const char *ciphers;

	if (conn == NULL)
		return -1;

	if (params->flags & TLS_CONN_REQUIRE_OCSP_ALL) {
		wpa_printf(MSG_INFO,
			   "OpenSSL: ocsp=3 not supported");
		return -1;
	}

	/*
	 * If the engine isn't explicitly configured, and any of the
	 * cert/key fields are actually PKCS#11 URIs, then automatically
	 * use the PKCS#11 ENGINE.
	 */
	if (!engine_id || os_strcmp(engine_id, "pkcs11") == 0)
		can_pkcs11 = 1;

	if (!key_id && params->private_key && can_pkcs11 &&
	    os_strncmp(params->private_key, "pkcs11:", 7) == 0) {
		can_pkcs11 = 2;
		key_id = params->private_key;
	}

	if (!cert_id && params->client_cert && can_pkcs11 &&
	    os_strncmp(params->client_cert, "pkcs11:", 7) == 0) {
		can_pkcs11 = 2;
		cert_id = params->client_cert;
	}

	if (!ca_cert_id && params->ca_cert && can_pkcs11 &&
	    os_strncmp(params->ca_cert, "pkcs11:", 7) == 0) {
		can_pkcs11 = 2;
		ca_cert_id = params->ca_cert;
	}

	/* If we need to automatically enable the PKCS#11 ENGINE, do so. */
	if (can_pkcs11 == 2 && !engine_id)
		engine_id = "pkcs11";

#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST)
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	if (params->flags & TLS_CONN_EAP_FAST) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Use TLSv1_method() for EAP-FAST");
		if (SSL_set_ssl_method(conn->ssl, TLSv1_method()) != 1) {
			tls_show_errors(MSG_INFO, __func__,
					"Failed to set TLSv1_method() for EAP-FAST");
			return -1;
		}
	}
#endif
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
#ifdef SSL_OP_NO_TLSv1_3
	if (params->flags & TLS_CONN_EAP_FAST) {
		/* Need to disable TLS v1.3 at least for now since OpenSSL 1.1.1
		 * refuses to start the handshake with the modified ciphersuite
		 * list (no TLS v1.3 ciphersuites included) for EAP-FAST. */
		wpa_printf(MSG_DEBUG, "OpenSSL: Disable TLSv1.3 for EAP-FAST");
		SSL_set_options(conn->ssl, SSL_OP_NO_TLSv1_3);
	}
#endif /* SSL_OP_NO_TLSv1_3 */
#endif
#endif /* EAP_FAST || EAP_FAST_DYNAMIC || EAP_SERVER_FAST */

	while ((err = ERR_get_error())) {
		wpa_printf(MSG_INFO, "%s: Clearing pending SSL error: %s",
			   __func__, ERR_error_string(err, NULL));
	}

	if (engine_id) {
		wpa_printf(MSG_DEBUG, "SSL: Initializing TLS engine");
		ret = tls_engine_init(conn, engine_id, params->pin,
				      key_id, cert_id, ca_cert_id);
		if (ret)
			return ret;
	}
	if (tls_connection_set_subject_match(conn,
					     params->subject_match,
					     params->altsubject_match,
					     params->suffix_match,
					     params->domain_match))
		return -1;

	if (engine_id && ca_cert_id) {
		if (tls_connection_engine_ca_cert(data, conn, ca_cert_id))
			return TLS_SET_PARAMS_ENGINE_PRV_VERIFY_FAILED;
	} else if (tls_connection_ca_cert(data, conn, params->ca_cert,
					  params->ca_cert_blob,
					  params->ca_cert_blob_len,
					  params->ca_path))
		return -1;

	if (engine_id && cert_id) {
		if (tls_connection_engine_client_cert(conn, cert_id))
			return TLS_SET_PARAMS_ENGINE_PRV_VERIFY_FAILED;
	} else if (tls_connection_client_cert(conn, params->client_cert,
					      params->client_cert_blob,
					      params->client_cert_blob_len))
		return -1;

	if (engine_id && key_id) {
		wpa_printf(MSG_DEBUG, "TLS: Using private key from engine");
		if (tls_connection_engine_private_key(conn))
			return TLS_SET_PARAMS_ENGINE_PRV_VERIFY_FAILED;
	} else if (tls_connection_private_key(data, conn,
					      params->private_key,
					      params->private_key_passwd,
					      params->private_key_blob,
					      params->private_key_blob_len)) {
		wpa_printf(MSG_INFO, "TLS: Failed to load private key '%s'",
			   params->private_key);
		return -1;
	}

	if (tls_connection_dh(conn, params->dh_file)) {
		wpa_printf(MSG_INFO, "TLS: Failed to load DH file '%s'",
			   params->dh_file);
		return -1;
	}

	ciphers = params->openssl_ciphers;
#ifdef CONFIG_SUITEB
#ifdef OPENSSL_IS_BORINGSSL
	if (ciphers && os_strcmp(ciphers, "SUITEB192") == 0) {
		/* BoringSSL removed support for SUITEB192, so need to handle
		 * this with hardcoded ciphersuite and additional checks for
		 * other parameters. */
		ciphers = "ECDHE-ECDSA-AES256-GCM-SHA384";
	}
#endif /* OPENSSL_IS_BORINGSSL */
#endif /* CONFIG_SUITEB */
	if (ciphers && SSL_set_cipher_list(conn->ssl, ciphers) != 1) {
		wpa_printf(MSG_INFO,
			   "OpenSSL: Failed to set cipher string '%s'",
			   ciphers);
		return -1;
	}

	if (tls_set_conn_flags(conn, params->flags,
			       params->openssl_ciphers) < 0)
		return -1;

#ifdef OPENSSL_IS_BORINGSSL
	if (params->flags & TLS_CONN_REQUEST_OCSP) {
		SSL_enable_ocsp_stapling(conn->ssl);
	}
#else /* OPENSSL_IS_BORINGSSL */
#ifdef HAVE_OCSP
	if (params->flags & TLS_CONN_REQUEST_OCSP) {
		SSL_CTX *ssl_ctx = data->ssl;
		SSL_set_tlsext_status_type(conn->ssl, TLSEXT_STATUSTYPE_ocsp);
		SSL_CTX_set_tlsext_status_cb(ssl_ctx, ocsp_resp_cb);
		SSL_CTX_set_tlsext_status_arg(ssl_ctx, conn);
	}
#else /* HAVE_OCSP */
	if (params->flags & TLS_CONN_REQUIRE_OCSP) {
		wpa_printf(MSG_INFO,
			   "OpenSSL: No OCSP support included - reject configuration");
		return -1;
	}
	if (params->flags & TLS_CONN_REQUEST_OCSP) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: No OCSP support included - allow optional OCSP case to continue");
	}
#endif /* HAVE_OCSP */
#endif /* OPENSSL_IS_BORINGSSL */

	conn->flags = params->flags;

	tls_get_errors(data);

	return 0;
}


int tls_global_set_params(void *tls_ctx,
			  const struct tls_connection_params *params)
{
	struct tls_data *data = tls_ctx;
	SSL_CTX *ssl_ctx = data->ssl;
	unsigned long err;

	while ((err = ERR_get_error())) {
		wpa_printf(MSG_INFO, "%s: Clearing pending SSL error: %s",
			   __func__, ERR_error_string(err, NULL));
	}

	if (tls_global_ca_cert(data, params->ca_cert) ||
	    tls_global_client_cert(data, params->client_cert) ||
	    tls_global_private_key(data, params->private_key,
				   params->private_key_passwd) ||
	    tls_global_dh(data, params->dh_file)) {
		wpa_printf(MSG_INFO, "TLS: Failed to set global parameters");
		return -1;
	}

	if (params->openssl_ciphers &&
	    SSL_CTX_set_cipher_list(ssl_ctx, params->openssl_ciphers) != 1) {
		wpa_printf(MSG_INFO,
			   "OpenSSL: Failed to set cipher string '%s'",
			   params->openssl_ciphers);
		return -1;
	}

#ifdef SSL_OP_NO_TICKET
	if (params->flags & TLS_CONN_DISABLE_SESSION_TICKET)
		SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_TICKET);
	else
		SSL_CTX_clear_options(ssl_ctx, SSL_OP_NO_TICKET);
#endif /*  SSL_OP_NO_TICKET */

#ifdef HAVE_OCSP
	SSL_CTX_set_tlsext_status_cb(ssl_ctx, ocsp_status_cb);
	SSL_CTX_set_tlsext_status_arg(ssl_ctx, ssl_ctx);
	os_free(tls_global->ocsp_stapling_response);
	if (params->ocsp_stapling_response)
		tls_global->ocsp_stapling_response =
			os_strdup(params->ocsp_stapling_response);
	else
		tls_global->ocsp_stapling_response = NULL;
#endif /* HAVE_OCSP */

	return 0;
}


#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST)
/* Pre-shared secred requires a patch to openssl, so this function is
 * commented out unless explicitly needed for EAP-FAST in order to be able to
 * build this file with unmodified openssl. */

#if (defined(OPENSSL_IS_BORINGSSL) || OPENSSL_VERSION_NUMBER >= 0x10100000L) && !defined(LIBRESSL_VERSION_NUMBER)
static int tls_sess_sec_cb(SSL *s, void *secret, int *secret_len,
			   STACK_OF(SSL_CIPHER) *peer_ciphers,
			   const SSL_CIPHER **cipher, void *arg)
#else /* OPENSSL_IS_BORINGSSL */
static int tls_sess_sec_cb(SSL *s, void *secret, int *secret_len,
			   STACK_OF(SSL_CIPHER) *peer_ciphers,
			   SSL_CIPHER **cipher, void *arg)
#endif /* OPENSSL_IS_BORINGSSL */
{
	struct tls_connection *conn = arg;
	int ret;

#if OPENSSL_VERSION_NUMBER < 0x10100000L || \
	(defined(LIBRESSL_VERSION_NUMBER) && \
	 LIBRESSL_VERSION_NUMBER < 0x20700000L)
	if (conn == NULL || conn->session_ticket_cb == NULL)
		return 0;

	ret = conn->session_ticket_cb(conn->session_ticket_cb_ctx,
				      conn->session_ticket,
				      conn->session_ticket_len,
				      s->s3->client_random,
				      s->s3->server_random, secret);
#else
	unsigned char client_random[SSL3_RANDOM_SIZE];
	unsigned char server_random[SSL3_RANDOM_SIZE];

	if (conn == NULL || conn->session_ticket_cb == NULL)
		return 0;

	SSL_get_client_random(s, client_random, sizeof(client_random));
	SSL_get_server_random(s, server_random, sizeof(server_random));

	ret = conn->session_ticket_cb(conn->session_ticket_cb_ctx,
				      conn->session_ticket,
				      conn->session_ticket_len,
				      client_random,
				      server_random, secret);
#endif

	os_free(conn->session_ticket);
	conn->session_ticket = NULL;

	if (ret <= 0)
		return 0;

	*secret_len = SSL_MAX_MASTER_KEY_LENGTH;
	return 1;
}


static int tls_session_ticket_ext_cb(SSL *s, const unsigned char *data,
				     int len, void *arg)
{
	struct tls_connection *conn = arg;

	if (conn == NULL || conn->session_ticket_cb == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "OpenSSL: %s: length=%d", __func__, len);

	os_free(conn->session_ticket);
	conn->session_ticket = NULL;

	wpa_hexdump(MSG_DEBUG, "OpenSSL: ClientHello SessionTicket "
		    "extension", data, len);

	conn->session_ticket = os_memdup(data, len);
	if (conn->session_ticket == NULL)
		return 0;

	conn->session_ticket_len = len;

	return 1;
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
		if (SSL_set_session_secret_cb(conn->ssl, tls_sess_sec_cb,
					      conn) != 1)
			return -1;
		SSL_set_session_ticket_ext_cb(conn->ssl,
					      tls_session_ticket_ext_cb, conn);
	} else {
		if (SSL_set_session_secret_cb(conn->ssl, NULL, NULL) != 1)
			return -1;
		SSL_set_session_ticket_ext_cb(conn->ssl, NULL, NULL);
	}

	return 0;
#else /* EAP_FAST || EAP_FAST_DYNAMIC || EAP_SERVER_FAST */
	return -1;
#endif /* EAP_FAST || EAP_FAST_DYNAMIC || EAP_SERVER_FAST */
}


int tls_get_library_version(char *buf, size_t buf_len)
{
#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
	return os_snprintf(buf, buf_len, "OpenSSL build=%s run=%s",
			   OPENSSL_VERSION_TEXT,
			   OpenSSL_version(OPENSSL_VERSION));
#else
	return os_snprintf(buf, buf_len, "OpenSSL build=%s run=%s",
			   OPENSSL_VERSION_TEXT,
			   SSLeay_version(SSLEAY_VERSION));
#endif
}


void tls_connection_set_success_data(struct tls_connection *conn,
				     struct wpabuf *data)
{
	SSL_SESSION *sess;
	struct wpabuf *old;

	if (tls_ex_idx_session < 0)
		goto fail;
	sess = SSL_get_session(conn->ssl);
	if (!sess)
		goto fail;
	old = SSL_SESSION_get_ex_data(sess, tls_ex_idx_session);
	if (old) {
		wpa_printf(MSG_DEBUG, "OpenSSL: Replacing old success data %p",
			   old);
		wpabuf_free(old);
	}
	if (SSL_SESSION_set_ex_data(sess, tls_ex_idx_session, data) != 1)
		goto fail;

	wpa_printf(MSG_DEBUG, "OpenSSL: Stored success data %p", data);
	conn->success_data = 1;
	return;

fail:
	wpa_printf(MSG_INFO, "OpenSSL: Failed to store success data");
	wpabuf_free(data);
}


void tls_connection_set_success_data_resumed(struct tls_connection *conn)
{
	wpa_printf(MSG_DEBUG,
		   "OpenSSL: Success data accepted for resumed session");
	conn->success_data = 1;
}


const struct wpabuf *
tls_connection_get_success_data(struct tls_connection *conn)
{
	SSL_SESSION *sess;

	if (tls_ex_idx_session < 0 ||
	    !(sess = SSL_get_session(conn->ssl)))
		return NULL;
	return SSL_SESSION_get_ex_data(sess, tls_ex_idx_session);
}


void tls_connection_remove_session(struct tls_connection *conn)
{
	SSL_SESSION *sess;

	sess = SSL_get_session(conn->ssl);
	if (!sess)
		return;

	if (SSL_CTX_remove_session(conn->ssl_ctx, sess) != 1)
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Session was not cached");
	else
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Removed cached session to disable session resumption");
}
