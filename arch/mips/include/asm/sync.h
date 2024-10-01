/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __MIPS_ASM_SYNC_H__
#define __MIPS_ASM_SYNC_H__

/*
 * sync types are defined by the MIPS64 Instruction Set documentation in Volume
 * II-A of the MIPS Architecture Reference Manual, which can be found here:
 *
 *   https://www.mips.com/?do-download=the-mips64-instruction-set-v6-06
 *
 * Two types of barrier are provided:
 *
 *   1) Completion barriers, which ensure that a memory operation has actually
 *      completed & often involve stalling the CPU pipeline to do so.
 *
 *   2) Ordering barriers, which only ensure that affected memory operations
 *      won't be reordered in the CPU pipeline in a manner that violates the
 *      restrictions imposed by the barrier.
 *
 * Ordering barriers can be more efficient than completion barriers, since:
 *
 *   a) Ordering barriers only require memory access instructions which preceed
 *      them in program order (older instructions) to reach a point in the
 *      load/store datapath beyond which reordering is not possible before
 *      allowing memory access instructions which follow them (younger
 *      instructions) to be performed.  That is, older instructions don't
 *      actually need to complete - they just need to get far enough that all
 *      other coherent CPUs will observe their completion before they observe
 *      the effects of younger instructions.
 *
 *   b) Multiple variants of ordering barrier are provided which allow the
 *      effects to be restricted to different combinations of older or younger
 *      loads or stores. By way of example, if we only care that stores older
 *      than a barrier are observed prior to stores that are younger than a
 *      barrier & don't care about the ordering of loads then the 'wmb'
 *      ordering barrier can be used. Limiting the barrier's effects to stores
 *      allows loads to continue unaffected & potentially allows the CPU to
 *      make progress faster than if younger loads had to wait for older stores
 *      to complete.
 */

/*
 * No sync instruction at all; used to allow code to nullify the effect of the
 * __SYNC() macro without needing lots of #ifdefery.
 */
#define __SYNC_none	-1

/*
 * A full completion barrier; all memory accesses appearing prior to this sync
 * instruction in program order must complete before any memory accesses
 * appearing after this sync instruction in program order.
 */
#define __SYNC_full	0x00

/*
 * For now we use a full completion barrier to implement all sync types, until
 * we're satisfied that lightweight ordering barriers defined by MIPSr6 are
 * sufficient to uphold our desired memory model.
 */
#define __SYNC_aq	__SYNC_full
#define __SYNC_rl	__SYNC_full
#define __SYNC_mb	__SYNC_full

/*
 * ...except on Cavium Octeon CPUs, which have been using the 'wmb' ordering
 * barrier since 2010 & omit 'rmb' barriers because the CPUs don't perform
 * speculative reads.
 */
#ifdef CONFIG_CPU_CAVIUM_OCTEON
# define __SYNC_rmb	__SYNC_none
# define __SYNC_wmb	0x04
#else
# define __SYNC_rmb	__SYNC_full
# define __SYNC_wmb	__SYNC_full
#endif

/*
 * A GINV sync is a little different; it doesn't relate directly to loads or
 * stores, but instead causes synchronization of an icache or TLB global
 * invalidation operation triggered by the ginvi or ginvt instructions
 * respectively. In cases where we need to know that a ginvi or ginvt operation
 * has been performed by all coherent CPUs, we must issue a sync instruction of
 * this type. Once this instruction graduates all coherent CPUs will have
 * observed the invalidation.
 */
#define __SYNC_ginv	0x14

/* Trivial; indicate that we always need this sync instruction. */
#define __SYNC_always	(1 << 0)

/*
 * Indicate that we need this sync instruction only on systems with weakly
 * ordered memory access. In general this is most MIPS systems, but there are
 * exceptions which provide strongly ordered memory.
 */
#ifdef CONFIG_WEAK_ORDERING
# define __SYNC_weak_ordering	(1 << 1)
#else
# define __SYNC_weak_ordering	0
#endif

/*
 * Indicate that we need this sync instruction only on systems where LL/SC
 * don't implicitly provide a memory barrier. In general this is most MIPS
 * systems.
 */
#ifdef CONFIG_WEAK_REORDERING_BEYOND_LLSC
# define __SYNC_weak_llsc	(1 << 2)
#else
# define __SYNC_weak_llsc	0
#endif

/*
 * Some Loongson 3 CPUs have a bug wherein execution of a memory access (load,
 * store or prefetch) in between an LL & SC can cause the SC instruction to
 * erroneously succeed, breaking atomicity. Whilst it's unusual to write code
 * containing such sequences, this bug bites harder than we might otherwise
 * expect due to reordering & speculation:
 *
 * 1) A memory access appearing prior to the LL in program order may actually
 *    be executed after the LL - this is the reordering case.
 *
 *    In order to avoid this we need to place a memory barrier (ie. a SYNC
 *    instruction) prior to every LL instruction, in between it and any earlier
 *    memory access instructions.
 *
 *    This reordering case is fixed by 3A R2 CPUs, ie. 3A2000 models and later.
 *
 * 2) If a conditional branch exists between an LL & SC with a target outside
 *    of the LL-SC loop, for example an exit upon value mismatch in cmpxchg()
 *    or similar, then misprediction of the branch may allow speculative
 *    execution of memory accesses from outside of the LL-SC loop.
 *
 *    In order to avoid this we need a memory barrier (ie. a SYNC instruction)
 *    at each affected branch target.
 *
 *    This case affects all current Loongson 3 CPUs.
 *
 * The above described cases cause an error in the cache coherence protocol;
 * such that the Invalidate of a competing LL-SC goes 'missing' and SC
 * erroneously observes its core still has Exclusive state and lets the SC
 * proceed.
 *
 * Therefore the error only occurs on SMP systems.
 */
#ifdef CONFIG_CPU_LOONGSON3_WORKAROUNDS
# define __SYNC_loongson3_war	(1 << 31)
#else
# define __SYNC_loongson3_war	0
#endif

/*
 * Some Cavium Octeon CPUs suffer from a bug that causes a single wmb ordering
 * barrier to be ineffective, requiring the use of 2 in sequence to provide an
 * effective barrier as noted by commit 6b07d38aaa52 ("MIPS: Octeon: Use
 * optimized memory barrier primitives."). Here we specify that the affected
 * sync instructions should be emitted twice.
 * Note that this expression is evaluated by the assembler (not the compiler),
 * and that the assembler evaluates '==' as 0 or -1, not 0 or 1.
 */
#ifdef CONFIG_CPU_CAVIUM_OCTEON
# define __SYNC_rpt(type)	(1 - (type == __SYNC_wmb))
#else
# define __SYNC_rpt(type)	1
#endif

/*
 * The main event. Here we actually emit a sync instruction of a given type, if
 * reason is non-zero.
 *
 * In future we have the option of emitting entries in a fixups-style table
 * here that would allow us to opportunistically remove some sync instructions
 * when we detect at runtime that we're running on a CPU that doesn't need
 * them.
 */
#ifdef CONFIG_CPU_HAS_SYNC
# define ____SYNC(_type, _reason, _else)			\
	.if	(( _type ) != -1) && ( _reason );		\
	.set	push;						\
	.set	MIPS_ISA_LEVEL_RAW;				\
	.rept	__SYNC_rpt(_type);				\
	sync	_type;						\
	.endr;							\
	.set	pop;						\
	.else;							\
	_else;							\
	.endif
#else
# define ____SYNC(_type, _reason, _else)
#endif

/*
 * Preprocessor magic to expand macros used as arguments before we insert them
 * into assembly code.
 */
#ifdef __ASSEMBLY__
# define ___SYNC(type, reason, else)				\
	____SYNC(type, reason, else)
#else
# define ___SYNC(type, reason, else)				\
	__stringify(____SYNC(type, reason, else))
#endif

#define __SYNC(type, reason)					\
	___SYNC(__SYNC_##type, __SYNC_##reason, )
#define __SYNC_ELSE(type, reason, else)				\
	___SYNC(__SYNC_##type, __SYNC_##reason, else)

#endif /* __MIPS_ASM_SYNC_H__ */
