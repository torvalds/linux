// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/cpu_ops.h>
#include <asm/sbi.h>
#include <asm/smp.h>

const struct cpu_operations *cpu_ops[NR_CPUS] __ro_after_init;

void *__cpu_up_stack_pointer[NR_CPUS] __section(".data");
void *__cpu_up_task_pointer[NR_CPUS] __section(".data");

extern const struct cpu_operations cpu_ops_sbi;
extern const struct cpu_operations cpu_ops_spinwait;

void cpu_update_secondary_bootdata(unsigned int cpuid,
				   struct task_struct *tidle)
{
	int hartid = cpuid_to_hartid_map(cpuid);

	/* Make sure tidle is updated */
	smp_mb();
	WRITE_ONCE(__cpu_up_stack_pointer[hartid],
		   task_stack_page(tidle) + THREAD_SIZE);
	WRITE_ONCE(__cpu_up_task_pointer[hartid], tidle);
}

void __init cpu_set_ops(int cpuid)
{
#if IS_ENABLED(CONFIG_RISCV_SBI)
	if (sbi_probe_extension(SBI_EXT_HSM) > 0) {
		if (!cpuid)
			pr_info("SBI v0.2 HSM extension detected\n");
		cpu_ops[cpuid] = &cpu_ops_sbi;
	} else
#endif
		cpu_ops[cpuid] = &cpu_ops_spinwait;
}
