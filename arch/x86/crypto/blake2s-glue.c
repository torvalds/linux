// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <crypto/internal/blake2s.h>

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

void blake2s_compress(struct blake2s_state *state, const u8 *block,
		      size_t nblocks, const u32 inc)
{
	/* SIMD disables preemption, so relax after processing each page. */
	BUILD_BUG_ON(SZ_4K / BLAKE2S_BLOCK_SIZE < 8);

	if (!static_branch_likely(&blake2s_use_ssse3) || !may_use_simd()) {
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
EXPORT_SYMBOL(blake2s_compress);

static int __init blake2s_mod_init(void)
{
	if (boot_cpu_has(X86_FEATURE_SSSE3))
		static_branch_enable(&blake2s_use_ssse3);

	if (IS_ENABLED(CONFIG_AS_AVX512) &&
	    boot_cpu_has(X86_FEATURE_AVX) &&
	    boot_cpu_has(X86_FEATURE_AVX2) &&
	    boot_cpu_has(X86_FEATURE_AVX512F) &&
	    boot_cpu_has(X86_FEATURE_AVX512VL) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM |
			      XFEATURE_MASK_AVX512, NULL))
		static_branch_enable(&blake2s_use_avx512);

	return 0;
}

module_init(blake2s_mod_init);

MODULE_LICENSE("GPL v2");
