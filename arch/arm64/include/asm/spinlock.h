/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/lse.h>
#include <asm/spinlock_types.h>
#include <asm/processor.h>

/*
 * Spinlock implementation.
 *
 * The memory barriers are implicit with the load-acquire and store-release
 * instructions.
 */
static inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	unsigned int tmp;
	arch_spinlock_t lockval;
	u32 owner;

	/*
	 * Ensure prior spin_lock operations to other locks have completed
	 * on this CPU before we test whether "lock" is locked.
	 */
	smp_mb();
	owner = READ_ONCE(lock->owner) << 16;

	asm volatile(
"	sevl\n"
"1:	wfe\n"
"2:	ldaxr	%w0, %2\n"
	/* Is the lock free? */
"	eor	%w1, %w0, %w0, ror #16\n"
"	cbz	%w1, 3f\n"
	/* Lock taken -- has there been a subsequent unlock->lock transition? */
"	eor	%w1, %w3, %w0, lsl #16\n"
"	cbz	%w1, 1b\n"
	/*
	 * The owner has been updated, so there was an unlock->lock
	 * transition that we missed. That means we can rely on the
	 * store-release of the unlock operation paired with the
	 * load-acquire of the lock operation to publish any of our
	 * previous stores to the new lock owner and therefore don't
	 * need to bother with the writeback below.
	 */
"	b	4f\n"
"3:\n"
	/*
	 * Serialise against any concurrent lockers by writing back the
	 * unlocked lock value
	 */
	ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
"	stxr	%w1, %w0, %2\n"
"	nop\n"
"	nop\n",
	/* LSE atomics */
"	mov	%w1, %w0\n"
"	cas	%w0, %w0, %2\n"
"	eor	%w1, %w1, %w0\n")
	/* Somebody else wrote to the lock, GOTO 10 and reload the value */
"	cbnz	%w1, 2b\n"
"4:"
	: "=&r" (lockval), "=&r" (tmp), "+Q" (*lock)
	: "r" (owner)
	: "memory");
}

#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned int tmp;
	arch_spinlock_t lockval, newval;

	asm volatile(
	/* Atomically increment the next ticket. */
	ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
"	prfm	pstl1strm, %3\n"
"1:	ldaxr	%w0, %3\n"
"	add	%w1, %w0, %w5\n"
"	stxr	%w2, %w1, %3\n"
"	cbnz	%w2, 1b\n",
	/* LSE atomics */
"	mov	%w2, %w5\n"
"	ldadda	%w2, %w0, %3\n"
"	nop\n"
"	nop\n"
"	nop\n"
	)

	/* Did we get the lock? */
"	eor	%w1, %w0, %w0, ror #16\n"
"	cbz	%w1, 3f\n"
	/*
	 * No: spin on the owner. Send a local event to avoid missing an
	 * unlock before the exclusive load.
	 */
"	sevl\n"
"2:	wfe\n"
"	ldaxrh	%w2, %4\n"
"	eor	%w1, %w2, %w0, lsr #16\n"
"	cbnz	%w1, 2b\n"
	/* We got the lock. Critical section starts here. */
"3:"
	: "=&r" (lockval), "=&r" (newval), "=&r" (tmp), "+Q" (*lock)
	: "Q" (lock->owner), "I" (1 << TICKET_SHIFT)
	: "memory");
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned int tmp;
	arch_spinlock_t lockval;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"	prfm	pstl1strm, %2\n"
	"1:	ldaxr	%w0, %2\n"
	"	eor	%w1, %w0, %w0, ror #16\n"
	"	cbnz	%w1, 2f\n"
	"	add	%w0, %w0, %3\n"
	"	stxr	%w1, %w0, %2\n"
	"	cbnz	%w1, 1b\n"
	"2:",
	/* LSE atomics */
	"	ldr	%w0, %2\n"
	"	eor	%w1, %w0, %w0, ror #16\n"
	"	cbnz	%w1, 1f\n"
	"	add	%w1, %w0, %3\n"
	"	casa	%w0, %w1, %2\n"
	"	and	%w1, %w1, #0xffff\n"
	"	eor	%w1, %w1, %w0, lsr #16\n"
	"1:")
	: "=&r" (lockval), "=&r" (tmp), "+Q" (*lock)
	: "I" (1 << TICKET_SHIFT)
	: "memory");

	return !tmp;
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	unsigned long tmp;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"	ldrh	%w1, %0\n"
	"	add	%w1, %w1, #1\n"
	"	stlrh	%w1, %0",
	/* LSE atomics */
	"	mov	%w1, #1\n"
	"	nop\n"
	"	staddlh	%w1, %0")
	: "=Q" (lock->owner), "=&r" (tmp)
	:
	: "memory");
}

static inline int arch_spin_value_unlocked(arch_spinlock_t lock)
{
	return lock.owner == lock.next;
}

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	smp_mb(); /* See arch_spin_unlock_wait */
	return !arch_spin_value_unlocked(READ_ONCE(*lock));
}

static inline int arch_spin_is_contended(arch_spinlock_t *lock)
{
	arch_spinlock_t lockval = READ_ONCE(*lock);
	return (lockval.next - lockval.owner) > 1;
}
#define arch_spin_is_contended	arch_spin_is_contended

/*
 * Write lock implementation.
 *
 * Write locks set bit 31. Unlocking, is done by writing 0 since the lock is
 * exclusively held.
 *
 * The memory barriers are implicit with the load-acquire and store-release
 * instructions.
 */

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned int tmp;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"	sevl\n"
	"1:	wfe\n"
	"2:	ldaxr	%w0, %1\n"
	"	cbnz	%w0, 1b\n"
	"	stxr	%w0, %w2, %1\n"
	"	cbnz	%w0, 2b\n"
	"	nop",
	/* LSE atomics */
	"1:	mov	%w0, wzr\n"
	"2:	casa	%w0, %w2, %1\n"
	"	cbz	%w0, 3f\n"
	"	ldxr	%w0, %1\n"
	"	cbz	%w0, 2b\n"
	"	wfe\n"
	"	b	1b\n"
	"3:")
	: "=&r" (tmp), "+Q" (rw->lock)
	: "r" (0x80000000)
	: "memory");
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned int tmp;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"1:	ldaxr	%w0, %1\n"
	"	cbnz	%w0, 2f\n"
	"	stxr	%w0, %w2, %1\n"
	"	cbnz	%w0, 1b\n"
	"2:",
	/* LSE atomics */
	"	mov	%w0, wzr\n"
	"	casa	%w0, %w2, %1\n"
	"	nop\n"
	"	nop")
	: "=&r" (tmp), "+Q" (rw->lock)
	: "r" (0x80000000)
	: "memory");

	return !tmp;
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	asm volatile(ARM64_LSE_ATOMIC_INSN(
	"	stlr	wzr, %0",
	"	swpl	wzr, wzr, %0")
	: "=Q" (rw->lock) :: "memory");
}

/* write_can_lock - would write_trylock() succeed? */
#define arch_write_can_lock(x)		((x)->lock == 0)

/*
 * Read lock implementation.
 *
 * It exclusively loads the lock value, increments it and stores the new value
 * back if positive and the CPU still exclusively owns the location. If the
 * value is negative, the lock is already held.
 *
 * During unlocking there may be multiple active read locks but no write lock.
 *
 * The memory barriers are implicit with the load-acquire and store-release
 * instructions.
 *
 * Note that in UNDEFINED cases, such as unlocking a lock twice, the LL/SC
 * and LSE implementations may exhibit different behaviour (although this
 * will have no effect on lockdep).
 */
static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned int tmp, tmp2;

	asm volatile(
	"	sevl\n"
	ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"1:	wfe\n"
	"2:	ldaxr	%w0, %2\n"
	"	add	%w0, %w0, #1\n"
	"	tbnz	%w0, #31, 1b\n"
	"	stxr	%w1, %w0, %2\n"
	"	nop\n"
	"	cbnz	%w1, 2b",
	/* LSE atomics */
	"1:	wfe\n"
	"2:	ldxr	%w0, %2\n"
	"	adds	%w1, %w0, #1\n"
	"	tbnz	%w1, #31, 1b\n"
	"	casa	%w0, %w1, %2\n"
	"	sbc	%w0, %w1, %w0\n"
	"	cbnz	%w0, 2b")
	: "=&r" (tmp), "=&r" (tmp2), "+Q" (rw->lock)
	:
	: "cc", "memory");
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned int tmp, tmp2;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"1:	ldxr	%w0, %2\n"
	"	sub	%w0, %w0, #1\n"
	"	stlxr	%w1, %w0, %2\n"
	"	cbnz	%w1, 1b",
	/* LSE atomics */
	"	movn	%w0, #0\n"
	"	nop\n"
	"	nop\n"
	"	staddl	%w0, %2")
	: "=&r" (tmp), "=&r" (tmp2), "+Q" (rw->lock)
	:
	: "memory");
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned int tmp, tmp2;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"	mov	%w1, #1\n"
	"1:	ldaxr	%w0, %2\n"
	"	add	%w0, %w0, #1\n"
	"	tbnz	%w0, #31, 2f\n"
	"	stxr	%w1, %w0, %2\n"
	"	cbnz	%w1, 1b\n"
	"2:",
	/* LSE atomics */
	"	ldr	%w0, %2\n"
	"	adds	%w1, %w0, #1\n"
	"	tbnz	%w1, #31, 1f\n"
	"	casa	%w0, %w1, %2\n"
	"	sbc	%w1, %w1, %w0\n"
	"	nop\n"
	"1:")
	: "=&r" (tmp), "=&r" (tmp2), "+Q" (rw->lock)
	:
	: "cc", "memory");

	return !tmp2;
}

/* read_can_lock - would read_trylock() succeed? */
#define arch_read_can_lock(x)		((x)->lock < 0x80000000)

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

/*
 * Accesses appearing in program order before a spin_lock() operation
 * can be reordered with accesses inside the critical section, by virtue
 * of arch_spin_lock being constructed using acquire semantics.
 *
 * In cases where this is problematic (e.g. try_to_wake_up), an
 * smp_mb__before_spinlock() can restore the required ordering.
 */
#define smp_mb__before_spinlock()	smp_mb()

#endif /* __ASM_SPINLOCK_H */
