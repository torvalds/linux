#ifndef _ASM_X86_SPINLOCK_H
#define _ASM_X86_SPINLOCK_H

#include <linux/jump_label.h>
#include <linux/atomic.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <linux/compiler.h>
#include <asm/paravirt.h>
#include <asm/bitops.h>

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 *
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * These are fair FIFO ticket locks, which support up to 2^16 CPUs.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */

#ifdef CONFIG_X86_32
# define LOCK_PTR_REG "a"
#else
# define LOCK_PTR_REG "D"
#endif

#if defined(CONFIG_X86_32) && (defined(CONFIG_X86_PPRO_FENCE))
/*
 * On PPro SMP, we use a locked operation to unlock
 * (PPro errata 66, 92)
 */
# define UNLOCK_LOCK_PREFIX LOCK_PREFIX
#else
# define UNLOCK_LOCK_PREFIX
#endif

/* How long a lock should spin before we consider blocking */
#define SPIN_THRESHOLD	(1 << 15)

extern struct static_key paravirt_ticketlocks_enabled;
static __always_inline bool static_key_false(struct static_key *key);

#ifdef CONFIG_PARAVIRT_SPINLOCKS

static inline void __ticket_enter_slowpath(arch_spinlock_t *lock)
{
	set_bit(0, (volatile unsigned long *)&lock->tickets.tail);
}

#else  /* !CONFIG_PARAVIRT_SPINLOCKS */
static __always_inline void __ticket_lock_spinning(arch_spinlock_t *lock,
							__ticket_t ticket)
{
}
static inline void __ticket_unlock_kick(arch_spinlock_t *lock,
							__ticket_t ticket)
{
}

#endif /* CONFIG_PARAVIRT_SPINLOCKS */

static __always_inline int arch_spin_value_unlocked(arch_spinlock_t lock)
{
	return lock.tickets.head == lock.tickets.tail;
}

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
 */
static __always_inline void arch_spin_lock(arch_spinlock_t *lock)
{
	register struct __raw_tickets inc = { .tail = TICKET_LOCK_INC };

	inc = xadd(&lock->tickets, inc);
	if (likely(inc.head == inc.tail))
		goto out;

	inc.tail &= ~TICKET_SLOWPATH_FLAG;
	for (;;) {
		unsigned count = SPIN_THRESHOLD;

		do {
			if (READ_ONCE(lock->tickets.head) == inc.tail)
				goto out;
			cpu_relax();
		} while (--count);
		__ticket_lock_spinning(lock, inc.tail);
	}
out:	barrier();	/* make sure nothing creeps before the lock is taken */
}

static __always_inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	arch_spinlock_t old, new;

	old.tickets = READ_ONCE(lock->tickets);
	if (old.tickets.head != (old.tickets.tail & ~TICKET_SLOWPATH_FLAG))
		return 0;

	new.head_tail = old.head_tail + (TICKET_LOCK_INC << TICKET_SHIFT);

	/* cmpxchg is a full barrier, so nothing can move before it */
	return cmpxchg(&lock->head_tail, old.head_tail, new.head_tail) == old.head_tail;
}

static inline void __ticket_unlock_slowpath(arch_spinlock_t *lock,
					    arch_spinlock_t old)
{
	arch_spinlock_t new;

	BUILD_BUG_ON(((__ticket_t)NR_CPUS) != NR_CPUS);

	/* Perform the unlock on the "before" copy */
	old.tickets.head += TICKET_LOCK_INC;

	/* Clear the slowpath flag */
	new.head_tail = old.head_tail & ~(TICKET_SLOWPATH_FLAG << TICKET_SHIFT);

	/*
	 * If the lock is uncontended, clear the flag - use cmpxchg in
	 * case it changes behind our back though.
	 */
	if (new.tickets.head != new.tickets.tail ||
	    cmpxchg(&lock->head_tail, old.head_tail,
					new.head_tail) != old.head_tail) {
		/*
		 * Lock still has someone queued for it, so wake up an
		 * appropriate waiter.
		 */
		__ticket_unlock_kick(lock, old.tickets.head);
	}
}

static __always_inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	if (TICKET_SLOWPATH_FLAG &&
	    static_key_false(&paravirt_ticketlocks_enabled)) {
		arch_spinlock_t prev;

		prev = *lock;
		add_smp(&lock->tickets.head, TICKET_LOCK_INC);

		/* add_smp() is a full mb() */

		if (unlikely(lock->tickets.tail & TICKET_SLOWPATH_FLAG))
			__ticket_unlock_slowpath(lock, prev);
	} else
		__add(&lock->tickets.head, TICKET_LOCK_INC, UNLOCK_LOCK_PREFIX);
}

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	struct __raw_tickets tmp = READ_ONCE(lock->tickets);

	return tmp.tail != tmp.head;
}

static inline int arch_spin_is_contended(arch_spinlock_t *lock)
{
	struct __raw_tickets tmp = READ_ONCE(lock->tickets);

	return (__ticket_t)(tmp.tail - tmp.head) > TICKET_LOCK_INC;
}
#define arch_spin_is_contended	arch_spin_is_contended

static __always_inline void arch_spin_lock_flags(arch_spinlock_t *lock,
						  unsigned long flags)
{
	arch_spin_lock(lock);
}

static inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	while (arch_spin_is_locked(lock))
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
 * On x86, we implement read-write locks using the generic qrwlock with
 * x86 specific optimization.
 */

#include <asm/qrwlock.h>

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* _ASM_X86_SPINLOCK_H */
