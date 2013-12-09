#ifndef _LTTNG_TRACE_CLOCK_H
#define _LTTNG_TRACE_CLOCK_H

/*
 * wrapper/trace-clock.h
 *
 * Contains LTTng trace clock mapping to LTTng trace clock or mainline monotonic
 * clock. This wrapper depends on CONFIG_HIGH_RES_TIMERS=y.
 *
 * Copyright (C) 2011-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifdef CONFIG_HAVE_TRACE_CLOCK
#include <linux/trace-clock.h>
#else /* CONFIG_HAVE_TRACE_CLOCK */

#include <linux/hardirq.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/version.h>
#include "../lttng-kernel-version.h"
#include "random.h"

#if LTTNG_KERNEL_RANGE(3,10,0, 3,10,14) || LTTNG_KERNEL_RANGE(3,11,0, 3,11,3)
#error "Linux kernels 3.10 and 3.11 introduce a deadlock in the timekeeping subsystem. Fixed by commit 7bd36014460f793c19e7d6c94dab67b0afcfcb7f \"timekeeping: Fix HRTICK related deadlock from ntp lock changes\" in Linux."
#endif

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
	return ktime_to_ns(ktime);
}

static inline u32 trace_clock_read32(void)
{
	return (u32) trace_clock_monotonic_wrapper();
}

static inline u64 trace_clock_read64(void)
{
	return (u64) trace_clock_monotonic_wrapper();
}

static inline u64 trace_clock_freq(void)
{
	return (u64) NSEC_PER_SEC;
}

static inline int trace_clock_uuid(char *uuid)
{
	return wrapper_get_bootid(uuid);
}

static inline int get_trace_clock(void)
{
	/*
	 * LTTng: Using mainline kernel monotonic clock. NMIs will not be
	 * traced, and expect significant performance degradation compared to
	 * the LTTng trace clocks. Integration of the LTTng 0.x trace clocks
	 * into LTTng 2.0 is planned in a near future.
	 */
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

#endif /* _LTTNG_TRACE_CLOCK_H */
