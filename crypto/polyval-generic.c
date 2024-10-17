// SPDX-License-Identifier: GPL-2.0-only
/*
 * POLYVAL: hash function for HCTR2.
 *
 * Copyright (c) 2007 Nokia Siemens Networks - Mikko Herranen <mh1@iki.fi>
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 * Copyright 2021 Google LLC
 */

/*
 * Code based on crypto/ghash-generic.c
 *
 * POLYVAL is a keyed hash function similar to GHASH. POLYVAL uses a different
 * modulus for finite field multiplication which makes hardware accelerated
 * implementations on little-endian machines faster. POLYVAL is used in the
 * kernel to implement HCTR2, but was originally specified for AES-GCM-SIV
 * (RFC 8452).
 *
 * For more information see:
 * Length-preserving encryption with HCTR2:
 *   https://eprint.iacr.org/2021/1441.pdf
 * AES-GCM-SIV: Nonce Misuse-Resistant Authenticated Encryption:
 *   https://datatracker.ietf.org/doc/html/rfc8452
 *
 * Like GHASH, POLYVAL is not a cryptographic hash function and should
 * not be used outside of crypto modes explicitly designed to use POLYVAL.
 *
 * This implementation uses a convenient trick involving the GHASH and POLYVAL
 * fields. This trick allows multiplication in the POLYVAL field to be
 * implemented by using multiplication in the GHASH field as a subroutine. An
 * element of the POLYVAL field can be converted to an element of the GHASH
 * field by computing x*REVERSE(a), where REVERSE reverses the byte-ordering of
 * a. Similarly, an element of the GHASH field can be converted back to the
 * POLYVAL field by computing REVERSE(x^{-1}*a). For more information, see:
 * https://datatracker.ietf.org/doc/html/rfc8452#appendix-A
 *
 * By using this trick, we do not need to implement the POLYVAL field for the
 * generic implementation.
 *
 * Warning: this generic implementation is not intended to be used in practice
 * and is not constant time. For practical use, a hardware accelerated
 * implementation of POLYVAL should be used instead.
 *
 */

#include <linux/unaligned.h>
#include <crypto/algapi.h>
#include <crypto/gf128mul.h>
#include <crypto/polyval.h>
#include <crypto/internal/hash.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

struct polyval_tfm_ctx {
	struct gf128mul_4k *gf128;
};

struct polyval_desc_ctx {
	union {
		u8 buffer[POLYVAL_BLOCK_SIZE];
		be128 buffer128;
	};
	u32 bytes;
};

static void copy_and_reverse(u8 dst[POLYVAL_BLOCK_SIZE],
			     const u8 src[POLYVAL_BLOCK_SIZE])
{
	u64 a = get_unaligned((const u64 *)&src[0]);
	u64 b = get_unaligned((const u64 *)&src[8]);

	put_unaligned(swab64(a), (u64 *)&dst[8]);
	put_unaligned(swab64(b), (u64 *)&dst[0]);
}

/*
 * Performs multiplication in the POLYVAL field using the GHASH field as a
 * subroutine.  This function is used as a fallback for hardware accelerated
 * implementations when simd registers are unavailable.
 *
 * Note: This function is not used for polyval-generic, instead we use the 4k
 * lookup table implementation for finite field multiplication.
 */
void polyval_mul_non4k(u8 *op1, const u8 *op2)
{
	be128 a, b;

	// Assume one argument is in Montgomery form and one is not.
	copy_and_reverse((u8 *)&a, op1);
	copy_and_reverse((u8 *)&b, op2);
	gf128mul_x_lle(&a, &a);
	gf128mul_lle(&a, &b);
	copy_and_reverse(op1, (u8 *)&a);
}
EXPORT_SYMBOL_GPL(polyval_mul_non4k);

/*
 * Perform a POLYVAL update using non4k multiplication.  This function is used
 * as a fallback for hardware accelerated implementations when simd registers
 * are unavailable.
 *
 * Note: This function is not used for polyval-generic, instead we use the 4k
 * lookup table implementation of finite field multiplication.
 */
void polyval_update_non4k(const u8 *key, const u8 *in,
			  size_t nblocks, u8 *accumulator)
{
	while (nblocks--) {
		crypto_xor(accumulator, in, POLYVAL_BLOCK_SIZE);
		polyval_mul_non4k(accumulator, key);
		in += POLYVAL_BLOCK_SIZE;
	}
}
EXPORT_SYMBOL_GPL(polyval_update_non4k);

static int polyval_setkey(struct crypto_shash *tfm,
			  const u8 *key, unsigned int keylen)
{
	struct polyval_tfm_ctx *ctx = crypto_shash_ctx(tfm);
	be128 k;

	if (keylen != POLYVAL_BLOCK_SIZE)
		return -EINVAL;

	gf128mul_free_4k(ctx->gf128);

	BUILD_BUG_ON(sizeof(k) != POLYVAL_BLOCK_SIZE);
	copy_and_reverse((u8 *)&k, key);
	gf128mul_x_lle(&k, &k);

	ctx->gf128 = gf128mul_init_4k_lle(&k);
	memzero_explicit(&k, POLYVAL_BLOCK_SIZE);

	if (!ctx->gf128)
		return -ENOMEM;

	return 0;
}

static int polyval_init(struct shash_desc *desc)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx, 0, sizeof(*dctx));

	return 0;
}

static int polyval_update(struct shash_desc *desc,
			 const u8 *src, unsigned int srclen)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);
	const struct polyval_tfm_ctx *ctx = crypto_shash_ctx(desc->tfm);
	u8 *pos;
	u8 tmp[POLYVAL_BLOCK_SIZE];
	int n;

	if (dctx->bytes) {
		n = min(srclen, dctx->bytes);
		pos = dctx->buffer + dctx->bytes - 1;

		dctx->bytes -= n;
		srclen -= n;

		while (n--)
			*pos-- ^= *src++;

		if (!dctx->bytes)
			gf128mul_4k_lle(&dctx->buffer128, ctx->gf128);
	}

	while (srclen >= POLYVAL_BLOCK_SIZE) {
		copy_and_reverse(tmp, src);
		crypto_xor(dctx->buffer, tmp, POLYVAL_BLOCK_SIZE);
		gf128mul_4k_lle(&dctx->buffer128, ctx->gf128);
		src += POLYVAL_BLOCK_SIZE;
		srclen -= POLYVAL_BLOCK_SIZE;
	}

	if (srclen) {
		dctx->bytes = POLYVAL_BLOCK_SIZE - srclen;
		pos = dctx->buffer + POLYVAL_BLOCK_SIZE - 1;
		while (srclen--)
			*pos-- ^= *src++;
	}

	return 0;
}

static int polyval_final(struct shash_desc *desc, u8 *dst)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);
	const struct polyval_tfm_ctx *ctx = crypto_shash_ctx(desc->tfm);

	if (dctx->bytes)
		gf128mul_4k_lle(&dctx->buffer128, ctx->gf128);
	copy_and_reverse(dst, dctx->buffer);
	return 0;
}

static void polyval_exit_tfm(struct crypto_tfm *tfm)
{
	struct polyval_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	gf128mul_free_4k(ctx->gf128);
}

static struct shash_alg polyval_alg = {
	.digestsize	= POLYVAL_DIGEST_SIZE,
	.init		= polyval_init,
	.update		= polyval_update,
	.final		= polyval_final,
	.setkey		= polyval_setkey,
	.descsize	= sizeof(struct polyval_desc_ctx),
	.base		= {
		.cra_name		= "polyval",
		.cra_driver_name	= "polyval-generic",
		.cra_priority		= 100,
		.cra_blocksize		= POLYVAL_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct polyval_tfm_ctx),
		.cra_module		= THIS_MODULE,
		.cra_exit		= polyval_exit_tfm,
	},
};

static int __init polyval_mod_init(void)
{
	return crypto_register_shash(&polyval_alg);
}

static void __exit polyval_mod_exit(void)
{
	crypto_unregister_shash(&polyval_alg);
}

subsys_initcall(polyval_mod_init);
module_exit(polyval_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("POLYVAL hash function");
MODULE_ALIAS_CRYPTO("polyval");
MODULE_ALIAS_CRYPTO("polyval-generic");
