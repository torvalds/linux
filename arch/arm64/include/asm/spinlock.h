/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/spinlock_types.h>
#include <asm/processor.h>

/*
 * Spinlock implementation.
 *
 * The old value is read exclusively and the new one, if unlocked, is written
 * exclusively. In case of failure, the loop is restarted.
 *
 * The memory barriers are implicit with the load-acquire and store-release
 * instructions.
 *
 * Unlocked value: 0
 * Locked value: 1
 */

#define arch_spin_is_locked(x)		((x)->lock != 0)
#define arch_spin_unlock_wait(lock) \
	do { while (arch_spin_is_locked(lock)) cpu_relax(); } while (0)

#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned int tmp;

	asm volatile(
	"	sevl\n"
	"1:	wfe\n"
	"2:	ldaxr	%w0, %1\n"
	"	cbnz	%w0, 1b\n"
	"	stxr	%w0, %w2, %1\n"
	"	cbnz	%w0, 2b\n"
	: "=&r" (tmp), "+Q" (lock->lock)
	: "r" (1)
	: "cc", "memory");
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned int tmp;

	asm volatile(
	"	ldaxr	%w0, %1\n"
	"	cbnz	%w0, 1f\n"
	"	stxr	%w0, %w2, %1\n"
	"1:\n"
	: "=&r" (tmp), "+Q" (lock->lock)
	: "r" (1)
	: "cc", "memory");

	return !tmp;
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	asm volatile(
	"	stlr	%w1, %0\n"
	: "=Q" (lock->lock) : "r" (0) : "memory");
}

/*
 * Write lock implementation.
 *
 * Write locks set bit 31. Unlocking, is done by writing 0 since the lock is
 * exclusively held.
 *
 * The memory barriers are implicit with the load-acquire and store-release
 * instructions.
 */

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned int tmp;

	asm volatile(
	"	sevl\n"
	"1:	wfe\n"
	"2:	ldaxr	%w0, %1\n"
	"	cbnz	%w0, 1b\n"
	"	stxr	%w0, %w2, %1\n"
	"	cbnz	%w0, 2b\n"
	: "=&r" (tmp), "+Q" (rw->lock)
	: "r" (0x80000000)
	: "cc", "memory");
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned int tmp;

	asm volatile(
	"	ldaxr	%w0, %1\n"
	"	cbnz	%w0, 1f\n"
	"	stxr	%w0, %w2, %1\n"
	"1:\n"
	: "=&r" (tmp), "+Q" (rw->lock)
	: "r" (0x80000000)
	: "cc", "memory");

	return !tmp;
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	asm volatile(
	"	stlr	%w1, %0\n"
	: "=Q" (rw->lock) : "r" (0) : "memory");
}

/* write_can_lock - would write_trylock() succeed? */
#define arch_write_can_lock(x)		((x)->lock == 0)

/*
 * Read lock implementation.
 *
 * It exclusively loads the lock value, increments it and stores the new value
 * back if positive and the CPU still exclusively owns the location. If the
 * value is negative, the lock is already held.
 *
 * During unlocking there may be multiple active read locks but no write lock.
 *
 * The memory barriers are implicit with the load-acquire and store-release
 * instructions.
 */
static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned int tmp, tmp2;

	asm volatile(
	"	sevl\n"
	"1:	wfe\n"
	"2:	ldaxr	%w0, %2\n"
	"	add	%w0, %w0, #1\n"
	"	tbnz	%w0, #31, 1b\n"
	"	stxr	%w1, %w0, %2\n"
	"	cbnz	%w1, 2b\n"
	: "=&r" (tmp), "=&r" (tmp2), "+Q" (rw->lock)
	:
	: "cc", "memory");
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned int tmp, tmp2;

	asm volatile(
	"1:	ldxr	%w0, %2\n"
	"	sub	%w0, %w0, #1\n"
	"	stlxr	%w1, %w0, %2\n"
	"	cbnz	%w1, 1b\n"
	: "=&r" (tmp), "=&r" (tmp2), "+Q" (rw->lock)
	:
	: "cc", "memory");
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned int tmp, tmp2 = 1;

	asm volatile(
	"	ldaxr	%w0, %2\n"
	"	add	%w0, %w0, #1\n"
	"	tbnz	%w0, #31, 1f\n"
	"	stxr	%w1, %w0, %2\n"
	"1:\n"
	: "=&r" (tmp), "+r" (tmp2), "+Q" (rw->lock)
	:
	: "cc", "memory");

	return !tmp2;
}

/* read_can_lock - would read_trylock() succeed? */
#define arch_read_can_lock(x)		((x)->lock < 0x80000000)

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* __ASM_SPINLOCK_H */
