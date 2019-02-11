/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 by Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#include <asm/addrspace.h>

/*
 * Sync types defined by the MIPS architecture (document MD00087 table 6.5)
 * These values are used with the sync instruction to perform memory barriers.
 * Types of ordering guarantees available through the SYNC instruction:
 * - Completion Barriers
 * - Ordering Barriers
 * As compared to the completion barrier, the ordering barrier is a
 * lighter-weight operation as it does not require the specified instructions
 * before the SYNC to be already completed. Instead it only requires that those
 * specified instructions which are subsequent to the SYNC in the instruction
 * stream are never re-ordered for processing ahead of the specified
 * instructions which are before the SYNC in the instruction stream.
 * This potentially reduces how many cycles the barrier instruction must stall
 * before it completes.
 * Implementations that do not use any of the non-zero values of stype to define
 * different barriers, such as ordering barriers, must make those stype values
 * act the same as stype zero.
 */

/*
 * Completion barriers:
 * - Every synchronizable specified memory instruction (loads or stores or both)
 *   that occurs in the instruction stream before the SYNC instruction must be
 *   already globally performed before any synchronizable specified memory
 *   instructions that occur after the SYNC are allowed to be performed, with
 *   respect to any other processor or coherent I/O module.
 *
 * - The barrier does not guarantee the order in which instruction fetches are
 *   performed.
 *
 * - A stype value of zero will always be defined such that it performs the most
 *   complete set of synchronization operations that are defined.This means
 *   stype zero always does a completion barrier that affects both loads and
 *   stores preceding the SYNC instruction and both loads and stores that are
 *   subsequent to the SYNC instruction. Non-zero values of stype may be defined
 *   by the architecture or specific implementations to perform synchronization
 *   behaviors that are less complete than that of stype zero. If an
 *   implementation does not use one of these non-zero values to define a
 *   different synchronization behavior, then that non-zero value of stype must
 *   act the same as stype zero completion barrier. This allows software written
 *   for an implementation with a lighter-weight barrier to work on another
 *   implementation which only implements the stype zero completion barrier.
 *
 * - A completion barrier is required, potentially in conjunction with SSNOP (in
 *   Release 1 of the Architecture) or EHB (in Release 2 of the Architecture),
 *   to guarantee that memory reference results are visible across operating
 *   mode changes. For example, a completion barrier is required on some
 *   implementations on entry to and exit from Debug Mode to guarantee that
 *   memory effects are handled correctly.
 */

/*
 * stype 0 - A completion barrier that affects preceding loads and stores and
 * subsequent loads and stores.
 * Older instructions which must reach the load/store ordering point before the
 * SYNC instruction completes: Loads, Stores
 * Younger instructions which must reach the load/store ordering point only
 * after the SYNC instruction completes: Loads, Stores
 * Older instructions which must be globally performed when the SYNC instruction
 * completes: Loads, Stores
 */
#define STYPE_SYNC 0x0

/*
 * Ordering barriers:
 * - Every synchronizable specified memory instruction (loads or stores or both)
 *   that occurs in the instruction stream before the SYNC instruction must
 *   reach a stage in the load/store datapath after which no instruction
 *   re-ordering is possible before any synchronizable specified memory
 *   instruction which occurs after the SYNC instruction in the instruction
 *   stream reaches the same stage in the load/store datapath.
 *
 * - If any memory instruction before the SYNC instruction in program order,
 *   generates a memory request to the external memory and any memory
 *   instruction after the SYNC instruction in program order also generates a
 *   memory request to external memory, the memory request belonging to the
 *   older instruction must be globally performed before the time the memory
 *   request belonging to the younger instruction is globally performed.
 *
 * - The barrier does not guarantee the order in which instruction fetches are
 *   performed.
 */

/*
 * stype 0x10 - An ordering barrier that affects preceding loads and stores and
 * subsequent loads and stores.
 * Older instructions which must reach the load/store ordering point before the
 * SYNC instruction completes: Loads, Stores
 * Younger instructions which must reach the load/store ordering point only
 * after the SYNC instruction completes: Loads, Stores
 * Older instructions which must be globally performed when the SYNC instruction
 * completes: N/A
 */
#define STYPE_SYNC_MB 0x10


#ifdef CONFIG_CPU_HAS_SYNC
#define __sync()				\
	__asm__ __volatile__(			\
		".set	push\n\t"		\
		".set	noreorder\n\t"		\
		".set	mips2\n\t"		\
		"sync\n\t"			\
		".set	pop"			\
		: /* no output */		\
		: /* no input */		\
		: "memory")
#else
#define __sync()	do { } while(0)
#endif

#define __fast_iob()				\
	__asm__ __volatile__(			\
		".set	push\n\t"		\
		".set	noreorder\n\t"		\
		"lw	$0,%0\n\t"		\
		"nop\n\t"			\
		".set	pop"			\
		: /* no output */		\
		: "m" (*(int *)CKSEG1)		\
		: "memory")
#ifdef CONFIG_CPU_CAVIUM_OCTEON
# define OCTEON_SYNCW_STR	".set push\n.set arch=octeon\nsyncw\nsyncw\n.set pop\n"
# define __syncw()	__asm__ __volatile__(OCTEON_SYNCW_STR : : : "memory")

# define fast_wmb()	__syncw()
# define fast_rmb()	barrier()
# define fast_mb()	__sync()
# define fast_iob()	do { } while (0)
#else /* ! CONFIG_CPU_CAVIUM_OCTEON */
# define fast_wmb()	__sync()
# define fast_rmb()	__sync()
# define fast_mb()	__sync()
# ifdef CONFIG_SGI_IP28
#  define fast_iob()				\
	__asm__ __volatile__(			\
		".set	push\n\t"		\
		".set	noreorder\n\t"		\
		"lw	$0,%0\n\t"		\
		"sync\n\t"			\
		"lw	$0,%0\n\t"		\
		".set	pop"			\
		: /* no output */		\
		: "m" (*(int *)CKSEG1ADDR(0x1fa00004)) \
		: "memory")
# else
#  define fast_iob()				\
	do {					\
		__sync();			\
		__fast_iob();			\
	} while (0)
# endif
#endif /* CONFIG_CPU_CAVIUM_OCTEON */

#ifdef CONFIG_CPU_HAS_WB

#include <asm/wbflush.h>

#define mb()		wbflush()
#define iob()		wbflush()

#else /* !CONFIG_CPU_HAS_WB */

#define mb()		fast_mb()
#define iob()		fast_iob()

#endif /* !CONFIG_CPU_HAS_WB */

#define wmb()		fast_wmb()
#define rmb()		fast_rmb()

#if defined(CONFIG_WEAK_ORDERING)
# ifdef CONFIG_CPU_CAVIUM_OCTEON
#  define __smp_mb()	__sync()
#  define __smp_rmb()	barrier()
#  define __smp_wmb()	__syncw()
# else
#  define __smp_mb()	__asm__ __volatile__("sync" : : :"memory")
#  define __smp_rmb()	__asm__ __volatile__("sync" : : :"memory")
#  define __smp_wmb()	__asm__ __volatile__("sync" : : :"memory")
# endif
#else
#define __smp_mb()	barrier()
#define __smp_rmb()	barrier()
#define __smp_wmb()	barrier()
#endif

#if defined(CONFIG_WEAK_REORDERING_BEYOND_LLSC) && defined(CONFIG_SMP)
#define __WEAK_LLSC_MB		"	sync	\n"
#else
#define __WEAK_LLSC_MB		"		\n"
#endif

#define smp_llsc_mb()	__asm__ __volatile__(__WEAK_LLSC_MB : : :"memory")

#ifdef CONFIG_CPU_CAVIUM_OCTEON
#define smp_mb__before_llsc() smp_wmb()
#define __smp_mb__before_llsc() __smp_wmb()
/* Cause previous writes to become visible on all CPUs as soon as possible */
#define nudge_writes() __asm__ __volatile__(".set push\n\t"		\
					    ".set arch=octeon\n\t"	\
					    "syncw\n\t"			\
					    ".set pop" : : : "memory")
#else
#define smp_mb__before_llsc() smp_llsc_mb()
#define __smp_mb__before_llsc() smp_llsc_mb()
#define nudge_writes() mb()
#endif

#define __smp_mb__before_atomic()	__smp_mb__before_llsc()
#define __smp_mb__after_atomic()	smp_llsc_mb()

/*
 * Some Loongson 3 CPUs have a bug wherein execution of a memory access (load,
 * store or pref) in between an ll & sc can cause the sc instruction to
 * erroneously succeed, breaking atomicity. Whilst it's unusual to write code
 * containing such sequences, this bug bites harder than we might otherwise
 * expect due to reordering & speculation:
 *
 * 1) A memory access appearing prior to the ll in program order may actually
 *    be executed after the ll - this is the reordering case.
 *
 *    In order to avoid this we need to place a memory barrier (ie. a sync
 *    instruction) prior to every ll instruction, in between it & any earlier
 *    memory access instructions. Many of these cases are already covered by
 *    smp_mb__before_llsc() but for the remaining cases, typically ones in
 *    which multiple CPUs may operate on a memory location but ordering is not
 *    usually guaranteed, we use loongson_llsc_mb() below.
 *
 *    This reordering case is fixed by 3A R2 CPUs, ie. 3A2000 models and later.
 *
 * 2) If a conditional branch exists between an ll & sc with a target outside
 *    of the ll-sc loop, for example an exit upon value mismatch in cmpxchg()
 *    or similar, then misprediction of the branch may allow speculative
 *    execution of memory accesses from outside of the ll-sc loop.
 *
 *    In order to avoid this we need a memory barrier (ie. a sync instruction)
 *    at each affected branch target, for which we also use loongson_llsc_mb()
 *    defined below.
 *
 *    This case affects all current Loongson 3 CPUs.
 */
#ifdef CONFIG_CPU_LOONGSON3_WORKAROUNDS /* Loongson-3's LLSC workaround */
#define loongson_llsc_mb()	__asm__ __volatile__(__WEAK_LLSC_MB : : :"memory")
#else
#define loongson_llsc_mb()	do { } while (0)
#endif

#include <asm-generic/barrier.h>

#endif /* __ASM_BARRIER_H */
