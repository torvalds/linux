/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#include <linux/version.h>
#include <linux/types.h>
#include <linux/module.h>
#include <csp/tmrHw.h>

#include <mach/timer.h>
/* The core.c file initializes timers 1 and 3 as a linux clocksource. */
/* The real time clock should probably be the real linux clocksource. */
/* In the meantime, this file should agree with core.c as to the */
/* profiling timer. If the clocksource is moved to rtc later, then */
/* we can init the profiling timer here instead. */

/* Timer 1 provides 25MHz resolution syncrhonized to scheduling and APM timing */
/* Timer 3 provides bus freqeuncy sychronized to ACLK, but spread spectrum will */
/* affect synchronization with scheduling and APM timing. */

#define PROF_TIMER 1

timer_tick_rate_t timer_get_tick_rate(void)
{
	return tmrHw_getCountRate(PROF_TIMER);
}

timer_tick_count_t timer_get_tick_count(void)
{
	return tmrHw_GetCurrentCount(PROF_TIMER);	/* change downcounter to upcounter */
}

timer_msec_t timer_ticks_to_msec(timer_tick_count_t ticks)
{
	static int tickRateMsec;

	if (tickRateMsec == 0) {
		tickRateMsec = timer_get_tick_rate() / 1000;
	}

	return ticks / tickRateMsec;
}

timer_msec_t timer_get_msec(void)
{
	return timer_ticks_to_msec(timer_get_tick_count());
}

EXPORT_SYMBOL(timer_get_tick_count);
EXPORT_SYMBOL(timer_ticks_to_msec);
EXPORT_SYMBOL(timer_get_tick_rate);
EXPORT_SYMBOL(timer_get_msec);
