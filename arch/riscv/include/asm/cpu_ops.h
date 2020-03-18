/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 * Based on arch/arm64/include/asm/cpu_ops.h
 */
#ifndef __ASM_CPU_OPS_H
#define __ASM_CPU_OPS_H

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/threads.h>

/**
 * struct cpu_operations - Callback operations for hotplugging CPUs.
 *
 * @name:		Name of the boot protocol.
 * @cpu_prepare:	Early one-time preparation step for a cpu. If there
 *			is a mechanism for doing so, tests whether it is
 *			possible to boot the given HART.
 * @cpu_start:		Boots a cpu into the kernel.
 */
struct cpu_operations {
	const char	*name;
	int		(*cpu_prepare)(unsigned int cpu);
	int		(*cpu_start)(unsigned int cpu,
				     struct task_struct *tidle);
};

extern const struct cpu_operations *cpu_ops[NR_CPUS];
void __init cpu_set_ops(int cpu);
void cpu_update_secondary_bootdata(unsigned int cpuid,
				   struct task_struct *tidle);

#endif /* ifndef __ASM_CPU_OPS_H */
