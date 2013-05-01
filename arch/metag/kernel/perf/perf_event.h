/*
 * Meta performance counter support.
 *  Copyright (C) 2012 Imagination Technologies Ltd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef METAG_PERF_EVENT_H_
#define METAG_PERF_EVENT_H_

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/perf_event.h>

/* For performance counter definitions */
#include <asm/metag_mem.h>

/*
 * The Meta core has two performance counters, with 24-bit resolution. Newer
 * cores generate an overflow interrupt on transition from 0xffffff to 0.
 *
 * Each counter consists of the counter id, hardware thread id, and the count
 * itself; each counter can be assigned to multiple hardware threads at any
 * one time, with the returned count being an aggregate of events. A small
 * number of events are thread global, i.e. they count the aggregate of all
 * threads' events, regardless of the thread selected.
 *
 * Newer cores can store an arbitrary 24-bit number in the counter, whereas
 * older cores will clear the counter bits on write.
 *
 * We also have a pseudo-counter in the form of the thread active cycles
 * counter (which, incidentally, is also bound to
 */

#define MAX_HWEVENTS		3
#define MAX_PERIOD		((1UL << 24) - 1)
#define METAG_INST_COUNTER	(MAX_HWEVENTS - 1)

/**
 * struct cpu_hw_events - a processor core's performance events
 * @events:	an array of perf_events active for a given index.
 * @used_mask:	a bitmap of in-use counters.
 * @pmu_lock:	a perf counter lock
 *
 * This is a per-cpu/core structure that maintains a record of its
 * performance counters' state.
 */
struct cpu_hw_events {
	struct perf_event	*events[MAX_HWEVENTS];
	unsigned long		used_mask[BITS_TO_LONGS(MAX_HWEVENTS)];
	raw_spinlock_t		pmu_lock;
};

/**
 * struct metag_pmu - the Meta PMU structure
 * @pmu:		core pmu structure
 * @name:		pmu name
 * @version:		core version
 * @handle_irq:		overflow interrupt handler
 * @enable:		enable a counter
 * @disable:		disable a counter
 * @read:		read the value of a counter
 * @write:		write a value to a counter
 * @event_map:		kernel event to counter event id map
 * @cache_events:	kernel cache counter to core cache counter map
 * @max_period:		maximum value of the counter before overflow
 * @max_events:		maximum number of counters available at any one time
 * @active_events:	number of active counters
 * @reserve_mutex:	counter reservation mutex
 *
 * This describes the main functionality and data used by the performance
 * event core.
 */
struct metag_pmu {
	struct pmu	pmu;
	const char	*name;
	u32		version;
	irqreturn_t	(*handle_irq)(int irq_num, void *dev);
	void		(*enable)(struct hw_perf_event *evt, int idx);
	void		(*disable)(struct hw_perf_event *evt, int idx);
	u64		(*read)(int idx);
	void		(*write)(int idx, u32 val);
	int		(*event_map)(int idx);
	const int	(*cache_events)[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX];
	u32		max_period;
	int		max_events;
	atomic_t	active_events;
	struct mutex	reserve_mutex;
};

/* Convenience macros for accessing the perf counters */
/* Define some convenience accessors */
#define PERF_COUNT(x)	(PERF_COUNT0 + (sizeof(u64) * (x)))
#define PERF_ICORE(x)	(PERF_ICORE0 + (sizeof(u64) * (x)))
#define PERF_CHAN(x)	(PERF_CHAN0 + (sizeof(u64) * (x)))

/* Cache index macros */
#define C(x) PERF_COUNT_HW_CACHE_##x
#define CACHE_OP_UNSUPPORTED	0xfffe
#define CACHE_OP_NONSENSE	0xffff

#endif
