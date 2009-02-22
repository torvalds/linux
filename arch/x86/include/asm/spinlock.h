#ifndef _ASM_X86_SPINLOCK_H
#define _ASM_X86_SPINLOCK_H

#include <asm/atomic.h>
#include <asm/rwlock.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <linux/compiler.h>
#include <asm/paravirt.h>
/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 *
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * These are fair FIFO ticket locks, which are currently limited to 256
 * CPUs.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */

#ifdef CONFIG_X86_32
# define LOCK_PTR_REG "a"
# define REG_PTR_MODE "k"
#else
# define LOCK_PTR_REG "D"
# define REG_PTR_MODE "q"
#endif

#if defined(CONFIG_X86_32) && \
	(defined(CONFIG_X86_OOSTORE) || defined(CONFIG_X86_PPRO_FENCE))
/*
 * On PPro SMP or if we are using OOSTORE, we use a locked operation to unlock
 * (PPro errata 66, 92)
 */
# define UNLOCK_LOCK_PREFIX LOCK_PREFIX
#else
# define UNLOCK_LOCK_PREFIX
#endif

/*
 * Ticket locks are conceptually two parts, one indicating the current head of
 * the queue, and the other indicating the current tail. The lock is acquired
 * by atomically noting the tail and incrementing it by one (thus adding
 * ourself to the queue and noting our position), then waiting until the head
 * becomes equal to the the initial value of the tail.
 *
 * We use an xadd covering *both* parts of the lock, to increment the tail and
 * also load the position of the head, which takes care of memory ordering
 * issues and should be optimal for the uncontended case. Note the tail must be
 * in the high part, because a wide xadd increment of the low part would carry
 * up and contaminate the high part.
 *
 * With fewer than 2^8 possible CPUs, we can use x86's partial registers to
 * save some instructions and make the code more elegant. There really isn't
 * much between them in performance though, especially as locks are out of line.
 */
#if (NR_CPUS < 256)
#define TICKET_SHIFT 8

static __always_inline void __ticket_spin_lock(raw_spinlock_t *lock)
{
	short inc = 0x0100;

	asm volatile (
		LOCK_PREFIX "xaddw %w0, %1\n"
		"1:\t"
		"cmpb %h0, %b0\n\t"
		"je 2f\n\t"
		"rep ; nop\n\t"
		"movb %1, %b0\n\t"
		/* don't need lfence here, because loads are in-order */
		"jmp 1b\n"
		"2:"
		: "+Q" (inc), "+m" (lock->slock)
		:
		: "memory", "cc");
}

static __always_inline int __ticket_spin_trylock(raw_spinlock_t *lock)
{
	int tmp, new;

	asm volatile("movzwl %2, %0\n\t"
		     "cmpb %h0,%b0\n\t"
		     "leal 0x100(%" REG_PTR_MODE "0), %1\n\t"
		     "jne 1f\n\t"
		     LOCK_PREFIX "cmpxchgw %w1,%2\n\t"
		     "1:"
		     "sete %b1\n\t"
		     "movzbl %b1,%0\n\t"
		     : "=&a" (tmp), "=&q" (new), "+m" (lock->slock)
		     :
		     : "memory", "cc");

	return tmp;
}

static __always_inline void __ticket_spin_unlock(raw_spinlock_t *lock)
{
	asm volatile(UNLOCK_LOCK_PREFIX "incb %0"
		     : "+m" (lock->slock)
		     :
		     : "memory", "cc");
}
#else
#define TICKET_SHIFT 16

static __always_inline void __ticket_spin_lock(raw_spinlock_t *lock)
{
	int inc = 0x00010000;
	int tmp;

	asm volatile(LOCK_PREFIX "xaddl %0, %1\n"
		     "movzwl %w0, %2\n\t"
		     "shrl $16, %0\n\t"
		     "1:\t"
		     "cmpl %0, %2\n\t"
		     "je 2f\n\t"
		     "rep ; nop\n\t"
		     "movzwl %1, %2\n\t"
		     /* don't need lfence here, because loads are in-order */
		     "jmp 1b\n"
		     "2:"
		     : "+r" (inc), "+m" (lock->slock), "=&r" (tmp)
		     :
		     : "memory", "cc");
}

static __always_inline int __ticket_spin_trylock(raw_spinlock_t *lock)
{
	int tmp;
	int new;

	asm volatile("movl %2,%0\n\t"
		     "movl %0,%1\n\t"
		     "roll $16, %0\n\t"
		     "cmpl %0,%1\n\t"
		     "leal 0x00010000(%" REG_PTR_MODE "0), %1\n\t"
		     "jne 1f\n\t"
		     LOCK_PREFIX "cmpxchgl %1,%2\n\t"
		     "1:"
		     "sete %b1\n\t"
		     "movzbl %b1,%0\n\t"
		     : "=&a" (tmp), "=&q" (new), "+m" (lock->slock)
		     :
		     : "memory", "cc");

	return tmp;
}

static __always_inline void __ticket_spin_unlock(raw_spinlock_t *lock)
{
	asm volatile(UNLOCK_LOCK_PREFIX "incw %0"
		     : "+m" (lock->slock)
		     :
		     : "memory", "cc");
}
#endif

static inline int __ticket_spin_is_locked(raw_spinlock_t *lock)
{
	int tmp = ACCESS_ONCE(lock->slock);

	return !!(((tmp >> TICKET_SHIFT) ^ tmp) & ((1 << TICKET_SHIFT) - 1));
}

static inline int __ticket_spin_is_contended(raw_spinlock_t *lock)
{
	int tmp = ACCESS_ONCE(lock->slock);

	return (((tmp >> TICKET_SHIFT) - tmp) & ((1 << TICKET_SHIFT) - 1)) > 1;
}

#ifdef CONFIG_PARAVIRT
/*
 * Define virtualization-friendly old-style lock byte lock, for use in
 * pv_lock_ops if desired.
 *
 * This differs from the pre-2.6.24 spinlock by always using xchgb
 * rather than decb to take the lock; this allows it to use a
 * zero-initialized lock structure.  It also maintains a 1-byte
 * contention counter, so that we can implement
 * __byte_spin_is_contended.
 */
struct __byte_spinlock {
	s8 lock;
	s8 spinners;
};

static inline int __byte_spin_is_locked(raw_spinlock_t *lock)
{
	struct __byte_spinlock *bl = (struct __byte_spinlock *)lock;
	return bl->lock != 0;
}

static inline int __byte_spin_is_contended(raw_spinlock_t *lock)
{
	struct __byte_spinlock *bl = (struct __byte_spinlock *)lock;
	return bl->spinners != 0;
}

static inline void __byte_spin_lock(raw_spinlock_t *lock)
{
	struct __byte_spinlock *bl = (struct __byte_spinlock *)lock;
	s8 val = 1;

	asm("1: xchgb %1, %0\n"
	    "   test %1,%1\n"
	    "   jz 3f\n"
	    "   " LOCK_PREFIX "incb %2\n"
	    "2: rep;nop\n"
	    "   cmpb $1, %0\n"
	    "   je 2b\n"
	    "   " LOCK_PREFIX "decb %2\n"
	    "   jmp 1b\n"
	    "3:"
	    : "+m" (bl->lock), "+q" (val), "+m" (bl->spinners): : "memory");
}

static inline int __byte_spin_trylock(raw_spinlock_t *lock)
{
	struct __byte_spinlock *bl = (struct __byte_spinlock *)lock;
	u8 old = 1;

	asm("xchgb %1,%0"
	    : "+m" (bl->lock), "+q" (old) : : "memory");

	return old == 0;
}

static inline void __byte_spin_unlock(raw_spinlock_t *lock)
{
	struct __byte_spinlock *bl = (struct __byte_spinlock *)lock;
	smp_wmb();
	bl->lock = 0;
}
#else  /* !CONFIG_PARAVIRT */
static inline int __raw_spin_is_locked(raw_spinlock_t *lock)
{
	return __ticket_spin_is_locked(lock);
}

static inline int __raw_spin_is_contended(raw_spinlock_t *lock)
{
	return __ticket_spin_is_contended(lock);
}
#define __raw_spin_is_contended	__raw_spin_is_contended

static __always_inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	__ticket_spin_lock(lock);
}

static __always_inline int __raw_spin_trylock(raw_spinlock_t *lock)
{
	return __ticket_spin_trylock(lock);
}

static __always_inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	__ticket_spin_unlock(lock);
}

static __always_inline void __raw_spin_lock_flags(raw_spinlock_t *lock,
						  unsigned long flags)
{
	__raw_spin_lock(lock);
}

#endif	/* CONFIG_PARAVIRT */

static inline void __raw_spin_unlock_wait(raw_spinlock_t *lock)
{
	while (__raw_spin_is_locked(lock))
		cpu_relax();
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
 */

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
static inline int __raw_read_can_lock(raw_rwlock_t *lock)
{
	return (int)(lock)->lock > 0;
}

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
static inline int __raw_write_can_lock(raw_rwlock_t *lock)
{
	return (lock)->lock == RW_LOCK_BIAS;
}

static inline void __raw_read_lock(raw_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX " subl $1,(%0)\n\t"
		     "jns 1f\n"
		     "call __read_lock_failed\n\t"
		     "1:\n"
		     ::LOCK_PTR_REG (rw) : "memory");
}

static inline void __raw_write_lock(raw_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX " subl %1,(%0)\n\t"
		     "jz 1f\n"
		     "call __write_lock_failed\n\t"
		     "1:\n"
		     ::LOCK_PTR_REG (rw), "i" (RW_LOCK_BIAS) : "memory");
}

static inline int __raw_read_trylock(raw_rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;

	atomic_dec(count);
	if (atomic_read(count) >= 0)
		return 1;
	atomic_inc(count);
	return 0;
}

static inline int __raw_write_trylock(raw_rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;

	if (atomic_sub_and_test(RW_LOCK_BIAS, count))
		return 1;
	atomic_add(RW_LOCK_BIAS, count);
	return 0;
}

static inline void __raw_read_unlock(raw_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX "incl %0" :"+m" (rw->lock) : : "memory");
}

static inline void __raw_write_unlock(raw_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX "addl %1, %0"
		     : "+m" (rw->lock) : "i" (RW_LOCK_BIAS) : "memory");
}

#define _raw_spin_relax(lock)	cpu_relax()
#define _raw_read_relax(lock)	cpu_relax()
#define _raw_write_relax(lock)	cpu_relax()

#endif /* _ASM_X86_SPINLOCK_H */
