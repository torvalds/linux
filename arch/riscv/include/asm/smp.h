/*
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#ifndef _ASM_RISCV_SMP_H
#define _ASM_RISCV_SMP_H

#include <linux/cpumask.h>
#include <linux/irqreturn.h>
#include <linux/thread_info.h>

#define INVALID_HARTID ULONG_MAX

struct seq_file;
extern unsigned long boot_cpu_hartid;

#ifdef CONFIG_SMP
/*
 * Mapping between linux logical cpu index and hartid.
 */
extern unsigned long __cpuid_to_hartid_map[NR_CPUS];
#define cpuid_to_hartid_map(cpu)    __cpuid_to_hartid_map[cpu]

/* print IPI stats */
void show_ipi_stats(struct seq_file *p, int prec);

/* SMP initialization hook for setup_arch */
void __init setup_smp(void);

/* Hook for the generic smp_call_function_many() routine. */
void arch_send_call_function_ipi_mask(struct cpumask *mask);

/* Hook for the generic smp_call_function_single() routine. */
void arch_send_call_function_single_ipi(int cpu);

int riscv_hartid_to_cpuid(int hartid);
void riscv_cpuid_to_hartid_mask(const struct cpumask *in, struct cpumask *out);

/*
 * Obtains the hart ID of the currently executing task.  This relies on
 * THREAD_INFO_IN_TASK, but we define that unconditionally.
 */
#define raw_smp_processor_id() (current_thread_info()->cpu)

#else

static inline void show_ipi_stats(struct seq_file *p, int prec)
{
}

static inline int riscv_hartid_to_cpuid(int hartid)
{
	if (hartid == boot_cpu_hartid)
		return 0;

	return -1;
}
static inline unsigned long cpuid_to_hartid_map(int cpu)
{
	return boot_cpu_hartid;
}

static inline void riscv_cpuid_to_hartid_mask(const struct cpumask *in,
					      struct cpumask *out)
{
	cpumask_set_cpu(cpuid_to_hartid_map(0), out);
}

#endif /* CONFIG_SMP */
#endif /* _ASM_RISCV_SMP_H */
