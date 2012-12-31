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

#ifndef _OSK_ARCH_POWER_H_
#define _OSK_ARCH_POWER_H_

#include <linux/pm_runtime.h>

OSK_STATIC_INLINE osk_power_request_result osk_power_request(osk_power_info *info, osk_power_state state)
{
	osk_power_request_result request_result = OSK_POWER_REQUEST_FINISHED;

	OSK_ASSERT(NULL != info);

	switch(state)
	{
		case OSK_POWER_STATE_OFF:
			/* request OS to suspend device*/
			break;
		case OSK_POWER_STATE_IDLE:
			/* request OS to idle device */
			break;
		case OSK_POWER_STATE_ACTIVE:
			/* request OS to resume device */
			break;
	}
	return request_result;
}

#endif
