// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CRC-T10DIF using [V]PCLMULQDQ instructions
 *
 * Copyright 2024 Google LLC
 */

#include <linux/crc-t10dif.h>
#include <linux/module.h>
#include "crc-pclmul-template.h"

static DEFINE_STATIC_KEY_FALSE(have_pclmulqdq);

DECLARE_CRC_PCLMUL_FUNCS(crc16_msb, u16);

u16 crc_t10dif_arch(u16 crc, const u8 *p, size_t len)
{
	CRC_PCLMUL(crc, p, len, crc16_msb, crc16_msb_0x8bb7_consts,
		   have_pclmulqdq);
	return crc_t10dif_generic(crc, p, len);
}
EXPORT_SYMBOL(crc_t10dif_arch);

static int __init crc_t10dif_x86_init(void)
{
	if (boot_cpu_has(X86_FEATURE_PCLMULQDQ)) {
		static_branch_enable(&have_pclmulqdq);
		INIT_CRC_PCLMUL(crc16_msb);
	}
	return 0;
}
arch_initcall(crc_t10dif_x86_init);

static void __exit crc_t10dif_x86_exit(void)
{
}
module_exit(crc_t10dif_x86_exit);

MODULE_DESCRIPTION("CRC-T10DIF using [V]PCLMULQDQ instructions");
MODULE_LICENSE("GPL");
