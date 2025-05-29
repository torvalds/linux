// SPDX-License-Identifier: GPL-2.0-only
/*
 * ChaCha stream cipher (RISC-V optimized)
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Author: Jerry Shih <jerry.shih@sifive.com>
 */

#include <asm/simd.h>
#include <asm/vector.h>
#include <crypto/chacha.h>
#include <crypto/internal/simd.h>
#include <linux/linkage.h>
#include <linux/module.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(use_zvkb);

asmlinkage void chacha_zvkb(struct chacha_state *state, const u8 *in, u8 *out,
			    size_t nblocks, int nrounds);

void hchacha_block_arch(const struct chacha_state *state,
			u32 out[HCHACHA_OUT_WORDS], int nrounds)
{
	hchacha_block_generic(state, out, nrounds);
}
EXPORT_SYMBOL(hchacha_block_arch);

void chacha_crypt_arch(struct chacha_state *state, u8 *dst, const u8 *src,
		       unsigned int bytes, int nrounds)
{
	u8 block_buffer[CHACHA_BLOCK_SIZE];
	unsigned int full_blocks = bytes / CHACHA_BLOCK_SIZE;
	unsigned int tail_bytes = bytes % CHACHA_BLOCK_SIZE;

	if (!static_branch_likely(&use_zvkb) || !crypto_simd_usable())
		return chacha_crypt_generic(state, dst, src, bytes, nrounds);

	kernel_vector_begin();
	if (full_blocks) {
		chacha_zvkb(state, src, dst, full_blocks, nrounds);
		src += full_blocks * CHACHA_BLOCK_SIZE;
		dst += full_blocks * CHACHA_BLOCK_SIZE;
	}
	if (tail_bytes) {
		memcpy(block_buffer, src, tail_bytes);
		chacha_zvkb(state, block_buffer, block_buffer, 1, nrounds);
		memcpy(dst, block_buffer, tail_bytes);
	}
	kernel_vector_end();
}
EXPORT_SYMBOL(chacha_crypt_arch);

bool chacha_is_arch_optimized(void)
{
	return static_key_enabled(&use_zvkb);
}
EXPORT_SYMBOL(chacha_is_arch_optimized);

static int __init riscv64_chacha_mod_init(void)
{
	if (riscv_isa_extension_available(NULL, ZVKB) &&
	    riscv_vector_vlen() >= 128)
		static_branch_enable(&use_zvkb);
	return 0;
}
subsys_initcall(riscv64_chacha_mod_init);

static void __exit riscv64_chacha_mod_exit(void)
{
}
module_exit(riscv64_chacha_mod_exit);

MODULE_DESCRIPTION("ChaCha stream cipher (RISC-V optimized)");
MODULE_AUTHOR("Jerry Shih <jerry.shih@sifive.com>");
MODULE_LICENSE("GPL");
