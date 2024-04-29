/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
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

#include <linux/jump_label.h>

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

int riscv_hartid_to_cpuid(unsigned long hartid);

/* Enable IPI for CPU hotplug */
void riscv_ipi_enable(void);

/* Disable IPI for CPU hotplug */
void riscv_ipi_disable(void);

/* Check if IPI interrupt numbers are available */
bool riscv_ipi_have_virq_range(void);

/* Set the IPI interrupt numbers for arch (called by irqchip drivers) */
void riscv_ipi_set_virq_range(int virq, int nr);

/* Check other CPUs stop or not */
bool smp_crash_stop_failed(void);

/* Secondary hart entry */
asmlinkage void smp_callin(void);

/*
 * Obtains the hart ID of the currently executing task.  This relies on
 * THREAD_INFO_IN_TASK, but we define that unconditionally.
 */
#define raw_smp_processor_id() (current_thread_info()->cpu)

#if defined CONFIG_HOTPLUG_CPU
int __cpu_disable(void);
static inline void __cpu_die(unsigned int cpu) { }
#endif /* CONFIG_HOTPLUG_CPU */

#else

static inline void show_ipi_stats(struct seq_file *p, int prec)
{
}

static inline int riscv_hartid_to_cpuid(unsigned long hartid)
{
	if (hartid == boot_cpu_hartid)
		return 0;

	return -1;
}
static inline unsigned long cpuid_to_hartid_map(int cpu)
{
	return boot_cpu_hartid;
}

static inline void riscv_ipi_enable(void)
{
}

static inline void riscv_ipi_disable(void)
{
}

static inline bool riscv_ipi_have_virq_range(void)
{
	return false;
}

static inline void riscv_ipi_set_virq_range(int virq, int nr)
{
}

#endif /* CONFIG_SMP */

#if defined(CONFIG_HOTPLUG_CPU) && (CONFIG_SMP)
bool cpu_has_hotplug(unsigned int cpu);
#else
static inline bool cpu_has_hotplug(unsigned int cpu)
{
	return false;
}
#endif

#endif /* _ASM_RISCV_SMP_H */
