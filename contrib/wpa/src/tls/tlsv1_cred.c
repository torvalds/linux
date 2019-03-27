/*
 * TLSv1 credentials
 * Copyright (c) 2006-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "base64.h"
#include "crypto/crypto.h"
#include "crypto/sha1.h"
#include "pkcs5.h"
#include "pkcs8.h"
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
	os_free(cred->ocsp_stapling_response);
	os_free(cred->ocsp_stapling_response_multi);
	os_free(cred);
}


static int tlsv1_add_cert_der(struct x509_certificate **chain,
			      const u8 *buf, size_t len)
{
	struct x509_certificate *cert, *p;
	char name[128];

	cert = x509_certificate_parse(buf, len);
	if (cert == NULL) {
		wpa_printf(MSG_INFO, "TLSv1: %s - failed to parse certificate",
			   __func__);
		return -1;
	}

	p = *chain;
	while (p && p->next)
		p = p->next;
	if (p && x509_name_compare(&cert->subject, &p->issuer) == 0) {
		/*
		 * The new certificate is the issuer of the last certificate in
		 * the chain - add the new certificate to the end.
		 */
		p->next = cert;
	} else {
		/* Add to the beginning of the chain */
		cert->next = *chain;
		*chain = cert;
	}

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
	if (cert && os_strncmp(cert, "hash://", 7) == 0) {
		const char *pos = cert + 7;
		if (os_strncmp(pos, "server/sha256/", 14) != 0) {
			wpa_printf(MSG_DEBUG,
				   "TLSv1: Unsupported ca_cert hash value '%s'",
				   cert);
			return -1;
		}
		pos += 14;
		if (os_strlen(pos) != 32 * 2) {
			wpa_printf(MSG_DEBUG,
				   "TLSv1: Unexpected SHA256 hash length in ca_cert '%s'",
				   cert);
			return -1;
		}
		if (hexstr2bin(pos, cred->srv_cert_hash, 32) < 0) {
			wpa_printf(MSG_DEBUG,
				   "TLSv1: Invalid SHA256 hash value in ca_cert '%s'",
				   cert);
			return -1;
		}
		cred->server_cert_only = 1;
		cred->ca_cert_verify = 0;
		wpa_printf(MSG_DEBUG,
			   "TLSv1: Checking only server certificate match");
		return 0;
	}

	if (cert && os_strncmp(cert, "probe://", 8) == 0) {
		cred->cert_probe = 1;
		cred->ca_cert_verify = 0;
		wpa_printf(MSG_DEBUG, "TLSv1: Only probe server certificate");
		return 0;
	}

	cred->ca_cert_verify = cert || cert_blob || path;

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
		const u8 *pos2;
		pos += os_strlen(pem_key_begin);
		end = search_tag(pem_key_end, pos, key + len - pos);
		if (!end)
			return NULL;
		pos2 = search_tag("Proc-Type: 4,ENCRYPTED", pos, end - pos);
		if (pos2) {
			wpa_printf(MSG_DEBUG, "TLSv1: Unsupported private key "
				   "format (Proc-Type/DEK-Info)");
			return NULL;
		}
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


#ifdef PKCS12_FUNCS

static int oid_is_rsadsi(struct asn1_oid *oid)
{
	return oid->len >= 4 &&
		oid->oid[0] == 1 /* iso */ &&
		oid->oid[1] == 2 /* member-body */ &&
		oid->oid[2] == 840 /* us */ &&
		oid->oid[3] == 113549 /* rsadsi */;
}


static int pkcs12_is_bagtype_oid(struct asn1_oid *oid, unsigned long type)
{
	return oid->len == 9 &&
		oid_is_rsadsi(oid) &&
		oid->oid[4] == 1 /* pkcs */ &&
		oid->oid[5] == 12 /* pkcs-12 */ &&
		oid->oid[6] == 10 &&
		oid->oid[7] == 1 /* bagtypes */ &&
		oid->oid[8] == type;
}


static int is_oid_pkcs7(struct asn1_oid *oid)
{
	return oid->len == 7 &&
		oid->oid[0] == 1 /* iso */ &&
		oid->oid[1] == 2 /* member-body */ &&
		oid->oid[2] == 840 /* us */ &&
		oid->oid[3] == 113549 /* rsadsi */ &&
		oid->oid[4] == 1 /* pkcs */ &&
		oid->oid[5] == 7 /* pkcs-7 */;
}


static int is_oid_pkcs7_data(struct asn1_oid *oid)
{
	return is_oid_pkcs7(oid) && oid->oid[6] == 1 /* data */;
}


static int is_oid_pkcs7_enc_data(struct asn1_oid *oid)
{
	return is_oid_pkcs7(oid) && oid->oid[6] == 6 /* encryptedData */;
}


static int is_oid_pkcs9(struct asn1_oid *oid)
{
	return oid->len >= 6 &&
		oid->oid[0] == 1 /* iso */ &&
		oid->oid[1] == 2 /* member-body */ &&
		oid->oid[2] == 840 /* us */ &&
		oid->oid[3] == 113549 /* rsadsi */ &&
		oid->oid[4] == 1 /* pkcs */ &&
		oid->oid[5] == 9 /* pkcs-9 */;
}


static int is_oid_pkcs9_friendly_name(struct asn1_oid *oid)
{
	return oid->len == 7 && is_oid_pkcs9(oid) &&
		oid->oid[6] == 20;
}


static int is_oid_pkcs9_local_key_id(struct asn1_oid *oid)
{
	return oid->len == 7 && is_oid_pkcs9(oid) &&
		oid->oid[6] == 21;
}


static int is_oid_pkcs9_x509_cert(struct asn1_oid *oid)
{
	return oid->len == 8 && is_oid_pkcs9(oid) &&
		oid->oid[6] == 22 /* certTypes */ &&
		oid->oid[7] == 1 /* x509Certificate */;
}


static int pkcs12_keybag(struct tlsv1_credentials *cred,
			 const u8 *buf, size_t len)
{
	/* TODO */
	return 0;
}


static int pkcs12_pkcs8_keybag(struct tlsv1_credentials *cred,
			       const u8 *buf, size_t len,
			       const char *passwd)
{
	struct crypto_private_key *key;

	/* PKCS8ShroudedKeyBag ::= EncryptedPrivateKeyInfo */
	key = pkcs8_enc_key_import(buf, len, passwd);
	if (!key)
		return -1;

	wpa_printf(MSG_DEBUG,
		   "PKCS #12: Successfully decrypted PKCS8ShroudedKeyBag");
	crypto_private_key_free(cred->key);
	cred->key = key;

	return 0;
}


static int pkcs12_certbag(struct tlsv1_credentials *cred,
			  const u8 *buf, size_t len)
{
	struct asn1_hdr hdr;
	struct asn1_oid oid;
	char obuf[80];
	const u8 *pos, *end;

	/*
	 * CertBag ::= SEQUENCE {
	 *     certId      BAG-TYPE.&id   ({CertTypes}),
	 *     certValue   [0] EXPLICIT BAG-TYPE.&Type ({CertTypes}{@certId})
	 * }
	 */

	if (asn1_get_next(buf, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected SEQUENCE (CertBag) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	if (asn1_get_oid(pos, end - pos, &oid, &pos)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Failed to parse OID (certId)");
		return -1;
	}

	asn1_oid_to_str(&oid, obuf, sizeof(obuf));
	wpa_printf(MSG_DEBUG, "PKCS #12: certId %s", obuf);

	if (!is_oid_pkcs9_x509_cert(&oid)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Ignored unsupported certificate type (certId %s)",
			   obuf);
	}

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC ||
	    hdr.tag != 0) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected [0] EXPLICIT (certValue) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	if (asn1_get_next(hdr.payload, hdr.length, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected OCTET STRING (x509Certificate) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	wpa_hexdump(MSG_DEBUG, "PKCS #12: x509Certificate",
		    hdr.payload, hdr.length);
	if (cred->cert) {
		struct x509_certificate *cert;

		wpa_printf(MSG_DEBUG, "PKCS #12: Ignore extra certificate");
		cert = x509_certificate_parse(hdr.payload, hdr.length);
		if (!cert) {
			wpa_printf(MSG_DEBUG,
				   "PKCS #12: Failed to parse x509Certificate");
			return 0;
		}
		x509_certificate_chain_free(cert);

		return 0;
	}
	return tlsv1_set_cert(cred, NULL, hdr.payload, hdr.length);
}


static int pkcs12_parse_attr_friendly_name(const u8 *pos, const u8 *end)
{
	struct asn1_hdr hdr;

	/*
	 * RFC 2985, 5.5.1:
	 * friendlyName ATTRIBUTE ::= {
	 *         WITH SYNTAX BMPString (SIZE(1..pkcs-9-ub-friendlyName))
	 *         EQUALITY MATCHING RULE caseIgnoreMatch
	 *         SINGLE VALUE TRUE
	 *          ID pkcs-9-at-friendlyName
	 * }
	 */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_BMPSTRING) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected BMPSTRING (friendlyName) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return 0;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "PKCS #12: friendlyName",
			  hdr.payload, hdr.length);
	return 0;
}


static int pkcs12_parse_attr_local_key_id(const u8 *pos, const u8 *end)
{
	struct asn1_hdr hdr;

	/*
	 * RFC 2985, 5.5.2:
	 * localKeyId ATTRIBUTE ::= {
	 *         WITH SYNTAX OCTET STRING
	 *         EQUALITY MATCHING RULE octetStringMatch
	 *         SINGLE VALUE TRUE
	 *         ID pkcs-9-at-localKeyId
	 * }
	 */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected OCTET STRING (localKeyID) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "PKCS #12: localKeyID",
			hdr.payload, hdr.length);
	return 0;
}


static int pkcs12_parse_attr(const u8 *pos, size_t len)
{
	const u8 *end = pos + len;
	struct asn1_hdr hdr;
	struct asn1_oid a_oid;
	char obuf[80];

	/*
	 * PKCS12Attribute ::= SEQUENCE {
	 * attrId      ATTRIBUTE.&id ({PKCS12AttrSet}),
	 * attrValues  SET OF ATTRIBUTE.&Type ({PKCS12AttrSet}{@attrId})
	 * }
	 */

	if (asn1_get_oid(pos, end - pos, &a_oid, &pos)) {
		wpa_printf(MSG_DEBUG, "PKCS #12: Failed to parse OID (attrId)");
		return -1;
	}

	asn1_oid_to_str(&a_oid, obuf, sizeof(obuf));
	wpa_printf(MSG_DEBUG, "PKCS #12: attrId %s", obuf);

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SET) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected SET (attrValues) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	wpa_hexdump_key(MSG_MSGDUMP, "PKCS #12: attrValues",
			hdr.payload, hdr.length);
	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	if (is_oid_pkcs9_friendly_name(&a_oid))
		return pkcs12_parse_attr_friendly_name(pos, end);
	if (is_oid_pkcs9_local_key_id(&a_oid))
		return pkcs12_parse_attr_local_key_id(pos, end);

	wpa_printf(MSG_DEBUG, "PKCS #12: Ignore unknown attribute");
	return 0;
}


static int pkcs12_safebag(struct tlsv1_credentials *cred,
			  const u8 *buf, size_t len, const char *passwd)
{
	struct asn1_hdr hdr;
	struct asn1_oid oid;
	char obuf[80];
	const u8 *pos = buf, *end = buf + len;
	const u8 *value;
	size_t value_len;

	wpa_hexdump_key(MSG_MSGDUMP, "PKCS #12: SafeBag", buf, len);

	/* BAG-TYPE ::= TYPE-IDENTIFIER */
	if (asn1_get_oid(pos, end - pos, &oid, &pos)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Failed to parse OID (BAG-TYPE)");
		return -1;
	}

	asn1_oid_to_str(&oid, obuf, sizeof(obuf));
	wpa_printf(MSG_DEBUG, "PKCS #12: BAG-TYPE %s", obuf);

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC ||
	    hdr.tag != 0) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected [0] EXPLICIT (bagValue) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return 0;
	}
	value = hdr.payload;
	value_len = hdr.length;
	wpa_hexdump_key(MSG_MSGDUMP, "PKCS #12: bagValue", value, value_len);
	pos = hdr.payload + hdr.length;

	if (pos < end) {
		/* bagAttributes  SET OF PKCS12Attribute OPTIONAL */
		if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_UNIVERSAL ||
		    hdr.tag != ASN1_TAG_SET) {
			wpa_printf(MSG_DEBUG,
				   "PKCS #12: Expected SET (bagAttributes) - found class %d tag 0x%x",
				   hdr.class, hdr.tag);
			return -1;
		}
		wpa_hexdump_key(MSG_MSGDUMP, "PKCS #12: bagAttributes",
				hdr.payload, hdr.length);

		pos = hdr.payload;
		end = hdr.payload + hdr.length;
		while (pos < end) {
			/* PKCS12Attribute ::= SEQUENCE */
			if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
			    hdr.class != ASN1_CLASS_UNIVERSAL ||
			    hdr.tag != ASN1_TAG_SEQUENCE) {
				wpa_printf(MSG_DEBUG,
					   "PKCS #12: Expected SEQUENCE (PKCS12Attribute) - found class %d tag 0x%x",
					   hdr.class, hdr.tag);
				return -1;
			}
			if (pkcs12_parse_attr(hdr.payload, hdr.length) < 0)
				return -1;
			pos = hdr.payload + hdr.length;
		}
	}

	if (pkcs12_is_bagtype_oid(&oid, 1))
		return pkcs12_keybag(cred, value, value_len);
	if (pkcs12_is_bagtype_oid(&oid, 2))
		return pkcs12_pkcs8_keybag(cred, value, value_len, passwd);
	if (pkcs12_is_bagtype_oid(&oid, 3))
		return pkcs12_certbag(cred, value, value_len);

	wpa_printf(MSG_DEBUG, "PKCS #12: Ignore unsupported BAG-TYPE");
	return 0;
}


static int pkcs12_safecontents(struct tlsv1_credentials *cred,
			       const u8 *buf, size_t len,
			       const char *passwd)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;

	/* SafeContents ::= SEQUENCE OF SafeBag */
	if (asn1_get_next(buf, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected SEQUENCE (SafeContents) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	/*
	 * SafeBag ::= SEQUENCE {
	 *   bagId          BAG-TYPE.&id ({PKCS12BagSet})
	 *   bagValue       [0] EXPLICIT BAG-TYPE.&Type({PKCS12BagSet}{@bagId}),
	 *   bagAttributes  SET OF PKCS12Attribute OPTIONAL
	 * }
	 */

	while (pos < end) {
		if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_UNIVERSAL ||
		    hdr.tag != ASN1_TAG_SEQUENCE) {
			wpa_printf(MSG_DEBUG,
				   "PKCS #12: Expected SEQUENCE (SafeBag) - found class %d tag 0x%x",
				   hdr.class, hdr.tag);
			return -1;
		}
		if (pkcs12_safebag(cred, hdr.payload, hdr.length, passwd) < 0)
			return -1;
		pos = hdr.payload + hdr.length;
	}

	return 0;
}


static int pkcs12_parse_content_data(struct tlsv1_credentials *cred,
				     const u8 *pos, const u8 *end,
				     const char *passwd)
{
	struct asn1_hdr hdr;

	/* Data ::= OCTET STRING */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected OCTET STRING (Data) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	wpa_hexdump(MSG_MSGDUMP, "PKCS #12: Data", hdr.payload, hdr.length);

	return pkcs12_safecontents(cred, hdr.payload, hdr.length, passwd);
}


static int pkcs12_parse_content_enc_data(struct tlsv1_credentials *cred,
					 const u8 *pos, const u8 *end,
					 const char *passwd)
{
	struct asn1_hdr hdr;
	struct asn1_oid oid;
	char buf[80];
	const u8 *enc_alg;
	u8 *data;
	size_t enc_alg_len, data_len;
	int res = -1;

	/*
	 * EncryptedData ::= SEQUENCE {
	 *   version Version,
	 *   encryptedContentInfo EncryptedContentInfo }
	 */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected SEQUENCE (EncryptedData) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return 0;
	}
	pos = hdr.payload;

	/* Version ::= INTEGER */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL || hdr.tag != ASN1_TAG_INTEGER) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: No INTEGER tag found for version; class=%d tag=0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	if (hdr.length != 1 || hdr.payload[0] != 0) {
		wpa_printf(MSG_DEBUG, "PKCS #12: Unrecognized PKCS #7 version");
		return -1;
	}
	pos = hdr.payload + hdr.length;

	wpa_hexdump(MSG_MSGDUMP, "PKCS #12: EncryptedContentInfo",
		    pos, end - pos);

	/*
	 * EncryptedContentInfo ::= SEQUENCE {
	 *   contentType ContentType,
	 *   contentEncryptionAlgorithm ContentEncryptionAlgorithmIdentifier,
	 *   encryptedContent [0] IMPLICIT EncryptedContent OPTIONAL }
	 */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected SEQUENCE (EncryptedContentInfo) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	end = pos + hdr.length;

	/* ContentType ::= OBJECT IDENTIFIER */
	if (asn1_get_oid(pos, end - pos, &oid, &pos)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Could not find OBJECT IDENTIFIER (contentType)");
		return -1;
	}
	asn1_oid_to_str(&oid, buf, sizeof(buf));
	wpa_printf(MSG_DEBUG, "PKCS #12: EncryptedContentInfo::contentType %s",
		   buf);

	if (!is_oid_pkcs7_data(&oid)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Unsupported EncryptedContentInfo::contentType %s",
			   buf);
		return 0;
	}

	/* ContentEncryptionAlgorithmIdentifier ::= AlgorithmIdentifier */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "PKCS #12: Expected SEQUENCE (ContentEncryptionAlgorithmIdentifier) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	enc_alg = hdr.payload;
	enc_alg_len = hdr.length;
	pos = hdr.payload + hdr.length;

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC ||
	    hdr.tag != 0) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected [0] IMPLICIT (encryptedContent) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	/* EncryptedContent ::= OCTET STRING */
	data = pkcs5_decrypt(enc_alg, enc_alg_len, hdr.payload, hdr.length,
			     passwd, &data_len);
	if (data) {
		wpa_hexdump_key(MSG_MSGDUMP,
				"PKCS #12: Decrypted encryptedContent",
				data, data_len);
		res = pkcs12_safecontents(cred, data, data_len, passwd);
		os_free(data);
	}

	return res;
}


static int pkcs12_parse_content(struct tlsv1_credentials *cred,
				const u8 *buf, size_t len,
				const char *passwd)
{
	const u8 *pos = buf;
	const u8 *end = buf + len;
	struct asn1_oid oid;
	char txt[80];
	struct asn1_hdr hdr;

	wpa_hexdump(MSG_MSGDUMP, "PKCS #12: ContentInfo", buf, len);

	if (asn1_get_oid(pos, end - pos, &oid, &pos)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Could not find OBJECT IDENTIFIER (contentType)");
		return 0;
	}

	asn1_oid_to_str(&oid, txt, sizeof(txt));
	wpa_printf(MSG_DEBUG, "PKCS #12: contentType %s", txt);

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC ||
	    hdr.tag != 0) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected [0] EXPLICIT (content) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return 0;
	}
	pos = hdr.payload;

	if (is_oid_pkcs7_data(&oid))
		return pkcs12_parse_content_data(cred, pos, end, passwd);
	if (is_oid_pkcs7_enc_data(&oid))
		return pkcs12_parse_content_enc_data(cred, pos, end, passwd);

	wpa_printf(MSG_DEBUG, "PKCS #12: Ignored unsupported contentType %s",
		   txt);

	return 0;
}


static int pkcs12_parse(struct tlsv1_credentials *cred,
			const u8 *key, size_t len, const char *passwd)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;
	struct asn1_oid oid;
	char buf[80];

	/*
	 * PFX ::= SEQUENCE {
	 *     version     INTEGER {v3(3)}(v3,...),
	 *     authSafe    ContentInfo,
	 *     macData     MacData OPTIONAL
	 * }
	 */

	if (asn1_get_next(key, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected SEQUENCE (PFX) - found class %d tag 0x%x; assume PKCS #12 not used",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	end = pos + hdr.length;

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL || hdr.tag != ASN1_TAG_INTEGER) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: No INTEGER tag found for version; class=%d tag=0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	if (hdr.length != 1 || hdr.payload[0] != 3) {
		wpa_printf(MSG_DEBUG, "PKCS #12: Unrecognized version");
		return -1;
	}
	pos = hdr.payload + hdr.length;

	/*
	 * ContentInfo ::= SEQUENCE {
	 *   contentType ContentType,
	 *   content [0] EXPLICIT ANY DEFINED BY contentType OPTIONAL }
	 */

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected SEQUENCE (authSafe) - found class %d tag 0x%x; assume PKCS #12 not used",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	end = pos + hdr.length;

	/* ContentType ::= OBJECT IDENTIFIER */
	if (asn1_get_oid(pos, end - pos, &oid, &pos)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Could not find OBJECT IDENTIFIER (contentType); assume PKCS #12 not used");
		return -1;
	}
	asn1_oid_to_str(&oid, buf, sizeof(buf));
	wpa_printf(MSG_DEBUG, "PKCS #12: contentType %s", buf);
	if (!is_oid_pkcs7_data(&oid)) {
		wpa_printf(MSG_DEBUG, "PKCS #12: Unsupported contentType %s",
			   buf);
		return -1;
	}

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC ||
	    hdr.tag != 0) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected [0] EXPLICIT (content) - found class %d tag 0x%x; assume PKCS #12 not used",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;

	/* Data ::= OCTET STRING */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected OCTET STRING (Data) - found class %d tag 0x%x; assume PKCS #12 not used",
			   hdr.class, hdr.tag);
		return -1;
	}

	/*
	 * AuthenticatedSafe ::= SEQUENCE OF ContentInfo
	 *     -- Data if unencrypted
	 *     -- EncryptedData if password-encrypted
	 *     -- EnvelopedData if public key-encrypted
	 */
	wpa_hexdump(MSG_MSGDUMP, "PKCS #12: Data content",
		    hdr.payload, hdr.length);

	if (asn1_get_next(hdr.payload, hdr.length, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #12: Expected SEQUENCE within Data content - found class %d tag 0x%x; assume PKCS #12 not used",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	end = pos + hdr.length;

	while (end > pos) {
		if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_UNIVERSAL ||
		    hdr.tag != ASN1_TAG_SEQUENCE) {
			wpa_printf(MSG_DEBUG,
				   "PKCS #12: Expected SEQUENCE (ContentInfo) - found class %d tag 0x%x; assume PKCS #12 not used",
				   hdr.class, hdr.tag);
			return -1;
		}
		if (pkcs12_parse_content(cred, hdr.payload, hdr.length,
					 passwd) < 0)
			return -1;

		pos = hdr.payload + hdr.length;
	}

	return 0;
}

#endif /* PKCS12_FUNCS */


static int tlsv1_set_key(struct tlsv1_credentials *cred,
			 const u8 *key, size_t len, const char *passwd)
{
	cred->key = crypto_private_key_import(key, len, passwd);
	if (cred->key == NULL)
		cred->key = tlsv1_set_key_pem(key, len);
	if (cred->key == NULL)
		cred->key = tlsv1_set_key_enc_pem(key, len, passwd);
#ifdef PKCS12_FUNCS
	if (!cred->key)
		pkcs12_parse(cred, key, len, passwd);
#endif /* PKCS12_FUNCS */
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
	cred->dh_p = os_memdup(hdr.payload, hdr.length);
	if (cred->dh_p == NULL)
		return -1;
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
	cred->dh_g = os_memdup(hdr.payload, hdr.length);
	if (cred->dh_g == NULL)
		return -1;
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
