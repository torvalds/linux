/*
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#ifndef _ASM_RISCV_SPINLOCK_H
#define _ASM_RISCV_SPINLOCK_H

#include <linux/kernel.h>
#include <asm/current.h>

/*
 * Simple spin lock operations.  These provide no fairness guarantees.
 */

/* FIXME: Replace this with a ticket lock, like MIPS. */

#define arch_spin_is_locked(x)	(READ_ONCE((x)->lock) != 0)

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	__asm__ __volatile__ (
		"amoswap.w.rl x0, x0, %0"
		: "=A" (lock->lock)
		:: "memory");
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	int tmp = 1, busy;

	__asm__ __volatile__ (
		"amoswap.w.aq %0, %2, %1"
		: "=r" (busy), "+A" (lock->lock)
		: "r" (tmp)
		: "memory");

	return !busy;
}

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	while (1) {
		if (arch_spin_is_locked(lock))
			continue;

		if (arch_spin_trylock(lock))
			break;
	}
}

static inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	smp_rmb();
	do {
		cpu_relax();
	} while (arch_spin_is_locked(lock));
	smp_acquire__after_ctrl_dep();
}

/***********************************************************/

static inline void arch_read_lock(arch_rwlock_t *lock)
{
	int tmp;

	__asm__ __volatile__(
		"1:	lr.w	%1, %0\n"
		"	bltz	%1, 1b\n"
		"	addi	%1, %1, 1\n"
		"	sc.w.aq	%1, %1, %0\n"
		"	bnez	%1, 1b\n"
		: "+A" (lock->lock), "=&r" (tmp)
		:: "memory");
}

static inline void arch_write_lock(arch_rwlock_t *lock)
{
	int tmp;

	__asm__ __volatile__(
		"1:	lr.w	%1, %0\n"
		"	bnez	%1, 1b\n"
		"	li	%1, -1\n"
		"	sc.w.aq	%1, %1, %0\n"
		"	bnez	%1, 1b\n"
		: "+A" (lock->lock), "=&r" (tmp)
		:: "memory");
}

static inline int arch_read_trylock(arch_rwlock_t *lock)
{
	int busy;

	__asm__ __volatile__(
		"1:	lr.w	%1, %0\n"
		"	bltz	%1, 1f\n"
		"	addi	%1, %1, 1\n"
		"	sc.w.aq	%1, %1, %0\n"
		"	bnez	%1, 1b\n"
		"1:\n"
		: "+A" (lock->lock), "=&r" (busy)
		:: "memory");

	return !busy;
}

static inline int arch_write_trylock(arch_rwlock_t *lock)
{
	int busy;

	__asm__ __volatile__(
		"1:	lr.w	%1, %0\n"
		"	bnez	%1, 1f\n"
		"	li	%1, -1\n"
		"	sc.w.aq	%1, %1, %0\n"
		"	bnez	%1, 1b\n"
		"1:\n"
		: "+A" (lock->lock), "=&r" (busy)
		:: "memory");

	return !busy;
}

static inline void arch_read_unlock(arch_rwlock_t *lock)
{
	__asm__ __volatile__(
		"amoadd.w.rl x0, %1, %0"
		: "+A" (lock->lock)
		: "r" (-1)
		: "memory");
}

static inline void arch_write_unlock(arch_rwlock_t *lock)
{
	__asm__ __volatile__ (
		"amoswap.w.rl x0, x0, %0"
		: "=A" (lock->lock)
		:: "memory");
}

#endif /* _ASM_RISCV_SPINLOCK_H */
