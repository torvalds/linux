// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RISC-V optimized CRC-T10DIF function
 *
 * Copyright 2025 Google LLC
 */

#include <asm/hwcap.h>
#include <asm/alternative-macros.h>
#include <linux/crc-t10dif.h>
#include <linux/module.h>

#include "crc-clmul.h"

u16 crc_t10dif_arch(u16 crc, const u8 *p, size_t len)
{
	if (riscv_has_extension_likely(RISCV_ISA_EXT_ZBC))
		return crc16_msb_clmul(crc, p, len, &crc16_msb_0x8bb7_consts);
	return crc_t10dif_generic(crc, p, len);
}
EXPORT_SYMBOL(crc_t10dif_arch);

MODULE_DESCRIPTION("RISC-V optimized CRC-T10DIF function");
MODULE_LICENSE("GPL");
