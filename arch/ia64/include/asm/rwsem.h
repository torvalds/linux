/*
 * R/W semaphores for ia64
 *
 * Copyright (C) 2003 Ken Chen <kenneth.w.chen@intel.com>
 * Copyright (C) 2003 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 2005 Christoph Lameter <cl@linux.com>
 *
 * Based on asm-i386/rwsem.h and other architecture implementation.
 *
 * The MSW of the count is the negated number of active writers and
 * waiting lockers, and the LSW is the total number of active locks.
 *
 * The lock count is initialized to 0 (no active and no waiting lockers).
 *
 * When a writer subtracts WRITE_BIAS, it'll get 0xffffffff00000001 for
 * the case of an uncontended lock. Readers increment by 1 and see a positive
 * value when uncontended, negative if there are writers (and maybe) readers
 * waiting (in which case it goes to sleep).
 */

#ifndef _ASM_IA64_RWSEM_H
#define _ASM_IA64_RWSEM_H

#ifndef _LINUX_RWSEM_H
#error "Please don't include <asm/rwsem.h> directly, use <linux/rwsem.h> instead."
#endif

#include <asm/intrinsics.h>

#define RWSEM_UNLOCKED_VALUE		__IA64_UL_CONST(0x0000000000000000)
#define RWSEM_ACTIVE_BIAS		(1L)
#define RWSEM_ACTIVE_MASK		(0xffffffffL)
#define RWSEM_WAITING_BIAS		(-0x100000000L)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)

/*
 * lock for reading
 */
static inline void
__down_read (struct rw_semaphore *sem)
{
	long result = ia64_fetchadd8_acq((unsigned long *)&sem->count, 1);

	if (result < 0)
		rwsem_down_read_failed(sem);
}

/*
 * lock for writing
 */
static inline void
__down_write (struct rw_semaphore *sem)
{
	long old, new;

	do {
		old = sem->count;
		new = old + RWSEM_ACTIVE_WRITE_BIAS;
	} while (cmpxchg_acq(&sem->count, old, new) != old);

	if (old != 0)
		rwsem_down_write_failed(sem);
}

/*
 * unlock after reading
 */
static inline void
__up_read (struct rw_semaphore *sem)
{
	long result = ia64_fetchadd8_rel((unsigned long *)&sem->count, -1);

	if (result < 0 && (--result & RWSEM_ACTIVE_MASK) == 0)
		rwsem_wake(sem);
}

/*
 * unlock after writing
 */
static inline void
__up_write (struct rw_semaphore *sem)
{
	long old, new;

	do {
		old = sem->count;
		new = old - RWSEM_ACTIVE_WRITE_BIAS;
	} while (cmpxchg_rel(&sem->count, old, new) != old);

	if (new < 0 && (new & RWSEM_ACTIVE_MASK) == 0)
		rwsem_wake(sem);
}

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
static inline int
__down_read_trylock (struct rw_semaphore *sem)
{
	long tmp;
	while ((tmp = sem->count) >= 0) {
		if (tmp == cmpxchg_acq(&sem->count, tmp, tmp+1)) {
			return 1;
		}
	}
	return 0;
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
static inline int
__down_write_trylock (struct rw_semaphore *sem)
{
	long tmp = cmpxchg_acq(&sem->count, RWSEM_UNLOCKED_VALUE,
			      RWSEM_ACTIVE_WRITE_BIAS);
	return tmp == RWSEM_UNLOCKED_VALUE;
}

/*
 * downgrade write lock to read lock
 */
static inline void
__downgrade_write (struct rw_semaphore *sem)
{
	long old, new;

	do {
		old = sem->count;
		new = old - RWSEM_WAITING_BIAS;
	} while (cmpxchg_rel(&sem->count, old, new) != old);

	if (old < 0)
		rwsem_downgrade_wake(sem);
}

/*
 * Implement atomic add functionality.  These used to be "inline" functions, but GCC v3.1
 * doesn't quite optimize this stuff right and ends up with bad calls to fetchandadd.
 */
#define rwsem_atomic_add(delta, sem)	atomic64_add(delta, (atomic64_t *)(&(sem)->count))
#define rwsem_atomic_update(delta, sem)	atomic64_add_return(delta, (atomic64_t *)(&(sem)->count))

#endif /* _ASM_IA64_RWSEM_H */
