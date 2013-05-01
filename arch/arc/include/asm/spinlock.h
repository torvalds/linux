/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/spinlock_types.h>
#include <asm/processor.h>
#include <asm/barrier.h>

#define arch_spin_is_locked(x)	((x)->slock != __ARCH_SPIN_LOCK_UNLOCKED__)
#define arch_spin_lock_flags(lock, flags)	arch_spin_lock(lock)
#define arch_spin_unlock_wait(x) \
	do { while (arch_spin_is_locked(x)) cpu_relax(); } while (0)

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned int tmp = __ARCH_SPIN_LOCK_LOCKED__;

	__asm__ __volatile__(
	"1:	ex  %0, [%1]		\n"
	"	breq  %0, %2, 1b	\n"
	: "+&r" (tmp)
	: "r"(&(lock->slock)), "ir"(__ARCH_SPIN_LOCK_LOCKED__)
	: "memory");
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned int tmp = __ARCH_SPIN_LOCK_LOCKED__;

	__asm__ __volatile__(
	"1:	ex  %0, [%1]		\n"
	: "+r" (tmp)
	: "r"(&(lock->slock))
	: "memory");

	return (tmp == __ARCH_SPIN_LOCK_UNLOCKED__);
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	lock->slock = __ARCH_SPIN_LOCK_UNLOCKED__;
	smp_mb();
}

/*
 * Read-write spinlocks, allowing multiple readers but only one writer.
 *
 * The spinlock itself is contained in @counter and access to it is
 * serialized with @lock_mutex.
 *
 * Unfair locking as Writers could be starved indefinitely by Reader(s)
 */

/* Would read_trylock() succeed? */
#define arch_read_can_lock(x)	((x)->counter > 0)

/* Would write_trylock() succeed? */
#define arch_write_can_lock(x)	((x)->counter == __ARCH_RW_LOCK_UNLOCKED__)

/* 1 - lock taken successfully */
static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	int ret = 0;

	arch_spin_lock(&(rw->lock_mutex));

	/*
	 * zero means writer holds the lock exclusively, deny Reader.
	 * Otherwise grant lock to first/subseq reader
	 */
	if (rw->counter > 0) {
		rw->counter--;
		ret = 1;
	}

	arch_spin_unlock(&(rw->lock_mutex));

	smp_mb();
	return ret;
}

/* 1 - lock taken successfully */
static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	int ret = 0;

	arch_spin_lock(&(rw->lock_mutex));

	/*
	 * If reader(s) hold lock (lock < __ARCH_RW_LOCK_UNLOCKED__),
	 * deny writer. Otherwise if unlocked grant to writer
	 * Hence the claim that Linux rwlocks are unfair to writers.
	 * (can be starved for an indefinite time by readers).
	 */
	if (rw->counter == __ARCH_RW_LOCK_UNLOCKED__) {
		rw->counter = 0;
		ret = 1;
	}
	arch_spin_unlock(&(rw->lock_mutex));

	return ret;
}

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	while (!arch_read_trylock(rw))
		cpu_relax();
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	while (!arch_write_trylock(rw))
		cpu_relax();
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	arch_spin_lock(&(rw->lock_mutex));
	rw->counter++;
	arch_spin_unlock(&(rw->lock_mutex));
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	arch_spin_lock(&(rw->lock_mutex));
	rw->counter = __ARCH_RW_LOCK_UNLOCKED__;
	arch_spin_unlock(&(rw->lock_mutex));
}

#define arch_read_lock_flags(lock, flags)	arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags)	arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* __ASM_SPINLOCK_H */
