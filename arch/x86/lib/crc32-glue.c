// SPDX-License-Identifier: GPL-2.0-only
/*
 * x86-optimized CRC32 functions
 *
 * Copyright (C) 2008 Intel Corporation
 * Copyright 2012 Xyratex Technology Limited
 * Copyright 2024 Google LLC
 */

#include <linux/crc32.h>
#include <linux/module.h>
#include "crc-pclmul-template.h"

static DEFINE_STATIC_KEY_FALSE(have_crc32);
static DEFINE_STATIC_KEY_FALSE(have_pclmulqdq);

DECLARE_CRC_PCLMUL_FUNCS(crc32_lsb, u32);

u32 crc32_le_arch(u32 crc, const u8 *p, size_t len)
{
	CRC_PCLMUL(crc, p, len, crc32_lsb, crc32_lsb_0xedb88320_consts,
		   have_pclmulqdq);
	return crc32_le_base(crc, p, len);
}
EXPORT_SYMBOL(crc32_le_arch);

#ifdef CONFIG_X86_64
#define CRC32_INST "crc32q %1, %q0"
#else
#define CRC32_INST "crc32l %1, %0"
#endif

/*
 * Use carryless multiply version of crc32c when buffer size is >= 512 to
 * account for FPU state save/restore overhead.
 */
#define CRC32C_PCLMUL_BREAKEVEN	512

asmlinkage u32 crc32c_x86_3way(u32 crc, const u8 *buffer, size_t len);

u32 crc32c_arch(u32 crc, const u8 *p, size_t len)
{
	size_t num_longs;

	if (!static_branch_likely(&have_crc32))
		return crc32c_base(crc, p, len);

	if (IS_ENABLED(CONFIG_X86_64) && len >= CRC32C_PCLMUL_BREAKEVEN &&
	    static_branch_likely(&have_pclmulqdq) && crypto_simd_usable()) {
		kernel_fpu_begin();
		crc = crc32c_x86_3way(crc, p, len);
		kernel_fpu_end();
		return crc;
	}

	for (num_longs = len / sizeof(unsigned long);
	     num_longs != 0; num_longs--, p += sizeof(unsigned long))
		asm(CRC32_INST : "+r" (crc) : ASM_INPUT_RM (*(unsigned long *)p));

	if (sizeof(unsigned long) > 4 && (len & 4)) {
		asm("crc32l %1, %0" : "+r" (crc) : ASM_INPUT_RM (*(u32 *)p));
		p += 4;
	}
	if (len & 2) {
		asm("crc32w %1, %0" : "+r" (crc) : ASM_INPUT_RM (*(u16 *)p));
		p += 2;
	}
	if (len & 1)
		asm("crc32b %1, %0" : "+r" (crc) : ASM_INPUT_RM (*p));

	return crc;
}
EXPORT_SYMBOL(crc32c_arch);

u32 crc32_be_arch(u32 crc, const u8 *p, size_t len)
{
	return crc32_be_base(crc, p, len);
}
EXPORT_SYMBOL(crc32_be_arch);

static int __init crc32_x86_init(void)
{
	if (boot_cpu_has(X86_FEATURE_XMM4_2))
		static_branch_enable(&have_crc32);
	if (boot_cpu_has(X86_FEATURE_PCLMULQDQ)) {
		static_branch_enable(&have_pclmulqdq);
		INIT_CRC_PCLMUL(crc32_lsb);
	}
	return 0;
}
arch_initcall(crc32_x86_init);

static void __exit crc32_x86_exit(void)
{
}
module_exit(crc32_x86_exit);

u32 crc32_optimizations(void)
{
	u32 optimizations = 0;

	if (static_key_enabled(&have_crc32))
		optimizations |= CRC32C_OPTIMIZATION;
	if (static_key_enabled(&have_pclmulqdq))
		optimizations |= CRC32_LE_OPTIMIZATION;
	return optimizations;
}
EXPORT_SYMBOL(crc32_optimizations);

MODULE_DESCRIPTION("x86-optimized CRC32 functions");
MODULE_LICENSE("GPL");
