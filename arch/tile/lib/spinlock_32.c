/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <asm/processor.h>
#include <arch/spr_def.h>

#include "spinlock_common.h"

void arch_spin_lock(arch_spinlock_t *lock)
{
	int my_ticket;
	int iterations = 0;
	int delta;

	while ((my_ticket = __insn_tns((void *)&lock->next_ticket)) & 1)
		delay_backoff(iterations++);

	/* Increment the next ticket number, implicitly releasing tns lock. */
	lock->next_ticket = my_ticket + TICKET_QUANTUM;

	/* Wait until it's our turn. */
	while ((delta = my_ticket - lock->current_ticket) != 0)
		relax((128 / CYCLES_PER_RELAX_LOOP) * delta);
}
EXPORT_SYMBOL(arch_spin_lock);

int arch_spin_trylock(arch_spinlock_t *lock)
{
	/*
	 * Grab a ticket; no need to retry if it's busy, we'll just
	 * treat that the same as "locked", since someone else
	 * will lock it momentarily anyway.
	 */
	int my_ticket = __insn_tns((void *)&lock->next_ticket);

	if (my_ticket == lock->current_ticket) {
		/* Not currently locked, so lock it by keeping this ticket. */
		lock->next_ticket = my_ticket + TICKET_QUANTUM;
		/* Success! */
		return 1;
	}

	if (!(my_ticket & 1)) {
		/* Release next_ticket. */
		lock->next_ticket = my_ticket;
	}

	return 0;
}
EXPORT_SYMBOL(arch_spin_trylock);

void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	u32 iterations = 0;
	while (arch_spin_is_locked(lock))
		delay_backoff(iterations++);
}
EXPORT_SYMBOL(arch_spin_unlock_wait);

/*
 * The low byte is always reserved to be the marker for a "tns" operation
 * since the low bit is set to "1" by a tns.  The next seven bits are
 * zeroes.  The next byte holds the "next" writer value, i.e. the ticket
 * available for the next task that wants to write.  The third byte holds
 * the current writer value, i.e. the writer who holds the current ticket.
 * If current == next == 0, there are no interested writers.
 */
#define WR_NEXT_SHIFT   _WR_NEXT_SHIFT
#define WR_CURR_SHIFT   _WR_CURR_SHIFT
#define WR_WIDTH        _WR_WIDTH
#define WR_MASK         ((1 << WR_WIDTH) - 1)

/*
 * The last eight bits hold the active reader count.  This has to be
 * zero before a writer can start to write.
 */
#define RD_COUNT_SHIFT  _RD_COUNT_SHIFT
#define RD_COUNT_WIDTH  _RD_COUNT_WIDTH
#define RD_COUNT_MASK   ((1 << RD_COUNT_WIDTH) - 1)


/*
 * We can get the read lock if everything but the reader bits (which
 * are in the high part of the word) is zero, i.e. no active or
 * waiting writers, no tns.
 *
 * We guard the tns/store-back with an interrupt critical section to
 * preserve the semantic that the same read lock can be acquired in an
 * interrupt context.
 */
int arch_read_trylock(arch_rwlock_t *rwlock)
{
	u32 val;
	__insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);
	val = __insn_tns((int *)&rwlock->lock);
	if (likely((val << _RD_COUNT_WIDTH) == 0)) {
		val += 1 << RD_COUNT_SHIFT;
		rwlock->lock = val;
		__insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0);
		BUG_ON(val == 0);  /* we don't expect wraparound */
		return 1;
	}
	if ((val & 1) == 0)
		rwlock->lock = val;
	__insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0);
	return 0;
}
EXPORT_SYMBOL(arch_read_trylock);

/*
 * Spin doing arch_read_trylock() until we acquire the lock.
 * ISSUE: This approach can permanently starve readers.  A reader who sees
 * a writer could instead take a ticket lock (just like a writer would),
 * and atomically enter read mode (with 1 reader) when it gets the ticket.
 * This way both readers and writers would always make forward progress
 * in a finite time.
 */
void arch_read_lock(arch_rwlock_t *rwlock)
{
	u32 iterations = 0;
	while (unlikely(!arch_read_trylock(rwlock)))
		delay_backoff(iterations++);
}
EXPORT_SYMBOL(arch_read_lock);

void arch_read_unlock(arch_rwlock_t *rwlock)
{
	u32 val, iterations = 0;

	mb();  /* guarantee anything modified under the lock is visible */
	for (;;) {
		__insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 1);
		val = __insn_tns((int *)&rwlock->lock);
		if (likely((val & 1) == 0)) {
			rwlock->lock = val - (1 << _RD_COUNT_SHIFT);
			__insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0);
			break;
		}
		__insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0);
		delay_backoff(iterations++);
	}
}
EXPORT_SYMBOL(arch_read_unlock);

/*
 * We don't need an interrupt critical section here (unlike for
 * arch_read_lock) since we should never use a bare write lock where
 * it could be interrupted by code that could try to re-acquire it.
 */
void arch_write_lock(arch_rwlock_t *rwlock)
{
	/*
	 * The trailing underscore on this variable (and curr_ below)
	 * reminds us that the high bits are garbage; we mask them out
	 * when we compare them.
	 */
	u32 my_ticket_;
	u32 iterations = 0;
	u32 val = __insn_tns((int *)&rwlock->lock);

	if (likely(val == 0)) {
		rwlock->lock = 1 << _WR_NEXT_SHIFT;
		return;
	}

	/*
	 * Wait until there are no readers, then bump up the next
	 * field and capture the ticket value.
	 */
	for (;;) {
		if (!(val & 1)) {
			if ((val >> RD_COUNT_SHIFT) == 0)
				break;
			rwlock->lock = val;
		}
		delay_backoff(iterations++);
		val = __insn_tns((int *)&rwlock->lock);
	}

	/* Take out the next ticket and extract my ticket value. */
	rwlock->lock = __insn_addb(val, 1 << WR_NEXT_SHIFT);
	my_ticket_ = val >> WR_NEXT_SHIFT;

	/* Wait until the "current" field matches our ticket. */
	for (;;) {
		u32 curr_ = val >> WR_CURR_SHIFT;
		u32 delta = ((my_ticket_ - curr_) & WR_MASK);
		if (likely(delta == 0))
			break;

		/* Delay based on how many lock-holders are still out there. */
		relax((256 / CYCLES_PER_RELAX_LOOP) * delta);

		/*
		 * Get a non-tns value to check; we don't need to tns
		 * it ourselves.  Since we're not tns'ing, we retry
		 * more rapidly to get a valid value.
		 */
		while ((val = rwlock->lock) & 1)
			relax(4);
	}
}
EXPORT_SYMBOL(arch_write_lock);

int arch_write_trylock(arch_rwlock_t *rwlock)
{
	u32 val = __insn_tns((int *)&rwlock->lock);

	/*
	 * If a tns is in progress, or there's a waiting or active locker,
	 * or active readers, we can't take the lock, so give up.
	 */
	if (unlikely(val != 0)) {
		if (!(val & 1))
			rwlock->lock = val;
		return 0;
	}

	/* Set the "next" field to mark it locked. */
	rwlock->lock = 1 << _WR_NEXT_SHIFT;
	return 1;
}
EXPORT_SYMBOL(arch_write_trylock);

void arch_write_unlock(arch_rwlock_t *rwlock)
{
	u32 val, eq, mask;

	mb();  /* guarantee anything modified under the lock is visible */
	val = __insn_tns((int *)&rwlock->lock);
	if (likely(val == (1 << _WR_NEXT_SHIFT))) {
		rwlock->lock = 0;
		return;
	}
	while (unlikely(val & 1)) {
		/* Limited backoff since we are the highest-priority task. */
		relax(4);
		val = __insn_tns((int *)&rwlock->lock);
	}
	mask = 1 << WR_CURR_SHIFT;
	val = __insn_addb(val, mask);
	eq = __insn_seqb(val, val << (WR_CURR_SHIFT - WR_NEXT_SHIFT));
	val = __insn_mz(eq & mask, val);
	rwlock->lock = val;
}
EXPORT_SYMBOL(arch_write_unlock);
