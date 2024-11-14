/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_VENDOR_EXTENSIONS_THEAD_H
#define _ASM_RISCV_VENDOR_EXTENSIONS_THEAD_H

#include <asm/vendor_extensions.h>

#include <linux/types.h>

/*
 * Extension keys must be strictly less than RISCV_ISA_VENDOR_EXT_MAX.
 */
#define RISCV_ISA_VENDOR_EXT_XTHEADVECTOR		0

extern struct riscv_isa_vendor_ext_data_list riscv_isa_vendor_ext_list_thead;

#endif
