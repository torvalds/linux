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

#ifndef _OSK_ARCH_WAITQ_H
#define _OSK_ARCH_WAITQ_H

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#include <linux/wait.h>
#include <linux/sched.h>

/*
 * Note:
 *
 * We do not need locking on the signalled member (see its doxygen description)
 */

OSK_STATIC_INLINE osk_error osk_waitq_init(osk_waitq * const waitq)
{
	OSK_ASSERT(NULL != waitq);
	waitq->signaled = MALI_FALSE;
	init_waitqueue_head(&waitq->wq);
	return OSK_ERR_NONE;
}

OSK_STATIC_INLINE void osk_waitq_wait(osk_waitq *waitq)
{
	OSK_ASSERT(NULL != waitq);
	wait_event(waitq->wq, waitq->signaled != MALI_FALSE);
}

OSK_STATIC_INLINE void osk_waitq_set(osk_waitq *waitq)
{
	OSK_ASSERT(NULL != waitq);
	waitq->signaled = MALI_TRUE;
	wake_up(&waitq->wq);
}

OSK_STATIC_INLINE void osk_waitq_clear(osk_waitq *waitq)
{
	OSK_ASSERT(NULL != waitq);
	waitq->signaled = MALI_FALSE;
}

OSK_STATIC_INLINE void osk_waitq_term(osk_waitq *waitq)
{
	OSK_ASSERT(NULL != waitq);
	/* NOP on Linux */
}

#endif /* _OSK_ARCH_WAITQ_H_ */
