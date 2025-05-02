// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CRC64 using [V]PCLMULQDQ instructions
 *
 * Copyright 2025 Google LLC
 */

#include <linux/crc64.h>
#include <linux/module.h>
#include "crc-pclmul-template.h"

static DEFINE_STATIC_KEY_FALSE(have_pclmulqdq);

DECLARE_CRC_PCLMUL_FUNCS(crc64_msb, u64);
DECLARE_CRC_PCLMUL_FUNCS(crc64_lsb, u64);

u64 crc64_be_arch(u64 crc, const u8 *p, size_t len)
{
	CRC_PCLMUL(crc, p, len, crc64_msb, crc64_msb_0x42f0e1eba9ea3693_consts,
		   have_pclmulqdq);
	return crc64_be_generic(crc, p, len);
}
EXPORT_SYMBOL_GPL(crc64_be_arch);

u64 crc64_nvme_arch(u64 crc, const u8 *p, size_t len)
{
	CRC_PCLMUL(crc, p, len, crc64_lsb, crc64_lsb_0x9a6c9329ac4bc9b5_consts,
		   have_pclmulqdq);
	return crc64_nvme_generic(crc, p, len);
}
EXPORT_SYMBOL_GPL(crc64_nvme_arch);

static int __init crc64_x86_init(void)
{
	if (boot_cpu_has(X86_FEATURE_PCLMULQDQ)) {
		static_branch_enable(&have_pclmulqdq);
		INIT_CRC_PCLMUL(crc64_msb);
		INIT_CRC_PCLMUL(crc64_lsb);
	}
	return 0;
}
arch_initcall(crc64_x86_init);

static void __exit crc64_x86_exit(void)
{
}
module_exit(crc64_x86_exit);

MODULE_DESCRIPTION("CRC64 using [V]PCLMULQDQ instructions");
MODULE_LICENSE("GPL");
