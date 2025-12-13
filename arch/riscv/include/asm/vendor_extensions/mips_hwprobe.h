/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 MIPS.
 */

#ifndef _ASM_RISCV_VENDOR_EXTENSIONS_MIPS_HWPROBE_H_
#define _ASM_RISCV_VENDOR_EXTENSIONS_MIPS_HWPROBE_H_

#include <linux/cpumask.h>
#include <uapi/asm/hwprobe.h>

#ifdef CONFIG_RISCV_ISA_VENDOR_EXT_MIPS
void hwprobe_isa_vendor_ext_mips_0(struct riscv_hwprobe *pair, const struct cpumask *cpus);
#else
static inline void hwprobe_isa_vendor_ext_mips_0(struct riscv_hwprobe *pair,
						 const struct cpumask *cpus)
{
	pair->value = 0;
}
#endif

#endif // _ASM_RISCV_VENDOR_EXTENSIONS_MIPS_HWPROBE_H_
