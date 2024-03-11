// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/errno.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/sched/task_stack.h>
#include <asm/cpu_ops.h>
#include <asm/sbi.h>
#include <asm/smp.h>

#include "head.h"

const struct cpu_operations cpu_ops_spinwait;
void *__cpu_spinwait_stack_pointer[NR_CPUS] __section(".data");
void *__cpu_spinwait_task_pointer[NR_CPUS] __section(".data");

static void cpu_update_secondary_bootdata(unsigned int cpuid,
				   struct task_struct *tidle)
{
	unsigned long hartid = cpuid_to_hartid_map(cpuid);

	/*
	 * The hartid must be less than NR_CPUS to avoid out-of-bound access
	 * errors for __cpu_spinwait_stack/task_pointer. That is not always possible
	 * for platforms with discontiguous hartid numbering scheme. That's why
	 * spinwait booting is not the recommended approach for any platforms
	 * booting Linux in S-mode and can be disabled in the future.
	 */
	if (hartid == INVALID_HARTID || hartid >= (unsigned long) NR_CPUS)
		return;

	/* Make sure tidle is updated */
	smp_mb();
	WRITE_ONCE(__cpu_spinwait_stack_pointer[hartid],
		   task_stack_page(tidle) + THREAD_SIZE);
	WRITE_ONCE(__cpu_spinwait_task_pointer[hartid], tidle);
}

static int spinwait_cpu_start(unsigned int cpuid, struct task_struct *tidle)
{
	/*
	 * In this protocol, all cpus boot on their own accord.  _start
	 * selects the first cpu to boot the kernel and causes the remainder
	 * of the cpus to spin in a loop waiting for their stack pointer to be
	 * setup by that main cpu.  Writing to bootdata
	 * (i.e __cpu_spinwait_stack_pointer) signals to the spinning cpus that they
	 * can continue the boot process.
	 */
	cpu_update_secondary_bootdata(cpuid, tidle);

	return 0;
}

const struct cpu_operations cpu_ops_spinwait = {
	.cpu_start	= spinwait_cpu_start,
};
