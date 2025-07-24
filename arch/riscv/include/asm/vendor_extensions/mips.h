/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 MIPS.
 */

#ifndef _ASM_RISCV_VENDOR_EXTENSIONS_MIPS_H
#define _ASM_RISCV_VENDOR_EXTENSIONS_MIPS_H

#include <linux/types.h>

#define RISCV_ISA_VENDOR_EXT_XMIPSEXECTL	0

#ifndef __ASSEMBLER__
struct riscv_isa_vendor_ext_data_list;
extern struct riscv_isa_vendor_ext_data_list riscv_isa_vendor_ext_list_mips;
#endif

#endif // _ASM_RISCV_VENDOR_EXTENSIONS_MIPS_H
