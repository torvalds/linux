/*
 * Copyright (C) 2010, 2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_time.c
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#include "mali_osk.h"
#include <linux/jiffies.h>
#include <linux/time.h>
#include <asm/delay.h>

int	_mali_osk_time_after( u32 ticka, u32 tickb )
{
	return time_after((unsigned long)ticka, (unsigned long)tickb);
}

u32	_mali_osk_time_mstoticks( u32 ms )
{
	return msecs_to_jiffies(ms);
}

u32	_mali_osk_time_tickstoms( u32 ticks )
{
	return jiffies_to_msecs(ticks);
}

u32	_mali_osk_time_tickcount( void )
{
	return jiffies;
}

void _mali_osk_time_ubusydelay( u32 usecs )
{
	udelay(usecs);
}

u64 _mali_osk_time_get_ns( void )
{
	struct timespec tsval;
	getnstimeofday(&tsval);
	return (u64)timespec_to_ns(&tsval);
}
