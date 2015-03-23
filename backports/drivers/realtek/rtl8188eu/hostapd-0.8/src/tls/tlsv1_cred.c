/*
 * TLSv1 credentials
 * Copyright (c) 2006-2009, Jouni Malinen <j@w1.fi>
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
#include "base64.h"
#include "crypto/crypto.h"
#include "x509v3.h"
#include "tlsv1_cred.h"


struct tlsv1_credentials * tlsv1_cred_alloc(void)
{
	struct tlsv1_credentials *cred;
	cred = os_zalloc(sizeof(*cred));
	return cred;
}


void tlsv1_cred_free(struct tlsv1_credentials *cred)
{
	if (cred == NULL)
		return;

	x509_certificate_chain_free(cred->trusted_certs);
	x509_certificate_chain_free(cred->cert);
	crypto_private_key_free(cred->key);
	os_free(cred->dh_p);
	os_free(cred->dh_g);
	os_free(cred);
}


static int tlsv1_add_cert_der(struct x509_certificate **chain,
			      const u8 *buf, size_t len)
{
	struct x509_certificate *cert;
	char name[128];

	cert = x509_certificate_parse(buf, len);
	if (cert == NULL) {
		wpa_printf(MSG_INFO, "TLSv1: %s - failed to parse certificate",
			   __func__);
		return -1;
	}

	cert->next = *chain;
	*chain = cert;

	x509_name_string(&cert->subject, name, sizeof(name));
	wpa_printf(MSG_DEBUG, "TLSv1: Added certificate: %s", name);

	return 0;
}


static const char *pem_cert_begin = "-----BEGIN CERTIFICATE-----";
static const char *pem_cert_end = "-----END CERTIFICATE-----";
static const char *pem_key_begin = "-----BEGIN RSA PRIVATE KEY-----";
static const char *pem_key_end = "-----END RSA PRIVATE KEY-----";
static const char *pem_key2_begin = "-----BEGIN PRIVATE KEY-----";
static const char *pem_key2_end = "-----END PRIVATE KEY-----";
static const char *pem_key_enc_begin = "-----BEGIN ENCRYPTED PRIVATE KEY-----";
static const char *pem_key_enc_end = "-----END ENCRYPTED PRIVATE KEY-----";


static const u8 * search_tag(const char *tag, const u8 *buf, size_t len)
{
	size_t i, plen;

	plen = os_strlen(tag);
	if (len < plen)
		return NULL;

	for (i = 0; i < len - plen; i++) {
		if (os_memcmp(buf + i, tag, plen) == 0)
			return buf + i;
	}

	return NULL;
}


static int tlsv1_add_cert(struct x509_certificate **chain,
			  const u8 *buf, size_t len)
{
	const u8 *pos, *end;
	unsigned char *der;
	size_t der_len;

	pos = search_tag(pem_cert_begin, buf, len);
	if (!pos) {
		wpa_printf(MSG_DEBUG, "TLSv1: No PEM certificate tag found - "
			   "assume DER format");
		return tlsv1_add_cert_der(chain, buf, len);
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Converting PEM format certificate into "
		   "DER format");

	while (pos) {
		pos += os_strlen(pem_cert_begin);
		end = search_tag(pem_cert_end, pos, buf + len - pos);
		if (end == NULL) {
			wpa_printf(MSG_INFO, "TLSv1: Could not find PEM "
				   "certificate end tag (%s)", pem_cert_end);
			return -1;
		}

		der = base64_decode(pos, end - pos, &der_len);
		if (der == NULL) {
			wpa_printf(MSG_INFO, "TLSv1: Could not decode PEM "
				   "certificate");
			return -1;
		}

		if (tlsv1_add_cert_der(chain, der, der_len) < 0) {
			wpa_printf(MSG_INFO, "TLSv1: Failed to parse PEM "
				   "certificate after DER conversion");
			os_free(der);
			return -1;
		}

		os_free(der);

		end += os_strlen(pem_cert_end);
		pos = search_tag(pem_cert_begin, end, buf + len - end);
	}

	return 0;
}


static int tlsv1_set_cert_chain(struct x509_certificate **chain,
				const char *cert, const u8 *cert_blob,
				size_t cert_blob_len)
{
	if (cert_blob)
		return tlsv1_add_cert(chain, cert_blob, cert_blob_len);

	if (cert) {
		u8 *buf;
		size_t len;
		int ret;

		buf = (u8 *) os_readfile(cert, &len);
		if (buf == NULL) {
			wpa_printf(MSG_INFO, "TLSv1: Failed to read '%s'",
				   cert);
			return -1;
		}

		ret = tlsv1_add_cert(chain, buf, len);
		os_free(buf);
		return ret;
	}

	return 0;
}


/**
 * tlsv1_set_ca_cert - Set trusted CA certificate(s)
 * @cred: TLSv1 credentials from tlsv1_cred_alloc()
 * @cert: File or reference name for X.509 certificate in PEM or DER format
 * @cert_blob: cert as inlined data or %NULL if not used
 * @cert_blob_len: ca_cert_blob length
 * @path: Path to CA certificates (not yet supported)
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_set_ca_cert(struct tlsv1_credentials *cred, const char *cert,
		      const u8 *cert_blob, size_t cert_blob_len,
		      const char *path)
{
	if (tlsv1_set_cert_chain(&cred->trusted_certs, cert,
				 cert_blob, cert_blob_len) < 0)
		return -1;

	if (path) {
		/* TODO: add support for reading number of certificate files */
		wpa_printf(MSG_INFO, "TLSv1: Use of CA certificate directory "
			   "not yet supported");
		return -1;
	}

	return 0;
}


/**
 * tlsv1_set_cert - Set certificate
 * @cred: TLSv1 credentials from tlsv1_cred_alloc()
 * @cert: File or reference name for X.509 certificate in PEM or DER format
 * @cert_blob: cert as inlined data or %NULL if not used
 * @cert_blob_len: cert_blob length
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_set_cert(struct tlsv1_credentials *cred, const char *cert,
		   const u8 *cert_blob, size_t cert_blob_len)
{
	return tlsv1_set_cert_chain(&cred->cert, cert,
				    cert_blob, cert_blob_len);
}


static struct crypto_private_key * tlsv1_set_key_pem(const u8 *key, size_t len)
{
	const u8 *pos, *end;
	unsigned char *der;
	size_t der_len;
	struct crypto_private_key *pkey;

	pos = search_tag(pem_key_begin, key, len);
	if (!pos) {
		pos = search_tag(pem_key2_begin, key, len);
		if (!pos)
			return NULL;
		pos += os_strlen(pem_key2_begin);
		end = search_tag(pem_key2_end, pos, key + len - pos);
		if (!end)
			return NULL;
	} else {
		pos += os_strlen(pem_key_begin);
		end = search_tag(pem_key_end, pos, key + len - pos);
		if (!end)
			return NULL;
	}

	der = base64_decode(pos, end - pos, &der_len);
	if (!der)
		return NULL;
	pkey = crypto_private_key_import(der, der_len, NULL);
	os_free(der);
	return pkey;
}


static struct crypto_private_key * tlsv1_set_key_enc_pem(const u8 *key,
							 size_t len,
							 const char *passwd)
{
	const u8 *pos, *end;
	unsigned char *der;
	size_t der_len;
	struct crypto_private_key *pkey;

	if (passwd == NULL)
		return NULL;
	pos = search_tag(pem_key_enc_begin, key, len);
	if (!pos)
		return NULL;
	pos += os_strlen(pem_key_enc_begin);
	end = search_tag(pem_key_enc_end, pos, key + len - pos);
	if (!end)
		return NULL;

	der = base64_decode(pos, end - pos, &der_len);
	if (!der)
		return NULL;
	pkey = crypto_private_key_import(der, der_len, passwd);
	os_free(der);
	return pkey;
}


static int tlsv1_set_key(struct tlsv1_credentials *cred,
			 const u8 *key, size_t len, const char *passwd)
{
	cred->key = crypto_private_key_import(key, len, passwd);
	if (cred->key == NULL)
		cred->key = tlsv1_set_key_pem(key, len);
	if (cred->key == NULL)
		cred->key = tlsv1_set_key_enc_pem(key, len, passwd);
	if (cred->key == NULL) {
		wpa_printf(MSG_INFO, "TLSv1: Failed to parse private key");
		return -1;
	}
	return 0;
}


/**
 * tlsv1_set_private_key - Set private key
 * @cred: TLSv1 credentials from tlsv1_cred_alloc()
 * @private_key: File or reference name for the key in PEM or DER format
 * @private_key_passwd: Passphrase for decrypted private key, %NULL if no
 * passphrase is used.
 * @private_key_blob: private_key as inlined data or %NULL if not used
 * @private_key_blob_len: private_key_blob length
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_set_private_key(struct tlsv1_credentials *cred,
			  const char *private_key,
			  const char *private_key_passwd,
			  const u8 *private_key_blob,
			  size_t private_key_blob_len)
{
	crypto_private_key_free(cred->key);
	cred->key = NULL;

	if (private_key_blob)
		return tlsv1_set_key(cred, private_key_blob,
				     private_key_blob_len,
				     private_key_passwd);

	if (private_key) {
		u8 *buf;
		size_t len;
		int ret;

		buf = (u8 *) os_readfile(private_key, &len);
		if (buf == NULL) {
			wpa_printf(MSG_INFO, "TLSv1: Failed to read '%s'",
				   private_key);
			return -1;
		}

		ret = tlsv1_set_key(cred, buf, len, private_key_passwd);
		os_free(buf);
		return ret;
	}

	return 0;
}


static int tlsv1_set_dhparams_der(struct tlsv1_credentials *cred,
				  const u8 *dh, size_t len)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;

	pos = dh;
	end = dh + len;

	/*
	 * DHParameter ::= SEQUENCE {
	 *   prime INTEGER, -- p
	 *   base INTEGER, -- g
	 *   privateValueLength INTEGER OPTIONAL }
	 */

	/* DHParamer ::= SEQUENCE */
	if (asn1_get_next(pos, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "DH: DH parameters did not start with a "
			   "valid SEQUENCE - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;

	/* prime INTEGER */
	if (asn1_get_next(pos, end - pos, &hdr) < 0)
		return -1;

	if (hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_INTEGER) {
		wpa_printf(MSG_DEBUG, "DH: No INTEGER tag found for p; "
			   "class=%d tag=0x%x", hdr.class, hdr.tag);
		return -1;
	}

	wpa_hexdump(MSG_MSGDUMP, "DH: prime (p)", hdr.payload, hdr.length);
	if (hdr.length == 0)
		return -1;
	os_free(cred->dh_p);
	cred->dh_p = os_malloc(hdr.length);
	if (cred->dh_p == NULL)
		return -1;
	os_memcpy(cred->dh_p, hdr.payload, hdr.length);
	cred->dh_p_len = hdr.length;
	pos = hdr.payload + hdr.length;

	/* base INTEGER */
	if (asn1_get_next(pos, end - pos, &hdr) < 0)
		return -1;

	if (hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_INTEGER) {
		wpa_printf(MSG_DEBUG, "DH: No INTEGER tag found for g; "
			   "class=%d tag=0x%x", hdr.class, hdr.tag);
		return -1;
	}

	wpa_hexdump(MSG_MSGDUMP, "DH: base (g)", hdr.payload, hdr.length);
	if (hdr.length == 0)
		return -1;
	os_free(cred->dh_g);
	cred->dh_g = os_malloc(hdr.length);
	if (cred->dh_g == NULL)
		return -1;
	os_memcpy(cred->dh_g, hdr.payload, hdr.length);
	cred->dh_g_len = hdr.length;

	return 0;
}


static const char *pem_dhparams_begin = "-----BEGIN DH PARAMETERS-----";
static const char *pem_dhparams_end = "-----END DH PARAMETERS-----";


static int tlsv1_set_dhparams_blob(struct tlsv1_credentials *cred,
				   const u8 *buf, size_t len)
{
	const u8 *pos, *end;
	unsigned char *der;
	size_t der_len;

	pos = search_tag(pem_dhparams_begin, buf, len);
	if (!pos) {
		wpa_printf(MSG_DEBUG, "TLSv1: No PEM dhparams tag found - "
			   "assume DER format");
		return tlsv1_set_dhparams_der(cred, buf, len);
	}

	wpa_printf(MSG_DEBUG, "TLSv1: Converting PEM format dhparams into DER "
		   "format");

	pos += os_strlen(pem_dhparams_begin);
	end = search_tag(pem_dhparams_end, pos, buf + len - pos);
	if (end == NULL) {
		wpa_printf(MSG_INFO, "TLSv1: Could not find PEM dhparams end "
			   "tag (%s)", pem_dhparams_end);
		return -1;
	}

	der = base64_decode(pos, end - pos, &der_len);
	if (der == NULL) {
		wpa_printf(MSG_INFO, "TLSv1: Could not decode PEM dhparams");
		return -1;
	}

	if (tlsv1_set_dhparams_der(cred, der, der_len) < 0) {
		wpa_printf(MSG_INFO, "TLSv1: Failed to parse PEM dhparams "
			   "DER conversion");
		os_free(der);
		return -1;
	}

	os_free(der);

	return 0;
}


/**
 * tlsv1_set_dhparams - Set Diffie-Hellman parameters
 * @cred: TLSv1 credentials from tlsv1_cred_alloc()
 * @dh_file: File or reference name for the DH params in PEM or DER format
 * @dh_blob: DH params as inlined data or %NULL if not used
 * @dh_blob_len: dh_blob length
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_set_dhparams(struct tlsv1_credentials *cred, const char *dh_file,
		       const u8 *dh_blob, size_t dh_blob_len)
{
	if (dh_blob)
		return tlsv1_set_dhparams_blob(cred, dh_blob, dh_blob_len);

	if (dh_file) {
		u8 *buf;
		size_t len;
		int ret;

		buf = (u8 *) os_readfile(dh_file, &len);
		if (buf == NULL) {
			wpa_printf(MSG_INFO, "TLSv1: Failed to read '%s'",
				   dh_file);
			return -1;
		}

		ret = tlsv1_set_dhparams_blob(cred, buf, len);
		os_free(buf);
		return ret;
	}

	return 0;
}
