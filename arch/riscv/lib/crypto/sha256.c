// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-256 (RISC-V accelerated)
 *
 * Copyright (C) 2022 VRULL GmbH
 * Author: Heiko Stuebner <heiko.stuebner@vrull.eu>
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Author: Jerry Shih <jerry.shih@sifive.com>
 */

#include <asm/vector.h>
#include <crypto/internal/sha2.h>
#include <linux/kernel.h>
#include <linux/module.h>

asmlinkage void sha256_transform_zvknha_or_zvknhb_zvkb(
	u32 state[SHA256_STATE_WORDS], const u8 *data, size_t nblocks);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_extensions);

void sha256_blocks_simd(u32 state[SHA256_STATE_WORDS],
			const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_extensions)) {
		kernel_vector_begin();
		sha256_transform_zvknha_or_zvknhb_zvkb(state, data, nblocks);
		kernel_vector_end();
	} else {
		sha256_blocks_generic(state, data, nblocks);
	}
}
EXPORT_SYMBOL_GPL(sha256_blocks_simd);

void sha256_blocks_arch(u32 state[SHA256_STATE_WORDS],
			const u8 *data, size_t nblocks)
{
	sha256_blocks_generic(state, data, nblocks);
}
EXPORT_SYMBOL_GPL(sha256_blocks_arch);

bool sha256_is_arch_optimized(void)
{
	return static_key_enabled(&have_extensions);
}
EXPORT_SYMBOL_GPL(sha256_is_arch_optimized);

static int __init riscv64_sha256_mod_init(void)
{
	/* Both zvknha and zvknhb provide the SHA-256 instructions. */
	if ((riscv_isa_extension_available(NULL, ZVKNHA) ||
	     riscv_isa_extension_available(NULL, ZVKNHB)) &&
	    riscv_isa_extension_available(NULL, ZVKB) &&
	    riscv_vector_vlen() >= 128)
		static_branch_enable(&have_extensions);
	return 0;
}
subsys_initcall(riscv64_sha256_mod_init);

static void __exit riscv64_sha256_mod_exit(void)
{
}
module_exit(riscv64_sha256_mod_exit);

MODULE_DESCRIPTION("SHA-256 (RISC-V accelerated)");
MODULE_AUTHOR("Heiko Stuebner <heiko.stuebner@vrull.eu>");
MODULE_LICENSE("GPL");
