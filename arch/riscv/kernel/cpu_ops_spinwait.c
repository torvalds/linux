// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/errno.h>
#include <linux/of.h>
#include <linux/string.h>
#include <asm/cpu_ops.h>
#include <asm/sbi.h>
#include <asm/smp.h>

const struct cpu_operations cpu_ops_spinwait;

static int spinwait_cpu_prepare(unsigned int cpuid)
{
	if (!cpu_ops_spinwait.cpu_start) {
		pr_err("cpu start method not defined for CPU [%d]\n", cpuid);
		return -ENODEV;
	}
	return 0;
}

static int spinwait_cpu_start(unsigned int cpuid, struct task_struct *tidle)
{
	/*
	 * In this protocol, all cpus boot on their own accord.  _start
	 * selects the first cpu to boot the kernel and causes the remainder
	 * of the cpus to spin in a loop waiting for their stack pointer to be
	 * setup by that main cpu.  Writing to bootdata
	 * (i.e __cpu_up_stack_pointer) signals to the spinning cpus that they
	 * can continue the boot process.
	 */
	cpu_update_secondary_bootdata(cpuid, tidle);

	return 0;
}

const struct cpu_operations cpu_ops_spinwait = {
	.name		= "spinwait",
	.cpu_prepare	= spinwait_cpu_prepare,
	.cpu_start	= spinwait_cpu_start,
};
