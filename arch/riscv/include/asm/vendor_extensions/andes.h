/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_VENDOR_EXTENSIONS_ANDES_H
#define _ASM_RISCV_VENDOR_EXTENSIONS_ANDES_H

#include <asm/vendor_extensions.h>

#include <linux/types.h>

#define RISCV_ISA_VENDOR_EXT_XANDESPMU		0

/*
 * Extension keys should be strictly less than max.
 * It is safe to increment this when necessary.
 */
#define RISCV_ISA_VENDOR_EXT_MAX_ANDES			32

extern struct riscv_isa_vendor_ext_data_list riscv_isa_vendor_ext_list_andes;

#endif
