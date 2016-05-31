/*
 * Linux performance counter support for ARC700 series
 *
 * Copyright (C) 2013-2015 Synopsys, Inc. (www.synopsys.com)
 *
 * This code is inspired by the perf support of various other architectures.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <asm/arcregs.h>
#include <asm/stacktrace.h>

struct arc_pmu {
	struct pmu	pmu;
	unsigned int	irq;
	int		n_counters;
	u64		max_period;
	int		ev_hw_idx[PERF_COUNT_ARC_HW_MAX];
};

struct arc_pmu_cpu {
	/*
	 * A 1 bit for an index indicates that the counter is being used for
	 * an event. A 0 means that the counter can be used.
	 */
	unsigned long	used_mask[BITS_TO_LONGS(ARC_PERF_MAX_COUNTERS)];

	/*
	 * The events that are active on the PMU for the given index.
	 */
	struct perf_event *act_counter[ARC_PERF_MAX_COUNTERS];
};

struct arc_callchain_trace {
	int depth;
	void *perf_stuff;
};

static int callchain_trace(unsigned int addr, void *data)
{
	struct arc_callchain_trace *ctrl = data;
	struct perf_callchain_entry_ctx *entry = ctrl->perf_stuff;
	perf_callchain_store(entry, addr);

	if (ctrl->depth++ < 3)
		return 0;

	return -1;
}

void
perf_callchain_kernel(struct perf_callchain_entry_ctx *entry, struct pt_regs *regs)
{
	struct arc_callchain_trace ctrl = {
		.depth = 0,
		.perf_stuff = entry,
	};

	arc_unwind_core(NULL, regs, callchain_trace, &ctrl);
}

void
perf_callchain_user(struct perf_callchain_entry_ctx *entry, struct pt_regs *regs)
{
	/*
	 * User stack can't be unwound trivially with kernel dwarf unwinder
	 * So for now just record the user PC
	 */
	perf_callchain_store(entry, instruction_pointer(regs));
}

static struct arc_pmu *arc_pmu;
static DEFINE_PER_CPU(struct arc_pmu_cpu, arc_pmu_cpu);

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
	uint64_t prev_raw_count = local64_read(&hwc->prev_count);
	uint64_t new_raw_count = arc_pmu_read_counter(idx);
	int64_t delta = new_raw_count - prev_raw_count;

	/*
	 * We don't afaraid of hwc->prev_count changing beneath our feet
	 * because there's no way for us to re-enter this function anytime.
	 */
	local64_set(&hwc->prev_count, new_raw_count);
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

	if (!is_sampling_event(event)) {
		hwc->sample_period  = arc_pmu->max_period;
		hwc->last_period = hwc->sample_period;
		local64_set(&hwc->period_left, hwc->sample_period);
	}

	hwc->config = 0;

	if (is_isa_arcv2()) {
		/* "exclude user" means "count only kernel" */
		if (event->attr.exclude_user)
			hwc->config |= ARC_REG_PCT_CONFIG_KERN;

		/* "exclude kernel" means "count only user" */
		if (event->attr.exclude_kernel)
			hwc->config |= ARC_REG_PCT_CONFIG_USER;
	}

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		if (event->attr.config >= PERF_COUNT_HW_MAX)
			return -ENOENT;
		if (arc_pmu->ev_hw_idx[event->attr.config] < 0)
			return -ENOENT;
		hwc->config |= arc_pmu->ev_hw_idx[event->attr.config];
		pr_debug("init event %d with h/w %d \'%s\'\n",
			 (int) event->attr.config, (int) hwc->config,
			 arc_pmu_ev_hw_map[event->attr.config]);
		return 0;

	case PERF_TYPE_HW_CACHE:
		ret = arc_pmu_cache_event(event->attr.config);
		if (ret < 0)
			return ret;
		hwc->config |= arc_pmu->ev_hw_idx[ret];
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

static int arc_pmu_event_set_period(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	s64 left = local64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int idx = hwc->idx;
	int overflow = 0;
	u64 value;

	if (unlikely(left <= -period)) {
		/* left underflowed by more than period. */
		left = period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		overflow = 1;
	} else	if (unlikely(left <= 0)) {
		/* left underflowed by less than period. */
		left += period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		overflow = 1;
	}

	if (left > arc_pmu->max_period)
		left = arc_pmu->max_period;

	value = arc_pmu->max_period - left;
	local64_set(&hwc->prev_count, value);

	/* Select counter */
	write_aux_reg(ARC_REG_PCT_INDEX, idx);

	/* Write value */
	write_aux_reg(ARC_REG_PCT_COUNTL, (u32)value);
	write_aux_reg(ARC_REG_PCT_COUNTH, (value >> 32));

	perf_event_update_userpage(event);

	return overflow;
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
		WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));

	hwc->state = 0;

	arc_pmu_event_set_period(event);

	/* Enable interrupt for this counter */
	if (is_sampling_event(event))
		write_aux_reg(ARC_REG_PCT_INT_CTRL,
			      read_aux_reg(ARC_REG_PCT_INT_CTRL) | (1 << idx));

	/* enable ARC pmu here */
	write_aux_reg(ARC_REG_PCT_INDEX, idx);		/* counter # */
	write_aux_reg(ARC_REG_PCT_CONFIG, hwc->config);	/* condition */
}

static void arc_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	/* Disable interrupt for this counter */
	if (is_sampling_event(event)) {
		/*
		 * Reset interrupt flag by writing of 1. This is required
		 * to make sure pending interrupt was not left.
		 */
		write_aux_reg(ARC_REG_PCT_INT_ACT, 1 << idx);
		write_aux_reg(ARC_REG_PCT_INT_CTRL,
			      read_aux_reg(ARC_REG_PCT_INT_CTRL) & ~(1 << idx));
	}

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
	struct arc_pmu_cpu *pmu_cpu = this_cpu_ptr(&arc_pmu_cpu);

	arc_pmu_stop(event, PERF_EF_UPDATE);
	__clear_bit(event->hw.idx, pmu_cpu->used_mask);

	pmu_cpu->act_counter[event->hw.idx] = 0;

	perf_event_update_userpage(event);
}

/* allocate hardware counter and optionally start counting */
static int arc_pmu_add(struct perf_event *event, int flags)
{
	struct arc_pmu_cpu *pmu_cpu = this_cpu_ptr(&arc_pmu_cpu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (__test_and_set_bit(idx, pmu_cpu->used_mask)) {
		idx = find_first_zero_bit(pmu_cpu->used_mask,
					  arc_pmu->n_counters);
		if (idx == arc_pmu->n_counters)
			return -EAGAIN;

		__set_bit(idx, pmu_cpu->used_mask);
		hwc->idx = idx;
	}

	write_aux_reg(ARC_REG_PCT_INDEX, idx);

	pmu_cpu->act_counter[idx] = event;

	if (is_sampling_event(event)) {
		/* Mimic full counter overflow as other arches do */
		write_aux_reg(ARC_REG_PCT_INT_CNTL, (u32)arc_pmu->max_period);
		write_aux_reg(ARC_REG_PCT_INT_CNTH,
			      (arc_pmu->max_period >> 32));
	}

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

#ifdef CONFIG_ISA_ARCV2
static irqreturn_t arc_pmu_intr(int irq, void *dev)
{
	struct perf_sample_data data;
	struct arc_pmu_cpu *pmu_cpu = this_cpu_ptr(&arc_pmu_cpu);
	struct pt_regs *regs;
	int active_ints;
	int idx;

	arc_pmu_disable(&arc_pmu->pmu);

	active_ints = read_aux_reg(ARC_REG_PCT_INT_ACT);

	regs = get_irq_regs();

	for (idx = 0; idx < arc_pmu->n_counters; idx++) {
		struct perf_event *event = pmu_cpu->act_counter[idx];
		struct hw_perf_event *hwc;

		if (!(active_ints & (1 << idx)))
			continue;

		/* Reset interrupt flag by writing of 1 */
		write_aux_reg(ARC_REG_PCT_INT_ACT, 1 << idx);

		/*
		 * On reset of "interrupt active" bit corresponding
		 * "interrupt enable" bit gets automatically reset as well.
		 * Now we need to re-enable interrupt for the counter.
		 */
		write_aux_reg(ARC_REG_PCT_INT_CTRL,
			read_aux_reg(ARC_REG_PCT_INT_CTRL) | (1 << idx));

		hwc = &event->hw;

		WARN_ON_ONCE(hwc->idx != idx);

		arc_perf_event_update(event, &event->hw, event->hw.idx);
		perf_sample_data_init(&data, 0, hwc->last_period);
		if (!arc_pmu_event_set_period(event))
			continue;

		if (perf_event_overflow(event, &data, regs))
			arc_pmu_stop(event, 0);
	}

	arc_pmu_enable(&arc_pmu->pmu);

	return IRQ_HANDLED;
}
#else

static irqreturn_t arc_pmu_intr(int irq, void *dev)
{
	return IRQ_NONE;
}

#endif /* CONFIG_ISA_ARCV2 */

static void arc_cpu_pmu_irq_init(void *data)
{
	int irq = *(int *)data;

	enable_percpu_irq(irq, IRQ_TYPE_NONE);

	/* Clear all pending interrupt flags */
	write_aux_reg(ARC_REG_PCT_INT_ACT, 0xffffffff);
}

static int arc_pmu_device_probe(struct platform_device *pdev)
{
	struct arc_reg_pct_build pct_bcr;
	struct arc_reg_cc_build cc_bcr;
	int i, j, has_interrupts;
	int counter_size;	/* in bits */

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
	BUG_ON(pct_bcr.c > ARC_PERF_MAX_COUNTERS);

	READ_BCR(ARC_REG_CC_BUILD, cc_bcr);
	BUG_ON(!cc_bcr.v); /* Counters exist but No countable conditions ? */

	arc_pmu = devm_kzalloc(&pdev->dev, sizeof(struct arc_pmu), GFP_KERNEL);
	if (!arc_pmu)
		return -ENOMEM;

	has_interrupts = is_isa_arcv2() ? pct_bcr.i : 0;

	arc_pmu->n_counters = pct_bcr.c;
	counter_size = 32 + (pct_bcr.s << 4);

	arc_pmu->max_period = (1ULL << counter_size) / 2 - 1ULL;

	pr_info("ARC perf\t: %d counters (%d bits), %d conditions%s\n",
		arc_pmu->n_counters, counter_size, cc_bcr.c,
		has_interrupts ? ", [overflow IRQ support]":"");

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

	if (has_interrupts) {
		int irq = platform_get_irq(pdev, 0);

		if (irq < 0) {
			pr_err("Cannot get IRQ number for the platform\n");
			return -ENODEV;
		}

		arc_pmu->irq = irq;

		/* intc map function ensures irq_set_percpu_devid() called */
		request_percpu_irq(irq, arc_pmu_intr, "ARC perf counters",
				   this_cpu_ptr(&arc_pmu_cpu));

		on_each_cpu(arc_cpu_pmu_irq_init, &irq, 1);

	} else
		arc_pmu->pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;

	return perf_pmu_register(&arc_pmu->pmu, pdev->name, PERF_TYPE_RAW);
}

#ifdef CONFIG_OF
static const struct of_device_id arc_pmu_match[] = {
	{ .compatible = "snps,arc700-pct" },
	{ .compatible = "snps,archs-pct" },
	{},
};
MODULE_DEVICE_TABLE(of, arc_pmu_match);
#endif

static struct platform_driver arc_pmu_driver = {
	.driver	= {
		.name		= "arc-pct",
		.of_match_table = of_match_ptr(arc_pmu_match),
	},
	.probe		= arc_pmu_device_probe,
};

module_platform_driver(arc_pmu_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mischa Jonker <mjonker@synopsys.com>");
MODULE_DESCRIPTION("ARC PMU driver");
