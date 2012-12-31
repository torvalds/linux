/*
 *
 * (C) COPYRIGHT 2010-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_ARCH_WORKQ_H_
#define _OSK_ARCH_WORKQ_H_

#include <linux/version.h> /* version detection */
#include "osk/include/mali_osk_failure.h"

#if MALI_LICENSE_IS_GPL == 0
/* Wrapper function to allow flushing in non-GPL driver */
void oskp_work_func_wrapper( struct work_struct *work );

/* Forward decl */
OSK_STATIC_INLINE void osk_workq_flush(osk_workq *wq);
#endif /* MALI_LICENSE_IS_GPL == 0 */

OSK_STATIC_INLINE osk_error osk_workq_init(osk_workq * const wq, const char *name, u32 flags)
{
#if MALI_LICENSE_IS_GPL 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
	unsigned int wqflags = 0;
#endif
	OSK_ASSERT(NULL != wq);
	OSK_ASSERT(NULL != name);
	OSK_ASSERT(0 == (flags & ~(OSK_WORKQ_NON_REENTRANT|OSK_WORKQ_HIGH_PRIORITY|OSK_WORKQ_RESCUER)));

	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		return OSK_ERR_FAIL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
	if (flags & OSK_WORKQ_NON_REENTRANT)
	{
		wqflags |= WQ_NON_REENTRANT;
	}
	if (flags & OSK_WORKQ_HIGH_PRIORITY)
	{
		wqflags |= WQ_HIGHPRI;
	}
	if (flags & OSK_WORKQ_RESCUER)
	{
		wqflags |= WQ_RESCUER;
	}

	wq->wqs = alloc_workqueue(name, wqflags, 1);
#else
	if (flags & OSK_WORKQ_NON_REENTRANT)
	{
		wq->wqs = create_singlethread_workqueue(name);
	}
	else
	{
		wq->wqs = create_workqueue(name);
	}
#endif
	if (NULL == wq->wqs)
	{
		return OSK_ERR_FAIL;
	}
#else
	/* Non-GPL driver uses global workqueue */
	spin_lock_init( &wq->active_items_lock );
	wq->nr_active_items = 0;
	init_waitqueue_head( &wq->waitq_zero_active_items );
#endif
	return OSK_ERR_NONE;
}

OSK_STATIC_INLINE void osk_workq_term(osk_workq *wq)
{
	OSK_ASSERT(NULL != wq);
#if MALI_LICENSE_IS_GPL
	destroy_workqueue(wq->wqs);
#else
	/* Non-GPL driver uses global workqueue, so flush it manually */
	osk_workq_flush(wq);
#endif
}

OSK_STATIC_INLINE void osk_workq_work_init(osk_workq_work * const wk, osk_workq_fn fn)
{
	work_func_t func_to_call_first;
	struct work_struct *os_work;
	OSK_ASSERT(NULL != wk);
	OSK_ASSERT(NULL != fn);
	OSK_ASSERT(0 ==	object_is_on_stack(wk));

#if MALI_LICENSE_IS_GPL
	func_to_call_first = fn;
	os_work = wk;
#else /* MALI_LICENSE_IS_GPL */
	func_to_call_first = &oskp_work_func_wrapper;
	wk->actual_fn = fn;
	os_work = &wk->os_work;
#endif /* MALI_LICENSE_IS_GPL */

	INIT_WORK(os_work, func_to_call_first);
}

OSK_STATIC_INLINE void osk_workq_work_init_on_stack(osk_workq_work * const wk, osk_workq_fn fn)
{
	work_func_t func_to_call_first;
	struct work_struct *os_work;
	OSK_ASSERT(NULL != wk);
	OSK_ASSERT(NULL != fn);
	OSK_ASSERT(0 !=	object_is_on_stack(wk));

#if MALI_LICENSE_IS_GPL
	func_to_call_first = fn;
	os_work = wk;
#else /* MALI_LICENSE_IS_GPL */
	func_to_call_first = &oskp_work_func_wrapper;
	wk->actual_fn = fn;
	os_work = &wk->os_work;
#endif /* MALI_LICENSE_IS_GPL */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	INIT_WORK_ONSTACK(os_work, func_to_call_first);
#else
	INIT_WORK_ON_STACK(os_work, func_to_call_first);
#endif
}

OSK_STATIC_INLINE void osk_workq_submit(osk_workq *wq, osk_workq_work * const wk)
{
	OSK_ASSERT(NULL != wk);
	OSK_ASSERT(NULL != wq);

#if MALI_LICENSE_IS_GPL
	queue_work(wq->wqs, wk);
#else
	/* Non-GPL driver uses global workqueue */
	{
		unsigned long flags;

		wk->parent_wq = wq;

		spin_lock_irqsave( &wq->active_items_lock, flags );
		++(wq->nr_active_items);
		spin_unlock_irqrestore( &wq->active_items_lock, flags );
		schedule_work(&wk->os_work);
	}
#endif
}

OSK_STATIC_INLINE void osk_workq_flush(osk_workq *wq)
{
	OSK_ASSERT(NULL != wq);

#if MALI_LICENSE_IS_GPL
	flush_workqueue(wq->wqs);
#else
	/* Non-GPL driver uses global workqueue, but we shouldn't use flush_scheduled_work() */

	/* Locking is required here, to ensure that the wait_queue object doesn't
	 * disappear from the work item. This is the sequence we must guard
	 * against:
	 * - work item thread (signaller) atomic-sets nr_active_items = 0
	 * - signaller gets scheduled out
	 * - another thread (waiter) calls osk_workq_term()
	 * - ...which calls osk_workq_flush()
	 * - ...which calls wait_event(), and immediately returns without blocking
	 * - osk_workq_term() completes
	 * - caller (waiter) frees the memory backing the osk_workq
	 * - signaller gets scheduled back in
	 * - tries to call wake_up() on a freed address - error!
	 */
	{
		unsigned long flags;
		spin_lock_irqsave( &wq->active_items_lock, flags );
		while( wq->nr_active_items != 0 )
		{
			spin_unlock_irqrestore( &wq->active_items_lock, flags );
			wait_event( wq->waitq_zero_active_items, (wq->nr_active_items == 0) );
			spin_lock_irqsave( &wq->active_items_lock, flags );
		}
		spin_unlock_irqrestore( &wq->active_items_lock, flags );
	}
#endif
}


#endif
