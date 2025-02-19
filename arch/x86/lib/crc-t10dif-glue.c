// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CRC-T10DIF using PCLMULQDQ instructions
 *
 * Copyright 2024 Google LLC
 */

#include <asm/cpufeatures.h>
#include <asm/simd.h>
#include <crypto/internal/simd.h>
#include <linux/crc-t10dif.h>
#include <linux/module.h>

static DEFINE_STATIC_KEY_FALSE(have_pclmulqdq);

asmlinkage u16 crc_t10dif_pcl(u16 init_crc, const u8 *buf, size_t len);

u16 crc_t10dif_arch(u16 crc, const u8 *p, size_t len)
{
	if (len >= 16 &&
	    static_key_enabled(&have_pclmulqdq) && crypto_simd_usable()) {
		kernel_fpu_begin();
		crc = crc_t10dif_pcl(crc, p, len);
		kernel_fpu_end();
		return crc;
	}
	return crc_t10dif_generic(crc, p, len);
}
EXPORT_SYMBOL(crc_t10dif_arch);

static int __init crc_t10dif_x86_init(void)
{
	if (boot_cpu_has(X86_FEATURE_PCLMULQDQ))
		static_branch_enable(&have_pclmulqdq);
	return 0;
}
arch_initcall(crc_t10dif_x86_init);

static void __exit crc_t10dif_x86_exit(void)
{
}
module_exit(crc_t10dif_x86_exit);

bool crc_t10dif_is_optimized(void)
{
	return static_key_enabled(&have_pclmulqdq);
}
EXPORT_SYMBOL(crc_t10dif_is_optimized);

MODULE_DESCRIPTION("CRC-T10DIF using PCLMULQDQ instructions");
MODULE_LICENSE("GPL");
