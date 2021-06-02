/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_QSPINLOCK_H
#define _ASM_POWERPC_QSPINLOCK_H

#include <asm-generic/qspinlock_types.h>
#include <asm/paravirt.h>

#define _Q_PENDING_LOOPS	(1 << 9) /* not tuned */

#ifdef CONFIG_PARAVIRT_SPINLOCKS
extern void native_queued_spin_lock_slowpath(struct qspinlock *lock, u32 val);
extern void __pv_queued_spin_lock_slowpath(struct qspinlock *lock, u32 val);
extern void __pv_queued_spin_unlock(struct qspinlock *lock);

static __always_inline void queued_spin_lock_slowpath(struct qspinlock *lock, u32 val)
{
	if (!is_shared_processor())
		native_queued_spin_lock_slowpath(lock, val);
	else
		__pv_queued_spin_lock_slowpath(lock, val);
}

#define queued_spin_unlock queued_spin_unlock
static inline void queued_spin_unlock(struct qspinlock *lock)
{
	if (!is_shared_processor())
		smp_store_release(&lock->locked, 0);
	else
		__pv_queued_spin_unlock(lock);
}

#else
extern void queued_spin_lock_slowpath(struct qspinlock *lock, u32 val);
#endif

static __always_inline void queued_spin_lock(struct qspinlock *lock)
{
	u32 val = 0;

	if (likely(atomic_try_cmpxchg_lock(&lock->val, &val, _Q_LOCKED_VAL)))
		return;

	queued_spin_lock_slowpath(lock, val);
}
#define queued_spin_lock queued_spin_lock

#ifdef CONFIG_PARAVIRT_SPINLOCKS
#define SPIN_THRESHOLD (1<<15) /* not tuned */

static __always_inline void pv_wait(u8 *ptr, u8 val)
{
	if (*ptr != val)
		return;
	yield_to_any();
	/*
	 * We could pass in a CPU here if waiting in the queue and yield to
	 * the previous CPU in the queue.
	 */
}

static __always_inline void pv_kick(int cpu)
{
	prod_cpu(cpu);
}

extern void __pv_init_lock_hash(void);

static inline void pv_spinlocks_init(void)
{
	__pv_init_lock_hash();
}

#endif

/*
 * Queued spinlocks rely heavily on smp_cond_load_relaxed() to busy-wait,
 * which was found to have performance problems if implemented with
 * the preferred spin_begin()/spin_end() SMT priority pattern. Use the
 * generic version instead.
 */

#include <asm-generic/qspinlock.h>

#endif /* _ASM_POWERPC_QSPINLOCK_H */
