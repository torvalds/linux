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

#ifndef _OSK_ARCH_TYPES_H_
#define _OSK_ARCH_TYPES_H_

#include <linux/version.h> /* version detection */
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#if MALI_LICENSE_IS_GPL
#include <linux/hrtimer.h>
#endif
#include <linux/workqueue.h>
#include <linux/mm_types.h>
#include <asm/atomic.h>
#include <linux/sched.h>

#include <malisw/mali_malisw.h>

 /* This will have to agree with the OSU definition of the CPU page size: CONFIG_CPU_PAGE_SIZE_LOG2 */
#define OSK_PAGE_SHIFT PAGE_SHIFT
#define OSK_PAGE_SIZE  PAGE_SIZE
#define OSK_PAGE_MASK  PAGE_MASK

#define OSK_MIN(x,y) min((x), (y))

typedef spinlock_t osk_spinlock;
typedef struct osk_spinlock_irq {
	spinlock_t	lock;
	unsigned long	flags;
} osk_spinlock_irq;

typedef struct mutex osk_mutex;
typedef struct rw_semaphore osk_rwlock;

typedef atomic_t osk_atomic;

typedef struct osk_waitq
{
       /**
		* set to MALI_TRUE when the waitq is signaled; set to MALI_FALSE when
		* not signaled.
		*
		* This does not require locking for the setter, clearer and waiter.
		* Here's why:
		* - The only sensible use of a waitq is for operations to occur in
		* strict order, without possibility of race between the callers of
		* osk_waitq_set() and osk_waitq_clear() (the setter and clearer).
		* Effectively, the clear must cause a later set to occur.
		* - When the clear/set operations occur in different threads, some
		* form of communication needs to happen for the clear to cause the
		* signal to occur later.
		* - This itself \b must involve a memory barrier, and so the clear is
		* guarenteed to be observed by the waiter such that it is before the set.
		* (and the set is observed after the clear).
		*
		* For example, running a GPU job might clear a "there are jobs in
		* flight" waitq. Running the job must issue an register write, (and
		* likely a post to a workqueue due to IRQ handling). Those operations
		* must cause a data barrier to occur. During IRQ handling/workqueue
		* processing, we might then set the waitq, and this happens after the
		* barrier. Hence, the set and clear are observed in strict order.
		*/
       mali_bool signaled;
       wait_queue_head_t wq;  /**< threads waiting for flag to be signalled */
} osk_waitq;

typedef struct osk_timer {
#if MALI_LICENSE_IS_GPL
	struct hrtimer timer;
	osk_timer_callback callback;
	void *data;
#else /* MALI_LICENSE_IS_GPL */
	struct timer_list timer;
#endif /* MALI_LICENSE_IS_GPL */
#ifdef MALI_DEBUG
	mali_bool active;
#endif
} osk_timer;

typedef struct page osk_page;
typedef struct vm_area_struct osk_vma;

typedef unsigned long osk_ticks; /* 32-bit resolution deemed to be sufficient */

/* Separate definitions for the following, to avoid wrapper functions for GPL drivers */
#if MALI_LICENSE_IS_GPL
typedef work_func_t		osk_workq_fn;

typedef struct work_struct osk_workq_work;

typedef struct osk_workq
{
	struct workqueue_struct *wqs;
} osk_workq;

#else /* MALI_LICENSE_IS_GPL */

/* Forward decls */
typedef struct osk_workq_work osk_workq_work;
typedef struct osk_workq osk_workq;

typedef void (*osk_workq_fn)(osk_workq_work *);

struct osk_workq_work
{
	struct work_struct os_work;
	/* Non-GPL driver must manually track work */
	osk_workq_fn actual_fn;
	osk_workq *parent_wq;
};

struct osk_workq
{
	/* Non-GPL driver shouldn't flush the global workqueue, so we do a manual form of flushing */
	spinlock_t active_items_lock;
	u32 nr_active_items;
	wait_queue_head_t waitq_zero_active_items;
};

#endif /* MALI_LICENSE_IS_GPL */

typedef struct device osk_power_info;

typedef struct timeval osk_timeval;

#endif /* _OSK_ARCH_TYPES_H_ */

