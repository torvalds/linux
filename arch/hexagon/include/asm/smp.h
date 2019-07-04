/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SMP definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/cpumask.h>

#define raw_smp_processor_id() (current_thread_info()->cpu)

enum ipi_message_type {
	IPI_NOP = 0,
	IPI_RESCHEDULE = 1,
	IPI_CALL_FUNC,
	IPI_CPU_STOP,
	IPI_TIMER,
};

extern void send_ipi(const struct cpumask *cpumask, enum ipi_message_type msg);
extern void smp_start_cpus(void);
extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

extern void smp_vm_unmask_irq(void *info);

#endif
