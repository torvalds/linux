/*
 * PKCS #5 (Password-based Encryption)
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
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
#include "crypto/crypto.h"
#include "crypto/md5.h"
#include "asn1.h"
#include "pkcs5.h"


struct pkcs5_params {
	enum pkcs5_alg {
		PKCS5_ALG_UNKNOWN,
		PKCS5_ALG_MD5_DES_CBC
	} alg;
	u8 salt[8];
	size_t salt_len;
	unsigned int iter_count;
};


enum pkcs5_alg pkcs5_get_alg(struct asn1_oid *oid)
{
	if (oid->len == 7 &&
	    oid->oid[0] == 1 /* iso */ &&
	    oid->oid[1] == 2 /* member-body */ &&
	    oid->oid[2] == 840 /* us */ &&
	    oid->oid[3] == 113549 /* rsadsi */ &&
	    oid->oid[4] == 1 /* pkcs */ &&
	    oid->oid[5] == 5 /* pkcs-5 */ &&
	    oid->oid[6] == 3 /* pbeWithMD5AndDES-CBC */)
		return PKCS5_ALG_MD5_DES_CBC;

	return PKCS5_ALG_UNKNOWN;
}


static int pkcs5_get_params(const u8 *enc_alg, size_t enc_alg_len,
			    struct pkcs5_params *params)
{
	struct asn1_hdr hdr;
	const u8 *enc_alg_end, *pos, *end;
	struct asn1_oid oid;
	char obuf[80];

	/* AlgorithmIdentifier */

	enc_alg_end = enc_alg + enc_alg_len;

	os_memset(params, 0, sizeof(*params));

	if (asn1_get_oid(enc_alg, enc_alg_end - enc_alg, &oid, &pos)) {
		wpa_printf(MSG_DEBUG, "PKCS #5: Failed to parse OID "
			   "(algorithm)");
		return -1;
	}

	asn1_oid_to_str(&oid, obuf, sizeof(obuf));
	wpa_printf(MSG_DEBUG, "PKCS #5: encryption algorithm %s", obuf);
	params->alg = pkcs5_get_alg(&oid);
	if (params->alg == PKCS5_ALG_UNKNOWN) {
		wpa_printf(MSG_INFO, "PKCS #5: unsupported encryption "
			   "algorithm %s", obuf);
		return -1;
	}

	/*
	 * PKCS#5, Section 8
	 * PBEParameter ::= SEQUENCE {
	 *   salt OCTET STRING SIZE(8),
	 *   iterationCount INTEGER }
	 */

	if (asn1_get_next(pos, enc_alg_end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "PKCS #5: Expected SEQUENCE "
			   "(PBEParameter) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	/* salt OCTET STRING SIZE(8) */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING ||
	    hdr.length != 8) {
		wpa_printf(MSG_DEBUG, "PKCS #5: Expected OCTETSTRING SIZE(8) "
			   "(salt) - found class %d tag 0x%x size %d",
			   hdr.class, hdr.tag, hdr.length);
		return -1;
	}
	pos = hdr.payload + hdr.length;
	os_memcpy(params->salt, hdr.payload, hdr.length);
	params->salt_len = hdr.length;
	wpa_hexdump(MSG_DEBUG, "PKCS #5: salt",
		    params->salt, params->salt_len);

	/* iterationCount INTEGER */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL || hdr.tag != ASN1_TAG_INTEGER) {
		wpa_printf(MSG_DEBUG, "PKCS #5: Expected INTEGER - found "
			   "class %d tag 0x%x", hdr.class, hdr.tag);
		return -1;
	}
	if (hdr.length == 1)
		params->iter_count = *hdr.payload;
	else if (hdr.length == 2)
		params->iter_count = WPA_GET_BE16(hdr.payload);
	else if (hdr.length == 4)
		params->iter_count = WPA_GET_BE32(hdr.payload);
	else {
		wpa_hexdump(MSG_DEBUG, "PKCS #5: Unsupported INTEGER value "
			    " (iterationCount)",
			    hdr.payload, hdr.length);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "PKCS #5: iterationCount=0x%x",
		   params->iter_count);
	if (params->iter_count == 0 || params->iter_count > 0xffff) {
		wpa_printf(MSG_INFO, "PKCS #5: Unsupported "
			   "iterationCount=0x%x", params->iter_count);
		return -1;
	}

	return 0;
}


static struct crypto_cipher * pkcs5_crypto_init(struct pkcs5_params *params,
						const char *passwd)
{
	unsigned int i;
	u8 hash[MD5_MAC_LEN];
	const u8 *addr[2];
	size_t len[2];

	if (params->alg != PKCS5_ALG_MD5_DES_CBC)
		return NULL;

	addr[0] = (const u8 *) passwd;
	len[0] = os_strlen(passwd);
	addr[1] = params->salt;
	len[1] = params->salt_len;
	if (md5_vector(2, addr, len, hash) < 0)
		return NULL;
	addr[0] = hash;
	len[0] = MD5_MAC_LEN;
	for (i = 1; i < params->iter_count; i++) {
		if (md5_vector(1, addr, len, hash) < 0)
			return NULL;
	}
	/* TODO: DES key parity bits(?) */
	wpa_hexdump_key(MSG_DEBUG, "PKCS #5: DES key", hash, 8);
	wpa_hexdump_key(MSG_DEBUG, "PKCS #5: DES IV", hash + 8, 8);

	return crypto_cipher_init(CRYPTO_CIPHER_ALG_DES, hash + 8, hash, 8);
}


u8 * pkcs5_decrypt(const u8 *enc_alg, size_t enc_alg_len,
		   const u8 *enc_data, size_t enc_data_len,
		   const char *passwd, size_t *data_len)
{
	struct crypto_cipher *ctx;
	u8 *eb, pad;
	struct pkcs5_params params;
	unsigned int i;

	if (pkcs5_get_params(enc_alg, enc_alg_len, &params) < 0) {
		wpa_printf(MSG_DEBUG, "PKCS #5: Unsupported parameters");
		return NULL;
	}

	ctx = pkcs5_crypto_init(&params, passwd);
	if (ctx == NULL) {
		wpa_printf(MSG_DEBUG, "PKCS #5: Failed to initialize crypto");
		return NULL;
	}

	/* PKCS #5, Section 7 - Decryption process */
	if (enc_data_len < 16 || enc_data_len % 8) {
		wpa_printf(MSG_INFO, "PKCS #5: invalid length of ciphertext "
			   "%d", (int) enc_data_len);
		crypto_cipher_deinit(ctx);
		return NULL;
	}

	eb = os_malloc(enc_data_len);
	if (eb == NULL) {
		crypto_cipher_deinit(ctx);
		return NULL;
	}

	if (crypto_cipher_decrypt(ctx, enc_data, eb, enc_data_len) < 0) {
		wpa_printf(MSG_DEBUG, "PKCS #5: Failed to decrypt EB");
		crypto_cipher_deinit(ctx);
		os_free(eb);
		return NULL;
	}
	crypto_cipher_deinit(ctx);

	pad = eb[enc_data_len - 1];
	if (pad > 8) {
		wpa_printf(MSG_INFO, "PKCS #5: Invalid PS octet 0x%x", pad);
		os_free(eb);
		return NULL;
	}
	for (i = enc_data_len - pad; i < enc_data_len; i++) {
		if (eb[i] != pad) {
			wpa_hexdump(MSG_INFO, "PKCS #5: Invalid PS",
				    eb + enc_data_len - pad, pad);
			os_free(eb);
			return NULL;
		}
	}

	wpa_hexdump_key(MSG_MSGDUMP, "PKCS #5: message M (encrypted key)",
			eb, enc_data_len - pad);

	*data_len = enc_data_len - pad;
	return eb;
}
