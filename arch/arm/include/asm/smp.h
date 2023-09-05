/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/smp.h
 *
 *  Copyright (C) 2004-2005 ARM Ltd.
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
 * Called from C code, this handles an IPI.
 */
void handle_IPI(int ipinr, struct pt_regs *regs);

/*
 * Setup the set of possible CPUs (via set_cpu_possible)
 */
extern void smp_init_cpus(void);

/*
 * Register IPI interrupts with the arch SMP code
 */
extern void set_smp_ipi_range(int ipi_base, int nr_ipi);

/*
 * Called from platform specific assembly code, this is the
 * secondary CPU entry point.
 */
asmlinkage void secondary_start_kernel(struct task_struct *task);


/*
 * Initial data for bringing up a secondary CPU.
 */
struct secondary_data {
	union {
		struct mpu_rgn_info *mpu_rgn_info;
		u64 pgdir;
	};
	unsigned long swapper_pg_dir;
	void *stack;
	struct task_struct *task;
};
extern struct secondary_data secondary_data;
extern void secondary_startup(void);
extern void secondary_startup_arm(void);

extern int __cpu_disable(void);

static inline void __cpu_die(unsigned int cpu) { }

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);
extern void arch_send_wakeup_ipi_mask(const struct cpumask *mask);

extern int register_ipi_completion(struct completion *completion, int cpu);

struct smp_operations {
#ifdef CONFIG_SMP
	/*
	 * Setup the set of possible CPUs (via set_cpu_possible)
	 */
	void (*smp_init_cpus)(void);
	/*
	 * Initialize cpu_possible map, and enable coherency
	 */
	void (*smp_prepare_cpus)(unsigned int max_cpus);

	/*
	 * Perform platform specific initialisation of the specified CPU.
	 */
	void (*smp_secondary_init)(unsigned int cpu);
	/*
	 * Boot a secondary CPU, and assign it the specified idle task.
	 * This also gives us the initial stack to use for this CPU.
	 */
	int  (*smp_boot_secondary)(unsigned int cpu, struct task_struct *idle);
#ifdef CONFIG_HOTPLUG_CPU
	int  (*cpu_kill)(unsigned int cpu);
	void (*cpu_die)(unsigned int cpu);
	bool  (*cpu_can_disable)(unsigned int cpu);
	int  (*cpu_disable)(unsigned int cpu);
#endif
#endif
};

struct of_cpu_method {
	const char *method;
	const struct smp_operations *ops;
};

#define CPU_METHOD_OF_DECLARE(name, _method, _ops)			\
	static const struct of_cpu_method __cpu_method_of_table_##name	\
		__used __section("__cpu_method_of_table")		\
		= { .method = _method, .ops = _ops }
/*
 * set platform specific SMP operations
 */
extern void smp_set_ops(const struct smp_operations *);

#endif /* ifndef __ASM_ARM_SMP_H */
