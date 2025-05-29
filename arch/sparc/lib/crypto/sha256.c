// SPDX-License-Identifier: GPL-2.0-only
/*
 * SHA-256 accelerated using the sparc64 sha256 opcodes
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * SHA224 Support Copyright 2007 Intel Corporation <jonathan.lynch@intel.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <asm/elf.h>
#include <asm/opcodes.h>
#include <asm/pstate.h>
#include <crypto/internal/sha2.h>
#include <linux/kernel.h>
#include <linux/module.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_sha256_opcodes);

asmlinkage void sha256_sparc64_transform(u32 state[SHA256_STATE_WORDS],
					 const u8 *data, size_t nblocks);

void sha256_blocks_arch(u32 state[SHA256_STATE_WORDS],
			const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_sha256_opcodes))
		sha256_sparc64_transform(state, data, nblocks);
	else
		sha256_blocks_generic(state, data, nblocks);
}
EXPORT_SYMBOL_GPL(sha256_blocks_arch);

bool sha256_is_arch_optimized(void)
{
	return static_key_enabled(&have_sha256_opcodes);
}
EXPORT_SYMBOL_GPL(sha256_is_arch_optimized);

static int __init sha256_sparc64_mod_init(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return 0;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_SHA256))
		return 0;

	static_branch_enable(&have_sha256_opcodes);
	pr_info("Using sparc64 sha256 opcode optimized SHA-256/SHA-224 implementation\n");
	return 0;
}
subsys_initcall(sha256_sparc64_mod_init);

static void __exit sha256_sparc64_mod_exit(void)
{
}
module_exit(sha256_sparc64_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-256 accelerated using the sparc64 sha256 opcodes");
