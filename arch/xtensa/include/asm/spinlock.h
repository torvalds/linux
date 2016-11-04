/*
 * include/asm-xtensa/spinlock.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_SPINLOCK_H
#define _XTENSA_SPINLOCK_H

#include <asm/barrier.h>
#include <asm/processor.h>

/*
 * spinlock
 *
 * There is at most one owner of a spinlock.  There are not different
 * types of spinlock owners like there are for rwlocks (see below).
 *
 * When trying to obtain a spinlock, the function "spins" forever, or busy-
 * waits, until the lock is obtained.  When spinning, presumably some other
 * owner will soon give up the spinlock making it available to others.  Use
 * the trylock functions to avoid spinning forever.
 *
 * possible values:
 *
 *    0         nobody owns the spinlock
 *    1         somebody owns the spinlock
 */

#define arch_spin_is_locked(x) ((x)->slock != 0)

static inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	smp_cond_load_acquire(&lock->slock, !VAL);
}

#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned long tmp;

	__asm__ __volatile__(
			"       movi    %0, 0\n"
			"       wsr     %0, scompare1\n"
			"1:     movi    %0, 1\n"
			"       s32c1i  %0, %1, 0\n"
			"       bnez    %0, 1b\n"
			: "=&a" (tmp)
			: "a" (&lock->slock)
			: "memory");
}

/* Returns 1 if the lock is obtained, 0 otherwise. */

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned long tmp;

	__asm__ __volatile__(
			"       movi    %0, 0\n"
			"       wsr     %0, scompare1\n"
			"       movi    %0, 1\n"
			"       s32c1i  %0, %1, 0\n"
			: "=&a" (tmp)
			: "a" (&lock->slock)
			: "memory");

	return tmp == 0 ? 1 : 0;
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	unsigned long tmp;

	__asm__ __volatile__(
			"       movi    %0, 0\n"
			"       s32ri   %0, %1, 0\n"
			: "=&a" (tmp)
			: "a" (&lock->slock)
			: "memory");
}

/*
 * rwlock
 *
 * Read-write locks are really a more flexible spinlock.  They allow
 * multiple readers but only one writer.  Write ownership is exclusive
 * (i.e., all other readers and writers are blocked from ownership while
 * there is a write owner).  These rwlocks are unfair to writers.  Writers
 * can be starved for an indefinite time by readers.
 *
 * possible values:
 *
 *   0          nobody owns the rwlock
 *  >0          one or more readers own the rwlock
 *                (the positive value is the actual number of readers)
 *  0x80000000  one writer owns the rwlock, no other writers, no readers
 */

#define arch_write_can_lock(x)  ((x)->lock == 0)

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__(
			"       movi    %0, 0\n"
			"       wsr     %0, scompare1\n"
			"1:     movi    %0, 1\n"
			"       slli    %0, %0, 31\n"
			"       s32c1i  %0, %1, 0\n"
			"       bnez    %0, 1b\n"
			: "=&a" (tmp)
			: "a" (&rw->lock)
			: "memory");
}

/* Returns 1 if the lock is obtained, 0 otherwise. */

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__(
			"       movi    %0, 0\n"
			"       wsr     %0, scompare1\n"
			"       movi    %0, 1\n"
			"       slli    %0, %0, 31\n"
			"       s32c1i  %0, %1, 0\n"
			: "=&a" (tmp)
			: "a" (&rw->lock)
			: "memory");

	return tmp == 0 ? 1 : 0;
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__(
			"       movi    %0, 0\n"
			"       s32ri   %0, %1, 0\n"
			: "=&a" (tmp)
			: "a" (&rw->lock)
			: "memory");
}

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned long tmp;
	unsigned long result;

	__asm__ __volatile__(
			"1:     l32i    %1, %2, 0\n"
			"       bltz    %1, 1b\n"
			"       wsr     %1, scompare1\n"
			"       addi    %0, %1, 1\n"
			"       s32c1i  %0, %2, 0\n"
			"       bne     %0, %1, 1b\n"
			: "=&a" (result), "=&a" (tmp)
			: "a" (&rw->lock)
			: "memory");
}

/* Returns 1 if the lock is obtained, 0 otherwise. */

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned long result;
	unsigned long tmp;

	__asm__ __volatile__(
			"       l32i    %1, %2, 0\n"
			"       addi    %0, %1, 1\n"
			"       bltz    %0, 1f\n"
			"       wsr     %1, scompare1\n"
			"       s32c1i  %0, %2, 0\n"
			"       sub     %0, %0, %1\n"
			"1:\n"
			: "=&a" (result), "=&a" (tmp)
			: "a" (&rw->lock)
			: "memory");

	return result == 0;
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned long tmp1, tmp2;

	__asm__ __volatile__(
			"1:     l32i    %1, %2, 0\n"
			"       addi    %0, %1, -1\n"
			"       wsr     %1, scompare1\n"
			"       s32c1i  %0, %2, 0\n"
			"       bne     %0, %1, 1b\n"
			: "=&a" (tmp1), "=&a" (tmp2)
			: "a" (&rw->lock)
			: "memory");
}

#define arch_read_lock_flags(lock, flags)	arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags)	arch_write_lock(lock)

#endif	/* _XTENSA_SPINLOCK_H */
