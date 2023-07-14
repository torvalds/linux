/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_QSPINLOCK_H
#define _ASM_QSPINLOCK_H

#include <asm-generic/qspinlock_types.h>

#define queued_spin_unlock queued_spin_unlock

static inline void queued_spin_unlock(struct qspinlock *lock)
{
	compiletime_assert_atomic_type(lock->locked);
	c_sync();
	WRITE_ONCE(lock->locked, 0);
}

#include <asm-generic/qspinlock.h>

#endif /* _ASM_QSPINLOCK_H */
