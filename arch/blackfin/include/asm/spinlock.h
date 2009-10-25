/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __BFIN_SPINLOCK_H
#define __BFIN_SPINLOCK_H

#ifndef CONFIG_SMP
# include <asm-generic/spinlock.h>
#else

#include <asm/atomic.h>

asmlinkage int __raw_spin_is_locked_asm(volatile int *ptr);
asmlinkage void __raw_spin_lock_asm(volatile int *ptr);
asmlinkage int __raw_spin_trylock_asm(volatile int *ptr);
asmlinkage void __raw_spin_unlock_asm(volatile int *ptr);
asmlinkage void __raw_read_lock_asm(volatile int *ptr);
asmlinkage int __raw_read_trylock_asm(volatile int *ptr);
asmlinkage void __raw_read_unlock_asm(volatile int *ptr);
asmlinkage void __raw_write_lock_asm(volatile int *ptr);
asmlinkage int __raw_write_trylock_asm(volatile int *ptr);
asmlinkage void __raw_write_unlock_asm(volatile int *ptr);

static inline int __raw_spin_is_locked(raw_spinlock_t *lock)
{
	return __raw_spin_is_locked_asm(&lock->lock);
}

static inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	__raw_spin_lock_asm(&lock->lock);
}

#define __raw_spin_lock_flags(lock, flags) __raw_spin_lock(lock)

static inline int __raw_spin_trylock(raw_spinlock_t *lock)
{
	return __raw_spin_trylock_asm(&lock->lock);
}

static inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	__raw_spin_unlock_asm(&lock->lock);
}

static inline void __raw_spin_unlock_wait(raw_spinlock_t *lock)
{
	while (__raw_spin_is_locked(lock))
		cpu_relax();
}

static inline int __raw_read_can_lock(raw_rwlock_t *rw)
{
	return __raw_uncached_fetch_asm(&rw->lock) > 0;
}

static inline int __raw_write_can_lock(raw_rwlock_t *rw)
{
	return __raw_uncached_fetch_asm(&rw->lock) == RW_LOCK_BIAS;
}

static inline void __raw_read_lock(raw_rwlock_t *rw)
{
	__raw_read_lock_asm(&rw->lock);
}

static inline int __raw_read_trylock(raw_rwlock_t *rw)
{
	return __raw_read_trylock_asm(&rw->lock);
}

static inline void __raw_read_unlock(raw_rwlock_t *rw)
{
	__raw_read_unlock_asm(&rw->lock);
}

static inline void __raw_write_lock(raw_rwlock_t *rw)
{
	__raw_write_lock_asm(&rw->lock);
}

static inline int __raw_write_trylock(raw_rwlock_t *rw)
{
	return __raw_write_trylock_asm(&rw->lock);
}

static inline void __raw_write_unlock(raw_rwlock_t *rw)
{
	__raw_write_unlock_asm(&rw->lock);
}

#define _raw_spin_relax(lock)  	cpu_relax()
#define _raw_read_relax(lock)	cpu_relax()
#define _raw_write_relax(lock)	cpu_relax()

#endif

#endif /*  !__BFIN_SPINLOCK_H */
