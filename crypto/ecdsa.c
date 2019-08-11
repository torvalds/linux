// SPDX-License-Identifier: GPL-2.0+
/*
 * Elliptic Curve Digital Signature Algorithm for Cryptographic API
 *
 * Copyright (C) 2019 Tribunal Superior Eleitoral. All Rights Reserved.
 * Written by Saulo Alessandre (saulo.alessandre@tse.jus.br || @gmail.com)
 *
 * References:
 * Mathematical routines for the NIST prime elliptic curves April 05, 2010
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <crypto/internal/akcipher.h>
#include <crypto/akcipher.h>
#include <linux/oid_registry.h>
#include "ecdsa_signature.asn1.h"
#include "ecdsa_params.asn1.h"
#include "ecc.h"
#include "ecc_curve_defs.h"
#include "crypto/public_key.h"

#define ECDSA_MAX_BITS     521
#define ECDSA_MAX_SIG_SIZE 140
#define ECDSA_MAX_DIGITS   9
#define MAX_DIGEST_SIZE    (512/8)

#define NIST_UNPACKED_KEY_ID 0x04
#define NISTP256_PACKED_KEY_SIZE 64
#define NISTP384_PACKED_KEY_SIZE 96

struct ecdsa_ctx {
	enum OID algo_oid;	/* overall public key oid */
	enum OID curve_oid;	/* parameter */
	enum OID digest_oid;	/* parameter */
	const struct ecc_curve *curve;	/* curve from oid */
	unsigned int digest_len;	/* parameter (bytes) */
	const char *digest;	/* digest name from oid */
	unsigned int key_len;	/* @key length (bytes) */
	const char *key;	/* raw public key */
	struct ecc_point pub_key;
	u64 _pubp[2][ECDSA_MAX_DIGITS];	/* point storage for @pub_key */
};

struct ecdsa_sig_ctx {
	u64 r[ECDSA_MAX_DIGITS];
	u64 s[ECDSA_MAX_DIGITS];
	int sig_size;
	u8 ndigits;
};

static int check_digest_len(int len)
{
	switch (len) {
	case 32:
	case 48:
	case 64:
		return 0;
	default:
		return -1;
	}
}

static int ecdsa_parse_sig_rs(struct ecdsa_sig_ctx *ctx, u64 * rs,
			      size_t hdrlen, unsigned char tag,
			      const void *value, size_t vlen)
{
	u8 ndigits;
	// skip byte 0 if exists
	const void *idx = value;
	if (*(u8 *) idx == 0x0) {
		idx++;
		vlen--;
	}
	ndigits = vlen / 8;
	if (ndigits == ctx->ndigits)
		ecc_swap_digits((const u64 *)idx, rs, ndigits);
	else {
		u8 nvalue[ECDSA_MAX_SIG_SIZE];
		const u8 start = (ctx->ndigits * 8) - vlen;
		memset(nvalue, 0, start);
		memcpy(nvalue + start, idx, vlen);
		ecc_swap_digits((const u64 *)nvalue, rs, ctx->ndigits);
		vlen = ctx->ndigits * 8;
	}
	ctx->sig_size += vlen;
	return 0;
}

int ecdsa_parse_sig_r(void *context, size_t hdrlen, unsigned char tag,
		      const void *value, size_t vlen)
{
	struct ecdsa_sig_ctx *ctx = context;
	return ecdsa_parse_sig_rs(ctx, ctx->r, hdrlen, tag, value, vlen);
}

int ecdsa_parse_sig_s(void *context, size_t hdrlen, unsigned char tag,
		      const void *value, size_t vlen)
{
	struct ecdsa_sig_ctx *ctx = context;
	return ecdsa_parse_sig_rs(ctx, ctx->s, hdrlen, tag, value, vlen);
}

#define ASN_TAG_SIZE	5

static int ecdsa_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct ecdsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct ecdsa_sig_ctx ctx_sig;
	u8 sig[ECDSA_MAX_SIG_SIZE];
	u8 digest[MAX_DIGEST_SIZE];
	u16 ndigits = ctx->pub_key.ndigits;
	u64 _r[ECDSA_MAX_DIGITS];	/* ecc_point x */
	u64 _s[ECDSA_MAX_DIGITS];	/* ecc_point y and temp s^{-1} */
	u64 e[ECDSA_MAX_DIGITS];	/* h \mod q */
	u64 v[ECDSA_MAX_DIGITS];	/* s^{-1}e \mod q */
	u64 u[ECDSA_MAX_DIGITS];	/* s^{-1}r \mod q */
	struct ecc_point cc = ECC_POINT_INIT(_r, _s, ndigits);	/* reuse r, s */
	struct scatterlist *sgl_s, *sgl_d;
	int err;

	if (!ctx->curve || !ctx->digest || !req->src || !ctx->pub_key.x)
		return -EBADMSG;
	if (check_digest_len(req->dst_len)) {
		printk("ecdsa_verify: invalid source digest size %d\n",
		       req->dst_len);
		return -EBADMSG;
	}
	if (check_digest_len(ctx->digest_len)) {
		printk("ecdsa_verify: invalid context digest size %d\n",
		       ctx->digest_len);
		return -EBADMSG;
	}

	sgl_s = req->src;
	sgl_d = (((void *)req->src) + sizeof(struct scatterlist));

	if (ctx->pub_key.ndigits != ctx->curve->g.ndigits ||
	    WARN_ON(sgl_s->length > sizeof(sig)) ||
	    WARN_ON(sgl_d->length > sizeof(digest))) {
		printk("ecdsa_verify: invalid curve size g(%d) pub(%d) \n",
		       ctx->curve->g.ndigits, ctx->pub_key.ndigits);
		return -EBADMSG;
	}
	sg_copy_to_buffer(sgl_s, sg_nents_for_len(sgl_s, sgl_s->length),
			  sig, sizeof(sig));
	sg_copy_to_buffer(sgl_d, sg_nents_for_len(sgl_d, sgl_d->length),
			  digest, sizeof(digest));

	ctx_sig.sig_size = 0;
	ctx_sig.ndigits = ndigits;
	err =
	    asn1_ber_decoder(&ecdsa_signature_decoder, &ctx_sig, sig,
			     sgl_s->length);
	if (err < 0)
		return err;

	/* Step 1: verify that 0 < r < q, 0 < s < q */
	if (vli_is_zero(ctx_sig.r, ndigits) ||
	    vli_cmp(ctx_sig.r, ctx->curve->n, ndigits) == 1 ||
	    vli_is_zero(ctx_sig.s, ndigits) ||
	    vli_cmp(ctx_sig.s, ctx->curve->n, ndigits) == 1)
		return -EKEYREJECTED;

	/* need truncate digest, like openssl */

	/* Step 2: calculate hash (h) of the message (passed as input) */
	/* Step 3: calculate e = h \mod q */
	vli_from_be64(e, digest, ndigits);
	if (vli_cmp(e, ctx->curve->n, ndigits) == 1)
		vli_sub(e, e, ctx->curve->n, ndigits);
	if (vli_is_zero(e, ndigits))
		e[0] = 1;

	/* Step 4: calculate _s = s^{-1} \mod q */
	vli_mod_inv(_s, ctx_sig.s, ctx->curve->n, ndigits);
	/* Step 5: calculate u = s^{-1} * e \mod q */
	vli_mod_mult_slow(u, _s, e, ctx->curve->n, ndigits);
	/* Step 6: calculate v = s^{-1} * r \mod q */
	vli_mod_mult_slow(v, _s, ctx_sig.r, ctx->curve->n, ndigits);
	/* Step 7: calculate cc = (x0, y0) = uG + vP */
	ecc_point_mult_shamir(&cc, u, &ctx->curve->g, v, &ctx->pub_key,
			      ctx->curve);
	/* v = x0 mod q */
	vli_mod_slow(v, cc.x, ctx->curve->n, ndigits);

	/* Step 9: if X0 == r signature is valid */
	if (vli_cmp(v, ctx_sig.r, ndigits) == 0)
		return 0;

	return -EKEYREJECTED;
}

static const struct ecc_curve *get_curve_by_oid(enum OID oid)
{
	switch (oid) {
	case OID_id_secp192r1:
		return &nist_p192;
	case OID_id_secp256r1:
		return &nist_p256;
	case OID_id_secp384r1:
		return &nist_p384;
	default:
		return NULL;
	}
}

int ecdsa_param_curve(void *context, size_t hdrlen, unsigned char tag,
		      const void *value, size_t vlen)
{
	struct ecdsa_ctx *ctx = context;

	ctx->curve_oid = look_up_OID(value, vlen);
	if (!ctx->curve_oid)
		return -EINVAL;
	ctx->curve = get_curve_by_oid(ctx->curve_oid);
	return 0;
}

/* Optional. If present should match expected digest algo OID. */
int ecdsa_param_digest(void *context, size_t hdrlen, unsigned char tag,
		       const void *value, size_t vlen)
{
	struct ecdsa_ctx *ctx = context;
	int digest_oid = look_up_OID(value, vlen);

	if (digest_oid != ctx->digest_oid)
		return -EINVAL;
	return 0;
}

int ecdsa_parse_pub_key(void *context, size_t hdrlen, unsigned char tag,
			const void *value, size_t vlen)
{
	struct ecdsa_ctx *ctx = context;

	ctx->key = value;
	ctx->key_len = vlen;
	return 0;
}

static u8 *pkey_unpack_u32(u32 * dst, void *src)
{
	memcpy(dst, src, sizeof(*dst));
	return src + sizeof(*dst);
}

/* Parse BER encoded subjectPublicKey. */
static int ecdsa_set_pub_key(struct crypto_akcipher *tfm, const void *key,
			     unsigned int keylen)
{
	struct ecdsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	unsigned int ndigits;
	u32 algo, paramlen;
	u8 *params;
	int err;
	const u8 nist_type = *(u8 *) key;
	u8 half_pub;

	/* Key parameters is in the key after keylen. */
	params = (u8 *) key + keylen;
	params = pkey_unpack_u32(&algo, params);
	params = pkey_unpack_u32(&paramlen, params);

	ctx->algo_oid = algo;
	err = lookup_oid_digest_info(ctx->algo_oid,
				     &ctx->digest, &ctx->digest_len,
				     &ctx->digest_oid);
	if (err < 0)
		return -ENOPKG;

	/* Parse SubjectPublicKeyInfo.AlgorithmIdentifier.parameters. */
	err = asn1_ber_decoder(&ecdsa_params_decoder, ctx, params, paramlen);
	if (err < 0)
		return err;
	ctx->key = key;
	ctx->key_len = keylen;
	if (!ctx->curve)
		return -ENOPKG;

	/*
	 * Accepts only uncompressed it's not accepted
	 */
	if (nist_type != NIST_UNPACKED_KEY_ID)
		return -ENOPKG;
	/* Skip nist type octet */
	ctx->key++;
	ctx->key_len--;
	if (ctx->key_len != NISTP256_PACKED_KEY_SIZE
	    && ctx->key_len != NISTP384_PACKED_KEY_SIZE)
		return -ENOPKG;
	ndigits = ctx->key_len / sizeof(u64) / 2;
	if (ndigits * 2 * sizeof(u64) < ctx->key_len)
		ndigits++;
	half_pub = ctx->key_len / 2;
	/*
	 * Sizes of key_len and curve should match each other.
	 */
	if (ctx->curve->g.ndigits != ndigits)
		return -ENOPKG;
	ctx->pub_key = ECC_POINT_INIT(ctx->_pubp[0], ctx->_pubp[1], ndigits);
	/*
	 * X509 stores key.x and key.y as BE
	 */
	vli_from_be64(ctx->pub_key.x, ctx->key, ndigits);
	vli_from_be64(ctx->pub_key.y, ctx->key + half_pub, ndigits);
	err = ecc_is_pubkey_valid_partial(ctx->curve, &ctx->pub_key);
	if (err)
		return -EKEYREJECTED;

	return 0;
}

static unsigned int ecdsa_max_size(struct crypto_akcipher *tfm)
{
	struct ecdsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	/*
	 * Verify doesn't need any output, so it's just informational
	 * for keyctl to determine the key bit size.
	 */
	return ctx->pub_key.ndigits * sizeof(u64);
}

static void ecdsa_exit_tfm(struct crypto_akcipher *tfm)
{
}

static struct akcipher_alg ecdsa_alg = {
	.verify = ecdsa_verify,
	.set_pub_key = ecdsa_set_pub_key,
	.max_size = ecdsa_max_size,
	.exit = ecdsa_exit_tfm,
	.base = {
		 .cra_name = "ecdsa",
		 .cra_driver_name = "ecdsa-generic",
		 .cra_priority = 100,
		 .cra_module = THIS_MODULE,
		 .cra_ctxsize = sizeof(struct ecdsa_ctx),
		 },
};

static int __init ecdsa_mod_init(void)
{
	return crypto_register_akcipher(&ecdsa_alg);
}

static void __exit ecdsa_mod_fini(void)
{
	crypto_unregister_akcipher(&ecdsa_alg);
}

module_init(ecdsa_mod_init);
module_exit(ecdsa_mod_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Saulo Alessandre <saulo.alessandre@gmail.com>");
MODULE_DESCRIPTION("EC-DSA generic algorithm");
MODULE_ALIAS_CRYPTO("ecdsa-generic");
