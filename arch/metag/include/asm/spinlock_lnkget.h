/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SPINLOCK_LNKGET_H
#define __ASM_SPINLOCK_LNKGET_H

/*
 * None of these asm statements clobber memory as LNKSET writes around
 * the cache so the memory it modifies cannot safely be read by any means
 * other than these accessors.
 */

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	int ret;

	asm volatile ("LNKGETD	%0, [%1]\n"
		      "TST	%0, #1\n"
		      "MOV	%0, #1\n"
		      "XORZ      %0, %0, %0\n"
		      : "=&d" (ret)
		      : "da" (&lock->lock)
		      : "cc");
	return ret;
}

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	int tmp;

	asm volatile ("1:     LNKGETD %0,[%1]\n"
		      "       TST     %0, #1\n"
		      "       ADD     %0, %0, #1\n"
		      "       LNKSETDZ [%1], %0\n"
		      "       BNZ     1b\n"
		      "       DEFR    %0, TXSTAT\n"
		      "       ANDT    %0, %0, #HI(0x3f000000)\n"
		      "       CMPT    %0, #HI(0x02000000)\n"
		      "       BNZ     1b\n"
		      : "=&d" (tmp)
		      : "da" (&lock->lock)
		      : "cc");

	smp_mb();
}

/* Returns 0 if failed to acquire lock */
static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	int tmp;

	asm volatile ("       LNKGETD %0,[%1]\n"
		      "       TST     %0, #1\n"
		      "       ADD     %0, %0, #1\n"
		      "       LNKSETDZ [%1], %0\n"
		      "       BNZ     1f\n"
		      "       DEFR    %0, TXSTAT\n"
		      "       ANDT    %0, %0, #HI(0x3f000000)\n"
		      "       CMPT    %0, #HI(0x02000000)\n"
		      "       MOV     %0, #1\n"
		      "1:     XORNZ   %0, %0, %0\n"
		      : "=&d" (tmp)
		      : "da" (&lock->lock)
		      : "cc");

	smp_mb();

	return tmp;
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	smp_mb();

	asm volatile ("       SETD    [%0], %1\n"
		      :
		      : "da" (&lock->lock), "da" (0)
		      : "memory");
}

/*
 * RWLOCKS
 *
 *
 * Write locks are easy - we just set bit 31.  When unlocking, we can
 * just write zero since the lock is exclusively held.
 */

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	int tmp;

	asm volatile ("1:     LNKGETD %0,[%1]\n"
		      "       CMP     %0, #0\n"
		      "       ADD     %0, %0, %2\n"
		      "       LNKSETDZ [%1], %0\n"
		      "       BNZ     1b\n"
		      "       DEFR    %0, TXSTAT\n"
		      "       ANDT    %0, %0, #HI(0x3f000000)\n"
		      "       CMPT    %0, #HI(0x02000000)\n"
		      "       BNZ     1b\n"
		      : "=&d" (tmp)
		      : "da" (&rw->lock), "bd" (0x80000000)
		      : "cc");

	smp_mb();
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	int tmp;

	asm volatile ("       LNKGETD %0,[%1]\n"
		      "       CMP     %0, #0\n"
		      "       ADD     %0, %0, %2\n"
		      "       LNKSETDZ [%1], %0\n"
		      "       BNZ     1f\n"
		      "       DEFR    %0, TXSTAT\n"
		      "       ANDT    %0, %0, #HI(0x3f000000)\n"
		      "       CMPT    %0, #HI(0x02000000)\n"
		      "       MOV     %0,#1\n"
		      "1:     XORNZ   %0, %0, %0\n"
		      : "=&d" (tmp)
		      : "da" (&rw->lock), "bd" (0x80000000)
		      : "cc");

	smp_mb();

	return tmp;
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	smp_mb();

	asm volatile ("       SETD    [%0], %1\n"
		      :
		      : "da" (&rw->lock), "da" (0)
		      : "memory");
}

/*
 * Read locks are a bit more hairy:
 *  - Exclusively load the lock value.
 *  - Increment it.
 *  - Store new lock value if positive, and we still own this location.
 *    If the value is negative, we've already failed.
 *  - If we failed to store the value, we want a negative result.
 *  - If we failed, try again.
 * Unlocking is similarly hairy.  We may have multiple read locks
 * currently active.  However, we know we won't have any write
 * locks.
 */
static inline void arch_read_lock(arch_rwlock_t *rw)
{
	int tmp;

	asm volatile ("1:     LNKGETD %0,[%1]\n"
		      "       ADDS    %0, %0, #1\n"
		      "       LNKSETDPL [%1], %0\n"
		      "       BMI     1b\n"
		      "       DEFR    %0, TXSTAT\n"
		      "       ANDT    %0, %0, #HI(0x3f000000)\n"
		      "       CMPT    %0, #HI(0x02000000)\n"
		      "       BNZ     1b\n"
		      : "=&d" (tmp)
		      : "da" (&rw->lock)
		      : "cc");

	smp_mb();
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	int tmp;

	smp_mb();

	asm volatile ("1:     LNKGETD %0,[%1]\n"
		      "       SUB     %0, %0, #1\n"
		      "       LNKSETD [%1], %0\n"
		      "       DEFR    %0, TXSTAT\n"
		      "       ANDT    %0, %0, #HI(0x3f000000)\n"
		      "       CMPT    %0, #HI(0x02000000)\n"
		      "       BNZ     1b\n"
		      : "=&d" (tmp)
		      : "da" (&rw->lock)
		      : "cc", "memory");
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	int tmp;

	asm volatile ("       LNKGETD %0,[%1]\n"
		      "       ADDS    %0, %0, #1\n"
		      "       LNKSETDPL [%1], %0\n"
		      "       BMI     1f\n"
		      "       DEFR    %0, TXSTAT\n"
		      "       ANDT    %0, %0, #HI(0x3f000000)\n"
		      "       CMPT    %0, #HI(0x02000000)\n"
		      "       MOV     %0,#1\n"
		      "       BZ      2f\n"
		      "1:     MOV     %0,#0\n"
		      "2:\n"
		      : "=&d" (tmp)
		      : "da" (&rw->lock)
		      : "cc");

	smp_mb();

	return tmp;
}

#endif /* __ASM_SPINLOCK_LNKGET_H */
