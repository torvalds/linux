#ifndef _ASM_UM_BARRIER_H_
#define _ASM_UM_BARRIER_H_

#include <asm/asm.h>
#include <asm/segment.h>
#include <asm/cpufeature.h>
#include <asm/cmpxchg.h>
#include <asm/nops.h>

#include <linux/kernel.h>
#include <linux/irqflags.h>

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 */
#ifdef CONFIG_X86_32

#define mb()	alternative("lock; addl $0,0(%%esp)", "mfence", X86_FEATURE_XMM2)
#define rmb()	alternative("lock; addl $0,0(%%esp)", "lfence", X86_FEATURE_XMM2)
#define wmb()	alternative("lock; addl $0,0(%%esp)", "sfence", X86_FEATURE_XMM)

#else /* CONFIG_X86_32 */

#define mb()	asm volatile("mfence" : : : "memory")
#define rmb()	asm volatile("lfence" : : : "memory")
#define wmb()	asm volatile("sfence" : : : "memory")

#endif /* CONFIG_X86_32 */

#ifdef CONFIG_X86_PPRO_FENCE
#define dma_rmb()	rmb()
#else /* CONFIG_X86_PPRO_FENCE */
#define dma_rmb()	barrier()
#endif /* CONFIG_X86_PPRO_FENCE */
#define dma_wmb()	barrier()

#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()

#define smp_store_mb(var, value) do { WRITE_ONCE(var, value); barrier(); } while (0)

#define read_barrier_depends()		do { } while (0)
#define smp_read_barrier_depends()	do { } while (0)

/*
 * Stop RDTSC speculation. This is needed when you need to use RDTSC
 * (or get_cycles or vread that possibly accesses the TSC) in a defined
 * code region.
 *
 * (Could use an alternative three way for this if there was one.)
 */
static inline void rdtsc_barrier(void)
{
	alternative_2("", "mfence", X86_FEATURE_MFENCE_RDTSC,
			  "lfence", X86_FEATURE_LFENCE_RDTSC);
}

#endif
