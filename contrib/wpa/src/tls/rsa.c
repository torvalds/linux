/*
 * RSA
 * Copyright (c) 2006-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "asn1.h"
#include "bignum.h"
#include "rsa.h"


struct crypto_rsa_key {
	int private_key; /* whether private key is set */
	struct bignum *n; /* modulus (p * q) */
	struct bignum *e; /* public exponent */
	/* The following parameters are available only if private_key is set */
	struct bignum *d; /* private exponent */
	struct bignum *p; /* prime p (factor of n) */
	struct bignum *q; /* prime q (factor of n) */
	struct bignum *dmp1; /* d mod (p - 1); CRT exponent */
	struct bignum *dmq1; /* d mod (q - 1); CRT exponent */
	struct bignum *iqmp; /* 1 / q mod p; CRT coefficient */
};


static const u8 * crypto_rsa_parse_integer(const u8 *pos, const u8 *end,
					   struct bignum *num)
{
	struct asn1_hdr hdr;

	if (pos == NULL)
		return NULL;

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL || hdr.tag != ASN1_TAG_INTEGER) {
		wpa_printf(MSG_DEBUG, "RSA: Expected INTEGER - found class %d "
			   "tag 0x%x", hdr.class, hdr.tag);
		return NULL;
	}

	if (bignum_set_unsigned_bin(num, hdr.payload, hdr.length) < 0) {
		wpa_printf(MSG_DEBUG, "RSA: Failed to parse INTEGER");
		return NULL;
	}

	return hdr.payload + hdr.length;
}


/**
 * crypto_rsa_import_public_key - Import an RSA public key
 * @buf: Key buffer (DER encoded RSA public key)
 * @len: Key buffer length in bytes
 * Returns: Pointer to the public key or %NULL on failure
 */
struct crypto_rsa_key *
crypto_rsa_import_public_key(const u8 *buf, size_t len)
{
	struct crypto_rsa_key *key;
	struct asn1_hdr hdr;
	const u8 *pos, *end;

	key = os_zalloc(sizeof(*key));
	if (key == NULL)
		return NULL;

	key->n = bignum_init();
	key->e = bignum_init();
	if (key->n == NULL || key->e == NULL) {
		crypto_rsa_free(key);
		return NULL;
	}

	/*
	 * PKCS #1, 7.1:
	 * RSAPublicKey ::= SEQUENCE {
	 *     modulus INTEGER, -- n
	 *     publicExponent INTEGER -- e
	 * }
	 */

	if (asn1_get_next(buf, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "RSA: Expected SEQUENCE "
			   "(public key) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		goto error;
	}
	pos = hdr.payload;
	end = pos + hdr.length;

	pos = crypto_rsa_parse_integer(pos, end, key->n);
	pos = crypto_rsa_parse_integer(pos, end, key->e);

	if (pos == NULL)
		goto error;

	if (pos != end) {
		wpa_hexdump(MSG_DEBUG,
			    "RSA: Extra data in public key SEQUENCE",
			    pos, end - pos);
		goto error;
	}

	return key;

error:
	crypto_rsa_free(key);
	return NULL;
}


struct crypto_rsa_key *
crypto_rsa_import_public_key_parts(const u8 *n, size_t n_len,
				   const u8 *e, size_t e_len)
{
	struct crypto_rsa_key *key;

	key = os_zalloc(sizeof(*key));
	if (key == NULL)
		return NULL;

	key->n = bignum_init();
	key->e = bignum_init();
	if (key->n == NULL || key->e == NULL ||
	    bignum_set_unsigned_bin(key->n, n, n_len) < 0 ||
	    bignum_set_unsigned_bin(key->e, e, e_len) < 0) {
		crypto_rsa_free(key);
		return NULL;
	}

	return key;
}


/**
 * crypto_rsa_import_private_key - Import an RSA private key
 * @buf: Key buffer (DER encoded RSA private key)
 * @len: Key buffer length in bytes
 * Returns: Pointer to the private key or %NULL on failure
 */
struct crypto_rsa_key *
crypto_rsa_import_private_key(const u8 *buf, size_t len)
{
	struct crypto_rsa_key *key;
	struct bignum *zero;
	struct asn1_hdr hdr;
	const u8 *pos, *end;

	key = os_zalloc(sizeof(*key));
	if (key == NULL)
		return NULL;

	key->private_key = 1;

	key->n = bignum_init();
	key->e = bignum_init();
	key->d = bignum_init();
	key->p = bignum_init();
	key->q = bignum_init();
	key->dmp1 = bignum_init();
	key->dmq1 = bignum_init();
	key->iqmp = bignum_init();

	if (key->n == NULL || key->e == NULL || key->d == NULL ||
	    key->p == NULL || key->q == NULL || key->dmp1 == NULL ||
	    key->dmq1 == NULL || key->iqmp == NULL) {
		crypto_rsa_free(key);
		return NULL;
	}

	/*
	 * PKCS #1, 7.2:
	 * RSAPrivateKey ::= SEQUENCE {
	 *    version Version,
	 *    modulus INTEGER, -- n
	 *    publicExponent INTEGER, -- e
	 *    privateExponent INTEGER, -- d
	 *    prime1 INTEGER, -- p
	 *    prime2 INTEGER, -- q
	 *    exponent1 INTEGER, -- d mod (p-1)
	 *    exponent2 INTEGER, -- d mod (q-1)
	 *    coefficient INTEGER -- (inverse of q) mod p
	 * }
	 *
	 * Version ::= INTEGER -- shall be 0 for this version of the standard
	 */
	if (asn1_get_next(buf, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "RSA: Expected SEQUENCE "
			   "(public key) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		goto error;
	}
	pos = hdr.payload;
	end = pos + hdr.length;

	zero = bignum_init();
	if (zero == NULL)
		goto error;
	pos = crypto_rsa_parse_integer(pos, end, zero);
	if (pos == NULL || bignum_cmp_d(zero, 0) != 0) {
		wpa_printf(MSG_DEBUG, "RSA: Expected zero INTEGER in the "
			   "beginning of private key; not found");
		bignum_deinit(zero);
		goto error;
	}
	bignum_deinit(zero);

	pos = crypto_rsa_parse_integer(pos, end, key->n);
	pos = crypto_rsa_parse_integer(pos, end, key->e);
	pos = crypto_rsa_parse_integer(pos, end, key->d);
	pos = crypto_rsa_parse_integer(pos, end, key->p);
	pos = crypto_rsa_parse_integer(pos, end, key->q);
	pos = crypto_rsa_parse_integer(pos, end, key->dmp1);
	pos = crypto_rsa_parse_integer(pos, end, key->dmq1);
	pos = crypto_rsa_parse_integer(pos, end, key->iqmp);

	if (pos == NULL)
		goto error;

	if (pos != end) {
		wpa_hexdump(MSG_DEBUG,
			    "RSA: Extra data in public key SEQUENCE",
			    pos, end - pos);
		goto error;
	}

	return key;

error:
	crypto_rsa_free(key);
	return NULL;
}


/**
 * crypto_rsa_get_modulus_len - Get the modulus length of the RSA key
 * @key: RSA key
 * Returns: Modulus length of the key
 */
size_t crypto_rsa_get_modulus_len(struct crypto_rsa_key *key)
{
	return bignum_get_unsigned_bin_len(key->n);
}


/**
 * crypto_rsa_exptmod - RSA modular exponentiation
 * @in: Input data
 * @inlen: Input data length
 * @out: Buffer for output data
 * @outlen: Maximum size of the output buffer and used size on success
 * @key: RSA key
 * @use_private: 1 = Use RSA private key, 0 = Use RSA public key
 * Returns: 0 on success, -1 on failure
 */
int crypto_rsa_exptmod(const u8 *in, size_t inlen, u8 *out, size_t *outlen,
		       struct crypto_rsa_key *key, int use_private)
{
	struct bignum *tmp, *a = NULL, *b = NULL;
	int ret = -1;
	size_t modlen;

	if (use_private && !key->private_key)
		return -1;

	tmp = bignum_init();
	if (tmp == NULL)
		return -1;

	if (bignum_set_unsigned_bin(tmp, in, inlen) < 0)
		goto error;
	if (bignum_cmp(key->n, tmp) < 0) {
		/* Too large input value for the RSA key modulus */
		goto error;
	}

	if (use_private) {
		/*
		 * Decrypt (or sign) using Chinese remainer theorem to speed
		 * up calculation. This is equivalent to tmp = tmp^d mod n
		 * (which would require more CPU to calculate directly).
		 *
		 * dmp1 = (1/e) mod (p-1)
		 * dmq1 = (1/e) mod (q-1)
		 * iqmp = (1/q) mod p, where p > q
		 * m1 = c^dmp1 mod p
		 * m2 = c^dmq1 mod q
		 * h = q^-1 (m1 - m2) mod p
		 * m = m2 + hq
		 */
		a = bignum_init();
		b = bignum_init();
		if (a == NULL || b == NULL)
			goto error;

		/* a = tmp^dmp1 mod p */
		if (bignum_exptmod(tmp, key->dmp1, key->p, a) < 0)
			goto error;

		/* b = tmp^dmq1 mod q */
		if (bignum_exptmod(tmp, key->dmq1, key->q, b) < 0)
			goto error;

		/* tmp = (a - b) * (1/q mod p) (mod p) */
		if (bignum_sub(a, b, tmp) < 0 ||
		    bignum_mulmod(tmp, key->iqmp, key->p, tmp) < 0)
			goto error;

		/* tmp = b + q * tmp */
		if (bignum_mul(tmp, key->q, tmp) < 0 ||
		    bignum_add(tmp, b, tmp) < 0)
			goto error;
	} else {
		/* Encrypt (or verify signature) */
		/* tmp = tmp^e mod N */
		if (bignum_exptmod(tmp, key->e, key->n, tmp) < 0)
			goto error;
	}

	modlen = crypto_rsa_get_modulus_len(key);
	if (modlen > *outlen) {
		*outlen = modlen;
		goto error;
	}

	if (bignum_get_unsigned_bin_len(tmp) > modlen)
		goto error; /* should never happen */

	*outlen = modlen;
	os_memset(out, 0, modlen);
	if (bignum_get_unsigned_bin(
		    tmp, out +
		    (modlen - bignum_get_unsigned_bin_len(tmp)), NULL) < 0)
		goto error;

	ret = 0;

error:
	bignum_deinit(tmp);
	bignum_deinit(a);
	bignum_deinit(b);
	return ret;
}


/**
 * crypto_rsa_free - Free RSA key
 * @key: RSA key to be freed
 *
 * This function frees an RSA key imported with either
 * crypto_rsa_import_public_key() or crypto_rsa_import_private_key().
 */
void crypto_rsa_free(struct crypto_rsa_key *key)
{
	if (key) {
		bignum_deinit(key->n);
		bignum_deinit(key->e);
		bignum_deinit(key->d);
		bignum_deinit(key->p);
		bignum_deinit(key->q);
		bignum_deinit(key->dmp1);
		bignum_deinit(key->dmq1);
		bignum_deinit(key->iqmp);
		os_free(key);
	}
}
