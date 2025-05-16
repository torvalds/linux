/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_RQSPINLOCK_H
#define _ASM_X86_RQSPINLOCK_H

#include <asm/paravirt.h>

#ifdef CONFIG_PARAVIRT
DECLARE_STATIC_KEY_FALSE(virt_spin_lock_key);

#define resilient_virt_spin_lock_enabled resilient_virt_spin_lock_enabled
static __always_inline bool resilient_virt_spin_lock_enabled(void)
{
       return static_branch_likely(&virt_spin_lock_key);
}

#ifdef CONFIG_QUEUED_SPINLOCKS
typedef struct qspinlock rqspinlock_t;
#else
typedef struct rqspinlock rqspinlock_t;
#endif
extern int resilient_tas_spin_lock(rqspinlock_t *lock);

#define resilient_virt_spin_lock resilient_virt_spin_lock
static inline int resilient_virt_spin_lock(rqspinlock_t *lock)
{
	return resilient_tas_spin_lock(lock);
}

#endif /* CONFIG_PARAVIRT */

#include <asm-generic/rqspinlock.h>

#endif /* _ASM_X86_RQSPINLOCK_H */
