/*
 * Linux performance counter support for ARC700 series
 *
 * Copyright (C) 2013 Synopsys, Inc. (www.synopsys.com)
 *
 * This code is inspired by the perf support of various other architectures.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <asm/arcregs.h>
#include <asm/stacktrace.h>

struct arc_pmu {
	struct pmu	pmu;
	int		counter_size;	/* in bits */
	int		n_counters;
	unsigned long	used_mask[BITS_TO_LONGS(ARC_PMU_MAX_HWEVENTS)];
	int		ev_hw_idx[PERF_COUNT_ARC_HW_MAX];
};

struct arc_callchain_trace {
	int depth;
	void *perf_stuff;
};

static int callchain_trace(unsigned int addr, void *data)
{
	struct arc_callchain_trace *ctrl = data;
	struct perf_callchain_entry *entry = ctrl->perf_stuff;
	perf_callchain_store(entry, addr);

	if (ctrl->depth++ < 3)
		return 0;

	return -1;
}

void
perf_callchain_kernel(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	struct arc_callchain_trace ctrl = {
		.depth = 0,
		.perf_stuff = entry,
	};

	arc_unwind_core(NULL, regs, callchain_trace, &ctrl);
}

void
perf_callchain_user(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	/*
	 * User stack can't be unwound trivially with kernel dwarf unwinder
	 * So for now just record the user PC
	 */
	perf_callchain_store(entry, instruction_pointer(regs));
}

static struct arc_pmu *arc_pmu;

/* read counter #idx; note that counter# != event# on ARC! */
static uint64_t arc_pmu_read_counter(int idx)
{
	uint32_t tmp;
	uint64_t result;

	/*
	 * ARC supports making 'snapshots' of the counters, so we don't
	 * need to care about counters wrapping to 0 underneath our feet
	 */
	write_aux_reg(ARC_REG_PCT_INDEX, idx);
	tmp = read_aux_reg(ARC_REG_PCT_CONTROL);
	write_aux_reg(ARC_REG_PCT_CONTROL, tmp | ARC_REG_PCT_CONTROL_SN);
	result = (uint64_t) (read_aux_reg(ARC_REG_PCT_SNAPH)) << 32;
	result |= read_aux_reg(ARC_REG_PCT_SNAPL);

	return result;
}

static void arc_perf_event_update(struct perf_event *event,
				  struct hw_perf_event *hwc, int idx)
{
	uint64_t prev_raw_count, new_raw_count;
	int64_t delta;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = arc_pmu_read_counter(idx);
	} while (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
				 new_raw_count) != prev_raw_count);

	delta = (new_raw_count - prev_raw_count) &
		((1ULL << arc_pmu->counter_size) - 1ULL);

	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);
}

static void arc_pmu_read(struct perf_event *event)
{
	arc_perf_event_update(event, &event->hw, event->hw.idx);
}

static int arc_pmu_cache_event(u64 config)
{
	unsigned int cache_type, cache_op, cache_result;
	int ret;

	cache_type	= (config >>  0) & 0xff;
	cache_op	= (config >>  8) & 0xff;
	cache_result	= (config >> 16) & 0xff;
	if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
		return -EINVAL;
	if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
		return -EINVAL;
	if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	ret = arc_pmu_cache_map[cache_type][cache_op][cache_result];

	if (ret == CACHE_OP_UNSUPPORTED)
		return -ENOENT;

	pr_debug("init cache event: type/op/result %d/%d/%d with h/w %d \'%s\'\n",
		 cache_type, cache_op, cache_result, ret,
		 arc_pmu_ev_hw_map[ret]);

	return ret;
}

/* initializes hw_perf_event structure if event is supported */
static int arc_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int ret;

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		if (event->attr.config >= PERF_COUNT_HW_MAX)
			return -ENOENT;
		if (arc_pmu->ev_hw_idx[event->attr.config] < 0)
			return -ENOENT;
		hwc->config = arc_pmu->ev_hw_idx[event->attr.config];
		pr_debug("init event %d with h/w %d \'%s\'\n",
			 (int) event->attr.config, (int) hwc->config,
			 arc_pmu_ev_hw_map[event->attr.config]);
		return 0;
	case PERF_TYPE_HW_CACHE:
		ret = arc_pmu_cache_event(event->attr.config);
		if (ret < 0)
			return ret;
		hwc->config = arc_pmu->ev_hw_idx[ret];
		return 0;
	default:
		return -ENOENT;
	}
}

/* starts all counters */
static void arc_pmu_enable(struct pmu *pmu)
{
	uint32_t tmp;
	tmp = read_aux_reg(ARC_REG_PCT_CONTROL);
	write_aux_reg(ARC_REG_PCT_CONTROL, (tmp & 0xffff0000) | 0x1);
}

/* stops all counters */
static void arc_pmu_disable(struct pmu *pmu)
{
	uint32_t tmp;
	tmp = read_aux_reg(ARC_REG_PCT_CONTROL);
	write_aux_reg(ARC_REG_PCT_CONTROL, (tmp & 0xffff0000) | 0x0);
}

/*
 * Assigns hardware counter to hardware condition.
 * Note that there is no separate start/stop mechanism;
 * stopping is achieved by assigning the 'never' condition
 */
static void arc_pmu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (WARN_ON_ONCE(idx == -1))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	event->hw.state = 0;

	/* enable ARC pmu here */
	write_aux_reg(ARC_REG_PCT_INDEX, idx);
	write_aux_reg(ARC_REG_PCT_CONFIG, hwc->config);
}

static void arc_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (!(event->hw.state & PERF_HES_STOPPED)) {
		/* stop ARC pmu here */
		write_aux_reg(ARC_REG_PCT_INDEX, idx);

		/* condition code #0 is always "never" */
		write_aux_reg(ARC_REG_PCT_CONFIG, 0);

		event->hw.state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) &&
	    !(event->hw.state & PERF_HES_UPTODATE)) {
		arc_perf_event_update(event, &event->hw, idx);
		event->hw.state |= PERF_HES_UPTODATE;
	}
}

static void arc_pmu_del(struct perf_event *event, int flags)
{
	arc_pmu_stop(event, PERF_EF_UPDATE);
	__clear_bit(event->hw.idx, arc_pmu->used_mask);

	perf_event_update_userpage(event);
}

/* allocate hardware counter and optionally start counting */
static int arc_pmu_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (__test_and_set_bit(idx, arc_pmu->used_mask)) {
		idx = find_first_zero_bit(arc_pmu->used_mask,
					  arc_pmu->n_counters);
		if (idx == arc_pmu->n_counters)
			return -EAGAIN;

		__set_bit(idx, arc_pmu->used_mask);
		hwc->idx = idx;
	}

	write_aux_reg(ARC_REG_PCT_INDEX, idx);
	write_aux_reg(ARC_REG_PCT_CONFIG, 0);
	write_aux_reg(ARC_REG_PCT_COUNTL, 0);
	write_aux_reg(ARC_REG_PCT_COUNTH, 0);
	local64_set(&hwc->prev_count, 0);

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (flags & PERF_EF_START)
		arc_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);

	return 0;
}

static int arc_pmu_device_probe(struct platform_device *pdev)
{
	struct arc_pmu *arc_pmu;
	struct arc_reg_pct_build pct_bcr;
	struct arc_reg_cc_build cc_bcr;
	int i, j, ret;

	union cc_name {
		struct {
			uint32_t word0, word1;
			char sentinel;
		} indiv;
		char str[9];
	} cc_name;


	READ_BCR(ARC_REG_PCT_BUILD, pct_bcr);
	if (!pct_bcr.v) {
		pr_err("This core does not have performance counters!\n");
		return -ENODEV;
	}
	BUG_ON(pct_bcr.c > ARC_PMU_MAX_HWEVENTS);

	READ_BCR(ARC_REG_CC_BUILD, cc_bcr);
	BUG_ON(!cc_bcr.v); /* Counters exist but No countable conditions ? */

	arc_pmu = devm_kzalloc(&pdev->dev, sizeof(struct arc_pmu), GFP_KERNEL);
	if (!arc_pmu)
		return -ENOMEM;

	arc_pmu->n_counters = pct_bcr.c;
	arc_pmu->counter_size = 32 + (pct_bcr.s << 4);

	pr_info("ARC perf\t: %d counters (%d bits), %d countable conditions\n",
		arc_pmu->n_counters, arc_pmu->counter_size, cc_bcr.c);

	cc_name.str[8] = 0;
	for (i = 0; i < PERF_COUNT_ARC_HW_MAX; i++)
		arc_pmu->ev_hw_idx[i] = -1;

	/* loop thru all available h/w condition indexes */
	for (j = 0; j < cc_bcr.c; j++) {
		write_aux_reg(ARC_REG_CC_INDEX, j);
		cc_name.indiv.word0 = read_aux_reg(ARC_REG_CC_NAME0);
		cc_name.indiv.word1 = read_aux_reg(ARC_REG_CC_NAME1);

		/* See if it has been mapped to a perf event_id */
		for (i = 0; i < ARRAY_SIZE(arc_pmu_ev_hw_map); i++) {
			if (arc_pmu_ev_hw_map[i] &&
			    !strcmp(arc_pmu_ev_hw_map[i], cc_name.str) &&
			    strlen(arc_pmu_ev_hw_map[i])) {
				pr_debug("mapping perf event %2d to h/w event \'%8s\' (idx %d)\n",
					 i, cc_name.str, j);
				arc_pmu->ev_hw_idx[i] = j;
			}
		}
	}

	arc_pmu->pmu = (struct pmu) {
		.pmu_enable	= arc_pmu_enable,
		.pmu_disable	= arc_pmu_disable,
		.event_init	= arc_pmu_event_init,
		.add		= arc_pmu_add,
		.del		= arc_pmu_del,
		.start		= arc_pmu_start,
		.stop		= arc_pmu_stop,
		.read		= arc_pmu_read,
	};

	/* ARC 700 PMU does not support sampling events */
	arc_pmu->pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;

	ret = perf_pmu_register(&arc_pmu->pmu, pdev->name, PERF_TYPE_RAW);

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id arc_pmu_match[] = {
	{ .compatible = "snps,arc700-pct" },
	{},
};
MODULE_DEVICE_TABLE(of, arc_pmu_match);
#endif

static struct platform_driver arc_pmu_driver = {
	.driver	= {
		.name		= "arc700-pct",
		.of_match_table = of_match_ptr(arc_pmu_match),
	},
	.probe		= arc_pmu_device_probe,
};

module_platform_driver(arc_pmu_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mischa Jonker <mjonker@synopsys.com>");
MODULE_DESCRIPTION("ARC PMU driver");
