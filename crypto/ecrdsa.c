// SPDX-License-Identifier: GPL-2.0+
/*
 * Elliptic Curve (Russian) Digital Signature Algorithm for Cryptographic API
 *
 * Copyright (c) 2019 Vitaly Chikunov <vt@altlinux.org>
 *
 * References:
 * GOST 34.10-2018, GOST R 34.10-2012, RFC 7091, ISO/IEC 14888-3:2018.
 *
 * Historical references:
 * GOST R 34.10-2001, RFC 4357, ISO/IEC 14888-3:2006/Amd 1:2010.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <crypto/sig.h>
#include <crypto/streebog.h>
#include <crypto/internal/ecc.h>
#include <crypto/internal/sig.h>
#include <linux/oid_registry.h>
#include "ecrdsa_params.asn1.h"
#include "ecrdsa_pub_key.asn1.h"
#include "ecrdsa_defs.h"

#define ECRDSA_MAX_SIG_SIZE (2 * 512 / 8)
#define ECRDSA_MAX_DIGITS (512 / 64)

struct ecrdsa_ctx {
	enum OID algo_oid; /* overall public key oid */
	enum OID curve_oid; /* parameter */
	enum OID digest_oid; /* parameter */
	const struct ecc_curve *curve; /* curve from oid */
	unsigned int digest_len; /* parameter (bytes) */
	const char *digest; /* digest name from oid */
	unsigned int key_len; /* @key length (bytes) */
	const char *key; /* raw public key */
	struct ecc_point pub_key;
	u64 _pubp[2][ECRDSA_MAX_DIGITS]; /* point storage for @pub_key */
};

static const struct ecc_curve *get_curve_by_oid(enum OID oid)
{
	switch (oid) {
	case OID_gostCPSignA:
	case OID_gostTC26Sign256B:
		return &gost_cp256a;
	case OID_gostCPSignB:
	case OID_gostTC26Sign256C:
		return &gost_cp256b;
	case OID_gostCPSignC:
	case OID_gostTC26Sign256D:
		return &gost_cp256c;
	case OID_gostTC26Sign512A:
		return &gost_tc512a;
	case OID_gostTC26Sign512B:
		return &gost_tc512b;
	/* The following two aren't implemented: */
	case OID_gostTC26Sign256A:
	case OID_gostTC26Sign512C:
	default:
		return NULL;
	}
}

static int ecrdsa_verify(struct crypto_sig *tfm,
			 const void *src, unsigned int slen,
			 const void *digest, unsigned int dlen)
{
	struct ecrdsa_ctx *ctx = crypto_sig_ctx(tfm);
	unsigned int ndigits = dlen / sizeof(u64);
	u64 r[ECRDSA_MAX_DIGITS]; /* witness (r) */
	u64 _r[ECRDSA_MAX_DIGITS]; /* -r */
	u64 s[ECRDSA_MAX_DIGITS]; /* second part of sig (s) */
	u64 e[ECRDSA_MAX_DIGITS]; /* h \mod q */
	u64 *v = e;		  /* e^{-1} \mod q */
	u64 z1[ECRDSA_MAX_DIGITS];
	u64 *z2 = _r;
	struct ecc_point cc = ECC_POINT_INIT(s, e, ndigits); /* reuse s, e */

	/*
	 * Digest value, digest algorithm, and curve (modulus) should have the
	 * same length (256 or 512 bits), public key and signature should be
	 * twice bigger.
	 */
	if (!ctx->curve ||
	    !ctx->digest ||
	    !src ||
	    !digest ||
	    !ctx->pub_key.x ||
	    dlen != ctx->digest_len ||
	    dlen != ctx->curve->g.ndigits * sizeof(u64) ||
	    ctx->pub_key.ndigits != ctx->curve->g.ndigits ||
	    dlen * 2 != slen ||
	    WARN_ON(slen > ECRDSA_MAX_SIG_SIZE) ||
	    WARN_ON(dlen > STREEBOG512_DIGEST_SIZE))
		return -EBADMSG;

	vli_from_be64(s, src, ndigits);
	vli_from_be64(r, src + ndigits * sizeof(u64), ndigits);

	/* Step 1: verify that 0 < r < q, 0 < s < q */
	if (vli_is_zero(r, ndigits) ||
	    vli_cmp(r, ctx->curve->n, ndigits) >= 0 ||
	    vli_is_zero(s, ndigits) ||
	    vli_cmp(s, ctx->curve->n, ndigits) >= 0)
		return -EKEYREJECTED;

	/* Step 2: calculate hash (h) of the message (passed as input) */
	/* Step 3: calculate e = h \mod q */
	vli_from_le64(e, digest, ndigits);
	if (vli_cmp(e, ctx->curve->n, ndigits) >= 0)
		vli_sub(e, e, ctx->curve->n, ndigits);
	if (vli_is_zero(e, ndigits))
		e[0] = 1;

	/* Step 4: calculate v = e^{-1} \mod q */
	vli_mod_inv(v, e, ctx->curve->n, ndigits);

	/* Step 5: calculate z_1 = sv \mod q, z_2 = -rv \mod q */
	vli_mod_mult_slow(z1, s, v, ctx->curve->n, ndigits);
	vli_sub(_r, ctx->curve->n, r, ndigits);
	vli_mod_mult_slow(z2, _r, v, ctx->curve->n, ndigits);

	/* Step 6: calculate point C = z_1P + z_2Q, and R = x_c \mod q */
	ecc_point_mult_shamir(&cc, z1, &ctx->curve->g, z2, &ctx->pub_key,
			      ctx->curve);
	if (vli_cmp(cc.x, ctx->curve->n, ndigits) >= 0)
		vli_sub(cc.x, cc.x, ctx->curve->n, ndigits);

	/* Step 7: if R == r signature is valid */
	if (!vli_cmp(cc.x, r, ndigits))
		return 0;
	else
		return -EKEYREJECTED;
}

int ecrdsa_param_curve(void *context, size_t hdrlen, unsigned char tag,
		       const void *value, size_t vlen)
{
	struct ecrdsa_ctx *ctx = context;

	ctx->curve_oid = look_up_OID(value, vlen);
	if (!ctx->curve_oid)
		return -EINVAL;
	ctx->curve = get_curve_by_oid(ctx->curve_oid);
	return 0;
}

/* Optional. If present should match expected digest algo OID. */
int ecrdsa_param_digest(void *context, size_t hdrlen, unsigned char tag,
			const void *value, size_t vlen)
{
	struct ecrdsa_ctx *ctx = context;
	int digest_oid = look_up_OID(value, vlen);

	if (digest_oid != ctx->digest_oid)
		return -EINVAL;
	return 0;
}

int ecrdsa_parse_pub_key(void *context, size_t hdrlen, unsigned char tag,
			 const void *value, size_t vlen)
{
	struct ecrdsa_ctx *ctx = context;

	ctx->key = value;
	ctx->key_len = vlen;
	return 0;
}

static u8 *ecrdsa_unpack_u32(u32 *dst, void *src)
{
	memcpy(dst, src, sizeof(u32));
	return src + sizeof(u32);
}

/* Parse BER encoded subjectPublicKey. */
static int ecrdsa_set_pub_key(struct crypto_sig *tfm, const void *key,
			      unsigned int keylen)
{
	struct ecrdsa_ctx *ctx = crypto_sig_ctx(tfm);
	unsigned int ndigits;
	u32 algo, paramlen;
	u8 *params;
	int err;

	err = asn1_ber_decoder(&ecrdsa_pub_key_decoder, ctx, key, keylen);
	if (err < 0)
		return err;

	/* Key parameters is in the key after keylen. */
	params = ecrdsa_unpack_u32(&paramlen,
			  ecrdsa_unpack_u32(&algo, (u8 *)key + keylen));

	if (algo == OID_gost2012PKey256) {
		ctx->digest	= "streebog256";
		ctx->digest_oid	= OID_gost2012Digest256;
		ctx->digest_len	= 256 / 8;
	} else if (algo == OID_gost2012PKey512) {
		ctx->digest	= "streebog512";
		ctx->digest_oid	= OID_gost2012Digest512;
		ctx->digest_len	= 512 / 8;
	} else
		return -ENOPKG;
	ctx->algo_oid = algo;

	/* Parse SubjectPublicKeyInfo.AlgorithmIdentifier.parameters. */
	err = asn1_ber_decoder(&ecrdsa_params_decoder, ctx, params, paramlen);
	if (err < 0)
		return err;
	/*
	 * Sizes of algo (set in digest_len) and curve should match
	 * each other.
	 */
	if (!ctx->curve ||
	    ctx->curve->g.ndigits * sizeof(u64) != ctx->digest_len)
		return -ENOPKG;
	/*
	 * Key is two 256- or 512-bit coordinates which should match
	 * curve size.
	 */
	if ((ctx->key_len != (2 * 256 / 8) &&
	     ctx->key_len != (2 * 512 / 8)) ||
	    ctx->key_len != ctx->curve->g.ndigits * sizeof(u64) * 2)
		return -ENOPKG;

	ndigits = ctx->key_len / sizeof(u64) / 2;
	ctx->pub_key = ECC_POINT_INIT(ctx->_pubp[0], ctx->_pubp[1], ndigits);
	vli_from_le64(ctx->pub_key.x, ctx->key, ndigits);
	vli_from_le64(ctx->pub_key.y, ctx->key + ndigits * sizeof(u64),
		      ndigits);

	if (ecc_is_pubkey_valid_partial(ctx->curve, &ctx->pub_key))
		return -EKEYREJECTED;

	return 0;
}

static unsigned int ecrdsa_key_size(struct crypto_sig *tfm)
{
	struct ecrdsa_ctx *ctx = crypto_sig_ctx(tfm);

	/*
	 * Verify doesn't need any output, so it's just informational
	 * for keyctl to determine the key bit size.
	 */
	return ctx->pub_key.ndigits * sizeof(u64);
}

static unsigned int ecrdsa_max_size(struct crypto_sig *tfm)
{
	struct ecrdsa_ctx *ctx = crypto_sig_ctx(tfm);

	return 2 * ctx->pub_key.ndigits * sizeof(u64);
}

static void ecrdsa_exit_tfm(struct crypto_sig *tfm)
{
}

static struct sig_alg ecrdsa_alg = {
	.verify		= ecrdsa_verify,
	.set_pub_key	= ecrdsa_set_pub_key,
	.key_size	= ecrdsa_key_size,
	.max_size	= ecrdsa_max_size,
	.exit		= ecrdsa_exit_tfm,
	.base = {
		.cra_name	 = "ecrdsa",
		.cra_driver_name = "ecrdsa-generic",
		.cra_priority	 = 100,
		.cra_module	 = THIS_MODULE,
		.cra_ctxsize	 = sizeof(struct ecrdsa_ctx),
	},
};

static int __init ecrdsa_mod_init(void)
{
	return crypto_register_sig(&ecrdsa_alg);
}

static void __exit ecrdsa_mod_fini(void)
{
	crypto_unregister_sig(&ecrdsa_alg);
}

module_init(ecrdsa_mod_init);
module_exit(ecrdsa_mod_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vitaly Chikunov <vt@altlinux.org>");
MODULE_DESCRIPTION("EC-RDSA generic algorithm");
MODULE_ALIAS_CRYPTO("ecrdsa");
MODULE_ALIAS_CRYPTO("ecrdsa-generic");
