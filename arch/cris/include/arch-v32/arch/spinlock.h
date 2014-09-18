#ifndef __ASM_ARCH_SPINLOCK_H
#define __ASM_ARCH_SPINLOCK_H

#include <linux/spinlock_types.h>

#define RW_LOCK_BIAS 0x01000000

extern void cris_spin_unlock(void *l, int val);
extern void cris_spin_lock(void *l);
extern int cris_spin_trylock(void *l);

static inline int arch_spin_is_locked(arch_spinlock_t *x)
{
	return *(volatile signed char *)(&(x)->slock) <= 0;
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	__asm__ volatile ("move.d %1,%0" \
			  : "=m" (lock->slock) \
			  : "r" (1) \
			  : "memory");
}

static inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	while (arch_spin_is_locked(lock))
		cpu_relax();
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	return cris_spin_trylock((void *)&lock->slock);
}

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	cris_spin_lock((void *)&lock->slock);
}

static inline void
arch_spin_lock_flags(arch_spinlock_t *lock, unsigned long flags)
{
	arch_spin_lock(lock);
}

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 *
 */

static inline int arch_read_can_lock(arch_rwlock_t *x)
{
	return (int)(x)->lock > 0;
}

static inline int arch_write_can_lock(arch_rwlock_t *x)
{
	return (x)->lock == RW_LOCK_BIAS;
}

static  inline void arch_read_lock(arch_rwlock_t *rw)
{
	arch_spin_lock(&rw->slock);
	while (rw->lock == 0);
	rw->lock--;
	arch_spin_unlock(&rw->slock);
}

static  inline void arch_write_lock(arch_rwlock_t *rw)
{
	arch_spin_lock(&rw->slock);
	while (rw->lock != RW_LOCK_BIAS);
	rw->lock = 0;
	arch_spin_unlock(&rw->slock);
}

static  inline void arch_read_unlock(arch_rwlock_t *rw)
{
	arch_spin_lock(&rw->slock);
	rw->lock++;
	arch_spin_unlock(&rw->slock);
}

static  inline void arch_write_unlock(arch_rwlock_t *rw)
{
	arch_spin_lock(&rw->slock);
	while (rw->lock != RW_LOCK_BIAS);
	rw->lock = RW_LOCK_BIAS;
	arch_spin_unlock(&rw->slock);
}

static  inline int arch_read_trylock(arch_rwlock_t *rw)
{
	int ret = 0;
	arch_spin_lock(&rw->slock);
	if (rw->lock != 0) {
		rw->lock--;
		ret = 1;
	}
	arch_spin_unlock(&rw->slock);
	return ret;
}

static  inline int arch_write_trylock(arch_rwlock_t *rw)
{
	int ret = 0;
	arch_spin_lock(&rw->slock);
	if (rw->lock == RW_LOCK_BIAS) {
		rw->lock = 0;
		ret = 1;
	}
	arch_spin_unlock(&rw->slock);
	return ret;
}

#define _raw_read_lock_flags(lock, flags) _raw_read_lock(lock)
#define _raw_write_lock_flags(lock, flags) _raw_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* __ASM_ARCH_SPINLOCK_H */
