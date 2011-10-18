/*
 *  arch/arm/include/asm/smp.h
 *
 *  Copyright (C) 2004-2005 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_SMP_H
#define __ASM_ARM_SMP_H

#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/thread_info.h>

#ifndef CONFIG_SMP
# error "<asm/smp.h> included in non-SMP build"
#endif

#define raw_smp_processor_id() (current_thread_info()->cpu)

struct seq_file;

/*
 * generate IPI list text
 */
extern void show_ipi_list(struct seq_file *, int);

/*
 * Called from assembly code, this handles an IPI.
 */
asmlinkage void do_IPI(int ipinr, struct pt_regs *regs);

/*
 * Setup the set of possible CPUs (via set_cpu_possible)
 */
extern void smp_init_cpus(void);


/*
 * Provide a function to raise an IPI cross call on CPUs in callmap.
 */
extern void set_smp_cross_call(void (*)(const struct cpumask *, unsigned int));

/*
 * Boot a secondary CPU, and assign it the specified idle task.
 * This also gives us the initial stack to use for this CPU.
 */
extern int boot_secondary(unsigned int cpu, struct task_struct *);

/*
 * Called from platform specific assembly code, this is the
 * secondary CPU entry point.
 */
asmlinkage void secondary_start_kernel(void);

/*
 * Perform platform specific initialisation of the specified CPU.
 */
extern void platform_secondary_init(unsigned int cpu);

/*
 * Initialize cpu_possible map, and enable coherency
 */
extern void platform_smp_prepare_cpus(unsigned int);

/*
 * Initial data for bringing up a secondary CPU.
 */
struct secondary_data {
	unsigned long pgdir;
	unsigned long swapper_pg_dir;
	void *stack;
};
extern struct secondary_data secondary_data;

extern int __cpu_disable(void);
extern int platform_cpu_disable(unsigned int cpu);

extern void __cpu_die(unsigned int cpu);
extern void cpu_die(void);

extern void platform_cpu_die(unsigned int cpu);
extern int platform_cpu_kill(unsigned int cpu);
extern void platform_cpu_enable(unsigned int cpu);

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

/*
 * show local interrupt info
 */
extern void show_local_irqs(struct seq_file *, int);

extern void smp_send_all_cpu_backtrace(void);

#endif /* ifndef __ASM_ARM_SMP_H */
