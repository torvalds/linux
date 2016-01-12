/*
 * Copyright (C) 2010-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_locks.c
 * Implemenation of the OS abstraction layer for the kernel device driver
 */

#include "mali_osk_locks.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"


#ifdef DEBUG
#ifdef LOCK_ORDER_CHECKING
static DEFINE_SPINLOCK(lock_tracking_lock);
static mali_bool add_lock_to_log_and_check(struct _mali_osk_lock_debug_s *lock, uint32_t tid);
static void remove_lock_from_log(struct _mali_osk_lock_debug_s *lock, uint32_t tid);
static const char *const lock_order_to_string(_mali_osk_lock_order_t order);
#endif /* LOCK_ORDER_CHECKING */

void _mali_osk_locks_debug_init(struct _mali_osk_lock_debug_s *checker, _mali_osk_lock_flags_t flags, _mali_osk_lock_order_t order)
{
	checker->orig_flags = flags;
	checker->owner = 0;

#ifdef LOCK_ORDER_CHECKING
	checker->order = order;
	checker->next = NULL;
#endif
}

void _mali_osk_locks_debug_add(struct _mali_osk_lock_debug_s *checker)
{
	checker->owner = _mali_osk_get_tid();

#ifdef LOCK_ORDER_CHECKING
	if (!(checker->orig_flags & _MALI_OSK_LOCKFLAG_UNORDERED)) {
		if (!add_lock_to_log_and_check(checker, _mali_osk_get_tid())) {
			printk(KERN_ERR "%d: ERROR lock %p taken while holding a lock of a higher order.\n",
			       _mali_osk_get_tid(), checker);
			dump_stack();
		}
	}
#endif
}

void _mali_osk_locks_debug_remove(struct _mali_osk_lock_debug_s *checker)
{

#ifdef LOCK_ORDER_CHECKING
	if (!(checker->orig_flags & _MALI_OSK_LOCKFLAG_UNORDERED)) {
		remove_lock_from_log(checker, _mali_osk_get_tid());
	}
#endif
	checker->owner = 0;
}


#ifdef LOCK_ORDER_CHECKING
/* Lock order checking
 * -------------------
 *
 * To assure that lock ordering scheme defined by _mali_osk_lock_order_t is strictly adhered to, the
 * following function will, together with a linked list and some extra members in _mali_osk_lock_debug_s,
 * make sure that a lock that is taken has a higher order than the current highest-order lock a
 * thread holds.
 *
 * This is done in the following manner:
 * - A linked list keeps track of locks held by a thread.
 * - A `next' pointer is added to each lock. This is used to chain the locks together.
 * - When taking a lock, the `add_lock_to_log_and_check' makes sure that taking
 *   the given lock is legal. It will follow the linked list  to find the last
 *   lock taken by this thread. If the last lock's order was lower than the
 *   lock that is to be taken, it appends the new lock to the list and returns
 *   true, if not, it return false. This return value is assert()'ed on in
 *   _mali_osk_lock_wait().
 */

static struct _mali_osk_lock_debug_s *lock_lookup_list;

static void dump_lock_tracking_list(void)
{
	struct _mali_osk_lock_debug_s *l;
	u32 n = 1;

	/* print list for debugging purposes */
	l = lock_lookup_list;

	while (NULL != l) {
		printk(" [lock: %p, tid_owner: %d, order: %d] ->", l, l->owner, l->order);
		l = l->next;
		MALI_DEBUG_ASSERT(n++ < 100);
	}
	printk(" NULL\n");
}

static int tracking_list_length(void)
{
	struct _mali_osk_lock_debug_s *l;
	u32 n = 0;
	l = lock_lookup_list;

	while (NULL != l) {
		l = l->next;
		n++;
		MALI_DEBUG_ASSERT(n < 100);
	}
	return n;
}

static mali_bool add_lock_to_log_and_check(struct _mali_osk_lock_debug_s *lock, uint32_t tid)
{
	mali_bool ret = MALI_FALSE;
	_mali_osk_lock_order_t highest_order_for_tid = _MALI_OSK_LOCK_ORDER_FIRST;
	struct _mali_osk_lock_debug_s *highest_order_lock = (struct _mali_osk_lock_debug_s *)0xbeefbabe;
	struct _mali_osk_lock_debug_s *l;
	unsigned long local_lock_flag;
	u32 len;

	spin_lock_irqsave(&lock_tracking_lock, local_lock_flag);
	len = tracking_list_length();

	l  = lock_lookup_list;
	if (NULL == l) { /* This is the first lock taken by this thread -- record and return true */
		lock_lookup_list = lock;
		spin_unlock_irqrestore(&lock_tracking_lock, local_lock_flag);
		return MALI_TRUE;
	} else {
		/* Traverse the locks taken and find the lock of the highest order.
		 * Since several threads may hold locks, each lock's owner must be
		 * checked so that locks not owned by this thread can be ignored. */
		for (;;) {
			MALI_DEBUG_ASSERT_POINTER(l);
			if (tid == l->owner && l->order >= highest_order_for_tid) {
				highest_order_for_tid = l->order;
				highest_order_lock = l;
			}

			if (NULL != l->next) {
				l = l->next;
			} else {
				break;
			}
		}

		l->next = lock;
		l->next = NULL;
	}

	/* We have now found the highest order lock currently held by this thread and can see if it is
	 * legal to take the requested lock. */
	ret = highest_order_for_tid < lock->order;

	if (!ret) {
		printk(KERN_ERR "Took lock of order %d (%s) while holding lock of order %d (%s)\n",
		       lock->order, lock_order_to_string(lock->order),
		       highest_order_for_tid, lock_order_to_string(highest_order_for_tid));
		dump_lock_tracking_list();
	}

	if (len + 1 != tracking_list_length()) {
		printk(KERN_ERR "************ lock: %p\n", lock);
		printk(KERN_ERR "************ before: %d *** after: %d ****\n", len, tracking_list_length());
		dump_lock_tracking_list();
		MALI_DEBUG_ASSERT_POINTER(NULL);
	}

	spin_unlock_irqrestore(&lock_tracking_lock, local_lock_flag);
	return ret;
}

static void remove_lock_from_log(struct _mali_osk_lock_debug_s *lock, uint32_t tid)
{
	struct _mali_osk_lock_debug_s *curr;
	struct _mali_osk_lock_debug_s *prev = NULL;
	unsigned long local_lock_flag;
	u32 len;
	u32 n = 0;

	spin_lock_irqsave(&lock_tracking_lock, local_lock_flag);
	len = tracking_list_length();
	curr = lock_lookup_list;

	if (NULL == curr) {
		printk(KERN_ERR "Error: Lock tracking list was empty on call to remove_lock_from_log\n");
		dump_lock_tracking_list();
	}

	MALI_DEBUG_ASSERT_POINTER(curr);


	while (lock != curr) {
		prev = curr;

		MALI_DEBUG_ASSERT_POINTER(curr);
		curr = curr->next;
		MALI_DEBUG_ASSERT(n++ < 100);
	}

	if (NULL == prev) {
		lock_lookup_list = curr->next;
	} else {
		MALI_DEBUG_ASSERT_POINTER(curr);
		MALI_DEBUG_ASSERT_POINTER(prev);
		prev->next = curr->next;
	}

	lock->next = NULL;

	if (len - 1 != tracking_list_length()) {
		printk(KERN_ERR "************ lock: %p\n", lock);
		printk(KERN_ERR "************ before: %d *** after: %d ****\n", len, tracking_list_length());
		dump_lock_tracking_list();
		MALI_DEBUG_ASSERT_POINTER(NULL);
	}

	spin_unlock_irqrestore(&lock_tracking_lock, local_lock_flag);
}

static const char *const lock_order_to_string(_mali_osk_lock_order_t order)
{
	switch (order) {
	case _MALI_OSK_LOCK_ORDER_SESSIONS:
		return "_MALI_OSK_LOCK_ORDER_SESSIONS";
		break;
	case _MALI_OSK_LOCK_ORDER_MEM_SESSION:
		return "_MALI_OSK_LOCK_ORDER_MEM_SESSION";
		break;
	case _MALI_OSK_LOCK_ORDER_MEM_INFO:
		return "_MALI_OSK_LOCK_ORDER_MEM_INFO";
		break;
	case _MALI_OSK_LOCK_ORDER_MEM_PT_CACHE:
		return "_MALI_OSK_LOCK_ORDER_MEM_PT_CACHE";
		break;
	case _MALI_OSK_LOCK_ORDER_DESCRIPTOR_MAP:
		return "_MALI_OSK_LOCK_ORDER_DESCRIPTOR_MAP";
		break;
	case _MALI_OSK_LOCK_ORDER_PM_EXECUTION:
		return "_MALI_OSK_LOCK_ORDER_PM_EXECUTION";
		break;
	case _MALI_OSK_LOCK_ORDER_EXECUTOR:
		return "_MALI_OSK_LOCK_ORDER_EXECUTOR";
		break;
	case _MALI_OSK_LOCK_ORDER_TIMELINE_SYSTEM:
		return "_MALI_OSK_LOCK_ORDER_TIMELINE_SYSTEM";
		break;
	case _MALI_OSK_LOCK_ORDER_SCHEDULER:
		return "_MALI_OSK_LOCK_ORDER_SCHEDULER";
		break;
	case _MALI_OSK_LOCK_ORDER_SCHEDULER_DEFERRED:
		return "_MALI_OSK_LOCK_ORDER_SCHEDULER_DEFERRED";
		break;
	case _MALI_OSK_LOCK_ORDER_DMA_COMMAND:
		return "_MALI_OSK_LOCK_ORDER_DMA_COMMAND";
		break;
	case _MALI_OSK_LOCK_ORDER_PROFILING:
		return "_MALI_OSK_LOCK_ORDER_PROFILING";
		break;
	case _MALI_OSK_LOCK_ORDER_L2:
		return "_MALI_OSK_LOCK_ORDER_L2";
		break;
	case _MALI_OSK_LOCK_ORDER_L2_COMMAND:
		return "_MALI_OSK_LOCK_ORDER_L2_COMMAND";
		break;
	case _MALI_OSK_LOCK_ORDER_UTILIZATION:
		return "_MALI_OSK_LOCK_ORDER_UTILIZATION";
		break;
	case _MALI_OSK_LOCK_ORDER_SESSION_PENDING_JOBS:
		return "_MALI_OSK_LOCK_ORDER_SESSION_PENDING_JOBS";
		break;
	case _MALI_OSK_LOCK_ORDER_PM_STATE:
		return "_MALI_OSK_LOCK_ORDER_PM_STATE";
		break;
	default:
		return "<UNKNOWN_LOCK_ORDER>";
	}
}
#endif /* LOCK_ORDER_CHECKING */
#endif /* DEBUG */
