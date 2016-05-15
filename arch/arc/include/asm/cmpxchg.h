/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_CMPXCHG_H
#define __ASM_ARC_CMPXCHG_H

#include <linux/types.h>

#include <asm/barrier.h>
#include <asm/smp.h>

#ifdef CONFIG_ARC_HAS_LLSC

static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long expected, unsigned long new)
{
	unsigned long prev;

	/*
	 * Explicit full memory barrier needed before/after as
	 * LLOCK/SCOND thmeselves don't provide any such semantics
	 */
	smp_mb();

	__asm__ __volatile__(
	"1:	llock   %0, [%1]	\n"
	"	brne    %0, %2, 2f	\n"
	"	scond   %3, [%1]	\n"
	"	bnz     1b		\n"
	"2:				\n"
	: "=&r"(prev)	/* Early clobber, to prevent reg reuse */
	: "r"(ptr),	/* Not "m": llock only supports reg direct addr mode */
	  "ir"(expected),
	  "r"(new)	/* can't be "ir". scond can't take LIMM for "b" */
	: "cc", "memory"); /* so that gcc knows memory is being written here */

	smp_mb();

	return prev;
}

#else

static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long expected, unsigned long new)
{
	unsigned long flags;
	int prev;
	volatile unsigned long *p = ptr;

	/*
	 * spin lock/unlock provide the needed smp_mb() before/after
	 */
	atomic_ops_lock(flags);
	prev = *p;
	if (prev == expected)
		*p = new;
	atomic_ops_unlock(flags);
	return prev;
}

#endif /* CONFIG_ARC_HAS_LLSC */

#define cmpxchg(ptr, o, n) ((typeof(*(ptr)))__cmpxchg((ptr), \
				(unsigned long)(o), (unsigned long)(n)))

/*
 * Since not supported natively, ARC cmpxchg() uses atomic_ops_lock (UP/SMP)
 * just to gaurantee semantics.
 * atomic_cmpxchg() needs to use the same locks as it's other atomic siblings
 * which also happens to be atomic_ops_lock.
 *
 * Thus despite semantically being different, implementation of atomic_cmpxchg()
 * is same as cmpxchg().
 */
#define atomic_cmpxchg(v, o, n) ((int)cmpxchg(&((v)->counter), (o), (n)))


/*
 * xchg (reg with memory) based on "Native atomic" EX insn
 */
static inline unsigned long __xchg(unsigned long val, volatile void *ptr,
				   int size)
{
	extern unsigned long __xchg_bad_pointer(void);

	switch (size) {
	case 4:
		smp_mb();

		__asm__ __volatile__(
		"	ex  %0, [%1]	\n"
		: "+r"(val)
		: "r"(ptr)
		: "memory");

		smp_mb();

		return val;
	}
	return __xchg_bad_pointer();
}

#define _xchg(ptr, with) ((typeof(*(ptr)))__xchg((unsigned long)(with), (ptr), \
						 sizeof(*(ptr))))

/*
 * xchg() maps directly to ARC EX instruction which guarantees atomicity.
 * However in !LLSC config, it also needs to be use @atomic_ops_lock spinlock
 * due to a subtle reason:
 *  - For !LLSC, cmpxchg() needs to use that lock (see above) and there is lot
 *    of  kernel code which calls xchg()/cmpxchg() on same data (see llist.h)
 *    Hence xchg() needs to follow same locking rules.
 *
 * Technically the lock is also needed for UP (boils down to irq save/restore)
 * but we can cheat a bit since cmpxchg() atomic_ops_lock() would cause irqs to
 * be disabled thus can't possibly be interrpted/preempted/clobbered by xchg()
 * Other way around, xchg is one instruction anyways, so can't be interrupted
 * as such
 */

#if !defined(CONFIG_ARC_HAS_LLSC) && defined(CONFIG_SMP)

#define xchg(ptr, with)			\
({					\
	unsigned long flags;		\
	typeof(*(ptr)) old_val;		\
					\
	atomic_ops_lock(flags);		\
	old_val = _xchg(ptr, with);	\
	atomic_ops_unlock(flags);	\
	old_val;			\
})

#else

#define xchg(ptr, with)  _xchg(ptr, with)

#endif

/*
 * "atomic" variant of xchg()
 * REQ: It needs to follow the same serialization rules as other atomic_xxx()
 * Since xchg() doesn't always do that, it would seem that following defintion
 * is incorrect. But here's the rationale:
 *   SMP : Even xchg() takes the atomic_ops_lock, so OK.
 *   LLSC: atomic_ops_lock are not relevant at all (even if SMP, since LLSC
 *         is natively "SMP safe", no serialization required).
 *   UP  : other atomics disable IRQ, so no way a difft ctxt atomic_xchg()
 *         could clobber them. atomic_xchg() itself would be 1 insn, so it
 *         can't be clobbered by others. Thus no serialization required when
 *         atomic_xchg is involved.
 */
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

#endif
