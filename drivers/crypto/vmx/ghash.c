// SPDX-License-Identifier: GPL-2.0
/**
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

#include <linux/types.h>
#include <linux/err.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <asm/simd.h>
#include <asm/switch_to.h>
#include <crypto/aes.h>
#include <crypto/ghash.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/b128ops.h>

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
	u8 buffer[GHASH_DIGEST_SIZE];
	int bytes;
};

static int p8_ghash_init(struct shash_desc *desc)
{
	struct p8_ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	dctx->bytes = 0;
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
				 struct p8_ghash_desc_ctx *dctx)
{
	if (crypto_simd_usable()) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		gcm_ghash_p8(dctx->shash, ctx->htable,
				dctx->buffer, GHASH_DIGEST_SIZE);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();
	} else {
		crypto_xor((u8 *)dctx->shash, dctx->buffer, GHASH_BLOCK_SIZE);
		gf128mul_lle((be128 *)dctx->shash, &ctx->key);
	}
}

static inline void __ghash_blocks(struct p8_ghash_ctx *ctx,
				  struct p8_ghash_desc_ctx *dctx,
				  const u8 *src, unsigned int srclen)
{
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
		while (srclen >= GHASH_BLOCK_SIZE) {
			crypto_xor((u8 *)dctx->shash, src, GHASH_BLOCK_SIZE);
			gf128mul_lle((be128 *)dctx->shash, &ctx->key);
			srclen -= GHASH_BLOCK_SIZE;
			src += GHASH_BLOCK_SIZE;
		}
	}
}

static int p8_ghash_update(struct shash_desc *desc,
			   const u8 *src, unsigned int srclen)
{
	unsigned int len;
	struct p8_ghash_ctx *ctx = crypto_tfm_ctx(crypto_shash_tfm(desc->tfm));
	struct p8_ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	if (dctx->bytes) {
		if (dctx->bytes + srclen < GHASH_DIGEST_SIZE) {
			memcpy(dctx->buffer + dctx->bytes, src,
				srclen);
			dctx->bytes += srclen;
			return 0;
		}
		memcpy(dctx->buffer + dctx->bytes, src,
			GHASH_DIGEST_SIZE - dctx->bytes);

		__ghash_block(ctx, dctx);

		src += GHASH_DIGEST_SIZE - dctx->bytes;
		srclen -= GHASH_DIGEST_SIZE - dctx->bytes;
		dctx->bytes = 0;
	}
	len = srclen & ~(GHASH_DIGEST_SIZE - 1);
	if (len) {
		__ghash_blocks(ctx, dctx, src, len);
		src += len;
		srclen -= len;
	}
	if (srclen) {
		memcpy(dctx->buffer, src, srclen);
		dctx->bytes = srclen;
	}
	return 0;
}

static int p8_ghash_final(struct shash_desc *desc, u8 *out)
{
	int i;
	struct p8_ghash_ctx *ctx = crypto_tfm_ctx(crypto_shash_tfm(desc->tfm));
	struct p8_ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	if (dctx->bytes) {
		for (i = dctx->bytes; i < GHASH_DIGEST_SIZE; i++)
			dctx->buffer[i] = 0;
		__ghash_block(ctx, dctx);
		dctx->bytes = 0;
	}
	memcpy(out, dctx->shash, GHASH_DIGEST_SIZE);
	return 0;
}

struct shash_alg p8_ghash_alg = {
	.digestsize = GHASH_DIGEST_SIZE,
	.init = p8_ghash_init,
	.update = p8_ghash_update,
	.final = p8_ghash_final,
	.setkey = p8_ghash_setkey,
	.descsize = sizeof(struct p8_ghash_desc_ctx)
		+ sizeof(struct ghash_desc_ctx),
	.base = {
		 .cra_name = "ghash",
		 .cra_driver_name = "p8_ghash",
		 .cra_priority = 1000,
		 .cra_blocksize = GHASH_BLOCK_SIZE,
		 .cra_ctxsize = sizeof(struct p8_ghash_ctx),
		 .cra_module = THIS_MODULE,
	},
};
