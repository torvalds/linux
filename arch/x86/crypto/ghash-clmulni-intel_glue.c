// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated GHASH implementation with Intel PCLMULQDQ-NI
 * instructions. This file contains glue code.
 *
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 */

#include <asm/cpu_device_id.h>
#include <asm/simd.h>
#include <crypto/b128ops.h>
#include <crypto/ghash.h>
#include <crypto/internal/hash.h>
#include <crypto/utils.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

asmlinkage void clmul_ghash_mul(char *dst, const le128 *shash);

asmlinkage int clmul_ghash_update(char *dst, const char *src,
				  unsigned int srclen, const le128 *shash);

struct x86_ghash_ctx {
	le128 shash;
};

static int ghash_init(struct shash_desc *desc)
{
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx, 0, sizeof(*dctx));

	return 0;
}

static int ghash_setkey(struct crypto_shash *tfm,
			const u8 *key, unsigned int keylen)
{
	struct x86_ghash_ctx *ctx = crypto_shash_ctx(tfm);
	u64 a, b;

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	/*
	 * GHASH maps bits to polynomial coefficients backwards, which makes it
	 * hard to implement.  But it can be shown that the GHASH multiplication
	 *
	 *	D * K (mod x^128 + x^7 + x^2 + x + 1)
	 *
	 * (where D is a data block and K is the key) is equivalent to:
	 *
	 *	bitreflect(D) * bitreflect(K) * x^(-127)
	 *		(mod x^128 + x^127 + x^126 + x^121 + 1)
	 *
	 * So, the code below precomputes:
	 *
	 *	bitreflect(K) * x^(-127) (mod x^128 + x^127 + x^126 + x^121 + 1)
	 *
	 * ... but in Montgomery form (so that Montgomery multiplication can be
	 * used), i.e. with an extra x^128 factor, which means actually:
	 *
	 *	bitreflect(K) * x (mod x^128 + x^127 + x^126 + x^121 + 1)
	 *
	 * The within-a-byte part of bitreflect() cancels out GHASH's built-in
	 * reflection, and thus bitreflect() is actually a byteswap.
	 */
	a = get_unaligned_be64(key);
	b = get_unaligned_be64(key + 8);
	ctx->shash.a = cpu_to_le64((a << 1) | (b >> 63));
	ctx->shash.b = cpu_to_le64((b << 1) | (a >> 63));
	if (a >> 63)
		ctx->shash.a ^= cpu_to_le64((u64)0xc2 << 56);
	return 0;
}

static int ghash_update(struct shash_desc *desc,
			 const u8 *src, unsigned int srclen)
{
	struct x86_ghash_ctx *ctx = crypto_shash_ctx(desc->tfm);
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	u8 *dst = dctx->buffer;
	int remain;

	kernel_fpu_begin();
	remain = clmul_ghash_update(dst, src, srclen, &ctx->shash);
	kernel_fpu_end();
	return remain;
}

static void ghash_flush(struct x86_ghash_ctx *ctx, struct ghash_desc_ctx *dctx,
			const u8 *src, unsigned int len)
{
	u8 *dst = dctx->buffer;

	kernel_fpu_begin();
	if (len) {
		crypto_xor(dst, src, len);
		clmul_ghash_mul(dst, &ctx->shash);
	}
	kernel_fpu_end();
}

static int ghash_finup(struct shash_desc *desc, const u8 *src,
		       unsigned int len, u8 *dst)
{
	struct x86_ghash_ctx *ctx = crypto_shash_ctx(desc->tfm);
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	u8 *buf = dctx->buffer;

	ghash_flush(ctx, dctx, src, len);
	memcpy(dst, buf, GHASH_BLOCK_SIZE);

	return 0;
}

static struct shash_alg ghash_alg = {
	.digestsize	= GHASH_DIGEST_SIZE,
	.init		= ghash_init,
	.update		= ghash_update,
	.finup		= ghash_finup,
	.setkey		= ghash_setkey,
	.descsize	= sizeof(struct ghash_desc_ctx),
	.base		= {
		.cra_name		= "ghash",
		.cra_driver_name	= "ghash-pclmulqdqni",
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize		= GHASH_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct x86_ghash_ctx),
		.cra_module		= THIS_MODULE,
	},
};

static const struct x86_cpu_id pcmul_cpu_id[] = {
	X86_MATCH_FEATURE(X86_FEATURE_PCLMULQDQ, NULL), /* Pickle-Mickle-Duck */
	{}
};
MODULE_DEVICE_TABLE(x86cpu, pcmul_cpu_id);

static int __init ghash_pclmulqdqni_mod_init(void)
{
	if (!x86_match_cpu(pcmul_cpu_id))
		return -ENODEV;

	return crypto_register_shash(&ghash_alg);
}

static void __exit ghash_pclmulqdqni_mod_exit(void)
{
	crypto_unregister_shash(&ghash_alg);
}

module_init(ghash_pclmulqdqni_mod_init);
module_exit(ghash_pclmulqdqni_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GHASH hash function, accelerated by PCLMULQDQ-NI");
MODULE_ALIAS_CRYPTO("ghash");
