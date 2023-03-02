/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_QSPINLOCK_PARAVIRT_H
#define __ASM_QSPINLOCK_PARAVIRT_H

#include <asm/ibt.h>

/*
 * For x86-64, PV_CALLEE_SAVE_REGS_THUNK() saves and restores 8 64-bit
 * registers. For i386, however, only 1 32-bit register needs to be saved
 * and restored. So an optimized version of __pv_queued_spin_unlock() is
 * hand-coded for 64-bit, but it isn't worthwhile to do it for 32-bit.
 */
#ifdef CONFIG_64BIT

__PV_CALLEE_SAVE_REGS_THUNK(__pv_queued_spin_unlock_slowpath, ".spinlock.text");
#define __pv_queued_spin_unlock	__pv_queued_spin_unlock

/*
 * Optimized assembly version of __raw_callee_save___pv_queued_spin_unlock
 * which combines the registers saving trunk and the body of the following
 * C code.  Note that it puts the code in the .spinlock.text section which
 * is equivalent to adding __lockfunc in the C code:
 *
 * void __lockfunc __pv_queued_spin_unlock(struct qspinlock *lock)
 * {
 *	u8 lockval = cmpxchg(&lock->locked, _Q_LOCKED_VAL, 0);
 *
 *	if (likely(lockval == _Q_LOCKED_VAL))
 *		return;
 *	pv_queued_spin_unlock_slowpath(lock, lockval);
 * }
 *
 * For x86-64,
 *   rdi = lock              (first argument)
 *   rsi = lockval           (second argument)
 *   rdx = internal variable (set to 0)
 */
#define PV_UNLOCK_ASM							\
	FRAME_BEGIN							\
	"push  %rdx\n\t"						\
	"mov   $0x1,%eax\n\t"						\
	"xor   %edx,%edx\n\t"						\
	LOCK_PREFIX "cmpxchg %dl,(%rdi)\n\t"				\
	"cmp   $0x1,%al\n\t"						\
	"jne   .slowpath\n\t"						\
	"pop   %rdx\n\t"						\
	FRAME_END							\
	ASM_RET								\
	".slowpath:\n\t"						\
	"push   %rsi\n\t"						\
	"movzbl %al,%esi\n\t"						\
	"call __raw_callee_save___pv_queued_spin_unlock_slowpath\n\t"	\
	"pop    %rsi\n\t"						\
	"pop    %rdx\n\t"						\
	FRAME_END

DEFINE_PARAVIRT_ASM(__raw_callee_save___pv_queued_spin_unlock,
		    PV_UNLOCK_ASM, .spinlock.text);

#else /* CONFIG_64BIT */

extern void __lockfunc __pv_queued_spin_unlock(struct qspinlock *lock);
__PV_CALLEE_SAVE_REGS_THUNK(__pv_queued_spin_unlock, ".spinlock.text");

#endif /* CONFIG_64BIT */
#endif
