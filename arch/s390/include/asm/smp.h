/*
 *    Copyright IBM Corp. 1999, 2012
 *    Author(s): Denis Joseph Barrow,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
 */
#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#ifdef CONFIG_SMP

#include <asm/lowcore.h>

#define raw_smp_processor_id()	(S390_lowcore.cpu_nr)

extern struct mutex smp_cpu_state_mutex;

extern int __cpu_up(unsigned int cpu, struct task_struct *tidle);

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

extern void smp_call_online_cpu(void (*func)(void *), void *);
extern void smp_call_ipl_cpu(void (*func)(void *), void *);

extern int smp_find_processor_id(u16 address);
extern int smp_store_status(int cpu);
extern int smp_vcpu_scheduled(int cpu);
extern void smp_yield_cpu(int cpu);
extern void smp_yield(void);
extern void smp_stop_cpu(void);
extern void smp_cpu_set_polarization(int cpu, int val);
extern int smp_cpu_get_polarization(int cpu);
extern void smp_fill_possible_mask(void);

#else /* CONFIG_SMP */

static inline void smp_call_ipl_cpu(void (*func)(void *), void *data)
{
	func(data);
}

static inline void smp_call_online_cpu(void (*func)(void *), void *data)
{
	func(data);
}

static inline int smp_find_processor_id(u16 address) { return 0; }
static inline int smp_store_status(int cpu) { return 0; }
static inline int smp_vcpu_scheduled(int cpu) { return 1; }
static inline void smp_yield_cpu(int cpu) { }
static inline void smp_yield(void) { }
static inline void smp_stop_cpu(void) { }
static inline void smp_fill_possible_mask(void) { }

#endif /* CONFIG_SMP */

#ifdef CONFIG_HOTPLUG_CPU
extern int smp_rescan_cpus(void);
extern void __noreturn cpu_die(void);
extern void __cpu_die(unsigned int cpu);
extern int __cpu_disable(void);
#else
static inline int smp_rescan_cpus(void) { return 0; }
static inline void cpu_die(void) { }
#endif

#endif /* __ASM_SMP_H */
