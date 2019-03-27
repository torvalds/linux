/*
 * PKCS #5 (Password-based Encryption)
 * Copyright (c) 2009-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/crypto.h"
#include "crypto/md5.h"
#include "crypto/sha1.h"
#include "asn1.h"
#include "pkcs5.h"


struct pkcs5_params {
	enum pkcs5_alg {
		PKCS5_ALG_UNKNOWN,
		PKCS5_ALG_MD5_DES_CBC,
		PKCS5_ALG_PBES2,
		PKCS5_ALG_SHA1_3DES_CBC,
	} alg;
	u8 salt[64];
	size_t salt_len;
	unsigned int iter_count;
	enum pbes2_enc_alg {
		PBES2_ENC_ALG_UNKNOWN,
		PBES2_ENC_ALG_DES_EDE3_CBC,
	} enc_alg;
	u8 iv[8];
	size_t iv_len;
};


static int oid_is_rsadsi(struct asn1_oid *oid)
{
	return oid->len >= 4 &&
		oid->oid[0] == 1 /* iso */ &&
		oid->oid[1] == 2 /* member-body */ &&
		oid->oid[2] == 840 /* us */ &&
		oid->oid[3] == 113549 /* rsadsi */;
}


static int pkcs5_is_oid(struct asn1_oid *oid, unsigned long alg)
{
	return oid->len == 7 &&
		oid_is_rsadsi(oid) &&
		oid->oid[4] == 1 /* pkcs */ &&
		oid->oid[5] == 5 /* pkcs-5 */ &&
		oid->oid[6] == alg;
}


static int enc_alg_is_oid(struct asn1_oid *oid, unsigned long alg)
{
	return oid->len == 6 &&
		oid_is_rsadsi(oid) &&
		oid->oid[4] == 3 /* encryptionAlgorithm */ &&
		oid->oid[5] == alg;
}


static int pkcs12_is_pbe_oid(struct asn1_oid *oid, unsigned long alg)
{
	return oid->len == 8 &&
		oid_is_rsadsi(oid) &&
		oid->oid[4] == 1 /* pkcs */ &&
		oid->oid[5] == 12 /* pkcs-12 */ &&
		oid->oid[6] == 1 /* pkcs-12PbeIds */ &&
		oid->oid[7] == alg;
}


static enum pkcs5_alg pkcs5_get_alg(struct asn1_oid *oid)
{
	if (pkcs5_is_oid(oid, 3)) /* pbeWithMD5AndDES-CBC (PBES1) */
		return PKCS5_ALG_MD5_DES_CBC;
	if (pkcs12_is_pbe_oid(oid, 3)) /* pbeWithSHAAnd3-KeyTripleDES-CBC */
		return PKCS5_ALG_SHA1_3DES_CBC;
	if (pkcs5_is_oid(oid, 13)) /* id-PBES2 (PBES2) */
		return PKCS5_ALG_PBES2;
	return PKCS5_ALG_UNKNOWN;
}


static int pkcs5_get_params_pbes2(struct pkcs5_params *params, const u8 *pos,
				  const u8 *enc_alg_end)
{
	struct asn1_hdr hdr;
	const u8 *end, *kdf_end;
	struct asn1_oid oid;
	char obuf[80];

	/*
	 * RFC 2898, Ch. A.4
	 *
	 * PBES2-params ::= SEQUENCE {
	 *     keyDerivationFunc AlgorithmIdentifier {{PBES2-KDFs}},
	 *     encryptionScheme AlgorithmIdentifier {{PBES2-Encs}} }
	 *
	 * PBES2-KDFs ALGORITHM-IDENTIFIER ::=
	 *     { {PBKDF2-params IDENTIFIED BY id-PBKDF2}, ... }
	 */

	if (asn1_get_next(pos, enc_alg_end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Expected SEQUENCE (PBES2-params) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Expected SEQUENCE (keyDerivationFunc) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	kdf_end = end = hdr.payload + hdr.length;

	if (asn1_get_oid(pos, end - pos, &oid, &pos)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Failed to parse OID (keyDerivationFunc algorithm)");
		return -1;
	}

	asn1_oid_to_str(&oid, obuf, sizeof(obuf));
	wpa_printf(MSG_DEBUG, "PKCS #5: PBES2 keyDerivationFunc algorithm %s",
		   obuf);
	if (!pkcs5_is_oid(&oid, 12)) /* id-PBKDF2 */ {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Unsupported PBES2 keyDerivationFunc algorithm %s",
			   obuf);
		return -1;
	}

	/*
	 * RFC 2898, C.
	 *
	 * PBKDF2-params ::= SEQUENCE {
	 *     salt CHOICE {
	 *       specified OCTET STRING,
	 *       otherSource AlgorithmIdentifier {{PBKDF2-SaltSources}}
	 *     },
	 *     iterationCount INTEGER (1..MAX),
	 *     keyLength INTEGER (1..MAX) OPTIONAL,
	 *     prf AlgorithmIdentifier {{PBKDF2-PRFs}} DEFAULT
	 *     algid-hmacWithSHA1
	 * }
	 */

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Expected SEQUENCE (PBKDF2-params) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	/* For now, only support the salt CHOICE specified (OCTET STRING) */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING ||
	    hdr.length > sizeof(params->salt)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Expected OCTET STRING (salt.specified) - found class %d tag 0x%x size %d",
			   hdr.class, hdr.tag, hdr.length);
		return -1;
	}
	pos = hdr.payload + hdr.length;
	os_memcpy(params->salt, hdr.payload, hdr.length);
	params->salt_len = hdr.length;
	wpa_hexdump(MSG_DEBUG, "PKCS #5: salt", params->salt, params->salt_len);

	/* iterationCount INTEGER */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL || hdr.tag != ASN1_TAG_INTEGER) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Expected INTEGER - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	if (hdr.length == 1) {
		params->iter_count = *hdr.payload;
	} else if (hdr.length == 2) {
		params->iter_count = WPA_GET_BE16(hdr.payload);
	} else if (hdr.length == 4) {
		params->iter_count = WPA_GET_BE32(hdr.payload);
	} else {
		wpa_hexdump(MSG_DEBUG,
			    "PKCS #5: Unsupported INTEGER value (iterationCount)",
			    hdr.payload, hdr.length);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "PKCS #5: iterationCount=0x%x",
		   params->iter_count);
	if (params->iter_count == 0 || params->iter_count > 0xffff) {
		wpa_printf(MSG_INFO, "PKCS #5: Unsupported iterationCount=0x%x",
			   params->iter_count);
		return -1;
	}

	/* For now, ignore optional keyLength and prf */

	pos = kdf_end;

	/* encryptionScheme AlgorithmIdentifier {{PBES2-Encs}} */

	if (asn1_get_next(pos, enc_alg_end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Expected SEQUENCE (encryptionScheme) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	if (asn1_get_oid(pos, end - pos, &oid, &pos)) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Failed to parse OID (encryptionScheme algorithm)");
		return -1;
	}

	asn1_oid_to_str(&oid, obuf, sizeof(obuf));
	wpa_printf(MSG_DEBUG, "PKCS #5: PBES2 encryptionScheme algorithm %s",
		   obuf);
	if (enc_alg_is_oid(&oid, 7)) {
		params->enc_alg = PBES2_ENC_ALG_DES_EDE3_CBC;
	} else {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Unsupported PBES2 encryptionScheme algorithm %s",
			   obuf);
		return -1;
	}

	/*
	 * RFC 2898, B.2.2:
	 * The parameters field associated with this OID in an
	 * AlgorithmIdentifier shall have type OCTET STRING (SIZE(8)),
	 * specifying the initialization vector for CBC mode.
	 */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING ||
	    hdr.length != 8) {
		wpa_printf(MSG_DEBUG,
			   "PKCS #5: Expected OCTET STRING (SIZE(8)) (IV) - found class %d tag 0x%x size %d",
			   hdr.class, hdr.tag, hdr.length);
		return -1;
	}
	os_memcpy(params->iv, hdr.payload, hdr.length);
	params->iv_len = hdr.length;
	wpa_hexdump(MSG_DEBUG, "PKCS #5: IV", params->iv, params->iv_len);

	return 0;
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

	if (params->alg == PKCS5_ALG_PBES2)
		return pkcs5_get_params_pbes2(params, pos, enc_alg_end);

	/* PBES1 */

	/*
	 * PKCS#5, Section 8
	 * PBEParameter ::= SEQUENCE {
	 *   salt OCTET STRING SIZE(8),
	 *   iterationCount INTEGER }
	 *
	 * Note: The same implementation can be used to parse the PKCS #12
	 * version described in RFC 7292, C:
	 * pkcs-12PbeParams ::= SEQUENCE {
	 *     salt        OCTET STRING,
	 *     iterations  INTEGER
	 * }
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

	/* salt OCTET STRING SIZE(8) (PKCS #5) or OCTET STRING (PKCS #12) */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING ||
	    hdr.length > sizeof(params->salt)) {
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


static struct crypto_cipher *
pkcs5_crypto_init_pbes2(struct pkcs5_params *params, const char *passwd)
{
	u8 key[24];

	if (params->enc_alg != PBES2_ENC_ALG_DES_EDE3_CBC ||
	    params->iv_len != 8)
		return NULL;

	wpa_hexdump_ascii_key(MSG_DEBUG, "PKCS #5: PBES2 password for PBKDF2",
			      passwd, os_strlen(passwd));
	wpa_hexdump(MSG_DEBUG, "PKCS #5: PBES2 salt for PBKDF2",
		    params->salt, params->salt_len);
	wpa_printf(MSG_DEBUG, "PKCS #5: PBES2 PBKDF2 iterations: %u",
		   params->iter_count);
	if (pbkdf2_sha1(passwd, params->salt, params->salt_len,
			params->iter_count, key, sizeof(key)) < 0)
		return NULL;
	wpa_hexdump_key(MSG_DEBUG, "PKCS #5: DES EDE3 key", key, sizeof(key));
	wpa_hexdump(MSG_DEBUG, "PKCS #5: DES IV", params->iv, params->iv_len);

	return crypto_cipher_init(CRYPTO_CIPHER_ALG_3DES, params->iv,
				  key, sizeof(key));
}


static void add_byte_array_mod(u8 *a, const u8 *b, size_t len)
{
	size_t i;
	unsigned int carry = 0;

	for (i = len - 1; i < len; i--) {
		carry = carry + a[i] + b[i];
		a[i] = carry & 0xff;
		carry >>= 8;
	}
}


static int pkcs12_key_gen(const u8 *pw, size_t pw_len, const u8 *salt,
			  size_t salt_len, u8 id, unsigned int iter,
			  size_t out_len, u8 *out)
{
	unsigned int u, v, S_len, P_len, i;
	u8 *D = NULL, *I = NULL, *B = NULL, *pos;
	int res = -1;

	/* RFC 7292, B.2 */
	u = SHA1_MAC_LEN;
	v = 64;

	/* D = copies of ID */
	D = os_malloc(v);
	if (!D)
		goto done;
	os_memset(D, id, v);

	/* S = copies of salt; P = copies of password, I = S || P */
	S_len = v * ((salt_len + v - 1) / v);
	P_len = v * ((pw_len + v - 1) / v);
	I = os_malloc(S_len + P_len);
	if (!I)
		goto done;
	pos = I;
	if (salt_len) {
		for (i = 0; i < S_len; i++)
			*pos++ = salt[i % salt_len];
	}
	if (pw_len) {
		for (i = 0; i < P_len; i++)
			*pos++ = pw[i % pw_len];
	}

	B = os_malloc(v);
	if (!B)
		goto done;

	for (;;) {
		u8 hash[SHA1_MAC_LEN];
		const u8 *addr[2];
		size_t len[2];

		addr[0] = D;
		len[0] = v;
		addr[1] = I;
		len[1] = S_len + P_len;
		if (sha1_vector(2, addr, len, hash) < 0)
			goto done;

		addr[0] = hash;
		len[0] = SHA1_MAC_LEN;
		for (i = 1; i < iter; i++) {
			if (sha1_vector(1, addr, len, hash) < 0)
				goto done;
		}

		if (out_len <= u) {
			os_memcpy(out, hash, out_len);
			res = 0;
			goto done;
		}

		os_memcpy(out, hash, u);
		out += u;
		out_len -= u;

		/* I_j = (I_j + B + 1) mod 2^(v*8) */
		/* B = copies of Ai (final hash value) */
		for (i = 0; i < v; i++)
			B[i] = hash[i % u];
		inc_byte_array(B, v);
		for (i = 0; i < S_len + P_len; i += v)
			add_byte_array_mod(&I[i], B, v);
	}

done:
	os_free(B);
	os_free(I);
	os_free(D);
	return res;
}


#define PKCS12_ID_ENC 1
#define PKCS12_ID_IV 2
#define PKCS12_ID_MAC 3

static struct crypto_cipher *
pkcs12_crypto_init_sha1(struct pkcs5_params *params, const char *passwd)
{
	unsigned int i;
	u8 *pw;
	size_t pw_len;
	u8 key[24];
	u8 iv[8];

	if (params->alg != PKCS5_ALG_SHA1_3DES_CBC)
		return NULL;

	pw_len = passwd ? os_strlen(passwd) : 0;
	pw = os_malloc(2 * (pw_len + 1));
	if (!pw)
		return NULL;
	if (pw_len) {
		for (i = 0; i <= pw_len; i++)
			WPA_PUT_BE16(&pw[2 * i], passwd[i]);
		pw_len = 2 * (pw_len + 1);
	}

	if (pkcs12_key_gen(pw, pw_len, params->salt, params->salt_len,
			   PKCS12_ID_ENC, params->iter_count,
			   sizeof(key), key) < 0 ||
	    pkcs12_key_gen(pw, pw_len, params->salt, params->salt_len,
			   PKCS12_ID_IV, params->iter_count,
			   sizeof(iv), iv) < 0) {
		os_free(pw);
		return NULL;
	}

	os_free(pw);

	wpa_hexdump_key(MSG_DEBUG, "PKCS #12: DES key", key, sizeof(key));
	wpa_hexdump_key(MSG_DEBUG, "PKCS #12: DES IV", iv, sizeof(iv));

	return crypto_cipher_init(CRYPTO_CIPHER_ALG_3DES, iv, key, sizeof(key));
}


static struct crypto_cipher * pkcs5_crypto_init(struct pkcs5_params *params,
						const char *passwd)
{
	unsigned int i;
	u8 hash[MD5_MAC_LEN];
	const u8 *addr[2];
	size_t len[2];

	if (params->alg == PKCS5_ALG_PBES2)
		return pkcs5_crypto_init_pbes2(params, passwd);

	if (params->alg == PKCS5_ALG_SHA1_3DES_CBC)
		return pkcs12_crypto_init_sha1(params, passwd);

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
