// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-256 optimized for ARM64
 *
 * Copyright 2025 Google LLC
 */
#include <asm/neon.h>
#include <crypto/internal/sha2.h>
#include <linux/kernel.h>
#include <linux/module.h>

asmlinkage void sha256_blocks_arch(u32 state[SHA256_STATE_WORDS],
				   const u8 *data, size_t nblocks);
EXPORT_SYMBOL_GPL(sha256_blocks_arch);
asmlinkage void sha256_block_neon(u32 state[SHA256_STATE_WORDS],
				  const u8 *data, size_t nblocks);
asmlinkage size_t __sha256_ce_transform(u32 state[SHA256_STATE_WORDS],
					const u8 *data, size_t nblocks);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_ce);

void sha256_blocks_simd(u32 state[SHA256_STATE_WORDS],
			const u8 *data, size_t nblocks)
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_neon)) {
		if (static_branch_likely(&have_ce)) {
			do {
				size_t rem;

				kernel_neon_begin();
				rem = __sha256_ce_transform(state,
							    data, nblocks);
				kernel_neon_end();
				data += (nblocks - rem) * SHA256_BLOCK_SIZE;
				nblocks = rem;
			} while (nblocks);
		} else {
			kernel_neon_begin();
			sha256_block_neon(state, data, nblocks);
			kernel_neon_end();
		}
	} else {
		sha256_blocks_arch(state, data, nblocks);
	}
}
EXPORT_SYMBOL_GPL(sha256_blocks_simd);

bool sha256_is_arch_optimized(void)
{
	/* We always can use at least the ARM64 scalar implementation. */
	return true;
}
EXPORT_SYMBOL_GPL(sha256_is_arch_optimized);

static int __init sha256_arm64_mod_init(void)
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    cpu_have_named_feature(ASIMD)) {
		static_branch_enable(&have_neon);
		if (cpu_have_named_feature(SHA2))
			static_branch_enable(&have_ce);
	}
	return 0;
}
subsys_initcall(sha256_arm64_mod_init);

static void __exit sha256_arm64_mod_exit(void)
{
}
module_exit(sha256_arm64_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-256 optimized for ARM64");
