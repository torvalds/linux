/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_X86_PARAVIRT_SPINLOCK_H
#define _ASM_X86_PARAVIRT_SPINLOCK_H

#include <asm/paravirt_types.h>

#ifdef CONFIG_SMP
#include <asm/spinlock_types.h>
#endif

struct qspinlock;

struct pv_lock_ops {
	void (*queued_spin_lock_slowpath)(struct qspinlock *lock, u32 val);
	struct paravirt_callee_save queued_spin_unlock;

	void (*wait)(u8 *ptr, u8 val);
	void (*kick)(int cpu);

	struct paravirt_callee_save vcpu_is_preempted;
} __no_randomize_layout;

extern struct pv_lock_ops pv_ops_lock;

#ifdef CONFIG_PARAVIRT_SPINLOCKS
extern void native_queued_spin_lock_slowpath(struct qspinlock *lock, u32 val);
extern void __pv_init_lock_hash(void);
extern void __pv_queued_spin_lock_slowpath(struct qspinlock *lock, u32 val);
extern void __raw_callee_save___pv_queued_spin_unlock(struct qspinlock *lock);
extern bool nopvspin;

static __always_inline void pv_queued_spin_lock_slowpath(struct qspinlock *lock,
							 u32 val)
{
	PVOP_VCALL2(pv_ops_lock, queued_spin_lock_slowpath, lock, val);
}

static __always_inline void pv_queued_spin_unlock(struct qspinlock *lock)
{
	PVOP_ALT_VCALLEE1(pv_ops_lock, queued_spin_unlock, lock,
			  "movb $0, (%%" _ASM_ARG1 ")",
			  ALT_NOT(X86_FEATURE_PVUNLOCK));
}

static __always_inline bool pv_vcpu_is_preempted(long cpu)
{
	return PVOP_ALT_CALLEE1(bool, pv_ops_lock, vcpu_is_preempted, cpu,
				"xor %%eax, %%eax",
				ALT_NOT(X86_FEATURE_VCPUPREEMPT));
}

#define queued_spin_unlock queued_spin_unlock
/**
 * queued_spin_unlock - release a queued spinlock
 * @lock : Pointer to queued spinlock structure
 *
 * A smp_store_release() on the least-significant byte.
 */
static inline void native_queued_spin_unlock(struct qspinlock *lock)
{
	smp_store_release(&lock->locked, 0);
}

static inline void queued_spin_lock_slowpath(struct qspinlock *lock, u32 val)
{
	pv_queued_spin_lock_slowpath(lock, val);
}

static inline void queued_spin_unlock(struct qspinlock *lock)
{
	kcsan_release();
	pv_queued_spin_unlock(lock);
}

#define vcpu_is_preempted vcpu_is_preempted
static inline bool vcpu_is_preempted(long cpu)
{
	return pv_vcpu_is_preempted(cpu);
}

static __always_inline void pv_wait(u8 *ptr, u8 val)
{
	PVOP_VCALL2(pv_ops_lock, wait, ptr, val);
}

static __always_inline void pv_kick(int cpu)
{
	PVOP_VCALL1(pv_ops_lock, kick, cpu);
}

void __raw_callee_save___native_queued_spin_unlock(struct qspinlock *lock);
bool __raw_callee_save___native_vcpu_is_preempted(long cpu);
#endif /* CONFIG_PARAVIRT_SPINLOCKS */

void __init native_pv_lock_init(void);
__visible void __native_queued_spin_unlock(struct qspinlock *lock);
bool pv_is_native_spin_unlock(void);
__visible bool __native_vcpu_is_preempted(long cpu);
bool pv_is_native_vcpu_is_preempted(void);

/*
 * virt_spin_lock_key - disables by default the virt_spin_lock() hijack.
 *
 * Native (and PV wanting native due to vCPU pinning) should keep this key
 * disabled. Native does not touch the key.
 *
 * When in a guest then native_pv_lock_init() enables the key first and
 * KVM/XEN might conditionally disable it later in the boot process again.
 */
DECLARE_STATIC_KEY_FALSE(virt_spin_lock_key);

/*
 * Shortcut for the queued_spin_lock_slowpath() function that allows
 * virt to hijack it.
 *
 * Returns:
 *   true - lock has been negotiated, all done;
 *   false - queued_spin_lock_slowpath() will do its thing.
 */
#define virt_spin_lock virt_spin_lock
static inline bool virt_spin_lock(struct qspinlock *lock)
{
	int val;

	if (!static_branch_likely(&virt_spin_lock_key))
		return false;

	/*
	 * On hypervisors without PARAVIRT_SPINLOCKS support we fall
	 * back to a Test-and-Set spinlock, because fair locks have
	 * horrible lock 'holder' preemption issues.
	 */

 __retry:
	val = atomic_read(&lock->val);

	if (val || !atomic_try_cmpxchg(&lock->val, &val, _Q_LOCKED_VAL)) {
		cpu_relax();
		goto __retry;
	}

	return true;
}

#endif /* _ASM_X86_PARAVIRT_SPINLOCK_H */
