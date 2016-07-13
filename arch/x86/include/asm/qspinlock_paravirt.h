#ifndef __ASM_QSPINLOCK_PARAVIRT_H
#define __ASM_QSPINLOCK_PARAVIRT_H

/*
 * For x86-64, PV_CALLEE_SAVE_REGS_THUNK() saves and restores 8 64-bit
 * registers. For i386, however, only 1 32-bit register needs to be saved
 * and restored. So an optimized version of __pv_queued_spin_unlock() is
 * hand-coded for 64-bit, but it isn't worthwhile to do it for 32-bit.
 */
#ifdef CONFIG_64BIT

PV_CALLEE_SAVE_REGS_THUNK(__pv_queued_spin_unlock_slowpath);
#define __pv_queued_spin_unlock	__pv_queued_spin_unlock
#define PV_UNLOCK		"__raw_callee_save___pv_queued_spin_unlock"
#define PV_UNLOCK_SLOWPATH	"__raw_callee_save___pv_queued_spin_unlock_slowpath"

/*
 * Optimized assembly version of __raw_callee_save___pv_queued_spin_unlock
 * which combines the registers saving trunk and the body of the following
 * C code:
 *
 * void __pv_queued_spin_unlock(struct qspinlock *lock)
 * {
 *	struct __qspinlock *l = (void *)lock;
 *	u8 lockval = cmpxchg(&l->locked, _Q_LOCKED_VAL, 0);
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
asm    (".pushsection .text;"
	".globl " PV_UNLOCK ";"
	".type " PV_UNLOCK ", @function;"
	".align 4,0x90;"
	PV_UNLOCK ": "
	FRAME_BEGIN
	"push  %rdx;"
	"mov   $0x1,%eax;"
	"xor   %edx,%edx;"
	"lock cmpxchg %dl,(%rdi);"
	"cmp   $0x1,%al;"
	"jne   .slowpath;"
	"pop   %rdx;"
	FRAME_END
	"ret;"
	".slowpath: "
	"push   %rsi;"
	"movzbl %al,%esi;"
	"call " PV_UNLOCK_SLOWPATH ";"
	"pop    %rsi;"
	"pop    %rdx;"
	FRAME_END
	"ret;"
	".size " PV_UNLOCK ", .-" PV_UNLOCK ";"
	".popsection");

#else /* CONFIG_64BIT */

extern void __pv_queued_spin_unlock(struct qspinlock *lock);
PV_CALLEE_SAVE_REGS_THUNK(__pv_queued_spin_unlock);

#endif /* CONFIG_64BIT */
#endif
