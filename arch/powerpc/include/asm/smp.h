/* 
 * smp.h: PowerPC-specific SMP code.
 *
 * Original was a copy of sparc smp.h.  Now heavily modified
 * for PPC.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996-2001 Cort Dougan <cort@fsmlabs.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_SMP_H
#define _ASM_POWERPC_SMP_H
#ifdef __KERNEL__

#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/irqreturn.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_PPC64
#include <asm/paca.h>
#endif
#include <asm/percpu.h>

extern int boot_cpuid;
extern int spinning_secondaries;

extern void cpu_die(void);

#ifdef CONFIG_SMP

struct smp_ops_t {
	void  (*message_pass)(int cpu, int msg);
#ifdef CONFIG_PPC_SMP_MUXED_IPI
	void  (*cause_ipi)(int cpu, unsigned long data);
#endif
	int   (*probe)(void);
	int   (*kick_cpu)(int nr);
	void  (*setup_cpu)(int nr);
	void  (*bringup_done)(void);
	void  (*take_timebase)(void);
	void  (*give_timebase)(void);
	int   (*cpu_disable)(void);
	void  (*cpu_die)(unsigned int nr);
	int   (*cpu_bootable)(unsigned int nr);
};

extern void smp_send_debugger_break(void);
extern void start_secondary_resume(void);
extern void __devinit smp_generic_give_timebase(void);
extern void __devinit smp_generic_take_timebase(void);

DECLARE_PER_CPU(unsigned int, cpu_pvr);

#ifdef CONFIG_HOTPLUG_CPU
extern void migrate_irqs(void);
int generic_cpu_disable(void);
void generic_cpu_die(unsigned int cpu);
void generic_mach_cpu_die(void);
void generic_set_cpu_dead(unsigned int cpu);
void generic_set_cpu_up(unsigned int cpu);
int generic_check_cpu_restart(unsigned int cpu);

extern void inhibit_secondary_onlining(void);
extern void uninhibit_secondary_onlining(void);

#else /* HOTPLUG_CPU */
static inline void inhibit_secondary_onlining(void) {}
static inline void uninhibit_secondary_onlining(void) {}

#endif

#ifdef CONFIG_PPC64
#define raw_smp_processor_id()	(local_paca->paca_index)
#define hard_smp_processor_id() (get_paca()->hw_cpu_id)
#else
/* 32-bit */
extern int smp_hw_index[];

#define raw_smp_processor_id()	(current_thread_info()->cpu)
#define hard_smp_processor_id() 	(smp_hw_index[smp_processor_id()])

static inline int get_hard_smp_processor_id(int cpu)
{
	return smp_hw_index[cpu];
}

static inline void set_hard_smp_processor_id(int cpu, int phys)
{
	smp_hw_index[cpu] = phys;
}
#endif

DECLARE_PER_CPU(cpumask_var_t, cpu_sibling_map);
DECLARE_PER_CPU(cpumask_var_t, cpu_core_map);

static inline struct cpumask *cpu_sibling_mask(int cpu)
{
	return per_cpu(cpu_sibling_map, cpu);
}

static inline struct cpumask *cpu_core_mask(int cpu)
{
	return per_cpu(cpu_core_map, cpu);
}

extern int cpu_to_core_id(int cpu);

/* Since OpenPIC has only 4 IPIs, we use slightly different message numbers.
 *
 * Make sure this matches openpic_request_IPIs in open_pic.c, or what shows up
 * in /proc/interrupts will be wrong!!! --Troy */
#define PPC_MSG_CALL_FUNCTION   0
#define PPC_MSG_RESCHEDULE      1
#define PPC_MSG_CALL_FUNC_SINGLE	2
#define PPC_MSG_DEBUGGER_BREAK  3

/* for irq controllers that have dedicated ipis per message (4) */
extern int smp_request_message_ipi(int virq, int message);
extern const char *smp_ipi_name[];

/* for irq controllers with only a single ipi */
extern void smp_muxed_ipi_set_data(int cpu, unsigned long data);
extern void smp_muxed_ipi_message_pass(int cpu, int msg);
extern irqreturn_t smp_ipi_demux(void);

void smp_init_pSeries(void);
void smp_init_cell(void);
void smp_init_celleb(void);
void smp_setup_cpu_maps(void);

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);

#else
/* for UP */
#define hard_smp_processor_id()		get_hard_smp_processor_id(0)
#define smp_setup_cpu_maps()

#endif /* CONFIG_SMP */

#ifdef CONFIG_PPC64
static inline int get_hard_smp_processor_id(int cpu)
{
	return paca[cpu].hw_cpu_id;
}

static inline void set_hard_smp_processor_id(int cpu, int phys)
{
	paca[cpu].hw_cpu_id = phys;
}

extern void smp_release_cpus(void);

#else
/* 32-bit */
#ifndef CONFIG_SMP
extern int boot_cpuid_phys;
static inline int get_hard_smp_processor_id(int cpu)
{
	return boot_cpuid_phys;
}

static inline void set_hard_smp_processor_id(int cpu, int phys)
{
	boot_cpuid_phys = phys;
}
#endif /* !CONFIG_SMP */
#endif /* !CONFIG_PPC64 */

extern int smt_enabled_at_boot;

extern int smp_mpic_probe(void);
extern void smp_mpic_setup_cpu(int cpu);
extern int smp_generic_kick_cpu(int nr);

extern void smp_generic_give_timebase(void);
extern void smp_generic_take_timebase(void);

extern struct smp_ops_t *smp_ops;

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

/* Definitions relative to the secondary CPU spin loop
 * and entry point. Not all of them exist on both 32 and
 * 64-bit but defining them all here doesn't harm
 */
extern void generic_secondary_smp_init(void);
extern void generic_secondary_thread_init(void);
extern unsigned long __secondary_hold_spinloop;
extern unsigned long __secondary_hold_acknowledge;
extern char __secondary_hold;

extern void __early_start(void);
#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_SMP_H) */
