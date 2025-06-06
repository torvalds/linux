/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 by Rivos Inc.
 */
#ifndef __ASM_CPU_OPS_SBI_H
#define __ASM_CPU_OPS_SBI_H

#ifndef __ASSEMBLER__
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/threads.h>

extern const struct cpu_operations cpu_ops_sbi;

/**
 * struct sbi_hart_boot_data - Hart specific boot used during booting and
 *			       cpu hotplug.
 * @task_ptr: A pointer to the hart specific tp
 * @stack_ptr: A pointer to the hart specific sp
 */
struct sbi_hart_boot_data {
	void *task_ptr;
	void *stack_ptr;
};
#endif

#endif /* ifndef __ASM_CPU_OPS_SBI_H */
