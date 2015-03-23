/*
 * PKCS #8 (Private-key information syntax)
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
#include "asn1.h"
#include "bignum.h"
#include "rsa.h"
#include "pkcs5.h"
#include "pkcs8.h"


struct crypto_private_key * pkcs8_key_import(const u8 *buf, size_t len)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;
	struct bignum *zero;
	struct asn1_oid oid;
	char obuf[80];

	/* PKCS #8, Chapter 6 */

	/* PrivateKeyInfo ::= SEQUENCE */
	if (asn1_get_next(buf, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Does not start with PKCS #8 "
			   "header (SEQUENCE); assume PKCS #8 not used");
		return NULL;
	}
	pos = hdr.payload;
	end = pos + hdr.length;

	/* version Version (Version ::= INTEGER) */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL || hdr.tag != ASN1_TAG_INTEGER) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Expected INTEGER - found "
			   "class %d tag 0x%x; assume PKCS #8 not used",
			   hdr.class, hdr.tag);
		return NULL;
	}

	zero = bignum_init();
	if (zero == NULL)
		return NULL;

	if (bignum_set_unsigned_bin(zero, hdr.payload, hdr.length) < 0) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Failed to parse INTEGER");
		bignum_deinit(zero);
		return NULL;
	}
	pos = hdr.payload + hdr.length;

	if (bignum_cmp_d(zero, 0) != 0) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Expected zero INTEGER in the "
			   "beginning of private key; not found; assume "
			   "PKCS #8 not used");
		bignum_deinit(zero);
		return NULL;
	}
	bignum_deinit(zero);

	/* privateKeyAlgorithm PrivateKeyAlgorithmIdentifier
	 * (PrivateKeyAlgorithmIdentifier ::= AlgorithmIdentifier) */
	if (asn1_get_next(pos, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Expected SEQUENCE "
			   "(AlgorithmIdentifier) - found class %d tag 0x%x; "
			   "assume PKCS #8 not used",
			   hdr.class, hdr.tag);
		return NULL;
	}

	if (asn1_get_oid(hdr.payload, hdr.length, &oid, &pos)) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Failed to parse OID "
			   "(algorithm); assume PKCS #8 not used");
		return NULL;
	}

	asn1_oid_to_str(&oid, obuf, sizeof(obuf));
	wpa_printf(MSG_DEBUG, "PKCS #8: algorithm=%s", obuf);

	if (oid.len != 7 ||
	    oid.oid[0] != 1 /* iso */ ||
	    oid.oid[1] != 2 /* member-body */ ||
	    oid.oid[2] != 840 /* us */ ||
	    oid.oid[3] != 113549 /* rsadsi */ ||
	    oid.oid[4] != 1 /* pkcs */ ||
	    oid.oid[5] != 1 /* pkcs-1 */ ||
	    oid.oid[6] != 1 /* rsaEncryption */) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Unsupported private key "
			   "algorithm %s", obuf);
		return NULL;
	}

	pos = hdr.payload + hdr.length;

	/* privateKey PrivateKey (PrivateKey ::= OCTET STRING) */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Expected OCTETSTRING "
			   "(privateKey) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "PKCS #8: Try to parse RSAPrivateKey");

	return (struct crypto_private_key *)
		crypto_rsa_import_private_key(hdr.payload, hdr.length);
}


struct crypto_private_key *
pkcs8_enc_key_import(const u8 *buf, size_t len, const char *passwd)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end, *enc_alg;
	size_t enc_alg_len;
	u8 *data;
	size_t data_len;

	if (passwd == NULL)
		return NULL;

	/*
	 * PKCS #8, Chapter 7
	 * EncryptedPrivateKeyInfo ::= SEQUENCE {
	 *   encryptionAlgorithm EncryptionAlgorithmIdentifier,
	 *   encryptedData EncryptedData }
	 * EncryptionAlgorithmIdentifier ::= AlgorithmIdentifier
	 * EncryptedData ::= OCTET STRING
	 */

	if (asn1_get_next(buf, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Does not start with PKCS #8 "
			   "header (SEQUENCE); assume encrypted PKCS #8 not "
			   "used");
		return NULL;
	}
	pos = hdr.payload;
	end = pos + hdr.length;

	/* encryptionAlgorithm EncryptionAlgorithmIdentifier */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Expected SEQUENCE "
			   "(AlgorithmIdentifier) - found class %d tag 0x%x; "
			   "assume encrypted PKCS #8 not used",
			   hdr.class, hdr.tag);
		return NULL;
	}
	enc_alg = hdr.payload;
	enc_alg_len = hdr.length;
	pos = hdr.payload + hdr.length;

	/* encryptedData EncryptedData */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG, "PKCS #8: Expected OCTETSTRING "
			   "(encryptedData) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return NULL;
	}

	data = pkcs5_decrypt(enc_alg, enc_alg_len, hdr.payload, hdr.length,
			     passwd, &data_len);
	if (data) {
		struct crypto_private_key *key;
		key = pkcs8_key_import(data, data_len);
		os_free(data);
		return key;
	}

	return NULL;
}
