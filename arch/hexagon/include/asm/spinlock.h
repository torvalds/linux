/*
 * Spinlock support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef _ASM_SPINLOCK_H
#define _ASM_SPINLOCK_H

#include <asm/irqflags.h>

/*
 * This file is pulled in for SMP builds.
 * Really need to check all the barrier stuff for "true" SMP
 */

/*
 * Read locks:
 * - load the lock value
 * - increment it
 * - if the lock value is still negative, go back and try again.
 * - unsuccessful store is unsuccessful.  Go back and try again.  Loser.
 * - successful store new lock value if positive -> lock acquired
 */
static inline void arch_read_lock(arch_rwlock_t *lock)
{
	__asm__ __volatile__(
		"1:	R6 = memw_locked(%0);\n"
		"	{ P3 = cmp.ge(R6,#0); R6 = add(R6,#1);}\n"
		"	{ if !P3 jump 1b; }\n"
		"	memw_locked(%0,P3) = R6;\n"
		"	{ if !P3 jump 1b; }\n"
		:
		: "r" (&lock->lock)
		: "memory", "r6", "p3"
	);

}

static inline void arch_read_unlock(arch_rwlock_t *lock)
{
	__asm__ __volatile__(
		"1:	R6 = memw_locked(%0);\n"
		"	R6 = add(R6,#-1);\n"
		"	memw_locked(%0,P3) = R6\n"
		"	if !P3 jump 1b;\n"
		:
		: "r" (&lock->lock)
		: "memory", "r6", "p3"
	);

}

/*  I think this returns 0 on fail, 1 on success.  */
static inline int arch_read_trylock(arch_rwlock_t *lock)
{
	int temp;
	__asm__ __volatile__(
		"	R6 = memw_locked(%1);\n"
		"	{ %0 = #0; P3 = cmp.ge(R6,#0); R6 = add(R6,#1);}\n"
		"	{ if !P3 jump 1f; }\n"
		"	memw_locked(%1,P3) = R6;\n"
		"	{ %0 = P3 }\n"
		"1:\n"
		: "=&r" (temp)
		: "r" (&lock->lock)
		: "memory", "r6", "p3"
	);
	return temp;
}

static inline int arch_read_can_lock(arch_rwlock_t *rwlock)
{
	return rwlock->lock == 0;
}

static inline int arch_write_can_lock(arch_rwlock_t *rwlock)
{
	return rwlock->lock == 0;
}

/*  Stuffs a -1 in the lock value?  */
static inline void arch_write_lock(arch_rwlock_t *lock)
{
	__asm__ __volatile__(
		"1:	R6 = memw_locked(%0)\n"
		"	{ P3 = cmp.eq(R6,#0);  R6 = #-1;}\n"
		"	{ if !P3 jump 1b; }\n"
		"	memw_locked(%0,P3) = R6;\n"
		"	{ if !P3 jump 1b; }\n"
		:
		: "r" (&lock->lock)
		: "memory", "r6", "p3"
	);
}


static inline int arch_write_trylock(arch_rwlock_t *lock)
{
	int temp;
	__asm__ __volatile__(
		"	R6 = memw_locked(%1)\n"
		"	{ %0 = #0; P3 = cmp.eq(R6,#0);  R6 = #-1;}\n"
		"	{ if !P3 jump 1f; }\n"
		"	memw_locked(%1,P3) = R6;\n"
		"	%0 = P3;\n"
		"1:\n"
		: "=&r" (temp)
		: "r" (&lock->lock)
		: "memory", "r6", "p3"
	);
	return temp;

}

static inline void arch_write_unlock(arch_rwlock_t *lock)
{
	smp_mb();
	lock->lock = 0;
}

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	__asm__ __volatile__(
		"1:	R6 = memw_locked(%0);\n"
		"	P3 = cmp.eq(R6,#0);\n"
		"	{ if !P3 jump 1b; R6 = #1; }\n"
		"	memw_locked(%0,P3) = R6;\n"
		"	{ if !P3 jump 1b; }\n"
		:
		: "r" (&lock->lock)
		: "memory", "r6", "p3"
	);

}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	smp_mb();
	lock->lock = 0;
}

static inline unsigned int arch_spin_trylock(arch_spinlock_t *lock)
{
	int temp;
	__asm__ __volatile__(
		"	R6 = memw_locked(%1);\n"
		"	P3 = cmp.eq(R6,#0);\n"
		"	{ if !P3 jump 1f; R6 = #1; %0 = #0; }\n"
		"	memw_locked(%1,P3) = R6;\n"
		"	%0 = P3;\n"
		"1:\n"
		: "=&r" (temp)
		: "r" (&lock->lock)
		: "memory", "r6", "p3"
	);
	return temp;
}

/*
 * SMP spinlocks are intended to allow only a single CPU at the lock
 */
#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)
#define arch_spin_unlock_wait(lock) \
	do {while (arch_spin_is_locked(lock)) cpu_relax(); } while (0)
#define arch_spin_is_locked(x) ((x)->lock != 0)

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#endif
