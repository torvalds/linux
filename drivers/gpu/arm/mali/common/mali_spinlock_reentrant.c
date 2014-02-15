/*
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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
