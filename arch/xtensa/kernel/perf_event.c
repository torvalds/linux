/*
 * Xtensa Performance Monitor Module driver
 * See Tensilica Debug User's Guide for PMU registers documentation.
 *
 * Copyright (C) 2015 Cadence Design Systems Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>

#include <asm/processor.h>
#include <asm/stacktrace.h>

/* Global control/status for all perf counters */
#define XTENSA_PMU_PMG			0x1000
/* Perf counter values */
#define XTENSA_PMU_PM(i)		(0x1080 + (i) * 4)
/* Perf counter control registers */
#define XTENSA_PMU_PMCTRL(i)		(0x1100 + (i) * 4)
/* Perf counter status registers */
#define XTENSA_PMU_PMSTAT(i)		(0x1180 + (i) * 4)

#define XTENSA_PMU_PMG_PMEN		0x1

#define XTENSA_PMU_COUNTER_MASK		0xffffffffULL
#define XTENSA_PMU_COUNTER_MAX		0x7fffffff

#define XTENSA_PMU_PMCTRL_INTEN		0x00000001
#define XTENSA_PMU_PMCTRL_KRNLCNT	0x00000008
#define XTENSA_PMU_PMCTRL_TRACELEVEL	0x000000f0
#define XTENSA_PMU_PMCTRL_SELECT_SHIFT	8
#define XTENSA_PMU_PMCTRL_SELECT	0x00001f00
#define XTENSA_PMU_PMCTRL_MASK_SHIFT	16
#define XTENSA_PMU_PMCTRL_MASK		0xffff0000

#define XTENSA_PMU_MASK(select, mask) \
	(((select) << XTENSA_PMU_PMCTRL_SELECT_SHIFT) | \
	 ((mask) << XTENSA_PMU_PMCTRL_MASK_SHIFT) | \
	 XTENSA_PMU_PMCTRL_TRACELEVEL | \
	 XTENSA_PMU_PMCTRL_INTEN)

#define XTENSA_PMU_PMSTAT_OVFL		0x00000001
#define XTENSA_PMU_PMSTAT_INTASRT	0x00000010

struct xtensa_pmu_events {
	/* Array of events currently on this core */
	struct perf_event *event[XCHAL_NUM_PERF_COUNTERS];
	/* Bitmap of used hardware counters */
	unsigned long used_mask[BITS_TO_LONGS(XCHAL_NUM_PERF_COUNTERS)];
};
static DEFINE_PER_CPU(struct xtensa_pmu_events, xtensa_pmu_events);

static const u32 xtensa_hw_ctl[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= XTENSA_PMU_MASK(0, 0x1),
	[PERF_COUNT_HW_INSTRUCTIONS]		= XTENSA_PMU_MASK(2, 0xffff),
	[PERF_COUNT_HW_CACHE_REFERENCES]	= XTENSA_PMU_MASK(10, 0x1),
	[PERF_COUNT_HW_CACHE_MISSES]		= XTENSA_PMU_MASK(12, 0x1),
	/* Taken and non-taken branches + taken loop ends */
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= XTENSA_PMU_MASK(2, 0x490),
	/* Instruction-related + other global stall cycles */
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= XTENSA_PMU_MASK(4, 0x1ff),
	/* Data-related global stall cycles */
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= XTENSA_PMU_MASK(3, 0x1ff),
};

#define C(_x) PERF_COUNT_HW_CACHE_##_x

static const u32 xtensa_cache_ctl[][C(OP_MAX)][C(RESULT_MAX)] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= XTENSA_PMU_MASK(10, 0x1),
			[C(RESULT_MISS)]	= XTENSA_PMU_MASK(10, 0x2),
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= XTENSA_PMU_MASK(11, 0x1),
			[C(RESULT_MISS)]	= XTENSA_PMU_MASK(11, 0x2),
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= XTENSA_PMU_MASK(8, 0x1),
			[C(RESULT_MISS)]	= XTENSA_PMU_MASK(8, 0x2),
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= XTENSA_PMU_MASK(9, 0x1),
			[C(RESULT_MISS)]	= XTENSA_PMU_MASK(9, 0x8),
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= XTENSA_PMU_MASK(7, 0x1),
			[C(RESULT_MISS)]	= XTENSA_PMU_MASK(7, 0x8),
		},
	},
};

static int xtensa_pmu_cache_event(u64 config)
{
	unsigned int cache_type, cache_op, cache_result;
	int ret;

	cache_type = (config >>  0) & 0xff;
	cache_op = (config >>  8) & 0xff;
	cache_result = (config >> 16) & 0xff;

	if (cache_type >= ARRAY_SIZE(xtensa_cache_ctl) ||
	    cache_op >= C(OP_MAX) ||
	    cache_result >= C(RESULT_MAX))
		return -EINVAL;

	ret = xtensa_cache_ctl[cache_type][cache_op][cache_result];

	if (ret == 0)
		return -EINVAL;

	return ret;
}

static inline uint32_t xtensa_pmu_read_counter(int idx)
{
	return get_er(XTENSA_PMU_PM(idx));
}

static inline void xtensa_pmu_write_counter(int idx, uint32_t v)
{
	set_er(v, XTENSA_PMU_PM(idx));
}

static void xtensa_perf_event_update(struct perf_event *event,
				     struct hw_perf_event *hwc, int idx)
{
	uint64_t prev_raw_count, new_raw_count;
	int64_t delta;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = xtensa_pmu_read_counter(event->hw.idx);
	} while (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
				 new_raw_count) != prev_raw_count);

	delta = (new_raw_count - prev_raw_count) & XTENSA_PMU_COUNTER_MASK;

	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);
}

static bool xtensa_perf_event_set_period(struct perf_event *event,
					 struct hw_perf_event *hwc, int idx)
{
	bool rc = false;
	s64 left;

	if (!is_sampling_event(event)) {
		left = XTENSA_PMU_COUNTER_MAX;
	} else {
		s64 period = hwc->sample_period;

		left = local64_read(&hwc->period_left);
		if (left <= -period) {
			left = period;
			local64_set(&hwc->period_left, left);
			hwc->last_period = period;
			rc = true;
		} else if (left <= 0) {
			left += period;
			local64_set(&hwc->period_left, left);
			hwc->last_period = period;
			rc = true;
		}
		if (left > XTENSA_PMU_COUNTER_MAX)
			left = XTENSA_PMU_COUNTER_MAX;
	}

	local64_set(&hwc->prev_count, -left);
	xtensa_pmu_write_counter(idx, -left);
	perf_event_update_userpage(event);

	return rc;
}

static void xtensa_pmu_enable(struct pmu *pmu)
{
	set_er(get_er(XTENSA_PMU_PMG) | XTENSA_PMU_PMG_PMEN, XTENSA_PMU_PMG);
}

static void xtensa_pmu_disable(struct pmu *pmu)
{
	set_er(get_er(XTENSA_PMU_PMG) & ~XTENSA_PMU_PMG_PMEN, XTENSA_PMU_PMG);
}

static int xtensa_pmu_event_init(struct perf_event *event)
{
	int ret;

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		if (event->attr.config >= ARRAY_SIZE(xtensa_hw_ctl) ||
		    xtensa_hw_ctl[event->attr.config] == 0)
			return -EINVAL;
		event->hw.config = xtensa_hw_ctl[event->attr.config];
		return 0;

	case PERF_TYPE_HW_CACHE:
		ret = xtensa_pmu_cache_event(event->attr.config);
		if (ret < 0)
			return ret;
		event->hw.config = ret;
		return 0;

	case PERF_TYPE_RAW:
		/* Not 'previous counter' select */
		if ((event->attr.config & XTENSA_PMU_PMCTRL_SELECT) ==
		    (1 << XTENSA_PMU_PMCTRL_SELECT_SHIFT))
			return -EINVAL;
		event->hw.config = (event->attr.config &
				    (XTENSA_PMU_PMCTRL_KRNLCNT |
				     XTENSA_PMU_PMCTRL_TRACELEVEL |
				     XTENSA_PMU_PMCTRL_SELECT |
				     XTENSA_PMU_PMCTRL_MASK)) |
			XTENSA_PMU_PMCTRL_INTEN;
		return 0;

	default:
		return -ENOENT;
	}
}

/*
 * Starts/Stops a counter present on the PMU. The PMI handler
 * should stop the counter when perf_event_overflow() returns
 * !0. ->start() will be used to continue.
 */
static void xtensa_pmu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (WARN_ON_ONCE(idx == -1))
		return;

	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));
		xtensa_perf_event_set_period(event, hwc, idx);
	}

	hwc->state = 0;

	set_er(hwc->config, XTENSA_PMU_PMCTRL(idx));
}

static void xtensa_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (!(hwc->state & PERF_HES_STOPPED)) {
		set_er(0, XTENSA_PMU_PMCTRL(idx));
		set_er(get_er(XTENSA_PMU_PMSTAT(idx)),
		       XTENSA_PMU_PMSTAT(idx));
		hwc->state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) &&
	    !(event->hw.state & PERF_HES_UPTODATE)) {
		xtensa_perf_event_update(event, &event->hw, idx);
		event->hw.state |= PERF_HES_UPTODATE;
	}
}

/*
 * Adds/Removes a counter to/from the PMU, can be done inside
 * a transaction, see the ->*_txn() methods.
 */
static int xtensa_pmu_add(struct perf_event *event, int flags)
{
	struct xtensa_pmu_events *ev = this_cpu_ptr(&xtensa_pmu_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (__test_and_set_bit(idx, ev->used_mask)) {
		idx = find_first_zero_bit(ev->used_mask,
					  XCHAL_NUM_PERF_COUNTERS);
		if (idx == XCHAL_NUM_PERF_COUNTERS)
			return -EAGAIN;

		__set_bit(idx, ev->used_mask);
		hwc->idx = idx;
	}
	ev->event[idx] = event;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		xtensa_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	return 0;
}

static void xtensa_pmu_del(struct perf_event *event, int flags)
{
	struct xtensa_pmu_events *ev = this_cpu_ptr(&xtensa_pmu_events);

	xtensa_pmu_stop(event, PERF_EF_UPDATE);
	__clear_bit(event->hw.idx, ev->used_mask);
	perf_event_update_userpage(event);
}

static void xtensa_pmu_read(struct perf_event *event)
{
	xtensa_perf_event_update(event, &event->hw, event->hw.idx);
}

static int callchain_trace(struct stackframe *frame, void *data)
{
	struct perf_callchain_entry *entry = data;

	perf_callchain_store(entry, frame->pc);
	return 0;
}

void perf_callchain_kernel(struct perf_callchain_entry *entry,
			   struct pt_regs *regs)
{
	xtensa_backtrace_kernel(regs, sysctl_perf_event_max_stack,
				callchain_trace, NULL, entry);
}

void perf_callchain_user(struct perf_callchain_entry *entry,
			 struct pt_regs *regs)
{
	xtensa_backtrace_user(regs, sysctl_perf_event_max_stack,
			      callchain_trace, entry);
}

void perf_event_print_debug(void)
{
	unsigned long flags;
	unsigned i;

	local_irq_save(flags);
	pr_info("CPU#%d: PMG: 0x%08lx\n", smp_processor_id(),
		get_er(XTENSA_PMU_PMG));
	for (i = 0; i < XCHAL_NUM_PERF_COUNTERS; ++i)
		pr_info("PM%d: 0x%08lx, PMCTRL%d: 0x%08lx, PMSTAT%d: 0x%08lx\n",
			i, get_er(XTENSA_PMU_PM(i)),
			i, get_er(XTENSA_PMU_PMCTRL(i)),
			i, get_er(XTENSA_PMU_PMSTAT(i)));
	local_irq_restore(flags);
}

irqreturn_t xtensa_pmu_irq_handler(int irq, void *dev_id)
{
	irqreturn_t rc = IRQ_NONE;
	struct xtensa_pmu_events *ev = this_cpu_ptr(&xtensa_pmu_events);
	unsigned i;

	for (i = find_first_bit(ev->used_mask, XCHAL_NUM_PERF_COUNTERS);
	     i < XCHAL_NUM_PERF_COUNTERS;
	     i = find_next_bit(ev->used_mask, XCHAL_NUM_PERF_COUNTERS, i + 1)) {
		uint32_t v = get_er(XTENSA_PMU_PMSTAT(i));
		struct perf_event *event = ev->event[i];
		struct hw_perf_event *hwc = &event->hw;
		u64 last_period;

		if (!(v & XTENSA_PMU_PMSTAT_OVFL))
			continue;

		set_er(v, XTENSA_PMU_PMSTAT(i));
		xtensa_perf_event_update(event, hwc, i);
		last_period = hwc->last_period;
		if (xtensa_perf_event_set_period(event, hwc, i)) {
			struct perf_sample_data data;
			struct pt_regs *regs = get_irq_regs();

			perf_sample_data_init(&data, 0, last_period);
			if (perf_event_overflow(event, &data, regs))
				xtensa_pmu_stop(event, 0);
		}

		rc = IRQ_HANDLED;
	}
	return rc;
}

static struct pmu xtensa_pmu = {
	.pmu_enable = xtensa_pmu_enable,
	.pmu_disable = xtensa_pmu_disable,
	.event_init = xtensa_pmu_event_init,
	.add = xtensa_pmu_add,
	.del = xtensa_pmu_del,
	.start = xtensa_pmu_start,
	.stop = xtensa_pmu_stop,
	.read = xtensa_pmu_read,
};

static void xtensa_pmu_setup(void)
{
	unsigned i;

	set_er(0, XTENSA_PMU_PMG);
	for (i = 0; i < XCHAL_NUM_PERF_COUNTERS; ++i) {
		set_er(0, XTENSA_PMU_PMCTRL(i));
		set_er(get_er(XTENSA_PMU_PMSTAT(i)), XTENSA_PMU_PMSTAT(i));
	}
}

static int xtensa_pmu_notifier(struct notifier_block *self,
			       unsigned long action, void *data)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		xtensa_pmu_setup();
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}

static int __init xtensa_pmu_init(void)
{
	int ret;
	int irq = irq_create_mapping(NULL, XCHAL_PROFILING_INTERRUPT);

	perf_cpu_notifier(xtensa_pmu_notifier);
#if XTENSA_FAKE_NMI
	enable_irq(irq);
#else
	ret = request_irq(irq, xtensa_pmu_irq_handler, IRQF_PERCPU,
			  "pmu", NULL);
	if (ret < 0)
		return ret;
#endif

	ret = perf_pmu_register(&xtensa_pmu, "cpu", PERF_TYPE_RAW);
	if (ret)
		free_irq(irq, NULL);

	return ret;
}
early_initcall(xtensa_pmu_init);
