/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#ifdef CONFIG_SMP

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/linkage.h>
#include <linux/threads.h>
#include <linux/cpumask.h>

struct smp_ops {
	void (*init_ipi)(void);
	void (*send_ipi_single)(int cpu, unsigned int action);
	void (*send_ipi_mask)(const struct cpumask *mask, unsigned int action);
};
extern struct smp_ops mp_ops;

extern int smp_num_siblings;
extern int num_processors;
extern int disabled_cpus;
extern cpumask_t cpu_sibling_map[];
extern cpumask_t cpu_llc_shared_map[];
extern cpumask_t cpu_core_map[];
extern cpumask_t cpu_foreign_map[];

void loongson_smp_setup(void);
void loongson_prepare_cpus(unsigned int max_cpus);
void loongson_boot_secondary(int cpu, struct task_struct *idle);
void loongson_init_secondary(void);
void loongson_smp_finish(void);
#ifdef CONFIG_HOTPLUG_CPU
int loongson_cpu_disable(void);
void loongson_cpu_die(unsigned int cpu);
#endif

static inline void __init plat_smp_setup(void)
{
	loongson_smp_setup();
}

static inline int raw_smp_processor_id(void)
{
#if defined(__VDSO__)
	extern int vdso_smp_processor_id(void)
		__compiletime_error("VDSO should not call smp_processor_id()");
	return vdso_smp_processor_id();
#else
	return current_thread_info()->cpu;
#endif
}
#define raw_smp_processor_id raw_smp_processor_id

/* Map from cpu id to sequential logical cpu number.  This will only
 * not be idempotent when cpus failed to come on-line.	*/
extern int __cpu_number_map[NR_CPUS];
#define cpu_number_map(cpu)  __cpu_number_map[cpu]

/* The reverse map from sequential logical cpu number to cpu id.  */
extern int __cpu_logical_map[NR_CPUS];
#define cpu_logical_map(cpu)  __cpu_logical_map[cpu]

#define cpu_physical_id(cpu)	cpu_logical_map(cpu)

#define ACTION_BOOT_CPU	0
#define ACTION_RESCHEDULE	1
#define ACTION_CALL_FUNCTION	2
#define ACTION_IRQ_WORK		3
#define ACTION_CLEAR_VECTOR	4
#define SMP_BOOT_CPU		BIT(ACTION_BOOT_CPU)
#define SMP_RESCHEDULE		BIT(ACTION_RESCHEDULE)
#define SMP_CALL_FUNCTION	BIT(ACTION_CALL_FUNCTION)
#define SMP_IRQ_WORK		BIT(ACTION_IRQ_WORK)
#define SMP_CLEAR_VECTOR	BIT(ACTION_CLEAR_VECTOR)

struct seq_file;

struct secondary_data {
	unsigned long stack;
	unsigned long thread_info;
};
extern struct secondary_data cpuboot_data;

extern asmlinkage void smpboot_entry(void);
extern asmlinkage void start_secondary(void);

extern void calculate_cpu_foreign_map(void);

/*
 * Generate IPI list text
 */
extern void show_ipi_list(struct seq_file *p, int prec);

static inline void arch_send_call_function_single_ipi(int cpu)
{
	mp_ops.send_ipi_single(cpu, ACTION_CALL_FUNCTION);
}

static inline void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	mp_ops.send_ipi_mask(mask, ACTION_CALL_FUNCTION);
}

#ifdef CONFIG_HOTPLUG_CPU
static inline int __cpu_disable(void)
{
	return loongson_cpu_disable();
}

static inline void __cpu_die(unsigned int cpu)
{
	loongson_cpu_die(cpu);
}
#endif

#else /* !CONFIG_SMP */
#define cpu_logical_map(cpu)	0
#endif /* CONFIG_SMP */

#endif /* __ASM_SMP_H */
