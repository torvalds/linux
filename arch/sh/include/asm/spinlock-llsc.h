/*
 * include/asm-sh/spinlock-llsc.h
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 * Copyright (C) 2006, 2007 Akio Idehara
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_SPINLOCK_LLSC_H
#define __ASM_SH_SPINLOCK_LLSC_H

#include <asm/barrier.h>
#include <asm/processor.h>

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 */

#define arch_spin_is_locked(x)		((x)->lock <= 0)
#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

static inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	smp_cond_load_acquire(&lock->lock, VAL > 0);
}

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions.  They have a cost.
 */
static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned long tmp;
	unsigned long oldval;

	__asm__ __volatile__ (
		"1:						\n\t"
		"movli.l	@%2, %0	! arch_spin_lock	\n\t"
		"mov		%0, %1				\n\t"
		"mov		#0, %0				\n\t"
		"movco.l	%0, @%2				\n\t"
		"bf		1b				\n\t"
		"cmp/pl		%1				\n\t"
		"bf		1b				\n\t"
		: "=&z" (tmp), "=&r" (oldval)
		: "r" (&lock->lock)
		: "t", "memory"
	);
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	unsigned long tmp;

	__asm__ __volatile__ (
		"mov		#1, %0 ! arch_spin_unlock	\n\t"
		"mov.l		%0, @%1				\n\t"
		: "=&z" (tmp)
		: "r" (&lock->lock)
		: "t", "memory"
	);
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned long tmp, oldval;

	__asm__ __volatile__ (
		"1:						\n\t"
		"movli.l	@%2, %0	! arch_spin_trylock	\n\t"
		"mov		%0, %1				\n\t"
		"mov		#0, %0				\n\t"
		"movco.l	%0, @%2				\n\t"
		"bf		1b				\n\t"
		"synco						\n\t"
		: "=&z" (tmp), "=&r" (oldval)
		: "r" (&lock->lock)
		: "t", "memory"
	);

	return oldval;
}

/*
 * Read-write spinlocks, allowing multiple readers but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts but no interrupt
 * writers. For those circumstances we can "mix" irq-safe locks - any writer
 * needs to get a irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_read_can_lock(x)	((x)->lock > 0)

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_write_can_lock(x)	((x)->lock == RW_LOCK_BIAS)

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__ (
		"1:						\n\t"
		"movli.l	@%1, %0	! arch_read_lock	\n\t"
		"cmp/pl		%0				\n\t"
		"bf		1b				\n\t"
		"add		#-1, %0				\n\t"
		"movco.l	%0, @%1				\n\t"
		"bf		1b				\n\t"
		: "=&z" (tmp)
		: "r" (&rw->lock)
		: "t", "memory"
	);
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__ (
		"1:						\n\t"
		"movli.l	@%1, %0	! arch_read_unlock	\n\t"
		"add		#1, %0				\n\t"
		"movco.l	%0, @%1				\n\t"
		"bf		1b				\n\t"
		: "=&z" (tmp)
		: "r" (&rw->lock)
		: "t", "memory"
	);
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__ (
		"1:						\n\t"
		"movli.l	@%1, %0	! arch_write_lock	\n\t"
		"cmp/hs		%2, %0				\n\t"
		"bf		1b				\n\t"
		"sub		%2, %0				\n\t"
		"movco.l	%0, @%1				\n\t"
		"bf		1b				\n\t"
		: "=&z" (tmp)
		: "r" (&rw->lock), "r" (RW_LOCK_BIAS)
		: "t", "memory"
	);
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	__asm__ __volatile__ (
		"mov.l		%1, @%0 ! arch_write_unlock	\n\t"
		:
		: "r" (&rw->lock), "r" (RW_LOCK_BIAS)
		: "t", "memory"
	);
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned long tmp, oldval;

	__asm__ __volatile__ (
		"1:						\n\t"
		"movli.l	@%2, %0	! arch_read_trylock	\n\t"
		"mov		%0, %1				\n\t"
		"cmp/pl		%0				\n\t"
		"bf		2f				\n\t"
		"add		#-1, %0				\n\t"
		"movco.l	%0, @%2				\n\t"
		"bf		1b				\n\t"
		"2:						\n\t"
		"synco						\n\t"
		: "=&z" (tmp), "=&r" (oldval)
		: "r" (&rw->lock)
		: "t", "memory"
	);

	return (oldval > 0);
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned long tmp, oldval;

	__asm__ __volatile__ (
		"1:						\n\t"
		"movli.l	@%2, %0	! arch_write_trylock	\n\t"
		"mov		%0, %1				\n\t"
		"cmp/hs		%3, %0				\n\t"
		"bf		2f				\n\t"
		"sub		%3, %0				\n\t"
		"2:						\n\t"
		"movco.l	%0, @%2				\n\t"
		"bf		1b				\n\t"
		"synco						\n\t"
		: "=&z" (tmp), "=&r" (oldval)
		: "r" (&rw->lock), "r" (RW_LOCK_BIAS)
		: "t", "memory"
	);

	return (oldval > (RW_LOCK_BIAS - 1));
}

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* __ASM_SH_SPINLOCK_LLSC_H */
