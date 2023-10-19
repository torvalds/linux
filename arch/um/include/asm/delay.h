/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_DELAY_H
#define __UM_DELAY_H
#include <asm-generic/delay.h>
#include <linux/time-internal.h>

static inline void um_ndelay(unsigned long nsecs)
{
	if (time_travel_mode == TT_MODE_INFCPU ||
	    time_travel_mode == TT_MODE_EXTERNAL) {
		time_travel_ndelay(nsecs);
		return;
	}
	ndelay(nsecs);
}
#undef ndelay
#define ndelay(n) um_ndelay(n)

static inline void um_udelay(unsigned long usecs)
{
	if (time_travel_mode == TT_MODE_INFCPU ||
	    time_travel_mode == TT_MODE_EXTERNAL) {
		time_travel_ndelay(1000 * usecs);
		return;
	}
	udelay(usecs);
}
#undef udelay
#define udelay(n) um_udelay(n)
#endif /* __UM_DELAY_H */
