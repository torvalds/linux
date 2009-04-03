#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/system.h>
#include <asm/processor.h>
#include <asm/spinlock_types.h>

static inline int __raw_spin_is_locked(raw_spinlock_t *x)
{
	volatile unsigned int *a = __ldcw_align(x);
	return *a == 0;
}

#define __raw_spin_lock(lock) __raw_spin_lock_flags(lock, 0)
#define __raw_spin_unlock_wait(x) \
		do { cpu_relax(); } while (__raw_spin_is_locked(x))

static inline void __raw_spin_lock_flags(raw_spinlock_t *x,
					 unsigned long flags)
{
	volatile unsigned int *a;

	mb();
	a = __ldcw_align(x);
	while (__ldcw(a) == 0)
		while (*a == 0)
			if (flags & PSW_SM_I) {
				local_irq_enable();
				cpu_relax();
				local_irq_disable();
			} else
				cpu_relax();
	mb();
}

static inline void __raw_spin_unlock(raw_spinlock_t *x)
{
	volatile unsigned int *a;
	mb();
	a = __ldcw_align(x);
	*a = 1;
	mb();
}

static inline int __raw_spin_trylock(raw_spinlock_t *x)
{
	volatile unsigned int *a;
	int ret;

	mb();
	a = __ldcw_align(x);
        ret = __ldcw(a) != 0;
	mb();

	return ret;
}

/*
 * Read-write spinlocks, allowing multiple readers but only one writer.
 * Linux rwlocks are unfair to writers; they can be starved for an indefinite
 * time by readers.  With care, they can also be taken in interrupt context.
 *
 * In the PA-RISC implementation, we have a spinlock and a counter.
 * Readers use the lock to serialise their access to the counter (which
 * records how many readers currently hold the lock).
 * Writers hold the spinlock, preventing any readers or other writers from
 * grabbing the rwlock.
 */

/* Note that we have to ensure interrupts are disabled in case we're
 * interrupted by some other code that wants to grab the same read lock */
static  __inline__ void __raw_read_lock(raw_rwlock_t *rw)
{
	unsigned long flags;
	local_irq_save(flags);
	__raw_spin_lock_flags(&rw->lock, flags);
	rw->counter++;
	__raw_spin_unlock(&rw->lock);
	local_irq_restore(flags);
}

/* Note that we have to ensure interrupts are disabled in case we're
 * interrupted by some other code that wants to grab the same read lock */
static  __inline__ void __raw_read_unlock(raw_rwlock_t *rw)
{
	unsigned long flags;
	local_irq_save(flags);
	__raw_spin_lock_flags(&rw->lock, flags);
	rw->counter--;
	__raw_spin_unlock(&rw->lock);
	local_irq_restore(flags);
}

/* Note that we have to ensure interrupts are disabled in case we're
 * interrupted by some other code that wants to grab the same read lock */
static __inline__ int __raw_read_trylock(raw_rwlock_t *rw)
{
	unsigned long flags;
 retry:
	local_irq_save(flags);
	if (__raw_spin_trylock(&rw->lock)) {
		rw->counter++;
		__raw_spin_unlock(&rw->lock);
		local_irq_restore(flags);
		return 1;
	}

	local_irq_restore(flags);
	/* If write-locked, we fail to acquire the lock */
	if (rw->counter < 0)
		return 0;

	/* Wait until we have a realistic chance at the lock */
	while (__raw_spin_is_locked(&rw->lock) && rw->counter >= 0)
		cpu_relax();

	goto retry;
}

/* Note that we have to ensure interrupts are disabled in case we're
 * interrupted by some other code that wants to read_trylock() this lock */
static __inline__ void __raw_write_lock(raw_rwlock_t *rw)
{
	unsigned long flags;
retry:
	local_irq_save(flags);
	__raw_spin_lock_flags(&rw->lock, flags);

	if (rw->counter != 0) {
		__raw_spin_unlock(&rw->lock);
		local_irq_restore(flags);

		while (rw->counter != 0)
			cpu_relax();

		goto retry;
	}

	rw->counter = -1; /* mark as write-locked */
	mb();
	local_irq_restore(flags);
}

static __inline__ void __raw_write_unlock(raw_rwlock_t *rw)
{
	rw->counter = 0;
	__raw_spin_unlock(&rw->lock);
}

/* Note that we have to ensure interrupts are disabled in case we're
 * interrupted by some other code that wants to read_trylock() this lock */
static __inline__ int __raw_write_trylock(raw_rwlock_t *rw)
{
	unsigned long flags;
	int result = 0;

	local_irq_save(flags);
	if (__raw_spin_trylock(&rw->lock)) {
		if (rw->counter == 0) {
			rw->counter = -1;
			result = 1;
		} else {
			/* Read-locked.  Oh well. */
			__raw_spin_unlock(&rw->lock);
		}
	}
	local_irq_restore(flags);

	return result;
}

/*
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
static __inline__ int __raw_read_can_lock(raw_rwlock_t *rw)
{
	return rw->counter >= 0;
}

/*
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
static __inline__ int __raw_write_can_lock(raw_rwlock_t *rw)
{
	return !rw->counter;
}

#define __raw_read_lock_flags(lock, flags) __raw_read_lock(lock)
#define __raw_write_lock_flags(lock, flags) __raw_write_lock(lock)

#define _raw_spin_relax(lock)	cpu_relax()
#define _raw_read_relax(lock)	cpu_relax()
#define _raw_write_relax(lock)	cpu_relax()

#endif /* __ASM_SPINLOCK_H */
