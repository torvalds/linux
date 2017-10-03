/*
 * include/asm-sh/spinlock-cas.h
 *
 * Copyright (C) 2015 SEI
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_SPINLOCK_CAS_H
#define __ASM_SH_SPINLOCK_CAS_H

#include <asm/barrier.h>
#include <asm/processor.h>

static inline unsigned __sl_cas(volatile unsigned *p, unsigned old, unsigned new)
{
	__asm__ __volatile__("cas.l %1,%0,@r0"
		: "+r"(new)
		: "r"(old), "z"(p)
		: "t", "memory" );
	return new;
}

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 */

#define arch_spin_is_locked(x)		((x)->lock <= 0)
#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	while (!__sl_cas(&lock->lock, 1, 0));
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	__sl_cas(&lock->lock, 0, 1);
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	return __sl_cas(&lock->lock, 1, 0);
}

/*
 * Read-write spinlocks, allowing multiple readers but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts but no interrupt
 * writers. For those circumstances we can "mix" irq-safe locks - any writer
 * needs to get a irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned old;
	do old = rw->lock;
	while (!old || __sl_cas(&rw->lock, old, old-1) != old);
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned old;
	do old = rw->lock;
	while (__sl_cas(&rw->lock, old, old+1) != old);
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	while (__sl_cas(&rw->lock, RW_LOCK_BIAS, 0) != RW_LOCK_BIAS);
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	__sl_cas(&rw->lock, 0, RW_LOCK_BIAS);
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned old;
	do old = rw->lock;
	while (old && __sl_cas(&rw->lock, old, old-1) != old);
	return !!old;
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	return __sl_cas(&rw->lock, RW_LOCK_BIAS, 0) == RW_LOCK_BIAS;
}

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* __ASM_SH_SPINLOCK_CAS_H */
