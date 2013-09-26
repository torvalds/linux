/* RSA asymmetric public-key algorithm [RFC3447]
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "RSA: "fmt
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <crypto/algapi.h>
#include "public_key.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RSA Public Key Algorithm");

#define kenter(FMT, ...) \
	pr_devel("==> %s("FMT")\n", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) \
	pr_devel("<== %s()"FMT"\n", __func__, ##__VA_ARGS__)

/*
 * Hash algorithm OIDs plus ASN.1 DER wrappings [RFC4880 sec 5.2.2].
 */
static const u8 RSA_digest_info_MD5[] = {
	0x30, 0x20, 0x30, 0x0C, 0x06, 0x08,
	0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x02, 0x05, /* OID */
	0x05, 0x00, 0x04, 0x10
};

static const u8 RSA_digest_info_SHA1[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05,
	0x2B, 0x0E, 0x03, 0x02, 0x1A,
	0x05, 0x00, 0x04, 0x14
};

static const u8 RSA_digest_info_RIPE_MD_160[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05,
	0x2B, 0x24, 0x03, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x14
};

static const u8 RSA_digest_info_SHA224[] = {
	0x30, 0x2d, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x04,
	0x05, 0x00, 0x04, 0x1C
};

static const u8 RSA_digest_info_SHA256[] = {
	0x30, 0x31, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x20
};

static const u8 RSA_digest_info_SHA384[] = {
	0x30, 0x41, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02,
	0x05, 0x00, 0x04, 0x30
};

static const u8 RSA_digest_info_SHA512[] = {
	0x30, 0x51, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03,
	0x05, 0x00, 0x04, 0x40
};

static const struct {
	const u8 *data;
	size_t size;
} RSA_ASN1_templates[PKEY_HASH__LAST] = {
#define _(X) { RSA_digest_info_##X, sizeof(RSA_digest_info_##X) }
	[PKEY_HASH_MD5]		= _(MD5),
	[PKEY_HASH_SHA1]	= _(SHA1),
	[PKEY_HASH_RIPE_MD_160]	= _(RIPE_MD_160),
	[PKEY_HASH_SHA256]	= _(SHA256),
	[PKEY_HASH_SHA384]	= _(SHA384),
	[PKEY_HASH_SHA512]	= _(SHA512),
	[PKEY_HASH_SHA224]	= _(SHA224),
#undef _
};

/*
 * RSAVP1() function [RFC3447 sec 5.2.2]
 */
static int RSAVP1(const struct public_key *key, MPI s, MPI *_m)
{
	MPI m;
	int ret;

	/* (1) Validate 0 <= s < n */
	if (mpi_cmp_ui(s, 0) < 0) {
		kleave(" = -EBADMSG [s < 0]");
		return -EBADMSG;
	}
	if (mpi_cmp(s, key->rsa.n) >= 0) {
		kleave(" = -EBADMSG [s >= n]");
		return -EBADMSG;
	}

	m = mpi_alloc(0);
	if (!m)
		return -ENOMEM;

	/* (2) m = s^e mod n */
	ret = mpi_powm(m, s, key->rsa.e, key->rsa.n);
	if (ret < 0) {
		mpi_free(m);
		return ret;
	}

	*_m = m;
	return 0;
}

/*
 * Integer to Octet String conversion [RFC3447 sec 4.1]
 */
static int RSA_I2OSP(MPI x, size_t xLen, u8 **_X)
{
	unsigned X_size, x_size;
	int X_sign;
	u8 *X;

	/* Make sure the string is the right length.  The number should begin
	 * with { 0x00, 0x01, ... } so we have to account for 15 leading zero
	 * bits not being reported by MPI.
	 */
	x_size = mpi_get_nbits(x);
	pr_devel("size(x)=%u xLen*8=%zu\n", x_size, xLen * 8);
	if (x_size != xLen * 8 - 15)
		return -ERANGE;

	X = mpi_get_buffer(x, &X_size, &X_sign);
	if (!X)
		return -ENOMEM;
	if (X_sign < 0) {
		kfree(X);
		return -EBADMSG;
	}
	if (X_size != xLen - 1) {
		kfree(X);
		return -EBADMSG;
	}

	*_X = X;
	return 0;
}

/*
 * Perform the RSA signature verification.
 * @H: Value of hash of data and metadata
 * @EM: The computed signature value
 * @k: The size of EM (EM[0] is an invalid location but should hold 0x00)
 * @hash_size: The size of H
 * @asn1_template: The DigestInfo ASN.1 template
 * @asn1_size: Size of asm1_template[]
 */
static int RSA_verify(const u8 *H, const u8 *EM, size_t k, size_t hash_size,
		      const u8 *asn1_template, size_t asn1_size)
{
	unsigned PS_end, T_offset, i;

	kenter(",,%zu,%zu,%zu", k, hash_size, asn1_size);

	if (k < 2 + 1 + asn1_size + hash_size)
		return -EBADMSG;

	/* Decode the EMSA-PKCS1-v1_5 */
	if (EM[1] != 0x01) {
		kleave(" = -EBADMSG [EM[1] == %02u]", EM[1]);
		return -EBADMSG;
	}

	T_offset = k - (asn1_size + hash_size);
	PS_end = T_offset - 1;
	if (EM[PS_end] != 0x00) {
		kleave(" = -EBADMSG [EM[T-1] == %02u]", EM[PS_end]);
		return -EBADMSG;
	}

	for (i = 2; i < PS_end; i++) {
		if (EM[i] != 0xff) {
			kleave(" = -EBADMSG [EM[PS%x] == %02u]", i - 2, EM[i]);
			return -EBADMSG;
		}
	}

	if (crypto_memneq(asn1_template, EM + T_offset, asn1_size) != 0) {
		kleave(" = -EBADMSG [EM[T] ASN.1 mismatch]");
		return -EBADMSG;
	}

	if (crypto_memneq(H, EM + T_offset + asn1_size, hash_size) != 0) {
		kleave(" = -EKEYREJECTED [EM[T] hash mismatch]");
		return -EKEYREJECTED;
	}

	kleave(" = 0");
	return 0;
}

/*
 * Perform the verification step [RFC3447 sec 8.2.2].
 */
static int RSA_verify_signature(const struct public_key *key,
				const struct public_key_signature *sig)
{
	size_t tsize;
	int ret;

	/* Variables as per RFC3447 sec 8.2.2 */
	const u8 *H = sig->digest;
	u8 *EM = NULL;
	MPI m = NULL;
	size_t k;

	kenter("");

	if (!RSA_ASN1_templates[sig->pkey_hash_algo].data)
		return -ENOTSUPP;

	/* (1) Check the signature size against the public key modulus size */
	k = mpi_get_nbits(key->rsa.n);
	tsize = mpi_get_nbits(sig->rsa.s);

	/* According to RFC 4880 sec 3.2, length of MPI is computed starting
	 * from most significant bit.  So the RFC 3447 sec 8.2.2 size check
	 * must be relaxed to conform with shorter signatures - so we fail here
	 * only if signature length is longer than modulus size.
	 */
	pr_devel("step 1: k=%zu size(S)=%zu\n", k, tsize);
	if (k < tsize) {
		ret = -EBADMSG;
		goto error;
	}

	/* Round up and convert to octets */
	k = (k + 7) / 8;

	/* (2b) Apply the RSAVP1 verification primitive to the public key */
	ret = RSAVP1(key, sig->rsa.s, &m);
	if (ret < 0)
		goto error;

	/* (2c) Convert the message representative (m) to an encoded message
	 *      (EM) of length k octets.
	 *
	 *      NOTE!  The leading zero byte is suppressed by MPI, so we pass a
	 *      pointer to the _preceding_ byte to RSA_verify()!
	 */
	ret = RSA_I2OSP(m, k, &EM);
	if (ret < 0)
		goto error;

	ret = RSA_verify(H, EM - 1, k, sig->digest_size,
			 RSA_ASN1_templates[sig->pkey_hash_algo].data,
			 RSA_ASN1_templates[sig->pkey_hash_algo].size);

error:
	kfree(EM);
	mpi_free(m);
	kleave(" = %d", ret);
	return ret;
}

const struct public_key_algorithm RSA_public_key_algorithm = {
	.name		= "RSA",
	.n_pub_mpi	= 2,
	.n_sec_mpi	= 3,
	.n_sig_mpi	= 1,
	.verify_signature = RSA_verify_signature,
};
EXPORT_SYMBOL_GPL(RSA_public_key_algorithm);
