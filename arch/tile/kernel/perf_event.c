/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 *
 * Perf_events support for Tile processor.
 *
 * This code is based upon the x86 perf event
 * code, which is:
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2009 Jaswinder Singh Rajput
 *  Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra
 *  Copyright (C) 2009 Intel Corporation, <markus.t.metzger@intel.com>
 *  Copyright (C) 2009 Google, Inc., Stephane Eranian
 */

#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/mutex.h>
#include <linux/bitmap.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/perf_event.h>
#include <linux/atomic.h>
#include <asm/traps.h>
#include <asm/stack.h>
#include <asm/pmc.h>
#include <hv/hypervisor.h>

#define TILE_MAX_COUNTERS	4

#define PERF_COUNT_0_IDX	0
#define PERF_COUNT_1_IDX	1
#define AUX_PERF_COUNT_0_IDX	2
#define AUX_PERF_COUNT_1_IDX	3

struct cpu_hw_events {
	int			n_events;
	struct perf_event	*events[TILE_MAX_COUNTERS]; /* counter order */
	struct perf_event	*event_list[TILE_MAX_COUNTERS]; /* enabled
								order */
	int			assign[TILE_MAX_COUNTERS];
	unsigned long		active_mask[BITS_TO_LONGS(TILE_MAX_COUNTERS)];
	unsigned long		used_mask;
};

/* TILE arch specific performance monitor unit */
struct tile_pmu {
	const char	*name;
	int		version;
	const int	*hw_events;	/* generic hw events table */
	/* generic hw cache events table */
	const int	(*cache_events)[PERF_COUNT_HW_CACHE_MAX]
				       [PERF_COUNT_HW_CACHE_OP_MAX]
				       [PERF_COUNT_HW_CACHE_RESULT_MAX];
	int		(*map_hw_event)(u64);	 /*method used to map
						  hw events */
	int		(*map_cache_event)(u64); /*method used to map
						  cache events */

	u64		max_period;		/* max sampling period */
	u64		cntval_mask;		/* counter width mask */
	int		cntval_bits;		/* counter width */
	int		max_events;		/* max generic hw events
						in map */
	int		num_counters;		/* number base + aux counters */
	int		num_base_counters;	/* number base counters */
};

DEFINE_PER_CPU(u64, perf_irqs);
static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

#define TILE_OP_UNSUPP		(-1)

#ifndef __tilegx__
/* TILEPro hardware events map */
static const int tile_hw_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= 0x01, /* ONE */
	[PERF_COUNT_HW_INSTRUCTIONS]		= 0x06, /* MP_BUNDLE_RETIRED */
	[PERF_COUNT_HW_CACHE_REFERENCES]	= TILE_OP_UNSUPP,
	[PERF_COUNT_HW_CACHE_MISSES]		= TILE_OP_UNSUPP,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x16, /*
					  MP_CONDITIONAL_BRANCH_ISSUED */
	[PERF_COUNT_HW_BRANCH_MISSES]		= 0x14, /*
					  MP_CONDITIONAL_BRANCH_MISSPREDICT */
	[PERF_COUNT_HW_BUS_CYCLES]		= TILE_OP_UNSUPP,
};
#else
/* TILEGx hardware events map */
static const int tile_hw_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= 0x181, /* ONE */
	[PERF_COUNT_HW_INSTRUCTIONS]		= 0xdb, /* INSTRUCTION_BUNDLE */
	[PERF_COUNT_HW_CACHE_REFERENCES]	= TILE_OP_UNSUPP,
	[PERF_COUNT_HW_CACHE_MISSES]		= TILE_OP_UNSUPP,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0xd9, /*
						COND_BRANCH_PRED_CORRECT */
	[PERF_COUNT_HW_BRANCH_MISSES]		= 0xda, /*
						COND_BRANCH_PRED_INCORRECT */
	[PERF_COUNT_HW_BUS_CYCLES]		= TILE_OP_UNSUPP,
};
#endif

#define C(x) PERF_COUNT_HW_CACHE_##x

/*
 * Generalized hw caching related hw_event table, filled
 * in on a per model basis. A value of -1 means
 * 'not supported', any other value means the
 * raw hw_event ID.
 */
#ifndef __tilegx__
/* TILEPro hardware cache event map */
static const int tile_cache_event_map[PERF_COUNT_HW_CACHE_MAX]
				     [PERF_COUNT_HW_CACHE_OP_MAX]
				     [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
[C(L1D)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = 0x21, /* RD_MISS */
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = 0x22, /* WR_MISS */
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
[C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0x12, /* MP_ICACHE_HIT_ISSUED */
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
[C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
[C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0x1d, /* TLB_CNT */
		[C(RESULT_MISS)] = 0x20, /* TLB_EXCEPTION */
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
[C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0x13, /* MP_ITLB_HIT_ISSUED */
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
[C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
};
#else
/* TILEGx hardware events map */
static const int tile_cache_event_map[PERF_COUNT_HW_CACHE_MAX]
				     [PERF_COUNT_HW_CACHE_OP_MAX]
				     [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
[C(L1D)] = {
	/*
	 * Like some other architectures (e.g. ARM), the performance
	 * counters don't differentiate between read and write
	 * accesses/misses, so this isn't strictly correct, but it's the
	 * best we can do. Writes and reads get combined.
	 */
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = 0x44, /* RD_MISS */
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = 0x45, /* WR_MISS */
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
[C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
[C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
[C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0x40, /* TLB_CNT */
		[C(RESULT_MISS)] = 0x43, /* TLB_EXCEPTION */
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = 0x40, /* TLB_CNT */
		[C(RESULT_MISS)] = 0x43, /* TLB_EXCEPTION */
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
[C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = 0xd4, /* ITLB_MISS_INT */
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = 0xd4, /* ITLB_MISS_INT */
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
[C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = TILE_OP_UNSUPP,
		[C(RESULT_MISS)] = TILE_OP_UNSUPP,
	},
},
};
#endif

static atomic_t tile_active_events;
static DEFINE_MUTEX(perf_intr_reserve_mutex);

static int tile_map_hw_event(u64 config);
static int tile_map_cache_event(u64 config);

static int tile_pmu_handle_irq(struct pt_regs *regs, int fault);

/*
 * To avoid new_raw_count getting larger then pre_raw_count
 * in tile_perf_event_update(), we limit the value of max_period to 2^31 - 1.
 */
static const struct tile_pmu tilepmu = {
#ifndef __tilegx__
	.name = "tilepro",
#else
	.name = "tilegx",
#endif
	.max_events = ARRAY_SIZE(tile_hw_event_map),
	.map_hw_event = tile_map_hw_event,
	.hw_events = tile_hw_event_map,
	.map_cache_event = tile_map_cache_event,
	.cache_events = &tile_cache_event_map,
	.cntval_bits = 32,
	.cntval_mask = (1ULL << 32) - 1,
	.max_period = (1ULL << 31) - 1,
	.num_counters = TILE_MAX_COUNTERS,
	.num_base_counters = TILE_BASE_COUNTERS,
};

static const struct tile_pmu *tile_pmu __read_mostly;

/*
 * Check whether perf event is enabled.
 */
int tile_perf_enabled(void)
{
	return atomic_read(&tile_active_events) != 0;
}

/*
 * Read Performance Counters.
 */
static inline u64 read_counter(int idx)
{
	u64 val = 0;

	/* __insn_mfspr() only takes an immediate argument */
	switch (idx) {
	case PERF_COUNT_0_IDX:
		val = __insn_mfspr(SPR_PERF_COUNT_0);
		break;
	case PERF_COUNT_1_IDX:
		val = __insn_mfspr(SPR_PERF_COUNT_1);
		break;
	case AUX_PERF_COUNT_0_IDX:
		val = __insn_mfspr(SPR_AUX_PERF_COUNT_0);
		break;
	case AUX_PERF_COUNT_1_IDX:
		val = __insn_mfspr(SPR_AUX_PERF_COUNT_1);
		break;
	default:
		WARN_ON_ONCE(idx > AUX_PERF_COUNT_1_IDX ||
				idx < PERF_COUNT_0_IDX);
	}

	return val;
}

/*
 * Write Performance Counters.
 */
static inline void write_counter(int idx, u64 value)
{
	/* __insn_mtspr() only takes an immediate argument */
	switch (idx) {
	case PERF_COUNT_0_IDX:
		__insn_mtspr(SPR_PERF_COUNT_0, value);
		break;
	case PERF_COUNT_1_IDX:
		__insn_mtspr(SPR_PERF_COUNT_1, value);
		break;
	case AUX_PERF_COUNT_0_IDX:
		__insn_mtspr(SPR_AUX_PERF_COUNT_0, value);
		break;
	case AUX_PERF_COUNT_1_IDX:
		__insn_mtspr(SPR_AUX_PERF_COUNT_1, value);
		break;
	default:
		WARN_ON_ONCE(idx > AUX_PERF_COUNT_1_IDX ||
				idx < PERF_COUNT_0_IDX);
	}
}

/*
 * Enable performance event by setting
 * Performance Counter Control registers.
 */
static inline void tile_pmu_enable_event(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg, mask;
	int shift, idx = hwc->idx;

	/*
	 * prevent early activation from tile_pmu_start() in hw_perf_enable
	 */

	if (WARN_ON_ONCE(idx == -1))
		return;

	if (idx < tile_pmu->num_base_counters)
		cfg = __insn_mfspr(SPR_PERF_COUNT_CTL);
	else
		cfg = __insn_mfspr(SPR_AUX_PERF_COUNT_CTL);

	switch (idx) {
	case PERF_COUNT_0_IDX:
	case AUX_PERF_COUNT_0_IDX:
		mask = TILE_EVENT_MASK;
		shift = 0;
		break;
	case PERF_COUNT_1_IDX:
	case AUX_PERF_COUNT_1_IDX:
		mask = TILE_EVENT_MASK << 16;
		shift = 16;
		break;
	default:
		WARN_ON_ONCE(idx < PERF_COUNT_0_IDX ||
			idx > AUX_PERF_COUNT_1_IDX);
		return;
	}

	/* Clear mask bits to enable the event. */
	cfg &= ~mask;
	cfg |= hwc->config << shift;

	if (idx < tile_pmu->num_base_counters)
		__insn_mtspr(SPR_PERF_COUNT_CTL, cfg);
	else
		__insn_mtspr(SPR_AUX_PERF_COUNT_CTL, cfg);
}

/*
 * Disable performance event by clearing
 * Performance Counter Control registers.
 */
static inline void tile_pmu_disable_event(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg, mask;
	int idx = hwc->idx;

	if (idx == -1)
		return;

	if (idx < tile_pmu->num_base_counters)
		cfg = __insn_mfspr(SPR_PERF_COUNT_CTL);
	else
		cfg = __insn_mfspr(SPR_AUX_PERF_COUNT_CTL);

	switch (idx) {
	case PERF_COUNT_0_IDX:
	case AUX_PERF_COUNT_0_IDX:
		mask = TILE_PLM_MASK;
		break;
	case PERF_COUNT_1_IDX:
	case AUX_PERF_COUNT_1_IDX:
		mask = TILE_PLM_MASK << 16;
		break;
	default:
		WARN_ON_ONCE(idx < PERF_COUNT_0_IDX ||
			idx > AUX_PERF_COUNT_1_IDX);
		return;
	}

	/* Set mask bits to disable the event. */
	cfg |= mask;

	if (idx < tile_pmu->num_base_counters)
		__insn_mtspr(SPR_PERF_COUNT_CTL, cfg);
	else
		__insn_mtspr(SPR_AUX_PERF_COUNT_CTL, cfg);
}

/*
 * Propagate event elapsed time into the generic event.
 * Can only be executed on the CPU where the event is active.
 * Returns the delta events processed.
 */
static u64 tile_perf_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int shift = 64 - tile_pmu->cntval_bits;
	u64 prev_raw_count, new_raw_count;
	u64 oldval;
	int idx = hwc->idx;
	u64 delta;

	/*
	 * Careful: an NMI might modify the previous event value.
	 *
	 * Our tactic to handle this is to first atomically read and
	 * exchange a new raw count - then add that new-prev delta
	 * count to the generic event atomically:
	 */
again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = read_counter(idx);

	oldval = local64_cmpxchg(&hwc->prev_count, prev_raw_count,
				 new_raw_count);
	if (oldval != prev_raw_count)
		goto again;

	/*
	 * Now we have the new raw value and have updated the prev
	 * timestamp already. We can now calculate the elapsed delta
	 * (event-)time and add that to the generic event.
	 *
	 * Careful, not all hw sign-extends above the physical width
	 * of the count.
	 */
	delta = (new_raw_count << shift) - (prev_raw_count << shift);
	delta >>= shift;

	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);

	return new_raw_count;
}

/*
 * Set the next IRQ period, based on the hwc->period_left value.
 * To be called with the event disabled in hw:
 */
static int tile_event_set_period(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	s64 left = local64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int ret = 0;

	/*
	 * If we are way outside a reasonable range then just skip forward:
	 */
	if (unlikely(left <= -period)) {
		left = period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (unlikely(left <= 0)) {
		left += period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}
	if (left > tile_pmu->max_period)
		left = tile_pmu->max_period;

	/*
	 * The hw event starts counting from this event offset,
	 * mark it to be able to extra future deltas:
	 */
	local64_set(&hwc->prev_count, (u64)-left);

	write_counter(idx, (u64)(-left) & tile_pmu->cntval_mask);

	perf_event_update_userpage(event);

	return ret;
}

/*
 * Stop the event but do not release the PMU counter
 */
static void tile_pmu_stop(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (__test_and_clear_bit(idx, cpuc->active_mask)) {
		tile_pmu_disable_event(event);
		cpuc->events[hwc->idx] = NULL;
		WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
		hwc->state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		/*
		 * Drain the remaining delta count out of a event
		 * that we are disabling:
		 */
		tile_perf_event_update(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

/*
 * Start an event (without re-assigning counter)
 */
static void tile_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx = event->hw.idx;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (WARN_ON_ONCE(idx == -1))
		return;

	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));
		tile_event_set_period(event);
	}

	event->hw.state = 0;

	cpuc->events[idx] = event;
	__set_bit(idx, cpuc->active_mask);

	unmask_pmc_interrupts();

	tile_pmu_enable_event(event);

	perf_event_update_userpage(event);
}

/*
 * Add a single event to the PMU.
 *
 * The event is added to the group of enabled events
 * but only if it can be scehduled with existing events.
 */
static int tile_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc;
	unsigned long mask;
	int b, max_cnt;

	hwc = &event->hw;

	/*
	 * We are full.
	 */
	if (cpuc->n_events == tile_pmu->num_counters)
		return -ENOSPC;

	cpuc->event_list[cpuc->n_events] = event;
	cpuc->n_events++;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (!(flags & PERF_EF_START))
		hwc->state |= PERF_HES_ARCH;

	/*
	 * Find first empty counter.
	 */
	max_cnt = tile_pmu->num_counters;
	mask = ~cpuc->used_mask;

	/* Find next free counter. */
	b = find_next_bit(&mask, max_cnt, 0);

	/* Should not happen. */
	if (WARN_ON_ONCE(b == max_cnt))
		return -ENOSPC;

	/*
	 * Assign counter to event.
	 */
	event->hw.idx = b;
	__set_bit(b, &cpuc->used_mask);

	/*
	 * Start if requested.
	 */
	if (flags & PERF_EF_START)
		tile_pmu_start(event, PERF_EF_RELOAD);

	return 0;
}

/*
 * Delete a single event from the PMU.
 *
 * The event is deleted from the group of enabled events.
 * If it is the last event, disable PMU interrupt.
 */
static void tile_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int i;

	/*
	 * Remove event from list, compact list if necessary.
	 */
	for (i = 0; i < cpuc->n_events; i++) {
		if (cpuc->event_list[i] == event) {
			while (++i < cpuc->n_events)
				cpuc->event_list[i-1] = cpuc->event_list[i];
			--cpuc->n_events;
			cpuc->events[event->hw.idx] = NULL;
			__clear_bit(event->hw.idx, &cpuc->used_mask);
			tile_pmu_stop(event, PERF_EF_UPDATE);
			break;
		}
	}
	/*
	 * If there are no events left, then mask PMU interrupt.
	 */
	if (cpuc->n_events == 0)
		mask_pmc_interrupts();
	perf_event_update_userpage(event);
}

/*
 * Propagate event elapsed time into the event.
 */
static inline void tile_pmu_read(struct perf_event *event)
{
	tile_perf_event_update(event);
}

/*
 * Map generic events to Tile PMU.
 */
static int tile_map_hw_event(u64 config)
{
	if (config >= tile_pmu->max_events)
		return -EINVAL;
	return tile_pmu->hw_events[config];
}

/*
 * Map generic hardware cache events to Tile PMU.
 */
static int tile_map_cache_event(u64 config)
{
	unsigned int cache_type, cache_op, cache_result;
	int code;

	if (!tile_pmu->cache_events)
		return -ENOENT;

	cache_type = (config >>  0) & 0xff;
	if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
		return -EINVAL;

	cache_op = (config >>  8) & 0xff;
	if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
		return -EINVAL;

	cache_result = (config >> 16) & 0xff;
	if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	code = (*tile_pmu->cache_events)[cache_type][cache_op][cache_result];
	if (code == TILE_OP_UNSUPP)
		return -EINVAL;

	return code;
}

static void tile_event_destroy(struct perf_event *event)
{
	if (atomic_dec_return(&tile_active_events) == 0)
		release_pmc_hardware();
}

static int __tile_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	int code;

	switch (attr->type) {
	case PERF_TYPE_HARDWARE:
		code = tile_pmu->map_hw_event(attr->config);
		break;
	case PERF_TYPE_HW_CACHE:
		code = tile_pmu->map_cache_event(attr->config);
		break;
	case PERF_TYPE_RAW:
		code = attr->config & TILE_EVENT_MASK;
		break;
	default:
		/* Should not happen. */
		return -EOPNOTSUPP;
	}

	if (code < 0)
		return code;

	hwc->config = code;
	hwc->idx = -1;

	if (attr->exclude_user)
		hwc->config |= TILE_CTL_EXCL_USER;

	if (attr->exclude_kernel)
		hwc->config |= TILE_CTL_EXCL_KERNEL;

	if (attr->exclude_hv)
		hwc->config |= TILE_CTL_EXCL_HV;

	if (!hwc->sample_period) {
		hwc->sample_period = tile_pmu->max_period;
		hwc->last_period = hwc->sample_period;
		local64_set(&hwc->period_left, hwc->sample_period);
	}
	event->destroy = tile_event_destroy;
	return 0;
}

static int tile_event_init(struct perf_event *event)
{
	int err = 0;
	perf_irq_t old_irq_handler = NULL;

	if (atomic_inc_return(&tile_active_events) == 1)
		old_irq_handler = reserve_pmc_hardware(tile_pmu_handle_irq);

	if (old_irq_handler) {
		pr_warn("PMC hardware busy (reserved by oprofile)\n");

		atomic_dec(&tile_active_events);
		return -EBUSY;
	}

	switch (event->attr.type) {
	case PERF_TYPE_RAW:
	case PERF_TYPE_HARDWARE:
	case PERF_TYPE_HW_CACHE:
		break;

	default:
		return -ENOENT;
	}

	err = __tile_event_init(event);
	if (err) {
		if (event->destroy)
			event->destroy(event);
	}
	return err;
}

static struct pmu tilera_pmu = {
	.event_init	= tile_event_init,
	.add		= tile_pmu_add,
	.del		= tile_pmu_del,

	.start		= tile_pmu_start,
	.stop		= tile_pmu_stop,

	.read		= tile_pmu_read,
};

/*
 * PMU's IRQ handler, PMU has 2 interrupts, they share the same handler.
 */
int tile_pmu_handle_irq(struct pt_regs *regs, int fault)
{
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct perf_event *event;
	struct hw_perf_event *hwc;
	u64 val;
	unsigned long status;
	int bit;

	__this_cpu_inc(perf_irqs);

	if (!atomic_read(&tile_active_events))
		return 0;

	status = pmc_get_overflow();
	pmc_ack_overflow(status);

	for_each_set_bit(bit, &status, tile_pmu->num_counters) {

		event = cpuc->events[bit];

		if (!event)
			continue;

		if (!test_bit(bit, cpuc->active_mask))
			continue;

		hwc = &event->hw;

		val = tile_perf_event_update(event);
		if (val & (1ULL << (tile_pmu->cntval_bits - 1)))
			continue;

		perf_sample_data_init(&data, 0, event->hw.last_period);
		if (!tile_event_set_period(event))
			continue;

		if (perf_event_overflow(event, &data, regs))
			tile_pmu_stop(event, 0);
	}

	return 0;
}

static bool __init supported_pmu(void)
{
	tile_pmu = &tilepmu;
	return true;
}

int __init init_hw_perf_events(void)
{
	supported_pmu();
	perf_pmu_register(&tilera_pmu, "cpu", PERF_TYPE_RAW);
	return 0;
}
arch_initcall(init_hw_perf_events);

/* Callchain handling code. */

/*
 * Tile specific backtracing code for perf_events.
 */
static inline void perf_callchain(struct perf_callchain_entry_ctx *entry,
		    struct pt_regs *regs)
{
	struct KBacktraceIterator kbt;
	unsigned int i;

	/*
	 * Get the address just after the "jalr" instruction that
	 * jumps to the handler for a syscall.  When we find this
	 * address in a backtrace, we silently ignore it, which gives
	 * us a one-step backtrace connection from the sys_xxx()
	 * function in the kernel to the xxx() function in libc.
	 * Otherwise, we lose the ability to properly attribute time
	 * from the libc calls to the kernel implementations, since
	 * oprofile only considers PCs from backtraces a pair at a time.
	 */
	unsigned long handle_syscall_pc = handle_syscall_link_address();

	KBacktraceIterator_init(&kbt, NULL, regs);
	kbt.profile = 1;

	/*
	 * The sample for the pc is already recorded.  Now we are adding the
	 * address of the callsites on the stack.  Our iterator starts
	 * with the frame of the (already sampled) call site.  If our
	 * iterator contained a "return address" field, we could have just
	 * used it and wouldn't have needed to skip the first
	 * frame.  That's in effect what the arm and x86 versions do.
	 * Instead we peel off the first iteration to get the equivalent
	 * behavior.
	 */

	if (KBacktraceIterator_end(&kbt))
		return;
	KBacktraceIterator_next(&kbt);

	/*
	 * Set stack depth to 16 for user and kernel space respectively, that
	 * is, total 32 stack frames.
	 */
	for (i = 0; i < 16; ++i) {
		unsigned long pc;
		if (KBacktraceIterator_end(&kbt))
			break;
		pc = kbt.it.pc;
		if (pc != handle_syscall_pc)
			perf_callchain_store(entry, pc);
		KBacktraceIterator_next(&kbt);
	}
}

void perf_callchain_user(struct perf_callchain_entry_ctx *entry,
		    struct pt_regs *regs)
{
	perf_callchain(entry, regs);
}

void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
		      struct pt_regs *regs)
{
	perf_callchain(entry, regs);
}
