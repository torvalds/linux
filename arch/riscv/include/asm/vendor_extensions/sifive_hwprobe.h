/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_VENDOR_EXTENSIONS_SIFIVE_HWPROBE_H
#define _ASM_RISCV_VENDOR_EXTENSIONS_SIFIVE_HWPROBE_H

#include <linux/cpumask.h>

#include <uapi/asm/hwprobe.h>

#ifdef CONFIG_RISCV_ISA_VENDOR_EXT_SIFIVE
void hwprobe_isa_vendor_ext_sifive_0(struct riscv_hwprobe *pair, const struct cpumask *cpus);
#else
static inline void hwprobe_isa_vendor_ext_sifive_0(struct riscv_hwprobe *pair,
						   const struct cpumask *cpus)
{
	pair->value = 0;
}
#endif

#endif
