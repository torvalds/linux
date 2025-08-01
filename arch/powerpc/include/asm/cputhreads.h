/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_CPUTHREADS_H
#define _ASM_POWERPC_CPUTHREADS_H

#ifndef __ASSEMBLER__
#include <linux/cpumask.h>
#include <asm/cpu_has_feature.h>

/*
 * Mapping of threads to cores
 *
 * Note: This implementation is limited to a power of 2 number of
 * threads per core and the same number for each core in the system
 * (though it would work if some processors had less threads as long
 * as the CPU numbers are still allocated, just not brought online).
 *
 * However, the API allows for a different implementation in the future
 * if needed, as long as you only use the functions and not the variables
 * directly.
 */

#ifdef CONFIG_SMP
extern int threads_per_core;
extern int threads_per_subcore;
extern int threads_shift;
extern cpumask_t threads_core_mask;
#else
#define threads_per_core	1
#define threads_per_subcore	1
#define threads_shift		0
#define has_big_cores		0
#define threads_core_mask	(*get_cpu_mask(0))
#endif

static inline int cpu_nr_cores(void)
{
	return nr_cpu_ids >> threads_shift;
}

#ifdef CONFIG_SMP
int cpu_core_index_of_thread(int cpu);
int cpu_first_thread_of_core(int core);
#else
static inline int cpu_core_index_of_thread(int cpu) { return cpu; }
static inline int cpu_first_thread_of_core(int core) { return core; }
#endif

static inline int cpu_thread_in_core(int cpu)
{
	return cpu & (threads_per_core - 1);
}

static inline int cpu_thread_in_subcore(int cpu)
{
	return cpu & (threads_per_subcore - 1);
}

static inline int cpu_first_thread_sibling(int cpu)
{
	return cpu & ~(threads_per_core - 1);
}

static inline int cpu_last_thread_sibling(int cpu)
{
	return cpu | (threads_per_core - 1);
}

/*
 * tlb_thread_siblings are siblings which share a TLB. This is not
 * architected, is not something a hypervisor could emulate and a future
 * CPU may change behaviour even in compat mode, so this should only be
 * used on PowerNV, and only with care.
 */
static inline int cpu_first_tlb_thread_sibling(int cpu)
{
	if (cpu_has_feature(CPU_FTR_ARCH_300) && (threads_per_core == 8))
		return cpu & ~0x6;	/* Big Core */
	else
		return cpu_first_thread_sibling(cpu);
}

static inline int cpu_last_tlb_thread_sibling(int cpu)
{
	if (cpu_has_feature(CPU_FTR_ARCH_300) && (threads_per_core == 8))
		return cpu | 0x6;	/* Big Core */
	else
		return cpu_last_thread_sibling(cpu);
}

static inline int cpu_tlb_thread_sibling_step(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_300) && (threads_per_core == 8))
		return 2;		/* Big Core */
	else
		return 1;
}

static inline u32 get_tensr(void)
{
#ifdef	CONFIG_BOOKE
	if (cpu_has_feature(CPU_FTR_SMT))
		return mfspr(SPRN_TENSR);
#endif
	return 1;
}

void book3e_start_thread(int thread, unsigned long addr);
void book3e_stop_thread(int thread);

#endif /* __ASSEMBLER__ */

#define INVALID_THREAD_HWID	0x0fff

#endif /* _ASM_POWERPC_CPUTHREADS_H */

