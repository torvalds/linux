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
 *
 * 32-bit SMP spinlocks.
 */

#ifndef _ASM_TILE_SPINLOCK_32_H
#define _ASM_TILE_SPINLOCK_32_H

#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/system.h>
#include <linux/compiler.h>

/*
 * We only use even ticket numbers so the '1' inserted by a tns is
 * an unambiguous "ticket is busy" flag.
 */
#define TICKET_QUANTUM 2


/*
 * SMP ticket spinlocks, allowing only a single CPU anywhere
 *
 * (the type definitions are in asm/spinlock_types.h)
 */
static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	/*
	 * Note that even if a new ticket is in the process of being
	 * acquired, so lock->next_ticket is 1, it's still reasonable
	 * to claim the lock is held, since it will be momentarily
	 * if not already.  There's no need to wait for a "valid"
	 * lock->next_ticket to become available.
	 */
	return lock->next_ticket != lock->current_ticket;
}

void arch_spin_lock(arch_spinlock_t *lock);

/* We cannot take an interrupt after getting a ticket, so don't enable them. */
#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

int arch_spin_trylock(arch_spinlock_t *lock);

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	/* For efficiency, overlap fetching the old ticket with the wmb(). */
	int old_ticket = lock->current_ticket;
	wmb();  /* guarantee anything modified under the lock is visible */
	lock->current_ticket = old_ticket + TICKET_QUANTUM;
}

void arch_spin_unlock_wait(arch_spinlock_t *lock);

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * We use a "tns/store-back" technique on a single word to manage
 * the lock state, looping around to retry if the tns returns 1.
 */

/* Internal layout of the word; do not use. */
#define _WR_NEXT_SHIFT	8
#define _WR_CURR_SHIFT  16
#define _WR_WIDTH       8
#define _RD_COUNT_SHIFT 24
#define _RD_COUNT_WIDTH 8

/* Internal functions; do not use. */
void arch_read_lock_slow(arch_rwlock_t *, u32);
int arch_read_trylock_slow(arch_rwlock_t *);
void arch_read_unlock_slow(arch_rwlock_t *);
void arch_write_lock_slow(arch_rwlock_t *, u32);
void arch_write_unlock_slow(arch_rwlock_t *, u32);

/**
 * arch_read_can_lock() - would read_trylock() succeed?
 */
static inline int arch_read_can_lock(arch_rwlock_t *rwlock)
{
	return (rwlock->lock << _RD_COUNT_WIDTH) == 0;
}

/**
 * arch_write_can_lock() - would write_trylock() succeed?
 */
static inline int arch_write_can_lock(arch_rwlock_t *rwlock)
{
	return rwlock->lock == 0;
}

/**
 * arch_read_lock() - acquire a read lock.
 */
static inline void arch_read_lock(arch_rwlock_t *rwlock)
{
	u32 val = __insn_tns((int *)&rwlock->lock);
	if (unlikely(val << _RD_COUNT_WIDTH)) {
		arch_read_lock_slow(rwlock, val);
		return;
	}
	rwlock->lock = val + (1 << _RD_COUNT_SHIFT);
}

/**
 * arch_read_lock() - acquire a write lock.
 */
static inline void arch_write_lock(arch_rwlock_t *rwlock)
{
	u32 val = __insn_tns((int *)&rwlock->lock);
	if (unlikely(val != 0)) {
		arch_write_lock_slow(rwlock, val);
		return;
	}
	rwlock->lock = 1 << _WR_NEXT_SHIFT;
}

/**
 * arch_read_trylock() - try to acquire a read lock.
 */
static inline int arch_read_trylock(arch_rwlock_t *rwlock)
{
	int locked;
	u32 val = __insn_tns((int *)&rwlock->lock);
	if (unlikely(val & 1))
		return arch_read_trylock_slow(rwlock);
	locked = (val << _RD_COUNT_WIDTH) == 0;
	rwlock->lock = val + (locked << _RD_COUNT_SHIFT);
	return locked;
}

/**
 * arch_write_trylock() - try to acquire a write lock.
 */
static inline int arch_write_trylock(arch_rwlock_t *rwlock)
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

/**
 * arch_read_unlock() - release a read lock.
 */
static inline void arch_read_unlock(arch_rwlock_t *rwlock)
{
	u32 val;
	mb();  /* guarantee anything modified under the lock is visible */
	val = __insn_tns((int *)&rwlock->lock);
	if (unlikely(val & 1)) {
		arch_read_unlock_slow(rwlock);
		return;
	}
	rwlock->lock = val - (1 << _RD_COUNT_SHIFT);
}

/**
 * arch_write_unlock() - release a write lock.
 */
static inline void arch_write_unlock(arch_rwlock_t *rwlock)
{
	u32 val;
	mb();  /* guarantee anything modified under the lock is visible */
	val = __insn_tns((int *)&rwlock->lock);
	if (unlikely(val != (1 << _WR_NEXT_SHIFT))) {
		arch_write_unlock_slow(rwlock, val);
		return;
	}
	rwlock->lock = 0;
}

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#endif /* _ASM_TILE_SPINLOCK_32_H */
