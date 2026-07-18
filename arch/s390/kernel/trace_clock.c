// SPDX-License-Identifier: GPL-2.0
#include <linux/trace_clock.h>
#include <linux/timex.h>
/*
 * trace_clock_s390_tod(): trace clock based on the s390 TOD clock
 *
 * Unlike the other clocks, this is not in nanoseconds.
 */
u64 notrace trace_clock_s390_tod(void)
{
	return get_tod_clock();
}
