/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_VENDOR_EXTENSIONS_SIFIVE_H
#define _ASM_RISCV_VENDOR_EXTENSIONS_SIFIVE_H

#include <asm/vendor_extensions.h>

#include <linux/types.h>

#define RISCV_ISA_VENDOR_EXT_XSFVQMACCDOD		0
#define RISCV_ISA_VENDOR_EXT_XSFVQMACCQOQ		1
#define RISCV_ISA_VENDOR_EXT_XSFVFNRCLIPXFQF		2
#define RISCV_ISA_VENDOR_EXT_XSFVFWMACCQQQ		3

extern struct riscv_isa_vendor_ext_data_list riscv_isa_vendor_ext_list_sifive;

#endif
