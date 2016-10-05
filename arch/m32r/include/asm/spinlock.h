#ifndef _ASM_M32R_SPINLOCK_H
#define _ASM_M32R_SPINLOCK_H

/*
 *  linux/include/asm-m32r/spinlock.h
 *
 *  M32R version:
 *    Copyright (C) 2001, 2002  Hitoshi Yamamoto
 *    Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#include <linux/compiler.h>
#include <linux/atomic.h>
#include <asm/dcache_clear.h>
#include <asm/page.h>
#include <asm/barrier.h>
#include <asm/processor.h>

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 *
 * (the type definitions are in asm/spinlock_types.h)
 *
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

#define arch_spin_is_locked(x)		(*(volatile int *)(&(x)->slock) <= 0)
#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

static inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	smp_cond_load_acquire(&lock->slock, VAL > 0);
}

/**
 * arch_spin_trylock - Try spin lock and return a result
 * @lock: Pointer to the lock variable
 *
 * arch_spin_trylock() tries to get the lock and returns a result.
 * On the m32r, the result value is 1 (= Success) or 0 (= Failure).
 */
static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	int oldval;
	unsigned long tmp1, tmp2;

	/*
	 * lock->slock :  =1 : unlock
	 *             : <=0 : lock
	 * {
	 *   oldval = lock->slock; <--+ need atomic operation
	 *   lock->slock = 0;      <--+
	 * }
	 */
	__asm__ __volatile__ (
		"# arch_spin_trylock		\n\t"
		"ldi	%1, #0;			\n\t"
		"mvfc	%2, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%3")
		"lock	%0, @%3;		\n\t"
		"unlock	%1, @%3;		\n\t"
		"mvtc	%2, psw;		\n\t"
		: "=&r" (oldval), "=&r" (tmp1), "=&r" (tmp2)
		: "r" (&lock->slock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);

	return (oldval > 0);
}

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned long tmp0, tmp1;

	/*
	 * lock->slock :  =1 : unlock
	 *             : <=0 : lock
	 *
	 * for ( ; ; ) {
	 *   lock->slock -= 1;  <-- need atomic operation
	 *   if (lock->slock == 0) break;
	 *   for ( ; lock->slock <= 0 ; );
	 * }
	 */
	__asm__ __volatile__ (
		"# arch_spin_lock		\n\t"
		".fillinsn			\n"
		"1:				\n\t"
		"mvfc	%1, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%2")
		"lock	%0, @%2;		\n\t"
		"addi	%0, #-1;		\n\t"
		"unlock	%0, @%2;		\n\t"
		"mvtc	%1, psw;		\n\t"
		"bltz	%0, 2f;			\n\t"
		LOCK_SECTION_START(".balign 4 \n\t")
		".fillinsn			\n"
		"2:				\n\t"
		"ld	%0, @%2;		\n\t"
		"bgtz	%0, 1b;			\n\t"
		"bra	2b;			\n\t"
		LOCK_SECTION_END
		: "=&r" (tmp0), "=&r" (tmp1)
		: "r" (&lock->slock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	mb();
	lock->slock = 1;
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
 * On x86, we implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "contended" bit.
 *
 * The inline assembly is non-obvious. Think about it.
 *
 * Changed to use the same technique as rw semaphores.  See
 * semaphore.h for details.  -ben
 */

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_read_can_lock(x) ((int)(x)->lock > 0)

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_write_can_lock(x) ((x)->lock == RW_LOCK_BIAS)

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned long tmp0, tmp1;

	/*
	 * rw->lock :  >0 : unlock
	 *          : <=0 : lock
	 *
	 * for ( ; ; ) {
	 *   rw->lock -= 1;  <-- need atomic operation
	 *   if (rw->lock >= 0) break;
	 *   rw->lock += 1;  <-- need atomic operation
	 *   for ( ; rw->lock <= 0 ; );
	 * }
	 */
	__asm__ __volatile__ (
		"# read_lock			\n\t"
		".fillinsn			\n"
		"1:				\n\t"
		"mvfc	%1, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%2")
		"lock	%0, @%2;		\n\t"
		"addi	%0, #-1;		\n\t"
		"unlock	%0, @%2;		\n\t"
		"mvtc	%1, psw;		\n\t"
		"bltz	%0, 2f;			\n\t"
		LOCK_SECTION_START(".balign 4 \n\t")
		".fillinsn			\n"
		"2:				\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%2")
		"lock	%0, @%2;		\n\t"
		"addi	%0, #1;			\n\t"
		"unlock	%0, @%2;		\n\t"
		"mvtc	%1, psw;		\n\t"
		".fillinsn			\n"
		"3:				\n\t"
		"ld	%0, @%2;		\n\t"
		"bgtz	%0, 1b;			\n\t"
		"bra	3b;			\n\t"
		LOCK_SECTION_END
		: "=&r" (tmp0), "=&r" (tmp1)
		: "r" (&rw->lock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned long tmp0, tmp1, tmp2;

	/*
	 * rw->lock :  =RW_LOCK_BIAS_STR : unlock
	 *          : !=RW_LOCK_BIAS_STR : lock
	 *
	 * for ( ; ; ) {
	 *   rw->lock -= RW_LOCK_BIAS_STR;  <-- need atomic operation
	 *   if (rw->lock == 0) break;
	 *   rw->lock += RW_LOCK_BIAS_STR;  <-- need atomic operation
	 *   for ( ; rw->lock != RW_LOCK_BIAS_STR ; ) ;
	 * }
	 */
	__asm__ __volatile__ (
		"# write_lock					\n\t"
		"seth	%1, #high(" RW_LOCK_BIAS_STR ");	\n\t"
		"or3	%1, %1, #low(" RW_LOCK_BIAS_STR ");	\n\t"
		".fillinsn					\n"
		"1:						\n\t"
		"mvfc	%2, psw;				\n\t"
		"clrpsw	#0x40 -> nop;				\n\t"
		DCACHE_CLEAR("%0", "r7", "%3")
		"lock	%0, @%3;				\n\t"
		"sub	%0, %1;					\n\t"
		"unlock	%0, @%3;				\n\t"
		"mvtc	%2, psw;				\n\t"
		"bnez	%0, 2f;					\n\t"
		LOCK_SECTION_START(".balign 4 \n\t")
		".fillinsn					\n"
		"2:						\n\t"
		"clrpsw	#0x40 -> nop;				\n\t"
		DCACHE_CLEAR("%0", "r7", "%3")
		"lock	%0, @%3;				\n\t"
		"add	%0, %1;					\n\t"
		"unlock	%0, @%3;				\n\t"
		"mvtc	%2, psw;				\n\t"
		".fillinsn					\n"
		"3:						\n\t"
		"ld	%0, @%3;				\n\t"
		"beq	%0, %1, 1b;				\n\t"
		"bra	3b;					\n\t"
		LOCK_SECTION_END
		: "=&r" (tmp0), "=&r" (tmp1), "=&r" (tmp2)
		: "r" (&rw->lock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r7"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned long tmp0, tmp1;

	__asm__ __volatile__ (
		"# read_unlock			\n\t"
		"mvfc	%1, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%2")
		"lock	%0, @%2;		\n\t"
		"addi	%0, #1;			\n\t"
		"unlock	%0, @%2;		\n\t"
		"mvtc	%1, psw;		\n\t"
		: "=&r" (tmp0), "=&r" (tmp1)
		: "r" (&rw->lock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	unsigned long tmp0, tmp1, tmp2;

	__asm__ __volatile__ (
		"# write_unlock					\n\t"
		"seth	%1, #high(" RW_LOCK_BIAS_STR ");	\n\t"
		"or3	%1, %1, #low(" RW_LOCK_BIAS_STR ");	\n\t"
		"mvfc	%2, psw;				\n\t"
		"clrpsw	#0x40 -> nop;				\n\t"
		DCACHE_CLEAR("%0", "r7", "%3")
		"lock	%0, @%3;				\n\t"
		"add	%0, %1;					\n\t"
		"unlock	%0, @%3;				\n\t"
		"mvtc	%2, psw;				\n\t"
		: "=&r" (tmp0), "=&r" (tmp1), "=&r" (tmp2)
		: "r" (&rw->lock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r7"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static inline int arch_read_trylock(arch_rwlock_t *lock)
{
	atomic_t *count = (atomic_t*)lock;
	if (atomic_dec_return(count) >= 0)
		return 1;
	atomic_inc(count);
	return 0;
}

static inline int arch_write_trylock(arch_rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;
	if (atomic_sub_and_test(RW_LOCK_BIAS, count))
		return 1;
	atomic_add(RW_LOCK_BIAS, count);
	return 0;
}

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif	/* _ASM_M32R_SPINLOCK_H */
