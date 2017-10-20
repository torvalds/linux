/* MN10300 spinlock support
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_SPINLOCK_H
#define _ASM_SPINLOCK_H

#include <linux/atomic.h>
#include <asm/barrier.h>
#include <asm/processor.h>
#include <asm/rwlock.h>
#include <asm/page.h>

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

#define arch_spin_is_locked(x)	(*(volatile signed char *)(&(x)->slock) != 0)

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	asm volatile(
		"	bclr	1,(0,%0)	\n"
		:
		: "a"(&lock->slock)
		: "memory", "cc");
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	int ret;

	asm volatile(
		"	mov	1,%0		\n"
		"	bset	%0,(%1)		\n"
		"	bne	1f		\n"
		"	clr	%0		\n"
		"1:	xor	1,%0		\n"
		: "=d"(ret)
		: "a"(&lock->slock)
		: "memory", "cc");

	return ret;
}

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	asm volatile(
		"1:	bset	1,(0,%0)	\n"
		"	bne	1b		\n"
		:
		: "a"(&lock->slock)
		: "memory", "cc");
}

static inline void arch_spin_lock_flags(arch_spinlock_t *lock,
					 unsigned long flags)
{
	int temp;

	asm volatile(
		"1:	bset	1,(0,%2)	\n"
		"	beq	3f		\n"
		"	mov	%1,epsw		\n"
		"2:	mov	(0,%2),%0	\n"
		"	or	%0,%0		\n"
		"	bne	2b		\n"
		"	mov	%3,%0		\n"
		"	mov	%0,epsw		\n"
		"	nop			\n"
		"	nop			\n"
		"	bra	1b\n"
		"3:				\n"
		: "=&d" (temp)
		: "d" (flags), "a"(&lock->slock), "i"(EPSW_IE | MN10300_CLI_LEVEL)
		: "memory", "cc");
}

#ifdef __KERNEL__

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_read_can_lock(x) ((int)(x)->lock > 0)

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_write_can_lock(x) ((x)->lock == RW_LOCK_BIAS)

/*
 * On mn10300, we implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "contended" bit.
 */
static inline void arch_read_lock(arch_rwlock_t *rw)
{
#if 0 //def CONFIG_MN10300_HAS_ATOMIC_OPS_UNIT
	__build_read_lock(rw, "__read_lock_failed");
#else
	{
		atomic_t *count = (atomic_t *)rw;
		while (atomic_dec_return(count) < 0)
			atomic_inc(count);
	}
#endif
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
#if 0 //def CONFIG_MN10300_HAS_ATOMIC_OPS_UNIT
	__build_write_lock(rw, "__write_lock_failed");
#else
	{
		atomic_t *count = (atomic_t *)rw;
		while (!atomic_sub_and_test(RW_LOCK_BIAS, count))
			atomic_add(RW_LOCK_BIAS, count);
	}
#endif
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
#if 0 //def CONFIG_MN10300_HAS_ATOMIC_OPS_UNIT
	__build_read_unlock(rw);
#else
	{
		atomic_t *count = (atomic_t *)rw;
		atomic_inc(count);
	}
#endif
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
#if 0 //def CONFIG_MN10300_HAS_ATOMIC_OPS_UNIT
	__build_write_unlock(rw);
#else
	{
		atomic_t *count = (atomic_t *)rw;
		atomic_add(RW_LOCK_BIAS, count);
	}
#endif
}

static inline int arch_read_trylock(arch_rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;
	atomic_dec(count);
	if (atomic_read(count) >= 0)
		return 1;
	atomic_inc(count);
	return 0;
}

static inline int arch_write_trylock(arch_rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;
	if (atomic_sub_and_test(RW_LOCK_BIAS, count))
		return 1;
	atomic_add(RW_LOCK_BIAS, count);
	return 0;
}

#define arch_read_lock_flags(lock, flags)  arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define _raw_spin_relax(lock)	cpu_relax()
#define _raw_read_relax(lock)	cpu_relax()
#define _raw_write_relax(lock)	cpu_relax()

#endif /* __KERNEL__ */
#endif /* _ASM_SPINLOCK_H */
