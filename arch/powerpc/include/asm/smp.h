/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 
 * smp.h: PowerPC-specific SMP code.
 *
 * Original was a copy of sparc smp.h.  Now heavily modified
 * for PPC.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996-2001 Cort Dougan <cort@fsmlabs.com>
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
extern u32 *cpu_to_phys_id;
extern bool coregroup_enabled;

extern int cpu_to_chip_id(int cpu);

#ifdef CONFIG_SMP

struct smp_ops_t {
	void  (*message_pass)(int cpu, int msg);
#ifdef CONFIG_PPC_SMP_MUXED_IPI
	void  (*cause_ipi)(int cpu);
#endif
	int   (*cause_nmi_ipi)(int cpu);
	void  (*probe)(void);
	int   (*kick_cpu)(int nr);
	int   (*prepare_cpu)(int nr);
	void  (*setup_cpu)(int nr);
	void  (*bringup_done)(void);
	void  (*take_timebase)(void);
	void  (*give_timebase)(void);
	int   (*cpu_disable)(void);
	void  (*cpu_die)(unsigned int nr);
	int   (*cpu_bootable)(unsigned int nr);
#ifdef CONFIG_HOTPLUG_CPU
	void  (*cpu_offline_self)(void);
#endif
};

extern int smp_send_nmi_ipi(int cpu, void (*fn)(struct pt_regs *), u64 delay_us);
extern int smp_send_safe_nmi_ipi(int cpu, void (*fn)(struct pt_regs *), u64 delay_us);
extern void smp_send_debugger_break(void);
extern void start_secondary_resume(void);
extern void smp_generic_give_timebase(void);
extern void smp_generic_take_timebase(void);

DECLARE_PER_CPU(unsigned int, cpu_pvr);

#ifdef CONFIG_HOTPLUG_CPU
int generic_cpu_disable(void);
void generic_cpu_die(unsigned int cpu);
void generic_set_cpu_dead(unsigned int cpu);
void generic_set_cpu_up(unsigned int cpu);
int generic_check_cpu_restart(unsigned int cpu);
int is_cpu_dead(unsigned int cpu);
#else
#define generic_set_cpu_up(i)	do { } while (0)
#endif

#ifdef CONFIG_PPC64
#define raw_smp_processor_id()	(local_paca->paca_index)
#define hard_smp_processor_id() (get_paca()->hw_cpu_id)
#else
/* 32-bit */
extern int smp_hw_index[];

/*
 * This is particularly ugly: it appears we can't actually get the definition
 * of task_struct here, but we need access to the CPU this task is running on.
 * Instead of using task_struct we're using _TASK_CPU which is extracted from
 * asm-offsets.h by kbuild to get the current processor ID.
 *
 * This also needs to be safeguarded when building asm-offsets.s because at
 * that time _TASK_CPU is not defined yet. It could have been guarded by
 * _TASK_CPU itself, but we want the build to fail if _TASK_CPU is missing
 * when building something else than asm-offsets.s
 */
#ifdef GENERATING_ASM_OFFSETS
#define raw_smp_processor_id()		(0)
#else
#define raw_smp_processor_id()		(*(unsigned int *)((void *)current + _TASK_CPU))
#endif
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
DECLARE_PER_CPU(cpumask_var_t, cpu_l2_cache_map);
DECLARE_PER_CPU(cpumask_var_t, cpu_core_map);
DECLARE_PER_CPU(cpumask_var_t, cpu_smallcore_map);

static inline struct cpumask *cpu_sibling_mask(int cpu)
{
	return per_cpu(cpu_sibling_map, cpu);
}

static inline struct cpumask *cpu_l2_cache_mask(int cpu)
{
	return per_cpu(cpu_l2_cache_map, cpu);
}

static inline struct cpumask *cpu_smallcore_mask(int cpu)
{
	return per_cpu(cpu_smallcore_map, cpu);
}

extern int cpu_to_core_id(int cpu);

extern bool has_big_cores;

#define cpu_smt_mask cpu_smt_mask
#ifdef CONFIG_SCHED_SMT
static inline const struct cpumask *cpu_smt_mask(int cpu)
{
	if (has_big_cores)
		return per_cpu(cpu_smallcore_map, cpu);

	return per_cpu(cpu_sibling_map, cpu);
}
#endif /* CONFIG_SCHED_SMT */

/* Since OpenPIC has only 4 IPIs, we use slightly different message numbers.
 *
 * Make sure this matches openpic_request_IPIs in open_pic.c, or what shows up
 * in /proc/interrupts will be wrong!!! --Troy */
#define PPC_MSG_CALL_FUNCTION	0
#define PPC_MSG_RESCHEDULE	1
#define PPC_MSG_TICK_BROADCAST	2
#define PPC_MSG_NMI_IPI		3

/* This is only used by the powernv kernel */
#define PPC_MSG_RM_HOST_ACTION	4

#define NMI_IPI_ALL_OTHERS		-2

#ifdef CONFIG_NMI_IPI
extern int smp_handle_nmi_ipi(struct pt_regs *regs);
#else
static inline int smp_handle_nmi_ipi(struct pt_regs *regs) { return 0; }
#endif

/* for irq controllers that have dedicated ipis per message (4) */
extern int smp_request_message_ipi(int virq, int message);
extern const char *smp_ipi_name[];

/* for irq controllers with only a single ipi */
extern void smp_muxed_ipi_message_pass(int cpu, int msg);
extern void smp_muxed_ipi_set_message(int cpu, int msg);
extern irqreturn_t smp_ipi_demux(void);
extern irqreturn_t smp_ipi_demux_relaxed(void);

void smp_init_pSeries(void);
void smp_init_cell(void);
void smp_setup_cpu_maps(void);

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);

#else
/* for UP */
#define hard_smp_processor_id()		get_hard_smp_processor_id(0)
#define smp_setup_cpu_maps()
static inline void inhibit_secondary_onlining(void) {}
static inline void uninhibit_secondary_onlining(void) {}
static inline const struct cpumask *cpu_sibling_mask(int cpu)
{
	return cpumask_of(cpu);
}

static inline const struct cpumask *cpu_smallcore_mask(int cpu)
{
	return cpumask_of(cpu);
}

#endif /* CONFIG_SMP */

#ifdef CONFIG_PPC64
static inline int get_hard_smp_processor_id(int cpu)
{
	return paca_ptrs[cpu]->hw_cpu_id;
}

static inline void set_hard_smp_processor_id(int cpu, int phys)
{
	paca_ptrs[cpu]->hw_cpu_id = phys;
}
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

#if defined(CONFIG_PPC64) && (defined(CONFIG_SMP) || defined(CONFIG_KEXEC_CORE))
extern void smp_release_cpus(void);
#else
static inline void smp_release_cpus(void) { };
#endif

extern int smt_enabled_at_boot;

extern void smp_mpic_probe(void);
extern void smp_mpic_setup_cpu(int cpu);
extern int smp_generic_kick_cpu(int nr);
extern int smp_generic_cpu_bootable(unsigned int nr);


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
extern unsigned long __secondary_hold_spinloop;
extern unsigned long __secondary_hold_acknowledge;
extern char __secondary_hold;
extern unsigned int booting_thread_hwid;

extern void __early_start(void);
#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_SMP_H) */
