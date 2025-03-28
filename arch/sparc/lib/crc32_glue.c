// SPDX-License-Identifier: GPL-2.0-only
/* Glue code for CRC32C optimized for sparc64 crypto opcodes.
 *
 * This is based largely upon arch/x86/crypto/crc32c-intel.c
 *
 * Copyright (C) 2008 Intel Corporation
 * Authors: Austin Zhang <austin_zhang@linux.intel.com>
 *          Kent Liu <kent.liu@intel.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/crc32.h>
#include <asm/pstate.h>
#include <asm/elf.h>

static DEFINE_STATIC_KEY_FALSE(have_crc32c_opcode);

u32 crc32_le_arch(u32 crc, const u8 *data, size_t len)
{
	return crc32_le_base(crc, data, len);
}
EXPORT_SYMBOL(crc32_le_arch);

void crc32c_sparc64(u32 *crcp, const u64 *data, size_t len);

u32 crc32c_arch(u32 crc, const u8 *data, size_t len)
{
	size_t n = -(uintptr_t)data & 7;

	if (!static_branch_likely(&have_crc32c_opcode))
		return crc32c_base(crc, data, len);

	if (n) {
		/* Data isn't 8-byte aligned.  Align it. */
		n = min(n, len);
		crc = crc32c_base(crc, data, n);
		data += n;
		len -= n;
	}
	n = len & ~7U;
	if (n) {
		crc32c_sparc64(&crc, (const u64 *)data, n);
		data += n;
		len -= n;
	}
	if (len)
		crc = crc32c_base(crc, data, len);
	return crc;
}
EXPORT_SYMBOL(crc32c_arch);

u32 crc32_be_arch(u32 crc, const u8 *data, size_t len)
{
	return crc32_be_base(crc, data, len);
}
EXPORT_SYMBOL(crc32_be_arch);

static int __init crc32_sparc_init(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return 0;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_CRC32C))
		return 0;

	static_branch_enable(&have_crc32c_opcode);
	pr_info("Using sparc64 crc32c opcode optimized CRC32C implementation\n");
	return 0;
}
arch_initcall(crc32_sparc_init);

static void __exit crc32_sparc_exit(void)
{
}
module_exit(crc32_sparc_exit);

u32 crc32_optimizations(void)
{
	if (static_key_enabled(&have_crc32c_opcode))
		return CRC32C_OPTIMIZATION;
	return 0;
}
EXPORT_SYMBOL(crc32_optimizations);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CRC32c (Castagnoli), sparc64 crc32c opcode accelerated");
