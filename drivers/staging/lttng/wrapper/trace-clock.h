/*
 * Copyright (C) 2011 Mathieu Desnoyers (mathieu.desnoyers@efficios.com)
 *
 * Contains LTTng trace clock mapping to LTTng trace clock or mainline monotonic
 * clock. This wrapper depends on CONFIG_HIGH_RES_TIMERS=y.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#ifndef _LTT_TRACE_CLOCK_H
#define _LTT_TRACE_CLOCK_H

#ifdef CONFIG_HAVE_TRACE_CLOCK
#include <linux/trace-clock.h>
#else /* CONFIG_HAVE_TRACE_CLOCK */

#include <linux/hardirq.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/hrtimer.h>

static inline u64 trace_clock_monotonic_wrapper(void)
{
	ktime_t ktime;

	/*
	 * Refuse to trace from NMIs with this wrapper, because an NMI could
	 * nest over the xtime write seqlock and deadlock.
	 */
	if (in_nmi())
		return (u64) -EIO;

	ktime = ktime_get();
	return (u64) ktime.tv64;
}

static inline u32 trace_clock_read32(void)
{
	return (u32) trace_clock_monotonic_wrapper();
}

static inline u64 trace_clock_read64(void)
{
	return (u64) trace_clock_monotonic_wrapper();
}

static inline u64 trace_clock_frequency(void)
{
	return (u64)NSEC_PER_SEC;
}

static inline u32 trace_clock_freq_scale(void)
{
	return 1;
}

static inline int get_trace_clock(void)
{
	printk(KERN_WARNING "LTTng: Using mainline kernel monotonic clock.\n");
	printk(KERN_WARNING "  * NMIs will not be traced,\n");
	printk(KERN_WARNING "  * expect significant performance degradation compared to the\n");
	printk(KERN_WARNING "    LTTng trace clocks.\n");
	printk(KERN_WARNING "Integration of the LTTng 0.x trace clocks into LTTng 2.0 is planned\n");
	printk(KERN_WARNING "in a near future.\n");

	return 0;
}

static inline void put_trace_clock(void)
{
}

#endif /* CONFIG_HAVE_TRACE_CLOCK */

#endif /* _LTT_TRACE_CLOCK_H */
