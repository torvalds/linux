// SPDX-License-Identifier: GPL-2.0-only
/*
 * x86-optimized CRC32 functions
 *
 * Copyright (C) 2008 Intel Corporation
 * Copyright 2012 Xyratex Technology Limited
 * Copyright 2024 Google LLC
 */

#include <asm/cpufeatures.h>
#include <asm/simd.h>
#include <crypto/internal/simd.h>
#include <linux/crc32.h>
#include <linux/linkage.h>
#include <linux/module.h>

/* minimum size of buffer for crc32_pclmul_le_16 */
#define CRC32_PCLMUL_MIN_LEN	64

static DEFINE_STATIC_KEY_FALSE(have_crc32);
static DEFINE_STATIC_KEY_FALSE(have_pclmulqdq);

u32 crc32_pclmul_le_16(u32 crc, const u8 *buffer, size_t len);

u32 crc32_le_arch(u32 crc, const u8 *p, size_t len)
{
	if (len >= CRC32_PCLMUL_MIN_LEN + 15 &&
	    static_branch_likely(&have_pclmulqdq) && crypto_simd_usable()) {
		size_t n = -(uintptr_t)p & 15;

		/* align p to 16-byte boundary */
		if (n) {
			crc = crc32_le_base(crc, p, n);
			p += n;
			len -= n;
		}
		n = round_down(len, 16);
		kernel_fpu_begin();
		crc = crc32_pclmul_le_16(crc, p, n);
		kernel_fpu_end();
		p += n;
		len -= n;
	}
	if (len)
		crc = crc32_le_base(crc, p, len);
	return crc;
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

u32 crc32c_le_arch(u32 crc, const u8 *p, size_t len)
{
	size_t num_longs;

	if (!static_branch_likely(&have_crc32))
		return crc32c_le_base(crc, p, len);

	if (IS_ENABLED(CONFIG_X86_64) && len >= CRC32C_PCLMUL_BREAKEVEN &&
	    static_branch_likely(&have_pclmulqdq) && crypto_simd_usable()) {
		kernel_fpu_begin();
		crc = crc32c_x86_3way(crc, p, len);
		kernel_fpu_end();
		return crc;
	}

	for (num_longs = len / sizeof(unsigned long);
	     num_longs != 0; num_longs--, p += sizeof(unsigned long))
		asm(CRC32_INST : "+r" (crc) : "rm" (*(unsigned long *)p));

	for (len %= sizeof(unsigned long); len; len--, p++)
		asm("crc32b %1, %0" : "+r" (crc) : "rm" (*p));

	return crc;
}
EXPORT_SYMBOL(crc32c_le_arch);

u32 crc32_be_arch(u32 crc, const u8 *p, size_t len)
{
	return crc32_be_base(crc, p, len);
}
EXPORT_SYMBOL(crc32_be_arch);

static int __init crc32_x86_init(void)
{
	if (boot_cpu_has(X86_FEATURE_XMM4_2))
		static_branch_enable(&have_crc32);
	if (boot_cpu_has(X86_FEATURE_PCLMULQDQ))
		static_branch_enable(&have_pclmulqdq);
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
