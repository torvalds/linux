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

#ifndef _OSK_ARCH_TIME_H
#define _OSK_ARCH_TIME_H

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

OSK_STATIC_INLINE osk_ticks osk_time_now(void)
{
	return jiffies;
}

OSK_STATIC_INLINE u32 osk_time_mstoticks(u32 ms)
{
	return msecs_to_jiffies(ms);
}

OSK_STATIC_INLINE u32 osk_time_elapsed(osk_ticks ticka, osk_ticks tickb)
{
	return jiffies_to_msecs((long)tickb - (long)ticka);
}

OSK_STATIC_INLINE mali_bool osk_time_after(osk_ticks ticka, osk_ticks tickb)
{
	return time_after(ticka, tickb);
}

OSK_STATIC_INLINE void osk_gettimeofday(osk_timeval *tv)
{
	struct timespec ts;
	getnstimeofday(&ts);

	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec/1000;
}

#endif /* _OSK_ARCH_TIME_H_ */
