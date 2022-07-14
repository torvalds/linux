// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <asm/cpu_ops.h>
#include <asm/cpu_ops_sbi.h>
#include <asm/sbi.h>
#include <asm/smp.h>

const struct cpu_operations *cpu_ops[NR_CPUS] __ro_after_init;

extern const struct cpu_operations cpu_ops_sbi;
#ifndef CONFIG_RISCV_BOOT_SPINWAIT
const struct cpu_operations cpu_ops_spinwait = {
	.name		= "",
	.cpu_prepare	= NULL,
	.cpu_start	= NULL,
};
#endif

void __init cpu_set_ops(int cpuid)
{
#if IS_ENABLED(CONFIG_RISCV_SBI)
	if (sbi_probe_extension(SBI_EXT_HSM) > 0) {
		if (!cpuid)
			pr_info("SBI HSM extension detected\n");
		cpu_ops[cpuid] = &cpu_ops_sbi;
	} else
#endif
		cpu_ops[cpuid] = &cpu_ops_spinwait;
}
