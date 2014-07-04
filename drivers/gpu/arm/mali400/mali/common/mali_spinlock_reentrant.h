/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __MALI_SPINLOCK_REENTRANT_H__
#define __MALI_SPINLOCK_REENTRANT_H__

#include "mali_osk.h"
#include "mali_kernel_common.h"

/**
 * Reentrant spinlock.
 */
struct mali_spinlock_reentrant {
	_mali_osk_spinlock_irq_t *lock;
	u32               owner;
	u32               counter;
};

/**
 * Create a new reentrant spinlock.
 *
 * @param lock_order Lock order.
 * @return New reentrant spinlock.
 */
struct mali_spinlock_reentrant *mali_spinlock_reentrant_init(_mali_osk_lock_order_t lock_order);

/**
 * Terminate reentrant spinlock and free any associated resources.
 *
 * @param spinlock Reentrant spinlock to terminate.
 */
void mali_spinlock_reentrant_term(struct mali_spinlock_reentrant *spinlock);

/**
 * Wait for reentrant spinlock to be signaled.
 *
 * @param spinlock Reentrant spinlock.
 * @param tid Thread ID.
 */
void mali_spinlock_reentrant_wait(struct mali_spinlock_reentrant *spinlock, u32 tid);

/**
 * Signal reentrant spinlock.
 *
 * @param spinlock Reentrant spinlock.
 * @param tid Thread ID.
 */
void mali_spinlock_reentrant_signal(struct mali_spinlock_reentrant *spinlock, u32 tid);

/**
 * Check if thread is holding reentrant spinlock.
 *
 * @param spinlock Reentrant spinlock.
 * @param tid Thread ID.
 * @return MALI_TRUE if thread is holding spinlock, MALI_FALSE if not.
 */
MALI_STATIC_INLINE mali_bool mali_spinlock_reentrant_is_held(struct mali_spinlock_reentrant *spinlock, u32 tid)
{
	MALI_DEBUG_ASSERT_POINTER(spinlock->lock);
	return (tid == spinlock->owner && 0 < spinlock->counter);
}

#endif /* __MALI_SPINLOCK_REENTRANT_H__ */
