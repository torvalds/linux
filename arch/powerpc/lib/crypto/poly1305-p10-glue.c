// SPDX-License-Identifier: GPL-2.0
/*
 * Poly1305 authenticator algorithm, RFC7539.
 *
 * Copyright 2023- IBM Corp. All rights reserved.
 */
#include <asm/switch_to.h>
#include <crypto/internal/poly1305.h>
#include <crypto/internal/simd.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/unaligned.h>

asmlinkage void poly1305_p10le_4blocks(struct poly1305_block_state *state, const u8 *m, u32 mlen);
asmlinkage void poly1305_64s(struct poly1305_block_state *state, const u8 *m, u32 mlen, int highbit);
asmlinkage void poly1305_emit_64(const struct poly1305_state *state, const u32 nonce[4], u8 digest[POLY1305_DIGEST_SIZE]);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_p10);

static inline bool is_state_base64(struct poly1305_block_state *state)
{
	return state->core_r.precomputed_s.r64[2];
}

static void vsx_begin(void)
{
	preempt_disable();
	enable_kernel_vsx();
}

static void vsx_end(void)
{
	disable_kernel_vsx();
	preempt_enable();
}

static void convert_to_base2_44(struct poly1305_block_state *state)
{
	u8 raw_key[POLY1305_BLOCK_SIZE];
	u64 h0, h1, h2;

	if (!is_state_base64(state))
		return;

	state->core_r.precomputed_s.r64[2] = 0;
	put_unaligned_le64(state->core_r.key.r64[0], raw_key + 0);
	put_unaligned_le64(state->core_r.key.r64[1], raw_key + 8);
	poly1305_core_setkey(&state->core_r, raw_key);

	h0 = state->h.h64[0];
	h1 = state->h.h64[1];
	h2 = state->h.h64[2];
	state->h.h64[0] = h0 & 0xfffffffffffULL;
	state->h.h64[1] = h0 >> 44 | (h1 & 0xffffffULL) << 20;
	state->h.h64[2] = h1 >> 24 | h2 << 40;
}

void poly1305_block_init_arch(struct poly1305_block_state *dctx,
			      const u8 raw_key[POLY1305_BLOCK_SIZE])
{
	dctx->core_r.precomputed_s.r64[2] = 0;
	if (!static_key_enabled(&have_p10) || !crypto_simd_usable())
		return poly1305_block_init_generic(dctx, raw_key);

	dctx->core_r.precomputed_s.r64[2] = 1;
	dctx->h = (struct poly1305_state){};
	dctx->core_r.key.r64[0] = get_unaligned_le64(raw_key + 0);
	dctx->core_r.key.r64[1] = get_unaligned_le64(raw_key + 8);
}
EXPORT_SYMBOL_GPL(poly1305_block_init_arch);

void poly1305_blocks_arch(struct poly1305_block_state *state, const u8 *src,
			  unsigned int len, u32 padbit)
{
	if (!static_key_enabled(&have_p10) || !is_state_base64(state) ||
	    !crypto_simd_usable()) {
		convert_to_base2_44(state);
		return poly1305_blocks_generic(state, src, len, padbit);
	}
	vsx_begin();
	if (len >= POLY1305_BLOCK_SIZE * 4) {
		poly1305_p10le_4blocks(state, src, len);
		src += len - (len % (POLY1305_BLOCK_SIZE * 4));
		len %= POLY1305_BLOCK_SIZE * 4;
	}
	while (len >= POLY1305_BLOCK_SIZE) {
		poly1305_64s(state, src, POLY1305_BLOCK_SIZE, padbit);
		len -= POLY1305_BLOCK_SIZE;
		src += POLY1305_BLOCK_SIZE;
	}
	vsx_end();
}
EXPORT_SYMBOL_GPL(poly1305_blocks_arch);

void poly1305_emit_arch(const struct poly1305_state *state,
			u8 digest[POLY1305_DIGEST_SIZE],
			const u32 nonce[4])
{
	struct poly1305_block_state *dctx =
		container_of(state, struct poly1305_block_state, h);

	if (!static_key_enabled(&have_p10) || !is_state_base64(dctx))
		return poly1305_emit_generic(state, digest, nonce);
	poly1305_emit_64(state, nonce, digest);
}
EXPORT_SYMBOL_GPL(poly1305_emit_arch);

bool poly1305_is_arch_optimized(void)
{
	return static_key_enabled(&have_p10);
}
EXPORT_SYMBOL(poly1305_is_arch_optimized);

static int __init poly1305_p10_init(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_31))
		static_branch_enable(&have_p10);
	return 0;
}
subsys_initcall(poly1305_p10_init);

static void __exit poly1305_p10_exit(void)
{
}
module_exit(poly1305_p10_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Danny Tsen <dtsen@linux.ibm.com>");
MODULE_DESCRIPTION("Optimized Poly1305 for P10");
