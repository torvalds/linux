/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000, 06 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_SPINLOCK_H
#define _ASM_SPINLOCK_H

#include <linux/compiler.h>

#include <asm/barrier.h>
#include <asm/compiler.h>
#include <asm/war.h>

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 *
 * Simple spin lock operations.	 There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * These are fair FIFO ticket locks
 *
 * (the type definitions are in asm/spinlock_types.h)
 */


/*
 * Ticket locks are conceptually two parts, one indicating the current head of
 * the queue, and the other indicating the current tail. The lock is acquired
 * by atomically noting the tail and incrementing it by one (thus adding
 * ourself to the queue and noting our position), then waiting until the head
 * becomes equal to the the initial value of the tail.
 */

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	u32 counters = ACCESS_ONCE(lock->lock);

	return ((counters >> 16) ^ counters) & 0xffff;
}

#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)
#define arch_spin_unlock_wait(x) \
	while (arch_spin_is_locked(x)) { cpu_relax(); }

static inline int arch_spin_is_contended(arch_spinlock_t *lock)
{
	u32 counters = ACCESS_ONCE(lock->lock);

	return (((counters >> 16) - counters) & 0xffff) > 1;
}
#define arch_spin_is_contended	arch_spin_is_contended

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	int my_ticket;
	int tmp;
	int inc = 0x10000;

	if (R10000_LLSC_WAR) {
		__asm__ __volatile__ (
		"	.set push		# arch_spin_lock	\n"
		"	.set noreorder					\n"
		"							\n"
		"1:	ll	%[ticket], %[ticket_ptr]		\n"
		"	addu	%[my_ticket], %[ticket], %[inc]		\n"
		"	sc	%[my_ticket], %[ticket_ptr]		\n"
		"	beqzl	%[my_ticket], 1b			\n"
		"	 nop						\n"
		"	srl	%[my_ticket], %[ticket], 16		\n"
		"	andi	%[ticket], %[ticket], 0xffff		\n"
		"	bne	%[ticket], %[my_ticket], 4f		\n"
		"	 subu	%[ticket], %[my_ticket], %[ticket]	\n"
		"2:							\n"
		"	.subsection 2					\n"
		"4:	andi	%[ticket], %[ticket], 0xffff		\n"
		"	sll	%[ticket], 5				\n"
		"							\n"
		"6:	bnez	%[ticket], 6b				\n"
		"	 subu	%[ticket], 1				\n"
		"							\n"
		"	lhu	%[ticket], %[serving_now_ptr]		\n"
		"	beq	%[ticket], %[my_ticket], 2b		\n"
		"	 subu	%[ticket], %[my_ticket], %[ticket]	\n"
		"	b	4b					\n"
		"	 subu	%[ticket], %[ticket], 1			\n"
		"	.previous					\n"
		"	.set pop					\n"
		: [ticket_ptr] "+" GCC_OFF12_ASM() (lock->lock),
		  [serving_now_ptr] "+m" (lock->h.serving_now),
		  [ticket] "=&r" (tmp),
		  [my_ticket] "=&r" (my_ticket)
		: [inc] "r" (inc));
	} else {
		__asm__ __volatile__ (
		"	.set push		# arch_spin_lock	\n"
		"	.set noreorder					\n"
		"							\n"
		"1:	ll	%[ticket], %[ticket_ptr]		\n"
		"	addu	%[my_ticket], %[ticket], %[inc]		\n"
		"	sc	%[my_ticket], %[ticket_ptr]		\n"
		"	beqz	%[my_ticket], 1b			\n"
		"	 srl	%[my_ticket], %[ticket], 16		\n"
		"	andi	%[ticket], %[ticket], 0xffff		\n"
		"	bne	%[ticket], %[my_ticket], 4f		\n"
		"	 subu	%[ticket], %[my_ticket], %[ticket]	\n"
		"2:							\n"
		"	.subsection 2					\n"
		"4:	andi	%[ticket], %[ticket], 0x1fff		\n"
		"	sll	%[ticket], 5				\n"
		"							\n"
		"6:	bnez	%[ticket], 6b				\n"
		"	 subu	%[ticket], 1				\n"
		"							\n"
		"	lhu	%[ticket], %[serving_now_ptr]		\n"
		"	beq	%[ticket], %[my_ticket], 2b		\n"
		"	 subu	%[ticket], %[my_ticket], %[ticket]	\n"
		"	b	4b					\n"
		"	 subu	%[ticket], %[ticket], 1			\n"
		"	.previous					\n"
		"	.set pop					\n"
		: [ticket_ptr] "+" GCC_OFF12_ASM() (lock->lock),
		  [serving_now_ptr] "+m" (lock->h.serving_now),
		  [ticket] "=&r" (tmp),
		  [my_ticket] "=&r" (my_ticket)
		: [inc] "r" (inc));
	}

	smp_llsc_mb();
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	unsigned int serving_now = lock->h.serving_now + 1;
	wmb();
	lock->h.serving_now = (u16)serving_now;
	nudge_writes();
}

static inline unsigned int arch_spin_trylock(arch_spinlock_t *lock)
{
	int tmp, tmp2, tmp3;
	int inc = 0x10000;

	if (R10000_LLSC_WAR) {
		__asm__ __volatile__ (
		"	.set push		# arch_spin_trylock	\n"
		"	.set noreorder					\n"
		"							\n"
		"1:	ll	%[ticket], %[ticket_ptr]		\n"
		"	srl	%[my_ticket], %[ticket], 16		\n"
		"	andi	%[now_serving], %[ticket], 0xffff	\n"
		"	bne	%[my_ticket], %[now_serving], 3f	\n"
		"	 addu	%[ticket], %[ticket], %[inc]		\n"
		"	sc	%[ticket], %[ticket_ptr]		\n"
		"	beqzl	%[ticket], 1b				\n"
		"	 li	%[ticket], 1				\n"
		"2:							\n"
		"	.subsection 2					\n"
		"3:	b	2b					\n"
		"	 li	%[ticket], 0				\n"
		"	.previous					\n"
		"	.set pop					\n"
		: [ticket_ptr] "+" GCC_OFF12_ASM() (lock->lock),
		  [ticket] "=&r" (tmp),
		  [my_ticket] "=&r" (tmp2),
		  [now_serving] "=&r" (tmp3)
		: [inc] "r" (inc));
	} else {
		__asm__ __volatile__ (
		"	.set push		# arch_spin_trylock	\n"
		"	.set noreorder					\n"
		"							\n"
		"1:	ll	%[ticket], %[ticket_ptr]		\n"
		"	srl	%[my_ticket], %[ticket], 16		\n"
		"	andi	%[now_serving], %[ticket], 0xffff	\n"
		"	bne	%[my_ticket], %[now_serving], 3f	\n"
		"	 addu	%[ticket], %[ticket], %[inc]		\n"
		"	sc	%[ticket], %[ticket_ptr]		\n"
		"	beqz	%[ticket], 1b				\n"
		"	 li	%[ticket], 1				\n"
		"2:							\n"
		"	.subsection 2					\n"
		"3:	b	2b					\n"
		"	 li	%[ticket], 0				\n"
		"	.previous					\n"
		"	.set pop					\n"
		: [ticket_ptr] "+" GCC_OFF12_ASM() (lock->lock),
		  [ticket] "=&r" (tmp),
		  [my_ticket] "=&r" (tmp2),
		  [now_serving] "=&r" (tmp3)
		: [inc] "r" (inc));
	}

	smp_llsc_mb();

	return tmp;
}

/*
 * Read-write spinlocks, allowing multiple readers but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts but no interrupt
 * writers. For those circumstances we can "mix" irq-safe locks - any writer
 * needs to get a irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */

/*
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_read_can_lock(rw)	((rw)->lock >= 0)

/*
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_write_can_lock(rw) (!(rw)->lock)

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned int tmp;

	if (R10000_LLSC_WAR) {
		__asm__ __volatile__(
		"	.set	noreorder	# arch_read_lock	\n"
		"1:	ll	%1, %2					\n"
		"	bltz	%1, 1b					\n"
		"	 addu	%1, 1					\n"
		"	sc	%1, %0					\n"
		"	beqzl	%1, 1b					\n"
		"	 nop						\n"
		"	.set	reorder					\n"
		: "=" GCC_OFF12_ASM() (rw->lock), "=&r" (tmp)
		: GCC_OFF12_ASM() (rw->lock)
		: "memory");
	} else {
		do {
			__asm__ __volatile__(
			"1:	ll	%1, %2	# arch_read_lock	\n"
			"	bltz	%1, 1b				\n"
			"	 addu	%1, 1				\n"
			"2:	sc	%1, %0				\n"
			: "=" GCC_OFF12_ASM() (rw->lock), "=&r" (tmp)
			: GCC_OFF12_ASM() (rw->lock)
			: "memory");
		} while (unlikely(!tmp));
	}

	smp_llsc_mb();
}

/* Note the use of sub, not subu which will make the kernel die with an
   overflow exception if we ever try to unlock an rwlock that is already
   unlocked or is being held by a writer.  */
static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned int tmp;

	smp_mb__before_llsc();

	if (R10000_LLSC_WAR) {
		__asm__ __volatile__(
		"1:	ll	%1, %2		# arch_read_unlock	\n"
		"	sub	%1, 1					\n"
		"	sc	%1, %0					\n"
		"	beqzl	%1, 1b					\n"
		: "=" GCC_OFF12_ASM() (rw->lock), "=&r" (tmp)
		: GCC_OFF12_ASM() (rw->lock)
		: "memory");
	} else {
		do {
			__asm__ __volatile__(
			"1:	ll	%1, %2	# arch_read_unlock	\n"
			"	sub	%1, 1				\n"
			"	sc	%1, %0				\n"
			: "=" GCC_OFF12_ASM() (rw->lock), "=&r" (tmp)
			: GCC_OFF12_ASM() (rw->lock)
			: "memory");
		} while (unlikely(!tmp));
	}
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned int tmp;

	if (R10000_LLSC_WAR) {
		__asm__ __volatile__(
		"	.set	noreorder	# arch_write_lock	\n"
		"1:	ll	%1, %2					\n"
		"	bnez	%1, 1b					\n"
		"	 lui	%1, 0x8000				\n"
		"	sc	%1, %0					\n"
		"	beqzl	%1, 1b					\n"
		"	 nop						\n"
		"	.set	reorder					\n"
		: "=" GCC_OFF12_ASM() (rw->lock), "=&r" (tmp)
		: GCC_OFF12_ASM() (rw->lock)
		: "memory");
	} else {
		do {
			__asm__ __volatile__(
			"1:	ll	%1, %2	# arch_write_lock	\n"
			"	bnez	%1, 1b				\n"
			"	 lui	%1, 0x8000			\n"
			"2:	sc	%1, %0				\n"
			: "=" GCC_OFF12_ASM() (rw->lock), "=&r" (tmp)
			: GCC_OFF12_ASM() (rw->lock)
			: "memory");
		} while (unlikely(!tmp));
	}

	smp_llsc_mb();
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	smp_mb();

	__asm__ __volatile__(
	"				# arch_write_unlock	\n"
	"	sw	$0, %0					\n"
	: "=m" (rw->lock)
	: "m" (rw->lock)
	: "memory");
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned int tmp;
	int ret;

	if (R10000_LLSC_WAR) {
		__asm__ __volatile__(
		"	.set	noreorder	# arch_read_trylock	\n"
		"	li	%2, 0					\n"
		"1:	ll	%1, %3					\n"
		"	bltz	%1, 2f					\n"
		"	 addu	%1, 1					\n"
		"	sc	%1, %0					\n"
		"	.set	reorder					\n"
		"	beqzl	%1, 1b					\n"
		"	 nop						\n"
		__WEAK_LLSC_MB
		"	li	%2, 1					\n"
		"2:							\n"
		: "=" GCC_OFF12_ASM() (rw->lock), "=&r" (tmp), "=&r" (ret)
		: GCC_OFF12_ASM() (rw->lock)
		: "memory");
	} else {
		__asm__ __volatile__(
		"	.set	noreorder	# arch_read_trylock	\n"
		"	li	%2, 0					\n"
		"1:	ll	%1, %3					\n"
		"	bltz	%1, 2f					\n"
		"	 addu	%1, 1					\n"
		"	sc	%1, %0					\n"
		"	beqz	%1, 1b					\n"
		"	 nop						\n"
		"	.set	reorder					\n"
		__WEAK_LLSC_MB
		"	li	%2, 1					\n"
		"2:							\n"
		: "=" GCC_OFF12_ASM() (rw->lock), "=&r" (tmp), "=&r" (ret)
		: GCC_OFF12_ASM() (rw->lock)
		: "memory");
	}

	return ret;
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned int tmp;
	int ret;

	if (R10000_LLSC_WAR) {
		__asm__ __volatile__(
		"	.set	noreorder	# arch_write_trylock	\n"
		"	li	%2, 0					\n"
		"1:	ll	%1, %3					\n"
		"	bnez	%1, 2f					\n"
		"	 lui	%1, 0x8000				\n"
		"	sc	%1, %0					\n"
		"	beqzl	%1, 1b					\n"
		"	 nop						\n"
		__WEAK_LLSC_MB
		"	li	%2, 1					\n"
		"	.set	reorder					\n"
		"2:							\n"
		: "=" GCC_OFF12_ASM() (rw->lock), "=&r" (tmp), "=&r" (ret)
		: GCC_OFF12_ASM() (rw->lock)
		: "memory");
	} else {
		do {
			__asm__ __volatile__(
			"	ll	%1, %3	# arch_write_trylock	\n"
			"	li	%2, 0				\n"
			"	bnez	%1, 2f				\n"
			"	lui	%1, 0x8000			\n"
			"	sc	%1, %0				\n"
			"	li	%2, 1				\n"
			"2:						\n"
			: "=" GCC_OFF12_ASM() (rw->lock), "=&r" (tmp),
			  "=&r" (ret)
			: GCC_OFF12_ASM() (rw->lock)
			: "memory");
		} while (unlikely(!tmp));

		smp_llsc_mb();
	}

	return ret;
}

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* _ASM_SPINLOCK_H */
