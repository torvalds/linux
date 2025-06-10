// SPDX-License-Identifier: GPL-2.0
/*
 * GHASH routines supporting VMX instructions on the Power 8
 *
 * Copyright (C) 2015, 2019 International Business Machines Inc.
 *
 * Author: Marcelo Henrique Cerri <mhcerri@br.ibm.com>
 *
 * Extended by Daniel Axtens <dja@axtens.net> to replace the fallback
 * mechanism. The new approach is based on arm64 code, which is:
 *   Copyright (C) 2014 - 2018 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include "aesp8-ppc.h"
#include <asm/switch_to.h>
#include <crypto/aes.h>
#include <crypto/gf128mul.h>
#include <crypto/ghash.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/uaccess.h>

void gcm_init_p8(u128 htable[16], const u64 Xi[2]);
void gcm_gmult_p8(u64 Xi[2], const u128 htable[16]);
void gcm_ghash_p8(u64 Xi[2], const u128 htable[16],
		  const u8 *in, size_t len);

struct p8_ghash_ctx {
	/* key used by vector asm */
	u128 htable[16];
	/* key used by software fallback */
	be128 key;
};

struct p8_ghash_desc_ctx {
	u64 shash[2];
};

static int p8_ghash_init(struct shash_desc *desc)
{
	struct p8_ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx->shash, 0, GHASH_DIGEST_SIZE);
	return 0;
}

static int p8_ghash_setkey(struct crypto_shash *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct p8_ghash_ctx *ctx = crypto_tfm_ctx(crypto_shash_tfm(tfm));

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	preempt_disable();
	pagefault_disable();
	enable_kernel_vsx();
	gcm_init_p8(ctx->htable, (const u64 *) key);
	disable_kernel_vsx();
	pagefault_enable();
	preempt_enable();

	memcpy(&ctx->key, key, GHASH_BLOCK_SIZE);

	return 0;
}

static inline void __ghash_block(struct p8_ghash_ctx *ctx,
				 struct p8_ghash_desc_ctx *dctx,
				 const u8 *src)
{
	if (crypto_simd_usable()) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		gcm_ghash_p8(dctx->shash, ctx->htable, src, GHASH_BLOCK_SIZE);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();
	} else {
		crypto_xor((u8 *)dctx->shash, src, GHASH_BLOCK_SIZE);
		gf128mul_lle((be128 *)dctx->shash, &ctx->key);
	}
}

static inline int __ghash_blocks(struct p8_ghash_ctx *ctx,
				 struct p8_ghash_desc_ctx *dctx,
				 const u8 *src, unsigned int srclen)
{
	int remain = srclen - round_down(srclen, GHASH_BLOCK_SIZE);

	srclen -= remain;
	if (crypto_simd_usable()) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		gcm_ghash_p8(dctx->shash, ctx->htable,
				src, srclen);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();
	} else {
		do {
			crypto_xor((u8 *)dctx->shash, src, GHASH_BLOCK_SIZE);
			gf128mul_lle((be128 *)dctx->shash, &ctx->key);
			srclen -= GHASH_BLOCK_SIZE;
			src += GHASH_BLOCK_SIZE;
		} while (srclen);
	}

	return remain;
}

static int p8_ghash_update(struct shash_desc *desc,
			   const u8 *src, unsigned int srclen)
{
	struct p8_ghash_ctx *ctx = crypto_tfm_ctx(crypto_shash_tfm(desc->tfm));
	struct p8_ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	return __ghash_blocks(ctx, dctx, src, srclen);
}

static int p8_ghash_finup(struct shash_desc *desc, const u8 *src,
			  unsigned int len, u8 *out)
{
	struct p8_ghash_ctx *ctx = crypto_tfm_ctx(crypto_shash_tfm(desc->tfm));
	struct p8_ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	if (len) {
		u8 buf[GHASH_BLOCK_SIZE] = {};

		memcpy(buf, src, len);
		__ghash_block(ctx, dctx, buf);
		memzero_explicit(buf, sizeof(buf));
	}
	memcpy(out, dctx->shash, GHASH_DIGEST_SIZE);
	return 0;
}

struct shash_alg p8_ghash_alg = {
	.digestsize = GHASH_DIGEST_SIZE,
	.init = p8_ghash_init,
	.update = p8_ghash_update,
	.finup = p8_ghash_finup,
	.setkey = p8_ghash_setkey,
	.descsize = sizeof(struct p8_ghash_desc_ctx),
	.base = {
		 .cra_name = "ghash",
		 .cra_driver_name = "p8_ghash",
		 .cra_priority = 1000,
		 .cra_flags = CRYPTO_AHASH_ALG_BLOCK_ONLY,
		 .cra_blocksize = GHASH_BLOCK_SIZE,
		 .cra_ctxsize = sizeof(struct p8_ghash_ctx),
		 .cra_module = THIS_MODULE,
	},
};
