/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 * Copyright (C) 2014 Stefan Kristiansson <stefan.kristiansson@saunalahti.fi>
 *
 * Ticket spinlocks, based on the ARM implementation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASM_OPENRISC_SPINLOCK_H
#define __ASM_OPENRISC_SPINLOCK_H

#include <asm/spinlock_types.h>

#define arch_spin_unlock_wait(lock) \
	do { while (arch_spin_is_locked(lock)) cpu_relax(); } while (0)

#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	u32 newval;
	arch_spinlock_t lockval;

	__asm__ __volatile__(
		"1:	l.lwa	%0, 0(%2)	\n"
		"	l.add	%1, %0, %3	\n"
		"	l.swa	0(%2), %1	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		: "=&r" (lockval), "=&r" (newval)
		: "r" (&lock->slock), "r" (1 << TICKET_SHIFT)
		: "cc", "memory");

	while (lockval.tickets.next != lockval.tickets.owner)
		lockval.tickets.owner = ACCESS_ONCE(lock->tickets.owner);

	smp_mb();
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned long contended, tmp;
	u32 slock;

	/* contended = (lock->tickets.owner != lock->tickets.next) */
	__asm__ __volatile__(
		"1:	l.lwa	%0, 0(%3)	\n"
		"	l.srli	%1, %0, 16	\n"
		"	l.andi	%2, %0, 0xffff	\n"
		"	l.sfeq	%1, %2		\n"
		"	l.bnf	1f		\n"
		"	 l.ori	%1, r0, 1	\n"
		"	l.add	%0, %0, %4	\n"
		"	l.swa	0(%3), %0	\n"
		"	l.bnf	1b		\n"
		"	 l.ori	%1, r0, 0	\n"
		"1:				\n"
		: "=&r" (slock), "=&r" (contended), "=&r" (tmp)
		: "r" (&lock->slock), "r" (1 << TICKET_SHIFT)
		: "cc", "memory");

	if (!contended) {
		smp_mb();
		return 1;
	} else {
		return 0;
	}
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	smp_mb();
	lock->tickets.owner++;
}

static inline int arch_spin_value_unlocked(arch_spinlock_t lock)
{
	return lock.tickets.owner == lock.tickets.next;
}

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	return !arch_spin_value_unlocked(ACCESS_ONCE(*lock));
}

static inline int arch_spin_is_contended(arch_spinlock_t *lock)
{
	struct __raw_tickets tickets = ACCESS_ONCE(lock->tickets);

	return (tickets.next - tickets.owner) > 1;
}
#define arch_spin_is_contended	arch_spin_is_contended

/*
 * RWLOCKS
 *
 *
 * Write locks are easy - we just set bit 31.  When unlocking, we can
 * just write zero since the lock is exclusively held.
 */

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__(
		"1:	l.lwa	%0, 0(%1)	\n"
		"	l.sfeqi	%0, 0		\n"
		"	l.bnf	1f		\n"
		"	 l.nop			\n"
		"	l.swa	0(%1), %2	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		"1:				\n"
		: "=&r" (tmp)
		: "r" (&rw->lock), "r" (0x80000000)
		: "cc", "memory");

	smp_mb();
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned long contended;

	__asm__ __volatile__(
		"1:	l.lwa	%0, 0(%1)	\n"
		"	l.sfeqi	%0, 0		\n"
		"	l.bnf	1f		\n"
		"	 l.nop			\n"
		"	l.swa	 0(%1), %2	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		"1:				\n"
		: "=&r" (contended)
		: "r" (&rw->lock), "r" (0x80000000)
		: "cc", "memory");

	if (!contended) {
		smp_mb();
		return 1;
	} else {
		return 0;
	}
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	smp_mb();
	rw->lock = 0;
}

/* write_can_lock - would write_trylock() succeed? */
#define arch_write_can_lock(x)		(ACCESS_ONCE((x)->lock) == 0)

/*
 * Read locks are a bit more hairy:
 *  - Exclusively load the lock value.
 *  - Increment it.
 *  - Store new lock value if positive, and we still own this location.
 *    If the value is negative, we've already failed.
 *  - If we failed to store the value, we want a negative result.
 *  - If we failed, try again.
 * Unlocking is similarly hairy.  We may have multiple read locks
 * currently active.  However, we know we won't have any write
 * locks.
 */
static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__(
		"1:	l.lwa		%0, 0(%1)	\n"
		"	l.sfltsi	%0, -1		\n"
		"	l.bf		1b		\n"
		"	 l.addi		%0, %0, 1	\n"
		"	l.swa		0(%1), %0	\n"
		"	l.bnf		1b		\n"
		"	 l.nop				\n"
		: "=&r" (tmp)
		: "r" (&rw->lock)
		: "cc", "memory");

	smp_mb();
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	smp_mb();

	__asm__ __volatile__(
		"1:	l.lwa	%0, 0(%1)	\n"
		"	l.addi	%0, %0, -1	\n"
		"	l.swa	0(%1), %0	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		: "=&r" (tmp)
		: "r" (&rw->lock)
		: "cc", "memory");

}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned long contended;

	__asm__ __volatile__(
		"1:	l.lwa		%0, 0(%1)	\n"
		"	l.sfltsi	%0, -1		\n"
		"	l.bf		1f		\n"
		"	 l.addi		%0, %0, 1	\n"
		"	l.swa		0(%1), %0	\n"
		"	l.bnf		1b		\n"
		"	 l.nop				\n"
		"1:					\n"
		: "=&r" (contended)
		: "r" (&rw->lock)
		: "cc", "memory");

	/* If the lock is negative, then it is already held for write. */
	if (contended < 0x80000000) {
		smp_mb();
		return 1;
	} else {
		return 0;
	}
}

/* read_can_lock - would read_trylock() succeed? */
#define arch_read_can_lock(x)		(ACCESS_ONCE((x)->lock) < 0x80000000)

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif
