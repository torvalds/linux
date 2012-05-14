/* smp.h: Sparc specific SMP stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_SMP_H
#define _SPARC_SMP_H

#include <linux/threads.h>
#include <asm/head.h>
#include <asm/btfixup.h>

#ifndef __ASSEMBLY__

#include <linux/cpumask.h>

#endif /* __ASSEMBLY__ */

#ifdef CONFIG_SMP

#ifndef __ASSEMBLY__

#include <asm/ptrace.h>
#include <asm/asi.h>
#include <linux/atomic.h>

/*
 *	Private routines/data
 */

extern unsigned char boot_cpu_id;
extern volatile unsigned long cpu_callin_map[NR_CPUS];
extern cpumask_t smp_commenced_mask;
extern struct linux_prom_registers smp_penguin_ctable;

typedef void (*smpfunc_t)(unsigned long, unsigned long, unsigned long,
		       unsigned long, unsigned long);

void cpu_panic(void);
extern void smp4m_irq_rotate(int cpu);

/*
 *	General functions that each host system must provide.
 */

void sun4m_init_smp(void);
void sun4d_init_smp(void);

void smp_callin(void);
void smp_boot_cpus(void);
void smp_store_cpu_info(int);

void smp_resched_interrupt(void);
void smp_call_function_single_interrupt(void);
void smp_call_function_interrupt(void);

struct seq_file;
void smp_bogo(struct seq_file *);
void smp_info(struct seq_file *);

struct sparc32_ipi_ops {
	void (*cross_call)(smpfunc_t func, cpumask_t mask, unsigned long arg1,
			   unsigned long arg2, unsigned long arg3,
			   unsigned long arg4);
	void (*resched)(int cpu);
	void (*single)(int cpu);
	void (*mask_one)(int cpu);
};
extern const struct sparc32_ipi_ops *sparc32_ipi_ops;

static inline void xc0(smpfunc_t func)
{
	sparc32_ipi_ops->cross_call(func, *cpu_online_mask, 0, 0, 0, 0);
}

static inline void xc1(smpfunc_t func, unsigned long arg1)
{
	sparc32_ipi_ops->cross_call(func, *cpu_online_mask, arg1, 0, 0, 0);
}
static inline void xc2(smpfunc_t func, unsigned long arg1, unsigned long arg2)
{
	sparc32_ipi_ops->cross_call(func, *cpu_online_mask, arg1, arg2, 0, 0);
}

static inline void xc3(smpfunc_t func, unsigned long arg1, unsigned long arg2,
		       unsigned long arg3)
{
	sparc32_ipi_ops->cross_call(func, *cpu_online_mask,
				    arg1, arg2, arg3, 0);
}

static inline void xc4(smpfunc_t func, unsigned long arg1, unsigned long arg2,
		       unsigned long arg3, unsigned long arg4)
{
	sparc32_ipi_ops->cross_call(func, *cpu_online_mask,
				    arg1, arg2, arg3, arg4);
}

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

static inline int cpu_logical_map(int cpu)
{
	return cpu;
}

static inline int hard_smp4m_processor_id(void)
{
	int cpuid;

	__asm__ __volatile__("rd %%tbr, %0\n\t"
			     "srl %0, 12, %0\n\t"
			     "and %0, 3, %0\n\t" :
			     "=&r" (cpuid));
	return cpuid;
}

static inline int hard_smp4d_processor_id(void)
{
	int cpuid;

	__asm__ __volatile__("lda [%%g0] %1, %0\n\t" :
			     "=&r" (cpuid) : "i" (ASI_M_VIKING_TMP1));
	return cpuid;
}

extern inline int hard_smpleon_processor_id(void)
{
	int cpuid;
	__asm__ __volatile__("rd     %%asr17,%0\n\t"
			     "srl    %0,28,%0" :
			     "=&r" (cpuid) : );
	return cpuid;
}

extern int hard_smp_processor_id(void);

#define raw_smp_processor_id()		(current_thread_info()->cpu)

#define prof_multiplier(__cpu)		cpu_data(__cpu).multiplier
#define prof_counter(__cpu)		cpu_data(__cpu).counter

void smp_setup_cpu_possible_map(void);

#endif /* !(__ASSEMBLY__) */

/* Sparc specific messages. */
#define MSG_CROSS_CALL         0x0005       /* run func on cpus */

/* Empirical PROM processor mailbox constants.  If the per-cpu mailbox
 * contains something other than one of these then the ipi is from
 * Linux's active_kernel_processor.  This facility exists so that
 * the boot monitor can capture all the other cpus when one catches
 * a watchdog reset or the user enters the monitor using L1-A keys.
 */
#define MBOX_STOPCPU          0xFB
#define MBOX_IDLECPU          0xFC
#define MBOX_IDLECPU2         0xFD
#define MBOX_STOPCPU2         0xFE

#else /* SMP */

#define hard_smp_processor_id()		0
#define smp_setup_cpu_possible_map() do { } while (0)

#endif /* !(SMP) */
#endif /* !(_SPARC_SMP_H) */
