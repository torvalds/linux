// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <crypto/internal/blake2s.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/hash.h>

#include <linux/types.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sizes.h>

#include <asm/cpufeature.h>
#include <asm/fpu/api.h>
#include <asm/processor.h>
#include <asm/simd.h>

asmlinkage void blake2s_compress_ssse3(struct blake2s_state *state,
				       const u8 *block, const size_t nblocks,
				       const u32 inc);
asmlinkage void blake2s_compress_avx512(struct blake2s_state *state,
					const u8 *block, const size_t nblocks,
					const u32 inc);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(blake2s_use_ssse3);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(blake2s_use_avx512);

void blake2s_compress_arch(struct blake2s_state *state,
			   const u8 *block, size_t nblocks,
			   const u32 inc)
{
	/* SIMD disables preemption, so relax after processing each page. */
	BUILD_BUG_ON(SZ_4K / BLAKE2S_BLOCK_SIZE < 8);

	if (!static_branch_likely(&blake2s_use_ssse3) || !crypto_simd_usable()) {
		blake2s_compress_generic(state, block, nblocks, inc);
		return;
	}

	do {
		const size_t blocks = min_t(size_t, nblocks,
					    SZ_4K / BLAKE2S_BLOCK_SIZE);

		kernel_fpu_begin();
		if (IS_ENABLED(CONFIG_AS_AVX512) &&
		    static_branch_likely(&blake2s_use_avx512))
			blake2s_compress_avx512(state, block, blocks, inc);
		else
			blake2s_compress_ssse3(state, block, blocks, inc);
		kernel_fpu_end();

		nblocks -= blocks;
		block += blocks * BLAKE2S_BLOCK_SIZE;
	} while (nblocks);
}
EXPORT_SYMBOL(blake2s_compress_arch);

static int crypto_blake2s_setkey(struct crypto_shash *tfm, const u8 *key,
				 unsigned int keylen)
{
	struct blake2s_tfm_ctx *tctx = crypto_shash_ctx(tfm);

	if (keylen == 0 || keylen > BLAKE2S_KEY_SIZE)
		return -EINVAL;

	memcpy(tctx->key, key, keylen);
	tctx->keylen = keylen;

	return 0;
}

static int crypto_blake2s_init(struct shash_desc *desc)
{
	struct blake2s_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct blake2s_state *state = shash_desc_ctx(desc);
	const int outlen = crypto_shash_digestsize(desc->tfm);

	if (tctx->keylen)
		blake2s_init_key(state, outlen, tctx->key, tctx->keylen);
	else
		blake2s_init(state, outlen);

	return 0;
}

static int crypto_blake2s_update(struct shash_desc *desc, const u8 *in,
				 unsigned int inlen)
{
	struct blake2s_state *state = shash_desc_ctx(desc);
	const size_t fill = BLAKE2S_BLOCK_SIZE - state->buflen;

	if (unlikely(!inlen))
		return 0;
	if (inlen > fill) {
		memcpy(state->buf + state->buflen, in, fill);
		blake2s_compress_arch(state, state->buf, 1, BLAKE2S_BLOCK_SIZE);
		state->buflen = 0;
		in += fill;
		inlen -= fill;
	}
	if (inlen > BLAKE2S_BLOCK_SIZE) {
		const size_t nblocks = DIV_ROUND_UP(inlen, BLAKE2S_BLOCK_SIZE);
		/* Hash one less (full) block than strictly possible */
		blake2s_compress_arch(state, in, nblocks - 1, BLAKE2S_BLOCK_SIZE);
		in += BLAKE2S_BLOCK_SIZE * (nblocks - 1);
		inlen -= BLAKE2S_BLOCK_SIZE * (nblocks - 1);
	}
	memcpy(state->buf + state->buflen, in, inlen);
	state->buflen += inlen;

	return 0;
}

static int crypto_blake2s_final(struct shash_desc *desc, u8 *out)
{
	struct blake2s_state *state = shash_desc_ctx(desc);

	blake2s_set_lastblock(state);
	memset(state->buf + state->buflen, 0,
	       BLAKE2S_BLOCK_SIZE - state->buflen); /* Padding */
	blake2s_compress_arch(state, state->buf, 1, state->buflen);
	cpu_to_le32_array(state->h, ARRAY_SIZE(state->h));
	memcpy(out, state->h, state->outlen);
	memzero_explicit(state, sizeof(*state));

	return 0;
}

static struct shash_alg blake2s_algs[] = {{
	.base.cra_name		= "blake2s-128",
	.base.cra_driver_name	= "blake2s-128-x86",
	.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
	.base.cra_ctxsize	= sizeof(struct blake2s_tfm_ctx),
	.base.cra_priority	= 200,
	.base.cra_blocksize     = BLAKE2S_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,

	.digestsize		= BLAKE2S_128_HASH_SIZE,
	.setkey			= crypto_blake2s_setkey,
	.init			= crypto_blake2s_init,
	.update			= crypto_blake2s_update,
	.final			= crypto_blake2s_final,
	.descsize		= sizeof(struct blake2s_state),
}, {
	.base.cra_name		= "blake2s-160",
	.base.cra_driver_name	= "blake2s-160-x86",
	.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
	.base.cra_ctxsize	= sizeof(struct blake2s_tfm_ctx),
	.base.cra_priority	= 200,
	.base.cra_blocksize     = BLAKE2S_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,

	.digestsize		= BLAKE2S_160_HASH_SIZE,
	.setkey			= crypto_blake2s_setkey,
	.init			= crypto_blake2s_init,
	.update			= crypto_blake2s_update,
	.final			= crypto_blake2s_final,
	.descsize		= sizeof(struct blake2s_state),
}, {
	.base.cra_name		= "blake2s-224",
	.base.cra_driver_name	= "blake2s-224-x86",
	.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
	.base.cra_ctxsize	= sizeof(struct blake2s_tfm_ctx),
	.base.cra_priority	= 200,
	.base.cra_blocksize     = BLAKE2S_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,

	.digestsize		= BLAKE2S_224_HASH_SIZE,
	.setkey			= crypto_blake2s_setkey,
	.init			= crypto_blake2s_init,
	.update			= crypto_blake2s_update,
	.final			= crypto_blake2s_final,
	.descsize		= sizeof(struct blake2s_state),
}, {
	.base.cra_name		= "blake2s-256",
	.base.cra_driver_name	= "blake2s-256-x86",
	.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
	.base.cra_ctxsize	= sizeof(struct blake2s_tfm_ctx),
	.base.cra_priority	= 200,
	.base.cra_blocksize     = BLAKE2S_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,

	.digestsize		= BLAKE2S_256_HASH_SIZE,
	.setkey			= crypto_blake2s_setkey,
	.init			= crypto_blake2s_init,
	.update			= crypto_blake2s_update,
	.final			= crypto_blake2s_final,
	.descsize		= sizeof(struct blake2s_state),
}};

static int __init blake2s_mod_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_SSSE3))
		return 0;

	static_branch_enable(&blake2s_use_ssse3);

	if (IS_ENABLED(CONFIG_AS_AVX512) &&
	    boot_cpu_has(X86_FEATURE_AVX) &&
	    boot_cpu_has(X86_FEATURE_AVX2) &&
	    boot_cpu_has(X86_FEATURE_AVX512F) &&
	    boot_cpu_has(X86_FEATURE_AVX512VL) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM |
			      XFEATURE_MASK_AVX512, NULL))
		static_branch_enable(&blake2s_use_avx512);

	return IS_REACHABLE(CONFIG_CRYPTO_HASH) ?
		crypto_register_shashes(blake2s_algs,
					ARRAY_SIZE(blake2s_algs)) : 0;
}

static void __exit blake2s_mod_exit(void)
{
	if (IS_REACHABLE(CONFIG_CRYPTO_HASH) && boot_cpu_has(X86_FEATURE_SSSE3))
		crypto_unregister_shashes(blake2s_algs, ARRAY_SIZE(blake2s_algs));
}

module_init(blake2s_mod_init);
module_exit(blake2s_mod_exit);

MODULE_ALIAS_CRYPTO("blake2s-128");
MODULE_ALIAS_CRYPTO("blake2s-128-x86");
MODULE_ALIAS_CRYPTO("blake2s-160");
MODULE_ALIAS_CRYPTO("blake2s-160-x86");
MODULE_ALIAS_CRYPTO("blake2s-224");
MODULE_ALIAS_CRYPTO("blake2s-224-x86");
MODULE_ALIAS_CRYPTO("blake2s-256");
MODULE_ALIAS_CRYPTO("blake2s-256-x86");
MODULE_LICENSE("GPL v2");
