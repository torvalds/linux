// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <asm/cpu_device_id.h>
#include <asm/fpu/api.h>
#include <crypto/internal/poly1305.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/unaligned.h>

struct poly1305_arch_internal {
	union {
		struct {
			u32 h[5];
			u32 is_base2_26;
		};
		u64 hs[3];
	};
	u64 r[2];
	u64 pad;
	struct { u32 r2, r1, r4, r3; } rn[9];
};

asmlinkage void poly1305_block_init_arch(
	struct poly1305_block_state *state,
	const u8 raw_key[POLY1305_BLOCK_SIZE]);
EXPORT_SYMBOL_GPL(poly1305_block_init_arch);
asmlinkage void poly1305_blocks_x86_64(struct poly1305_arch_internal *ctx,
				       const u8 *inp,
				       const size_t len, const u32 padbit);
asmlinkage void poly1305_emit_x86_64(const struct poly1305_state *ctx,
				     u8 mac[POLY1305_DIGEST_SIZE],
				     const u32 nonce[4]);
asmlinkage void poly1305_emit_avx(const struct poly1305_state *ctx,
				  u8 mac[POLY1305_DIGEST_SIZE],
				  const u32 nonce[4]);
asmlinkage void poly1305_blocks_avx(struct poly1305_arch_internal *ctx,
				    const u8 *inp, const size_t len,
				    const u32 padbit);
asmlinkage void poly1305_blocks_avx2(struct poly1305_arch_internal *ctx,
				     const u8 *inp, const size_t len,
				     const u32 padbit);
asmlinkage void poly1305_blocks_avx512(struct poly1305_arch_internal *ctx,
				       const u8 *inp,
				       const size_t len, const u32 padbit);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(poly1305_use_avx);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(poly1305_use_avx2);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(poly1305_use_avx512);

void poly1305_blocks_arch(struct poly1305_block_state *state, const u8 *inp,
			  unsigned int len, u32 padbit)
{
	struct poly1305_arch_internal *ctx =
		container_of(&state->h.h, struct poly1305_arch_internal, h);

	/* SIMD disables preemption, so relax after processing each page. */
	BUILD_BUG_ON(SZ_4K < POLY1305_BLOCK_SIZE ||
		     SZ_4K % POLY1305_BLOCK_SIZE);

	if (!static_branch_likely(&poly1305_use_avx)) {
		poly1305_blocks_x86_64(ctx, inp, len, padbit);
		return;
	}

	do {
		const unsigned int bytes = min(len, SZ_4K);

		kernel_fpu_begin();
		if (static_branch_likely(&poly1305_use_avx512))
			poly1305_blocks_avx512(ctx, inp, bytes, padbit);
		else if (static_branch_likely(&poly1305_use_avx2))
			poly1305_blocks_avx2(ctx, inp, bytes, padbit);
		else
			poly1305_blocks_avx(ctx, inp, bytes, padbit);
		kernel_fpu_end();

		len -= bytes;
		inp += bytes;
	} while (len);
}
EXPORT_SYMBOL_GPL(poly1305_blocks_arch);

void poly1305_emit_arch(const struct poly1305_state *ctx,
			u8 mac[POLY1305_DIGEST_SIZE], const u32 nonce[4])
{
	if (!static_branch_likely(&poly1305_use_avx))
		poly1305_emit_x86_64(ctx, mac, nonce);
	else
		poly1305_emit_avx(ctx, mac, nonce);
}
EXPORT_SYMBOL_GPL(poly1305_emit_arch);

bool poly1305_is_arch_optimized(void)
{
	return static_key_enabled(&poly1305_use_avx);
}
EXPORT_SYMBOL(poly1305_is_arch_optimized);

static int __init poly1305_simd_mod_init(void)
{
	if (boot_cpu_has(X86_FEATURE_AVX) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL))
		static_branch_enable(&poly1305_use_avx);
	if (boot_cpu_has(X86_FEATURE_AVX) && boot_cpu_has(X86_FEATURE_AVX2) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL))
		static_branch_enable(&poly1305_use_avx2);
	if (boot_cpu_has(X86_FEATURE_AVX) && boot_cpu_has(X86_FEATURE_AVX2) &&
	    boot_cpu_has(X86_FEATURE_AVX512F) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM | XFEATURE_MASK_AVX512, NULL) &&
	    /* Skylake downclocks unacceptably much when using zmm, but later generations are fast. */
	    boot_cpu_data.x86_vfm != INTEL_SKYLAKE_X)
		static_branch_enable(&poly1305_use_avx512);
	return 0;
}
subsys_initcall(poly1305_simd_mod_init);

static void __exit poly1305_simd_mod_exit(void)
{
}
module_exit(poly1305_simd_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
MODULE_DESCRIPTION("Poly1305 authenticator");
