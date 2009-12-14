#ifndef _ASM_IA64_SPINLOCK_H
#define _ASM_IA64_SPINLOCK_H

/*
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *
 * This file is used for SMP configurations only.
 */

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/bitops.h>

#include <asm/atomic.h>
#include <asm/intrinsics.h>
#include <asm/system.h>

#define __raw_spin_lock_init(x)			((x)->lock = 0)

/*
 * Ticket locks are conceptually two parts, one indicating the current head of
 * the queue, and the other indicating the current tail. The lock is acquired
 * by atomically noting the tail and incrementing it by one (thus adding
 * ourself to the queue and noting our position), then waiting until the head
 * becomes equal to the the initial value of the tail.
 * The pad bits in the middle are used to prevent the next_ticket number
 * overflowing into the now_serving number.
 *
 *   31             17  16    15  14                    0
 *  +----------------------------------------------------+
 *  |  now_serving     | padding |   next_ticket         |
 *  +----------------------------------------------------+
 */

#define TICKET_SHIFT	17
#define TICKET_BITS	15
#define	TICKET_MASK	((1 << TICKET_BITS) - 1)

static __always_inline void __ticket_spin_lock(raw_spinlock_t *lock)
{
	int	*p = (int *)&lock->lock, ticket, serve;

	ticket = ia64_fetchadd(1, p, acq);

	if (!(((ticket >> TICKET_SHIFT) ^ ticket) & TICKET_MASK))
		return;

	ia64_invala();

	for (;;) {
		asm volatile ("ld4.c.nc %0=[%1]" : "=r"(serve) : "r"(p) : "memory");

		if (!(((serve >> TICKET_SHIFT) ^ ticket) & TICKET_MASK))
			return;
		cpu_relax();
	}
}

static __always_inline int __ticket_spin_trylock(raw_spinlock_t *lock)
{
	int tmp = ACCESS_ONCE(lock->lock);

	if (!(((tmp >> TICKET_SHIFT) ^ tmp) & TICKET_MASK))
		return ia64_cmpxchg(acq, &lock->lock, tmp, tmp + 1, sizeof (tmp)) == tmp;
	return 0;
}

static __always_inline void __ticket_spin_unlock(raw_spinlock_t *lock)
{
	unsigned short	*p = (unsigned short *)&lock->lock + 1, tmp;

	asm volatile ("ld2.bias %0=[%1]" : "=r"(tmp) : "r"(p));
	ACCESS_ONCE(*p) = (tmp + 2) & ~1;
}

static __always_inline void __ticket_spin_unlock_wait(raw_spinlock_t *lock)
{
	int	*p = (int *)&lock->lock, ticket;

	ia64_invala();

	for (;;) {
		asm volatile ("ld4.c.nc %0=[%1]" : "=r"(ticket) : "r"(p) : "memory");
		if (!(((ticket >> TICKET_SHIFT) ^ ticket) & TICKET_MASK))
			return;
		cpu_relax();
	}
}

static inline int __ticket_spin_is_locked(raw_spinlock_t *lock)
{
	long tmp = ACCESS_ONCE(lock->lock);

	return !!(((tmp >> TICKET_SHIFT) ^ tmp) & TICKET_MASK);
}

static inline int __ticket_spin_is_contended(raw_spinlock_t *lock)
{
	long tmp = ACCESS_ONCE(lock->lock);

	return ((tmp - (tmp >> TICKET_SHIFT)) & TICKET_MASK) > 1;
}

static inline int __raw_spin_is_locked(raw_spinlock_t *lock)
{
	return __ticket_spin_is_locked(lock);
}

static inline int __raw_spin_is_contended(raw_spinlock_t *lock)
{
	return __ticket_spin_is_contended(lock);
}
#define __raw_spin_is_contended	__raw_spin_is_contended

static __always_inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	__ticket_spin_lock(lock);
}

static __always_inline int __raw_spin_trylock(raw_spinlock_t *lock)
{
	return __ticket_spin_trylock(lock);
}

static __always_inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	__ticket_spin_unlock(lock);
}

static __always_inline void __raw_spin_lock_flags(raw_spinlock_t *lock,
						  unsigned long flags)
{
	__raw_spin_lock(lock);
}

static inline void __raw_spin_unlock_wait(raw_spinlock_t *lock)
{
	__ticket_spin_unlock_wait(lock);
}

#define __raw_read_can_lock(rw)		(*(volatile int *)(rw) >= 0)
#define __raw_write_can_lock(rw)	(*(volatile int *)(rw) == 0)

#ifdef ASM_SUPPORTED

static __always_inline void
__raw_read_lock_flags(raw_rwlock_t *lock, unsigned long flags)
{
	__asm__ __volatile__ (
		"tbit.nz p6, p0 = %1,%2\n"
		"br.few 3f\n"
		"1:\n"
		"fetchadd4.rel r2 = [%0], -1;;\n"
		"(p6) ssm psr.i\n"
		"2:\n"
		"hint @pause\n"
		"ld4 r2 = [%0];;\n"
		"cmp4.lt p7,p0 = r2, r0\n"
		"(p7) br.cond.spnt.few 2b\n"
		"(p6) rsm psr.i\n"
		";;\n"
		"3:\n"
		"fetchadd4.acq r2 = [%0], 1;;\n"
		"cmp4.lt p7,p0 = r2, r0\n"
		"(p7) br.cond.spnt.few 1b\n"
		: : "r"(lock), "r"(flags), "i"(IA64_PSR_I_BIT)
		: "p6", "p7", "r2", "memory");
}

#define __raw_read_lock(lock) __raw_read_lock_flags(lock, 0)

#else /* !ASM_SUPPORTED */

#define __raw_read_lock_flags(rw, flags) __raw_read_lock(rw)

#define __raw_read_lock(rw)								\
do {											\
	raw_rwlock_t *__read_lock_ptr = (rw);						\
											\
	while (unlikely(ia64_fetchadd(1, (int *) __read_lock_ptr, acq) < 0)) {		\
		ia64_fetchadd(-1, (int *) __read_lock_ptr, rel);			\
		while (*(volatile int *)__read_lock_ptr < 0)				\
			cpu_relax();							\
	}										\
} while (0)

#endif /* !ASM_SUPPORTED */

#define __raw_read_unlock(rw)					\
do {								\
	raw_rwlock_t *__read_lock_ptr = (rw);			\
	ia64_fetchadd(-1, (int *) __read_lock_ptr, rel);	\
} while (0)

#ifdef ASM_SUPPORTED

static __always_inline void
__raw_write_lock_flags(raw_rwlock_t *lock, unsigned long flags)
{
	__asm__ __volatile__ (
		"tbit.nz p6, p0 = %1, %2\n"
		"mov ar.ccv = r0\n"
		"dep r29 = -1, r0, 31, 1\n"
		"br.few 3f;;\n"
		"1:\n"
		"(p6) ssm psr.i\n"
		"2:\n"
		"hint @pause\n"
		"ld4 r2 = [%0];;\n"
		"cmp4.eq p0,p7 = r0, r2\n"
		"(p7) br.cond.spnt.few 2b\n"
		"(p6) rsm psr.i\n"
		";;\n"
		"3:\n"
		"cmpxchg4.acq r2 = [%0], r29, ar.ccv;;\n"
		"cmp4.eq p0,p7 = r0, r2\n"
		"(p7) br.cond.spnt.few 1b;;\n"
		: : "r"(lock), "r"(flags), "i"(IA64_PSR_I_BIT)
		: "ar.ccv", "p6", "p7", "r2", "r29", "memory");
}

#define __raw_write_lock(rw) __raw_write_lock_flags(rw, 0)

#define __raw_write_trylock(rw)							\
({										\
	register long result;							\
										\
	__asm__ __volatile__ (							\
		"mov ar.ccv = r0\n"						\
		"dep r29 = -1, r0, 31, 1;;\n"					\
		"cmpxchg4.acq %0 = [%1], r29, ar.ccv\n"				\
		: "=r"(result) : "r"(rw) : "ar.ccv", "r29", "memory");		\
	(result == 0);								\
})

static inline void __raw_write_unlock(raw_rwlock_t *x)
{
	u8 *y = (u8 *)x;
	barrier();
	asm volatile ("st1.rel.nta [%0] = r0\n\t" :: "r"(y+3) : "memory" );
}

#else /* !ASM_SUPPORTED */

#define __raw_write_lock_flags(l, flags) __raw_write_lock(l)

#define __raw_write_lock(l)								\
({											\
	__u64 ia64_val, ia64_set_val = ia64_dep_mi(-1, 0, 31, 1);			\
	__u32 *ia64_write_lock_ptr = (__u32 *) (l);					\
	do {										\
		while (*ia64_write_lock_ptr)						\
			ia64_barrier();							\
		ia64_val = ia64_cmpxchg4_acq(ia64_write_lock_ptr, ia64_set_val, 0);	\
	} while (ia64_val);								\
})

#define __raw_write_trylock(rw)						\
({									\
	__u64 ia64_val;							\
	__u64 ia64_set_val = ia64_dep_mi(-1, 0, 31,1);			\
	ia64_val = ia64_cmpxchg4_acq((__u32 *)(rw), ia64_set_val, 0);	\
	(ia64_val == 0);						\
})

static inline void __raw_write_unlock(raw_rwlock_t *x)
{
	barrier();
	x->write_lock = 0;
}

#endif /* !ASM_SUPPORTED */

static inline int __raw_read_trylock(raw_rwlock_t *x)
{
	union {
		raw_rwlock_t lock;
		__u32 word;
	} old, new;
	old.lock = new.lock = *x;
	old.lock.write_lock = new.lock.write_lock = 0;
	++new.lock.read_counter;
	return (u32)ia64_cmpxchg4_acq((__u32 *)(x), new.word, old.word) == old.word;
}

#define _raw_spin_relax(lock)	cpu_relax()
#define _raw_read_relax(lock)	cpu_relax()
#define _raw_write_relax(lock)	cpu_relax()

#endif /*  _ASM_IA64_SPINLOCK_H */
