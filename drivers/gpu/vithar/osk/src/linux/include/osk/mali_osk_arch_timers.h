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
 * @file mali_osk_arch_timers.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_ARCH_TIMERS_H
#define _OSK_ARCH_TIMERS_H

#if MALI_LICENSE_IS_GPL
	#include "mali_osk_arch_timers_gpl.h"
#else
	#include "mali_osk_arch_timers_commercial.h"
#endif

#endif /* _OSK_ARCH_TIMERS_H_ */

