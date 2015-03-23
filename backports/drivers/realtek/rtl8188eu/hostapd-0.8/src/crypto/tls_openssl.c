/*
 * SSL/TLS interface functions for OpenSSL
 * Copyright (c) 2004-2010, Jouni Malinen <j@w1.fi>
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

#ifndef CONFIG_SMARTCARD
#ifndef OPENSSL_NO_ENGINE
#define OPENSSL_NO_ENGINE
#endif
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pkcs12.h>
#include <openssl/x509v3.h>
#ifndef OPENSSL_NO_ENGINE
#include <openssl/engine.h>
#endif /* OPENSSL_NO_ENGINE */

#ifdef ANDROID
#include <openssl/pem.h>
#include "keystore_get.h"
#endif /* ANDROID */

#include "common.h"
#include "crypto.h"
#include "tls.h"

#if OPENSSL_VERSION_NUMBER >= 0x0090800fL
#define OPENSSL_d2i_TYPE const unsigned char **
#else
#define OPENSSL_d2i_TYPE unsigned char **
#endif

#ifdef SSL_F_SSL_SET_SESSION_TICKET_EXT
#ifdef SSL_OP_NO_TICKET
/*
 * Session ticket override patch was merged into OpenSSL 0.9.9 tree on
 * 2008-11-15. This version uses a bit different API compared to the old patch.
 */
#define CONFIG_OPENSSL_TICKET_OVERRIDE
#endif
#endif

static int tls_openssl_ref_count = 0;

struct tls_global {
	void (*event_cb)(void *ctx, enum tls_event ev,
			 union tls_event_data *data);
	void *cb_ctx;
};

static struct tls_global *tls_global = NULL;


struct tls_connection {
	SSL *ssl;
	BIO *ssl_in, *ssl_out;
#ifndef OPENSSL_NO_ENGINE
	ENGINE *engine;        /* functional reference to the engine */
	EVP_PKEY *private_key; /* the private key if using engine */
#endif /* OPENSSL_NO_ENGINE */
	char *subject_match, *altsubject_match;
	int read_alerts, write_alerts, failed;

	tls_session_ticket_cb session_ticket_cb;
	void *session_ticket_cb_ctx;

	/* SessionTicket received from OpenSSL hello_extension_cb (server) */
	u8 *session_ticket;
	size_t session_ticket_len;

	unsigned int ca_cert_verify:1;
	unsigned int cert_probe:1;
	unsigned int server_cert_only:1;

	u8 srv_cert_hash[32];
};


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

	cert = d2i_X509(NULL, (OPENSSL_d2i_TYPE) &priv->cert->pbCertEncoded,
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
		cert = d2i_X509(NULL, (OPENSSL_d2i_TYPE) &ctx->pbCertEncoded,
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

		if (!X509_STORE_add_cert(ssl_ctx->cert_store, cert)) {
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
		wpa_printf(MSG_INFO, "SSL: SSL3 alert: %s:%s:%s",
			   where & SSL_CB_READ ?
			   "read (remote end reported an error)" :
			   "write (local SSL3 detected an error)",
			   SSL_alert_type_string_long(ret),
			   SSL_alert_desc_string_long(ret));
		if ((ret >> 8) == SSL3_AL_FATAL) {
			struct tls_connection *conn =
				SSL_get_app_data((SSL *) ssl);
			if (where & SSL_CB_READ)
				conn->read_alerts++;
			else
				conn->write_alerts++;
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
		ENGINE_free(engine);
		wpa_printf(MSG_DEBUG, "ENGINE: engine '%s' is already "
			   "available", id);
		return 0;
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

	if (!pkcs11_so_path || !pkcs11_module_path)
		return 0;

	pre_cmd[1] = pkcs11_so_path;
	pre_cmd[3] = engine_id;
	post_cmd[1] = pkcs11_module_path;

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


void * tls_init(const struct tls_config *conf)
{
	SSL_CTX *ssl;

	if (tls_openssl_ref_count == 0) {
		tls_global = os_zalloc(sizeof(*tls_global));
		if (tls_global == NULL)
			return NULL;
		if (conf) {
			tls_global->event_cb = conf->event_cb;
			tls_global->cb_ctx = conf->cb_ctx;
		}

#ifdef CONFIG_FIPS
#ifdef OPENSSL_FIPS
		if (conf && conf->fips_mode) {
			if (!FIPS_mode_set(1)) {
				wpa_printf(MSG_ERROR, "Failed to enable FIPS "
					   "mode");
				ERR_load_crypto_strings();
				ERR_print_errors_fp(stderr);
				return NULL;
			} else
				wpa_printf(MSG_INFO, "Running in FIPS mode");
		}
#else /* OPENSSL_FIPS */
		if (conf && conf->fips_mode) {
			wpa_printf(MSG_ERROR, "FIPS mode requested, but not "
				   "supported");
			return NULL;
		}
#endif /* OPENSSL_FIPS */
#endif /* CONFIG_FIPS */
		SSL_load_error_strings();
		SSL_library_init();
#if (OPENSSL_VERSION_NUMBER >= 0x0090800fL) && !defined(OPENSSL_NO_SHA256)
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
	}
	tls_openssl_ref_count++;

	ssl = SSL_CTX_new(TLSv1_method());
	if (ssl == NULL)
		return NULL;

	SSL_CTX_set_info_callback(ssl, ssl_info_cb);

#ifndef OPENSSL_NO_ENGINE
	if (conf &&
	    (conf->opensc_engine_path || conf->pkcs11_engine_path ||
	     conf->pkcs11_module_path)) {
		wpa_printf(MSG_DEBUG, "ENGINE: Loading dynamic engine");
		ERR_load_ENGINE_strings();
		ENGINE_load_dynamic();

		if (tls_engine_load_dynamic_opensc(conf->opensc_engine_path) ||
		    tls_engine_load_dynamic_pkcs11(conf->pkcs11_engine_path,
						   conf->pkcs11_module_path)) {
			tls_deinit(ssl);
			return NULL;
		}
	}
#endif /* OPENSSL_NO_ENGINE */

	return ssl;
}


void tls_deinit(void *ssl_ctx)
{
	SSL_CTX *ssl = ssl_ctx;
	SSL_CTX_free(ssl);

	tls_openssl_ref_count--;
	if (tls_openssl_ref_count == 0) {
#ifndef OPENSSL_NO_ENGINE
		ENGINE_cleanup();
#endif /* OPENSSL_NO_ENGINE */
		CRYPTO_cleanup_all_ex_data();
		ERR_remove_state(0);
		ERR_free_strings();
		EVP_cleanup();
		os_free(tls_global);
		tls_global = NULL;
	}
}


static int tls_engine_init(struct tls_connection *conn, const char *engine_id,
			   const char *pin, const char *key_id,
			   const char *cert_id, const char *ca_cert_id)
{
#ifndef OPENSSL_NO_ENGINE
	int ret = -1;
	if (engine_id == NULL) {
		wpa_printf(MSG_ERROR, "ENGINE: Engine ID not set");
		return -1;
	}
	if (pin == NULL) {
		wpa_printf(MSG_ERROR, "ENGINE: Smartcard PIN not set");
		return -1;
	}
	if (key_id == NULL) {
		wpa_printf(MSG_ERROR, "ENGINE: Key Id not set");
		return -1;
	}

	ERR_clear_error();
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

	if (ENGINE_ctrl_cmd_string(conn->engine, "PIN", pin, 0) == 0) {
		wpa_printf(MSG_ERROR, "ENGINE: cannot set pin [%s]",
			   ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	/* load private key first in-case PIN is required for cert */
	conn->private_key = ENGINE_load_private_key(conn->engine,
						    key_id, NULL, NULL);
	if (!conn->private_key) {
		wpa_printf(MSG_ERROR, "ENGINE: cannot load private key with id"
				" '%s' [%s]", key_id,
			   ERR_error_string(ERR_get_error(), NULL));
		ret = TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED;
		goto err;
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
#ifndef OPENSSL_NO_ENGINE
	wpa_printf(MSG_DEBUG, "ENGINE: engine deinit");
	if (conn->private_key) {
		EVP_PKEY_free(conn->private_key);
		conn->private_key = NULL;
	}
	if (conn->engine) {
		ENGINE_finish(conn->engine);
		conn->engine = NULL;
	}
#endif /* OPENSSL_NO_ENGINE */
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

struct tls_connection * tls_connection_init(void *ssl_ctx)
{
	SSL_CTX *ssl = ssl_ctx;
	struct tls_connection *conn;
	long options;

	conn = os_zalloc(sizeof(*conn));
	if (conn == NULL)
		return NULL;
	conn->ssl = SSL_new(ssl);
	if (conn->ssl == NULL) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to initialize new SSL connection");
		os_free(conn);
		return NULL;
	}

	SSL_set_app_data(conn->ssl, conn);
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
	SSL_free(conn->ssl);
	tls_engine_deinit(conn);
	os_free(conn->subject_match);
	os_free(conn->altsubject_match);
	os_free(conn->session_ticket);
	os_free(conn);
}


int tls_connection_established(void *ssl_ctx, struct tls_connection *conn)
{
	return conn ? SSL_is_init_finished(conn->ssl) : 0;
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
	return 0;
}


static int tls_match_altsubject_component(X509 *cert, int type,
					  const char *value, size_t len)
{
	GENERAL_NAME *gen;
	void *ext;
	int i, found = 0;

	ext = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);

	for (i = 0; ext && i < sk_GENERAL_NAME_num(ext); i++) {
		gen = sk_GENERAL_NAME_value(ext, i);
		if (gen->type != type)
			continue;
		if (os_strlen((char *) gen->d.ia5->data) == len &&
		    os_memcmp(value, gen->d.ia5->data, len) == 0)
			found++;
	}

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

	if (tls_global->event_cb == NULL)
		return;

	cert = get_x509_cert(err_cert);
	os_memset(&ev, 0, sizeof(ev));
	ev.cert_fail.reason = reason != TLS_FAIL_UNSPECIFIED ?
		reason : openssl_tls_fail_reason(err);
	ev.cert_fail.depth = depth;
	ev.cert_fail.subject = subject;
	ev.cert_fail.reason_txt = err_str;
	ev.cert_fail.cert = cert;
	tls_global->event_cb(tls_global->cb_ctx, TLS_CERT_CHAIN_FAILURE, &ev);
	wpabuf_free(cert);
}


static void openssl_tls_cert_event(struct tls_connection *conn,
				   X509 *err_cert, int depth,
				   const char *subject)
{
	struct wpabuf *cert = NULL;
	union tls_event_data ev;
#ifdef CONFIG_SHA256
	u8 hash[32];
#endif /* CONFIG_SHA256 */

	if (tls_global->event_cb == NULL)
		return;

	os_memset(&ev, 0, sizeof(ev));
	if (conn->cert_probe) {
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
	tls_global->event_cb(tls_global->cb_ctx, TLS_PEER_CERTIFICATE, &ev);
	wpabuf_free(cert);
}


static int tls_verify_cb(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
	char buf[256];
	X509 *err_cert;
	int err, depth;
	SSL *ssl;
	struct tls_connection *conn;
	char *match, *altmatch;
	const char *err_str;

	err_cert = X509_STORE_CTX_get_current_cert(x509_ctx);
	err = X509_STORE_CTX_get_error(x509_ctx);
	depth = X509_STORE_CTX_get_error_depth(x509_ctx);
	ssl = X509_STORE_CTX_get_ex_data(x509_ctx,
					 SSL_get_ex_data_X509_STORE_CTX_idx());
	X509_NAME_oneline(X509_get_subject_name(err_cert), buf, sizeof(buf));

	conn = SSL_get_app_data(ssl);
	if (conn == NULL)
		return 0;
	match = conn->subject_match;
	altmatch = conn->altsubject_match;

	if (!preverify_ok && !conn->ca_cert_verify)
		preverify_ok = 1;
	if (!preverify_ok && depth > 0 && conn->server_cert_only)
		preverify_ok = 1;

	err_str = X509_verify_cert_error_string(err);

#ifdef CONFIG_SHA256
	if (preverify_ok && depth == 0 && conn->server_cert_only) {
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

	return preverify_ok;
}


#ifndef OPENSSL_NO_STDIO
static int tls_load_ca_der(void *_ssl_ctx, const char *ca_cert)
{
	SSL_CTX *ssl_ctx = _ssl_ctx;
	X509_LOOKUP *lookup;
	int ret = 0;

	lookup = X509_STORE_add_lookup(ssl_ctx->cert_store,
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


#ifdef ANDROID
static BIO * BIO_from_keystore(const char *key)
{
	BIO *bio = NULL;
	char value[KEYSTORE_MESSAGE_SIZE];
	int length = keystore_get(key, strlen(key), value);
	if (length != -1 && (bio = BIO_new(BIO_s_mem())) != NULL)
		BIO_write(bio, value, length);
	return bio;
}
#endif /* ANDROID */


static int tls_connection_ca_cert(void *_ssl_ctx, struct tls_connection *conn,
				  const char *ca_cert, const u8 *ca_cert_blob,
				  size_t ca_cert_blob_len, const char *ca_path)
{
	SSL_CTX *ssl_ctx = _ssl_ctx;

	/*
	 * Remove previously configured trusted CA certificates before adding
	 * new ones.
	 */
	X509_STORE_free(ssl_ctx->cert_store);
	ssl_ctx->cert_store = X509_STORE_new();
	if (ssl_ctx->cert_store == NULL) {
		wpa_printf(MSG_DEBUG, "OpenSSL: %s - failed to allocate new "
			   "certificate store", __func__);
		return -1;
	}

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
		X509 *cert = d2i_X509(NULL, (OPENSSL_d2i_TYPE) &ca_cert_blob,
				      ca_cert_blob_len);
		if (cert == NULL) {
			tls_show_errors(MSG_WARNING, __func__,
					"Failed to parse ca_cert_blob");
			return -1;
		}

		if (!X509_STORE_add_cert(ssl_ctx->cert_store, cert)) {
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
	if (ca_cert && os_strncmp("keystore://", ca_cert, 11) == 0) {
		BIO *bio = BIO_from_keystore(&ca_cert[11]);
		STACK_OF(X509_INFO) *stack = NULL;
		int i;

		if (bio) {
			stack = PEM_X509_INFO_read_bio(bio, NULL, NULL, NULL);
			BIO_free(bio);
		}
		if (!stack)
			return -1;

		for (i = 0; i < sk_X509_INFO_num(stack); ++i) {
			X509_INFO *info = sk_X509_INFO_value(stack, i);
			if (info->x509) {
				X509_STORE_add_cert(ssl_ctx->cert_store,
						    info->x509);
			}
			if (info->crl) {
				X509_STORE_add_crl(ssl_ctx->cert_store,
						   info->crl);
			}
		}
		sk_X509_INFO_pop_free(stack, X509_INFO_free);
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
			    tls_load_ca_der(ssl_ctx, ca_cert) == 0) {
				wpa_printf(MSG_DEBUG, "OpenSSL: %s - loaded "
					   "DER format CA certificate",
					   __func__);
			} else
				return -1;
		} else {
			wpa_printf(MSG_DEBUG, "TLS: Trusted root "
				   "certificate(s) loaded");
			tls_get_errors(ssl_ctx);
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


static int tls_global_ca_cert(SSL_CTX *ssl_ctx, const char *ca_cert)
{
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
		X509_STORE *cs = SSL_CTX_get_cert_store(ssl_ctx);
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
					    const char *altsubject_match)
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

	return 0;
}


int tls_connection_set_verify(void *ssl_ctx, struct tls_connection *conn,
			      int verify_peer)
{
	static int counter = 0;

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

	SSL_set_accept_state(conn->ssl);

	/*
	 * Set session id context in order to avoid fatal errors when client
	 * tries to resume a session. However, set the context to a unique
	 * value in order to effectively disable session resumption for now
	 * since not all areas of the server code are ready for it (e.g.,
	 * EAP-TTLS needs special handling for Phase 2 after abbreviated TLS
	 * handshake).
	 */
	counter++;
	SSL_set_session_id_context(conn->ssl,
				   (const unsigned char *) &counter,
				   sizeof(counter));

	return 0;
}


static int tls_connection_client_cert(struct tls_connection *conn,
				      const char *client_cert,
				      const u8 *client_cert_blob,
				      size_t client_cert_blob_len)
{
	if (client_cert == NULL && client_cert_blob == NULL)
		return 0;

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
			BIO_free(bio);
		}
		if (x509) {
			if (SSL_use_certificate(conn->ssl, x509) == 1)
				ret = 0;
			X509_free(x509);
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


static int tls_global_client_cert(SSL_CTX *ssl_ctx, const char *client_cert)
{
#ifndef OPENSSL_NO_STDIO
	if (client_cert == NULL)
		return 0;

	if (SSL_CTX_use_certificate_file(ssl_ctx, client_cert,
					 SSL_FILETYPE_ASN1) != 1 &&
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


static int tls_passwd_cb(char *buf, int size, int rwflag, void *password)
{
	if (password == NULL) {
		return 0;
	}
	os_strlcpy(buf, (char *) password, size);
	return os_strlen(buf);
}


#ifdef PKCS12_FUNCS
static int tls_parse_pkcs12(SSL_CTX *ssl_ctx, SSL *ssl, PKCS12 *p12,
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
			if (SSL_CTX_use_certificate(ssl_ctx, cert) != 1)
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
			if (SSL_CTX_use_PrivateKey(ssl_ctx, pkey) != 1)
				res = -1;
		}
		EVP_PKEY_free(pkey);
	}

	if (certs) {
		while ((cert = sk_X509_pop(certs)) != NULL) {
			X509_NAME_oneline(X509_get_subject_name(cert), buf,
					  sizeof(buf));
			wpa_printf(MSG_DEBUG, "TLS: additional certificate"
				   " from PKCS12: subject='%s'", buf);
			/*
			 * There is no SSL equivalent for the chain cert - so
			 * always add it to the context...
			 */
			if (SSL_CTX_add_extra_chain_cert(ssl_ctx, cert) != 1) {
				res = -1;
				break;
			}
		}
		sk_X509_free(certs);
	}

	PKCS12_free(p12);

	if (res < 0)
		tls_get_errors(ssl_ctx);

	return res;
}
#endif  /* PKCS12_FUNCS */


static int tls_read_pkcs12(SSL_CTX *ssl_ctx, SSL *ssl, const char *private_key,
			   const char *passwd)
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

	return tls_parse_pkcs12(ssl_ctx, ssl, p12, passwd);

#else /* PKCS12_FUNCS */
	wpa_printf(MSG_INFO, "TLS: PKCS12 support disabled - cannot read "
		   "p12/pfx files");
	return -1;
#endif  /* PKCS12_FUNCS */
}


static int tls_read_pkcs12_blob(SSL_CTX *ssl_ctx, SSL *ssl,
				const u8 *blob, size_t len, const char *passwd)
{
#ifdef PKCS12_FUNCS
	PKCS12 *p12;

	p12 = d2i_PKCS12(NULL, (OPENSSL_d2i_TYPE) &blob, len);
	if (p12 == NULL) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to use PKCS#12 blob");
		return -1;
	}

	return tls_parse_pkcs12(ssl_ctx, ssl, p12, passwd);

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
		wpa_printf(MSG_ERROR, "ENGINE: cannot load client cert with id"
			   " '%s' [%s]", cert_id,
			   ERR_error_string(ERR_get_error(), NULL));
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


static int tls_connection_engine_ca_cert(void *_ssl_ctx,
					 struct tls_connection *conn,
					 const char *ca_cert_id)
{
#ifndef OPENSSL_NO_ENGINE
	X509 *cert;
	SSL_CTX *ssl_ctx = _ssl_ctx;

	if (tls_engine_get_cert(conn, ca_cert_id, &cert))
		return -1;

	/* start off the same as tls_connection_ca_cert */
	X509_STORE_free(ssl_ctx->cert_store);
	ssl_ctx->cert_store = X509_STORE_new();
	if (ssl_ctx->cert_store == NULL) {
		wpa_printf(MSG_DEBUG, "OpenSSL: %s - failed to allocate new "
			   "certificate store", __func__);
		X509_free(cert);
		return -1;
	}
	if (!X509_STORE_add_cert(ssl_ctx->cert_store, cert)) {
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
	return 0;

#else /* OPENSSL_NO_ENGINE */
	return -1;
#endif /* OPENSSL_NO_ENGINE */
}


static int tls_connection_engine_private_key(struct tls_connection *conn)
{
#ifndef OPENSSL_NO_ENGINE
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


static int tls_connection_private_key(void *_ssl_ctx,
				      struct tls_connection *conn,
				      const char *private_key,
				      const char *private_key_passwd,
				      const u8 *private_key_blob,
				      size_t private_key_blob_len)
{
	SSL_CTX *ssl_ctx = _ssl_ctx;
	char *passwd;
	int ok;

	if (private_key == NULL && private_key_blob == NULL)
		return 0;

	if (private_key_passwd) {
		passwd = os_strdup(private_key_passwd);
		if (passwd == NULL)
			return -1;
	} else
		passwd = NULL;

	SSL_CTX_set_default_passwd_cb(ssl_ctx, tls_passwd_cb);
	SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, passwd);

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

		if (tls_read_pkcs12_blob(ssl_ctx, conn->ssl, private_key_blob,
					 private_key_blob_len, passwd) == 0) {
			wpa_printf(MSG_DEBUG, "OpenSSL: PKCS#12 as blob --> "
				   "OK");
			ok = 1;
			break;
		}

		break;
	}

#ifdef ANDROID
	if (!ok && private_key &&
	    os_strncmp("keystore://", private_key, 11) == 0) {
		BIO *bio = BIO_from_keystore(&private_key[11]);
		EVP_PKEY *pkey = NULL;
		if (bio) {
			pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
			BIO_free(bio);
		}
		if (pkey) {
			if (SSL_use_PrivateKey(conn->ssl, pkey) == 1) {
				wpa_printf(MSG_DEBUG, "OpenSSL: Private key "
					   "from keystore");
				ok = 1;
			}
			EVP_PKEY_free(pkey);
		}
	}
#endif /* ANDROID */

	while (!ok && private_key) {
#ifndef OPENSSL_NO_STDIO
		if (SSL_use_PrivateKey_file(conn->ssl, private_key,
					    SSL_FILETYPE_ASN1) == 1) {
			wpa_printf(MSG_DEBUG, "OpenSSL: "
				   "SSL_use_PrivateKey_File (DER) --> OK");
			ok = 1;
			break;
		}

		if (SSL_use_PrivateKey_file(conn->ssl, private_key,
					    SSL_FILETYPE_PEM) == 1) {
			wpa_printf(MSG_DEBUG, "OpenSSL: "
				   "SSL_use_PrivateKey_File (PEM) --> OK");
			ok = 1;
			break;
		}
#else /* OPENSSL_NO_STDIO */
		wpa_printf(MSG_DEBUG, "OpenSSL: %s - OPENSSL_NO_STDIO",
			   __func__);
#endif /* OPENSSL_NO_STDIO */

		if (tls_read_pkcs12(ssl_ctx, conn->ssl, private_key, passwd)
		    == 0) {
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
		os_free(passwd);
		return -1;
	}
	ERR_clear_error();
	SSL_CTX_set_default_passwd_cb(ssl_ctx, NULL);
	os_free(passwd);
	
	if (!SSL_check_private_key(conn->ssl)) {
		tls_show_errors(MSG_INFO, __func__, "Private key failed "
				"verification");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "SSL: Private key loaded successfully");
	return 0;
}


static int tls_global_private_key(SSL_CTX *ssl_ctx, const char *private_key,
				  const char *private_key_passwd)
{
	char *passwd;

	if (private_key == NULL)
		return 0;

	if (private_key_passwd) {
		passwd = os_strdup(private_key_passwd);
		if (passwd == NULL)
			return -1;
	} else
		passwd = NULL;

	SSL_CTX_set_default_passwd_cb(ssl_ctx, tls_passwd_cb);
	SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, passwd);
	if (
#ifndef OPENSSL_NO_STDIO
	    SSL_CTX_use_PrivateKey_file(ssl_ctx, private_key,
					SSL_FILETYPE_ASN1) != 1 &&
	    SSL_CTX_use_PrivateKey_file(ssl_ctx, private_key,
					SSL_FILETYPE_PEM) != 1 &&
#endif /* OPENSSL_NO_STDIO */
	    tls_read_pkcs12(ssl_ctx, NULL, private_key, passwd)) {
		tls_show_errors(MSG_INFO, __func__,
				"Failed to load private key");
		os_free(passwd);
		ERR_clear_error();
		return -1;
	}
	os_free(passwd);
	ERR_clear_error();
	SSL_CTX_set_default_passwd_cb(ssl_ctx, NULL);
	
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


static int tls_global_dh(SSL_CTX *ssl_ctx, const char *dh_file)
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


int tls_connection_get_keys(void *ssl_ctx, struct tls_connection *conn,
			    struct tls_keys *keys)
{
	SSL *ssl;

	if (conn == NULL || keys == NULL)
		return -1;
	ssl = conn->ssl;
	if (ssl == NULL || ssl->s3 == NULL || ssl->session == NULL)
		return -1;

	os_memset(keys, 0, sizeof(*keys));
	keys->master_key = ssl->session->master_key;
	keys->master_key_len = ssl->session->master_key_length;
	keys->client_random = ssl->s3->client_random;
	keys->client_random_len = SSL3_RANDOM_SIZE;
	keys->server_random = ssl->s3->server_random;
	keys->server_random_len = SSL3_RANDOM_SIZE;

	return 0;
}


int tls_connection_prf(void *tls_ctx, struct tls_connection *conn,
		       const char *label, int server_random_first,
		       u8 *out, size_t out_len)
{
	return -1;
}


static struct wpabuf *
openssl_handshake(struct tls_connection *conn, const struct wpabuf *in_data,
		  int server)
{
	int res;
	struct wpabuf *out_data;

	/*
	 * Give TLS handshake data from the server (if available) to OpenSSL
	 * for processing.
	 */
	if (in_data &&
	    BIO_write(conn->ssl_in, wpabuf_head(in_data), wpabuf_len(in_data))
	    < 0) {
		tls_show_errors(MSG_INFO, __func__,
				"Handshake failed - BIO_write");
		return NULL;
	}

	/* Initiate TLS handshake or continue the existing handshake */
	if (server)
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
		}
	}

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
			     struct wpabuf **appl_data, int server)
{
	struct wpabuf *out_data;

	if (appl_data)
		*appl_data = NULL;

	out_data = openssl_handshake(conn, in_data, server);
	if (out_data == NULL)
		return NULL;

	if (SSL_is_init_finished(conn->ssl) && appl_data && in_data)
		*appl_data = openssl_get_appl_data(conn, wpabuf_len(in_data));

	return out_data;
}


struct wpabuf *
tls_connection_handshake(void *ssl_ctx, struct tls_connection *conn,
			 const struct wpabuf *in_data,
			 struct wpabuf **appl_data)
{
	return openssl_connection_handshake(conn, in_data, appl_data, 0);
}


struct wpabuf * tls_connection_server_handshake(void *tls_ctx,
						struct tls_connection *conn,
						const struct wpabuf *in_data,
						struct wpabuf **appl_data)
{
	return openssl_connection_handshake(conn, in_data, appl_data, 1);
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

	return buf;
}


int tls_connection_resumed(void *ssl_ctx, struct tls_connection *conn)
{
	return conn ? conn->ssl->hit : 0;
}


int tls_connection_set_cipher_list(void *tls_ctx, struct tls_connection *conn,
				   u8 *ciphers)
{
	char buf[100], *pos, *end;
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
		default:
			wpa_printf(MSG_DEBUG, "TLS: Unsupported "
				   "cipher selection: %d", *c);
			return -1;
		}
		ret = os_snprintf(pos, end - pos, ":%s", suite);
		if (ret < 0 || ret >= end - pos)
			break;
		pos += ret;

		c++;
	}

	wpa_printf(MSG_DEBUG, "OpenSSL: cipher suites: %s", buf + 1);

	if (SSL_set_cipher_list(conn->ssl, buf + 1) != 1) {
		tls_show_errors(MSG_INFO, __func__,
				"Cipher suite configuration failed");
		return -1;
	}

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

#ifdef CONFIG_OPENSSL_TICKET_OVERRIDE
	if (SSL_set_session_ticket_ext(conn->ssl, (void *) data,
				       data_len) != 1)
		return -1;
#else /* CONFIG_OPENSSL_TICKET_OVERRIDE */
	if (SSL_set_hello_extension(conn->ssl, ext_type, (void *) data,
				    data_len) != 1)
		return -1;
#endif /* CONFIG_OPENSSL_TICKET_OVERRIDE */

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


int tls_connection_set_params(void *tls_ctx, struct tls_connection *conn,
			      const struct tls_connection_params *params)
{
	int ret;
	unsigned long err;

	if (conn == NULL)
		return -1;

	while ((err = ERR_get_error())) {
		wpa_printf(MSG_INFO, "%s: Clearing pending SSL error: %s",
			   __func__, ERR_error_string(err, NULL));
	}

	if (params->engine) {
		wpa_printf(MSG_DEBUG, "SSL: Initializing TLS engine");
		ret = tls_engine_init(conn, params->engine_id, params->pin,
				      params->key_id, params->cert_id,
				      params->ca_cert_id);
		if (ret)
			return ret;
	}
	if (tls_connection_set_subject_match(conn,
					     params->subject_match,
					     params->altsubject_match))
		return -1;

	if (params->engine && params->ca_cert_id) {
		if (tls_connection_engine_ca_cert(tls_ctx, conn,
						  params->ca_cert_id))
			return TLS_SET_PARAMS_ENGINE_PRV_VERIFY_FAILED;
	} else if (tls_connection_ca_cert(tls_ctx, conn, params->ca_cert,
					  params->ca_cert_blob,
					  params->ca_cert_blob_len,
					  params->ca_path))
		return -1;

	if (params->engine && params->cert_id) {
		if (tls_connection_engine_client_cert(conn, params->cert_id))
			return TLS_SET_PARAMS_ENGINE_PRV_VERIFY_FAILED;
	} else if (tls_connection_client_cert(conn, params->client_cert,
					      params->client_cert_blob,
					      params->client_cert_blob_len))
		return -1;

	if (params->engine && params->key_id) {
		wpa_printf(MSG_DEBUG, "TLS: Using private key from engine");
		if (tls_connection_engine_private_key(conn))
			return TLS_SET_PARAMS_ENGINE_PRV_VERIFY_FAILED;
	} else if (tls_connection_private_key(tls_ctx, conn,
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

	tls_get_errors(tls_ctx);

	return 0;
}


int tls_global_set_params(void *tls_ctx,
			  const struct tls_connection_params *params)
{
	SSL_CTX *ssl_ctx = tls_ctx;
	unsigned long err;

	while ((err = ERR_get_error())) {
		wpa_printf(MSG_INFO, "%s: Clearing pending SSL error: %s",
			   __func__, ERR_error_string(err, NULL));
	}

	if (tls_global_ca_cert(ssl_ctx, params->ca_cert))
		return -1;

	if (tls_global_client_cert(ssl_ctx, params->client_cert))
		return -1;

	if (tls_global_private_key(ssl_ctx, params->private_key,
				   params->private_key_passwd))
		return -1;

	if (tls_global_dh(ssl_ctx, params->dh_file)) {
		wpa_printf(MSG_INFO, "TLS: Failed to load DH file '%s'",
			   params->dh_file);
		return -1;
	}

	return 0;
}


int tls_connection_get_keyblock_size(void *tls_ctx,
				     struct tls_connection *conn)
{
	const EVP_CIPHER *c;
	const EVP_MD *h;

	if (conn == NULL || conn->ssl == NULL ||
	    conn->ssl->enc_read_ctx == NULL ||
	    conn->ssl->enc_read_ctx->cipher == NULL ||
	    conn->ssl->read_hash == NULL)
		return -1;

	c = conn->ssl->enc_read_ctx->cipher;
#if OPENSSL_VERSION_NUMBER >= 0x00909000L
	h = EVP_MD_CTX_md(conn->ssl->read_hash);
#else
	h = conn->ssl->read_hash;
#endif

	return 2 * (EVP_CIPHER_key_length(c) +
		    EVP_MD_size(h) +
		    EVP_CIPHER_iv_length(c));
}


unsigned int tls_capabilities(void *tls_ctx)
{
	return 0;
}


int tls_connection_set_ia(void *tls_ctx, struct tls_connection *conn,
			  int tls_ia)
{
	return -1;
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


#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST)
/* Pre-shared secred requires a patch to openssl, so this function is
 * commented out unless explicitly needed for EAP-FAST in order to be able to
 * build this file with unmodified openssl. */

static int tls_sess_sec_cb(SSL *s, void *secret, int *secret_len,
			   STACK_OF(SSL_CIPHER) *peer_ciphers,
			   SSL_CIPHER **cipher, void *arg)
{
	struct tls_connection *conn = arg;
	int ret;

	if (conn == NULL || conn->session_ticket_cb == NULL)
		return 0;

	ret = conn->session_ticket_cb(conn->session_ticket_cb_ctx,
				      conn->session_ticket,
				      conn->session_ticket_len,
				      s->s3->client_random,
				      s->s3->server_random, secret);
	os_free(conn->session_ticket);
	conn->session_ticket = NULL;

	if (ret <= 0)
		return 0;

	*secret_len = SSL_MAX_MASTER_KEY_LENGTH;
	return 1;
}


#ifdef CONFIG_OPENSSL_TICKET_OVERRIDE
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

	conn->session_ticket = os_malloc(len);
	if (conn->session_ticket == NULL)
		return 0;

	os_memcpy(conn->session_ticket, data, len);
	conn->session_ticket_len = len;

	return 1;
}
#else /* CONFIG_OPENSSL_TICKET_OVERRIDE */
#ifdef SSL_OP_NO_TICKET
static void tls_hello_ext_cb(SSL *s, int client_server, int type,
			     unsigned char *data, int len, void *arg)
{
	struct tls_connection *conn = arg;

	if (conn == NULL || conn->session_ticket_cb == NULL)
		return;

	wpa_printf(MSG_DEBUG, "OpenSSL: %s: type=%d length=%d", __func__,
		   type, len);

	if (type == TLSEXT_TYPE_session_ticket && !client_server) {
		os_free(conn->session_ticket);
		conn->session_ticket = NULL;

		wpa_hexdump(MSG_DEBUG, "OpenSSL: ClientHello SessionTicket "
			    "extension", data, len);
		conn->session_ticket = os_malloc(len);
		if (conn->session_ticket == NULL)
			return;

		os_memcpy(conn->session_ticket, data, len);
		conn->session_ticket_len = len;
	}
}
#else /* SSL_OP_NO_TICKET */
static int tls_hello_ext_cb(SSL *s, TLS_EXTENSION *ext, void *arg)
{
	struct tls_connection *conn = arg;

	if (conn == NULL || conn->session_ticket_cb == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "OpenSSL: %s: type=%d length=%d", __func__,
		   ext->type, ext->length);

	os_free(conn->session_ticket);
	conn->session_ticket = NULL;

	if (ext->type == 35) {
		wpa_hexdump(MSG_DEBUG, "OpenSSL: ClientHello SessionTicket "
			    "extension", ext->data, ext->length);
		conn->session_ticket = os_malloc(ext->length);
		if (conn->session_ticket == NULL)
			return SSL_AD_INTERNAL_ERROR;

		os_memcpy(conn->session_ticket, ext->data, ext->length);
		conn->session_ticket_len = ext->length;
	}

	return 0;
}
#endif /* SSL_OP_NO_TICKET */
#endif /* CONFIG_OPENSSL_TICKET_OVERRIDE */
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
#ifdef CONFIG_OPENSSL_TICKET_OVERRIDE
		SSL_set_session_ticket_ext_cb(conn->ssl,
					      tls_session_ticket_ext_cb, conn);
#else /* CONFIG_OPENSSL_TICKET_OVERRIDE */
#ifdef SSL_OP_NO_TICKET
		SSL_set_tlsext_debug_callback(conn->ssl, tls_hello_ext_cb);
		SSL_set_tlsext_debug_arg(conn->ssl, conn);
#else /* SSL_OP_NO_TICKET */
		if (SSL_set_hello_extension_cb(conn->ssl, tls_hello_ext_cb,
					       conn) != 1)
			return -1;
#endif /* SSL_OP_NO_TICKET */
#endif /* CONFIG_OPENSSL_TICKET_OVERRIDE */
	} else {
		if (SSL_set_session_secret_cb(conn->ssl, NULL, NULL) != 1)
			return -1;
#ifdef CONFIG_OPENSSL_TICKET_OVERRIDE
		SSL_set_session_ticket_ext_cb(conn->ssl, NULL, NULL);
#else /* CONFIG_OPENSSL_TICKET_OVERRIDE */
#ifdef SSL_OP_NO_TICKET
		SSL_set_tlsext_debug_callback(conn->ssl, NULL);
		SSL_set_tlsext_debug_arg(conn->ssl, conn);
#else /* SSL_OP_NO_TICKET */
		if (SSL_set_hello_extension_cb(conn->ssl, NULL, NULL) != 1)
			return -1;
#endif /* SSL_OP_NO_TICKET */
#endif /* CONFIG_OPENSSL_TICKET_OVERRIDE */
	}

	return 0;
#else /* EAP_FAST || EAP_FAST_DYNAMIC || EAP_SERVER_FAST */
	return -1;
#endif /* EAP_FAST || EAP_FAST_DYNAMIC || EAP_SERVER_FAST */
}
