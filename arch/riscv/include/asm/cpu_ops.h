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
 * @cpu_start:		Boots a cpu into the kernel.
 * @cpu_stop:		Makes a cpu leave the kernel. Must not fail. Called from
 *			the cpu being stopped.
 * @cpu_is_stopped:	Ensures a cpu has left the kernel. Called from another
 *			cpu.
 */
struct cpu_operations {
	int		(*cpu_start)(unsigned int cpu,
				     struct task_struct *tidle);
#ifdef CONFIG_HOTPLUG_CPU
	void		(*cpu_stop)(void);
	int		(*cpu_is_stopped)(unsigned int cpu);
#endif
};

extern const struct cpu_operations cpu_ops_spinwait;
extern const struct cpu_operations *cpu_ops;
void __init cpu_set_ops(void);

#endif /* ifndef __ASM_CPU_OPS_H */
