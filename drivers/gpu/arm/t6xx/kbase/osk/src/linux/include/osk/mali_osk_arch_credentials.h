/*
 *
 * (C) COPYRIGHT 2008-2012 ARM Limited. All rights reserved.
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

#ifndef _OSK_ARCH_CREDENTIALS_H_
#define _OSK_ARCH_CREDENTIALS_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#include <linux/cred.h>

OSK_STATIC_INLINE mali_bool osk_is_privileged(void)
{
	mali_bool is_privileged = MALI_FALSE;

	/* Check if the caller is root */
	if (current_euid() == 0)
	{
		is_privileged = MALI_TRUE;
	}

	return is_privileged;
}

OSK_STATIC_INLINE mali_bool osk_is_policy_realtime(void)
{
	int policy = current->policy;

	if (policy == SCHED_FIFO || policy == SCHED_RR)
	{
		return MALI_TRUE;
	}

	return MALI_FALSE;
}

OSK_STATIC_INLINE void osk_get_process_priority(osk_process_priority *prio)
{
	/* Note that we return the current process priority.
	 * If called from a kernel thread the priority returned
	 * will be the kernel thread priority and not the user
	 * process that is currently submitting jobs to the scheduler.
	 */
	OSK_ASSERT(prio);

	if(osk_is_policy_realtime())
	{
		prio->is_realtime = MALI_TRUE;
		/* NOTE: realtime range was in the range 0..99 (lowest to highest) so we invert
		 * the priority and scale to -20..0 to normalize the result with the NICE range
		 */
		prio->priority = (((MAX_RT_PRIO-1) - current->rt_priority) / 5) - 20;
		/* Realtime range returned:
		 * -20 - highest priority
		 *  0  - lowest priority
		 */
	}
	else
	{
		prio->is_realtime = MALI_FALSE;
		prio->priority = (current->static_prio - MAX_RT_PRIO) - 20;
		/* NICE range returned:
		 * -20 - highest priority
		 * +19 - lowest priority
		 */
	}
}

#endif /* _OSK_ARCH_CREDENTIALS_H_ */
