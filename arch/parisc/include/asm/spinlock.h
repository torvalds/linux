/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/barrier.h>
#include <asm/ldcw.h>
#include <asm/processor.h>
#include <asm/spinlock_types.h>

static inline void arch_spin_val_check(int lock_val)
{
	if (IS_ENABLED(CONFIG_LIGHTWEIGHT_SPINLOCK_CHECK))
		asm volatile(	"andcm,= %0,%1,%%r0\n"
				".word %2\n"
		: : "r" (lock_val), "r" (__ARCH_SPIN_LOCK_UNLOCKED_VAL),
			"i" (SPINLOCK_BREAK_INSN));
}

static inline int arch_spin_is_locked(arch_spinlock_t *x)
{
	volatile unsigned int *a;
	int lock_val;

	a = __ldcw_align(x);
	lock_val = READ_ONCE(*a);
	arch_spin_val_check(lock_val);
	return (lock_val == 0);
}

static inline void arch_spin_lock(arch_spinlock_t *x)
{
	volatile unsigned int *a;

	a = __ldcw_align(x);
	do {
		int lock_val_old;

		lock_val_old = __ldcw(a);
		arch_spin_val_check(lock_val_old);
		if (lock_val_old)
			return;	/* got lock */

		/* wait until we should try to get lock again */
		while (*a == 0)
			continue;
	} while (1);
}

static inline void arch_spin_unlock(arch_spinlock_t *x)
{
	volatile unsigned int *a;

	a = __ldcw_align(x);
	/* Release with ordered store. */
	__asm__ __volatile__("stw,ma %0,0(%1)"
		: : "r"(__ARCH_SPIN_LOCK_UNLOCKED_VAL), "r"(a) : "memory");
}

static inline int arch_spin_trylock(arch_spinlock_t *x)
{
	volatile unsigned int *a;
	int lock_val;

	a = __ldcw_align(x);
	lock_val = __ldcw(a);
	arch_spin_val_check(lock_val);
	return lock_val != 0;
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

#endif /* __ASM_SPINLOCK_H */
