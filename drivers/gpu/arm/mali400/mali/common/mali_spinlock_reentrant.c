/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_spinlock_reentrant.h"

#include "mali_osk.h"
#include "mali_kernel_common.h"

struct mali_spinlock_reentrant *mali_spinlock_reentrant_init(_mali_osk_lock_order_t lock_order)
{
	struct mali_spinlock_reentrant *spinlock;

	spinlock = _mali_osk_calloc(1, sizeof(struct mali_spinlock_reentrant));
	if (NULL == spinlock) {
		return NULL;
	}

	spinlock->lock = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED, lock_order);
	if (NULL == spinlock->lock) {
		mali_spinlock_reentrant_term(spinlock);
		return NULL;
	}

	return spinlock;
}

void mali_spinlock_reentrant_term(struct mali_spinlock_reentrant *spinlock)
{
	MALI_DEBUG_ASSERT_POINTER(spinlock);
	MALI_DEBUG_ASSERT(0 == spinlock->counter && 0 == spinlock->owner);

	if (NULL != spinlock->lock) {
		_mali_osk_spinlock_irq_term(spinlock->lock);
	}

	_mali_osk_free(spinlock);
}

void mali_spinlock_reentrant_wait(struct mali_spinlock_reentrant *spinlock, u32 tid)
{
	MALI_DEBUG_ASSERT_POINTER(spinlock);
	MALI_DEBUG_ASSERT_POINTER(spinlock->lock);
	MALI_DEBUG_ASSERT(0 != tid);

	MALI_DEBUG_PRINT(5, ("%s ^\n", __FUNCTION__));

	if (tid != spinlock->owner) {
		_mali_osk_spinlock_irq_lock(spinlock->lock);
		MALI_DEBUG_ASSERT(0 == spinlock->owner && 0 == spinlock->counter);
		spinlock->owner = tid;
	}

	MALI_DEBUG_PRINT(5, ("%s v\n", __FUNCTION__));

	++spinlock->counter;
}

void mali_spinlock_reentrant_signal(struct mali_spinlock_reentrant *spinlock, u32 tid)
{
	MALI_DEBUG_ASSERT_POINTER(spinlock);
	MALI_DEBUG_ASSERT_POINTER(spinlock->lock);
	MALI_DEBUG_ASSERT(0 != tid && tid == spinlock->owner);

	--spinlock->counter;
	if (0 == spinlock->counter) {
		spinlock->owner = 0;
		MALI_DEBUG_PRINT(5, ("%s release last\n", __FUNCTION__));
		_mali_osk_spinlock_irq_unlock(spinlock->lock);
	}
}
