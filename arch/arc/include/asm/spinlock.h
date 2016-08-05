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

#ifdef CONFIG_ARC_HAS_LLSC

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned int val;

	smp_mb();

	__asm__ __volatile__(
	"1:	llock	%[val], [%[slock]]	\n"
	"	breq	%[val], %[LOCKED], 1b	\n"	/* spin while LOCKED */
	"	scond	%[LOCKED], [%[slock]]	\n"	/* acquire */
	"	bnz	1b			\n"
	"					\n"
	: [val]		"=&r"	(val)
	: [slock]	"r"	(&(lock->slock)),
	  [LOCKED]	"r"	(__ARCH_SPIN_LOCK_LOCKED__)
	: "memory", "cc");

	smp_mb();
}

/* 1 - lock taken successfully */
static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned int val, got_it = 0;

	smp_mb();

	__asm__ __volatile__(
	"1:	llock	%[val], [%[slock]]	\n"
	"	breq	%[val], %[LOCKED], 4f	\n"	/* already LOCKED, just bail */
	"	scond	%[LOCKED], [%[slock]]	\n"	/* acquire */
	"	bnz	1b			\n"
	"	mov	%[got_it], 1		\n"
	"4:					\n"
	"					\n"
	: [val]		"=&r"	(val),
	  [got_it]	"+&r"	(got_it)
	: [slock]	"r"	(&(lock->slock)),
	  [LOCKED]	"r"	(__ARCH_SPIN_LOCK_LOCKED__)
	: "memory", "cc");

	smp_mb();

	return got_it;
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	smp_mb();

	lock->slock = __ARCH_SPIN_LOCK_UNLOCKED__;

	smp_mb();
}

/*
 * Read-write spinlocks, allowing multiple readers but only one writer.
 * Unfair locking as Writers could be starved indefinitely by Reader(s)
 */

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned int val;

	smp_mb();

	/*
	 * zero means writer holds the lock exclusively, deny Reader.
	 * Otherwise grant lock to first/subseq reader
	 *
	 * 	if (rw->counter > 0) {
	 *		rw->counter--;
	 *		ret = 1;
	 *	}
	 */

	__asm__ __volatile__(
	"1:	llock	%[val], [%[rwlock]]	\n"
	"	brls	%[val], %[WR_LOCKED], 1b\n"	/* <= 0: spin while write locked */
	"	sub	%[val], %[val], 1	\n"	/* reader lock */
	"	scond	%[val], [%[rwlock]]	\n"
	"	bnz	1b			\n"
	"					\n"
	: [val]		"=&r"	(val)
	: [rwlock]	"r"	(&(rw->counter)),
	  [WR_LOCKED]	"ir"	(0)
	: "memory", "cc");

	smp_mb();
}

/* 1 - lock taken successfully */
static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned int val, got_it = 0;

	smp_mb();

	__asm__ __volatile__(
	"1:	llock	%[val], [%[rwlock]]	\n"
	"	brls	%[val], %[WR_LOCKED], 4f\n"	/* <= 0: already write locked, bail */
	"	sub	%[val], %[val], 1	\n"	/* counter-- */
	"	scond	%[val], [%[rwlock]]	\n"
	"	bnz	1b			\n"	/* retry if collided with someone */
	"	mov	%[got_it], 1		\n"
	"					\n"
	"4: ; --- done ---			\n"

	: [val]		"=&r"	(val),
	  [got_it]	"+&r"	(got_it)
	: [rwlock]	"r"	(&(rw->counter)),
	  [WR_LOCKED]	"ir"	(0)
	: "memory", "cc");

	smp_mb();

	return got_it;
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned int val;

	smp_mb();

	/*
	 * If reader(s) hold lock (lock < __ARCH_RW_LOCK_UNLOCKED__),
	 * deny writer. Otherwise if unlocked grant to writer
	 * Hence the claim that Linux rwlocks are unfair to writers.
	 * (can be starved for an indefinite time by readers).
	 *
	 *	if (rw->counter == __ARCH_RW_LOCK_UNLOCKED__) {
	 *		rw->counter = 0;
	 *		ret = 1;
	 *	}
	 */

	__asm__ __volatile__(
	"1:	llock	%[val], [%[rwlock]]	\n"
	"	brne	%[val], %[UNLOCKED], 1b	\n"	/* while !UNLOCKED spin */
	"	mov	%[val], %[WR_LOCKED]	\n"
	"	scond	%[val], [%[rwlock]]	\n"
	"	bnz	1b			\n"
	"					\n"
	: [val]		"=&r"	(val)
	: [rwlock]	"r"	(&(rw->counter)),
	  [UNLOCKED]	"ir"	(__ARCH_RW_LOCK_UNLOCKED__),
	  [WR_LOCKED]	"ir"	(0)
	: "memory", "cc");

	smp_mb();
}

/* 1 - lock taken successfully */
static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned int val, got_it = 0;

	smp_mb();

	__asm__ __volatile__(
	"1:	llock	%[val], [%[rwlock]]	\n"
	"	brne	%[val], %[UNLOCKED], 4f	\n"	/* !UNLOCKED, bail */
	"	mov	%[val], %[WR_LOCKED]	\n"
	"	scond	%[val], [%[rwlock]]	\n"
	"	bnz	1b			\n"	/* retry if collided with someone */
	"	mov	%[got_it], 1		\n"
	"					\n"
	"4: ; --- done ---			\n"

	: [val]		"=&r"	(val),
	  [got_it]	"+&r"	(got_it)
	: [rwlock]	"r"	(&(rw->counter)),
	  [UNLOCKED]	"ir"	(__ARCH_RW_LOCK_UNLOCKED__),
	  [WR_LOCKED]	"ir"	(0)
	: "memory", "cc");

	smp_mb();

	return got_it;
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned int val;

	smp_mb();

	/*
	 * rw->counter++;
	 */
	__asm__ __volatile__(
	"1:	llock	%[val], [%[rwlock]]	\n"
	"	add	%[val], %[val], 1	\n"
	"	scond	%[val], [%[rwlock]]	\n"
	"	bnz	1b			\n"
	"					\n"
	: [val]		"=&r"	(val)
	: [rwlock]	"r"	(&(rw->counter))
	: "memory", "cc");

	smp_mb();
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	smp_mb();

	rw->counter = __ARCH_RW_LOCK_UNLOCKED__;

	smp_mb();
}

#else	/* !CONFIG_ARC_HAS_LLSC */

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned int val = __ARCH_SPIN_LOCK_LOCKED__;

	/*
	 * This smp_mb() is technically superfluous, we only need the one
	 * after the lock for providing the ACQUIRE semantics.
	 * However doing the "right" thing was regressing hackbench
	 * so keeping this, pending further investigation
	 */
	smp_mb();

	__asm__ __volatile__(
	"1:	ex  %0, [%1]		\n"
	"	breq  %0, %2, 1b	\n"
	: "+&r" (val)
	: "r"(&(lock->slock)), "ir"(__ARCH_SPIN_LOCK_LOCKED__)
	: "memory");

	/*
	 * ACQUIRE barrier to ensure load/store after taking the lock
	 * don't "bleed-up" out of the critical section (leak-in is allowed)
	 * http://www.spinics.net/lists/kernel/msg2010409.html
	 *
	 * ARCv2 only has load-load, store-store and all-all barrier
	 * thus need the full all-all barrier
	 */
	smp_mb();
}

/* 1 - lock taken successfully */
static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned int val = __ARCH_SPIN_LOCK_LOCKED__;

	smp_mb();

	__asm__ __volatile__(
	"1:	ex  %0, [%1]		\n"
	: "+r" (val)
	: "r"(&(lock->slock))
	: "memory");

	smp_mb();

	return (val == __ARCH_SPIN_LOCK_UNLOCKED__);
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	unsigned int val = __ARCH_SPIN_LOCK_UNLOCKED__;

	/*
	 * RELEASE barrier: given the instructions avail on ARCv2, full barrier
	 * is the only option
	 */
	smp_mb();

	__asm__ __volatile__(
	"	ex  %0, [%1]		\n"
	: "+r" (val)
	: "r"(&(lock->slock))
	: "memory");

	/*
	 * superfluous, but keeping for now - see pairing version in
	 * arch_spin_lock above
	 */
	smp_mb();
}

/*
 * Read-write spinlocks, allowing multiple readers but only one writer.
 * Unfair locking as Writers could be starved indefinitely by Reader(s)
 *
 * The spinlock itself is contained in @counter and access to it is
 * serialized with @lock_mutex.
 */

/* 1 - lock taken successfully */
static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	int ret = 0;
	unsigned long flags;

	local_irq_save(flags);
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
	local_irq_restore(flags);

	smp_mb();
	return ret;
}

/* 1 - lock taken successfully */
static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	int ret = 0;
	unsigned long flags;

	local_irq_save(flags);
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
	local_irq_restore(flags);

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
	unsigned long flags;

	local_irq_save(flags);
	arch_spin_lock(&(rw->lock_mutex));
	rw->counter++;
	arch_spin_unlock(&(rw->lock_mutex));
	local_irq_restore(flags);
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	unsigned long flags;

	local_irq_save(flags);
	arch_spin_lock(&(rw->lock_mutex));
	rw->counter = __ARCH_RW_LOCK_UNLOCKED__;
	arch_spin_unlock(&(rw->lock_mutex));
	local_irq_restore(flags);
}

#endif

#define arch_read_can_lock(x)	((x)->counter > 0)
#define arch_write_can_lock(x)	((x)->counter == __ARCH_RW_LOCK_UNLOCKED__)

#define arch_read_lock_flags(lock, flags)	arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags)	arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* __ASM_SPINLOCK_H */
