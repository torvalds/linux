/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/barrier.h
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2013 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_BARRIER_H
#define _ASM_RISCV_BARRIER_H

#ifndef __ASSEMBLY__
#include <asm/fence.h>

#define nop()		__asm__ __volatile__ ("nop")
#define __nops(n)	".rept	" #n "\nnop\n.endr\n"
#define nops(n)		__asm__ __volatile__ (__nops(n))


/* These barriers need to enforce ordering on both devices or memory. */
#define __mb()		RISCV_FENCE(iorw, iorw)
#define __rmb()		RISCV_FENCE(ir, ir)
#define __wmb()		RISCV_FENCE(ow, ow)

/* These barriers do not need to enforce ordering on devices, just memory. */
#define __smp_mb()	RISCV_FENCE(rw, rw)
#define __smp_rmb()	RISCV_FENCE(r, r)
#define __smp_wmb()	RISCV_FENCE(w, w)

#define __smp_store_release(p, v)					\
do {									\
	compiletime_assert_atomic_type(*p);				\
	RISCV_FENCE(rw, w);						\
	WRITE_ONCE(*p, v);						\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	RISCV_FENCE(r, rw);						\
	___p1;								\
})

/*
 * This is a very specific barrier: it's currently only used in two places in
 * the kernel, both in the scheduler.  See include/linux/spinlock.h for the two
 * orderings it guarantees, but the "critical section is RCsc" guarantee
 * mandates a barrier on RISC-V.  The sequence looks like:
 *
 *    lr.aq lock
 *    sc    lock <= LOCKED
 *    smp_mb__after_spinlock()
 *    // critical section
 *    lr    lock
 *    sc.rl lock <= UNLOCKED
 *
 * The AQ/RL pair provides a RCpc critical section, but there's not really any
 * way we can take advantage of that here because the ordering is only enforced
 * on that one lock.  Thus, we're just doing a full fence.
 *
 * Since we allow writeX to be called from preemptive regions we need at least
 * an "o" in the predecessor set to ensure device writes are visible before the
 * task is marked as available for scheduling on a new hart.  While I don't see
 * any concrete reason we need a full IO fence, it seems safer to just upgrade
 * this in order to avoid any IO crossing a scheduling boundary.  In both
 * instances the scheduler pairs this with an mb(), so nothing is necessary on
 * the new hart.
 */
#define smp_mb__after_spinlock()	RISCV_FENCE(iorw, iorw)

#include <asm-generic/barrier.h>

#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_BARRIER_H */
