// SPDX-License-Identifier: GPL-2.0+
//
// Linux performance counter support for ARC CPUs.
// This code is inspired by the perf support of various other architectures.
//
// Copyright (C) 2013-2018 Synopsys, Inc. (www.synopsys.com)

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <asm/arcregs.h>
#include <asm/stacktrace.h>

/* HW holds 8 symbols + one for null terminator */
#define ARCPMU_EVENT_NAME_LEN	9

enum arc_pmu_attr_groups {
	ARCPMU_ATTR_GR_EVENTS,
	ARCPMU_ATTR_GR_FORMATS,
	ARCPMU_NR_ATTR_GR
};

struct arc_pmu_raw_event_entry {
	char name[ARCPMU_EVENT_NAME_LEN];
};

struct arc_pmu {
	struct pmu	pmu;
	unsigned int	irq;
	int		n_counters;
	int		n_events;
	u64		max_period;
	int		ev_hw_idx[PERF_COUNT_ARC_HW_MAX];

	struct arc_pmu_raw_event_entry	*raw_entry;
	struct attribute		**attrs;
	struct perf_pmu_events_attr	*attr;
	const struct attribute_group	*attr_groups[ARCPMU_NR_ATTR_GR + 1];
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

void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
			   struct pt_regs *regs)
{
	struct arc_callchain_trace ctrl = {
		.depth = 0,
		.perf_stuff = entry,
	};

	arc_unwind_core(NULL, regs, callchain_trace, &ctrl);
}

void perf_callchain_user(struct perf_callchain_entry_ctx *entry,
			 struct pt_regs *regs)
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
static u64 arc_pmu_read_counter(int idx)
{
	u32 tmp;
	u64 result;

	/*
	 * ARC supports making 'snapshots' of the counters, so we don't
	 * need to care about counters wrapping to 0 underneath our feet
	 */
	write_aux_reg(ARC_REG_PCT_INDEX, idx);
	tmp = read_aux_reg(ARC_REG_PCT_CONTROL);
	write_aux_reg(ARC_REG_PCT_CONTROL, tmp | ARC_REG_PCT_CONTROL_SN);
	result = (u64) (read_aux_reg(ARC_REG_PCT_SNAPH)) << 32;
	result |= read_aux_reg(ARC_REG_PCT_SNAPL);

	return result;
}

static void arc_perf_event_update(struct perf_event *event,
				  struct hw_perf_event *hwc, int idx)
{
	u64 prev_raw_count = local64_read(&hwc->prev_count);
	u64 new_raw_count = arc_pmu_read_counter(idx);
	s64 delta = new_raw_count - prev_raw_count;

	/*
	 * We aren't afraid of hwc->prev_count changing beneath our feet
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
		hwc->sample_period = arc_pmu->max_period;
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
		pr_debug("init event %d with h/w %08x \'%s\'\n",
			 (int)event->attr.config, (int)hwc->config,
			 arc_pmu_ev_hw_map[event->attr.config]);
		return 0;

	case PERF_TYPE_HW_CACHE:
		ret = arc_pmu_cache_event(event->attr.config);
		if (ret < 0)
			return ret;
		hwc->config |= arc_pmu->ev_hw_idx[ret];
		pr_debug("init cache event with h/w %08x \'%s\'\n",
			 (int)hwc->config, arc_pmu_ev_hw_map[ret]);
		return 0;

	case PERF_TYPE_RAW:
		if (event->attr.config >= arc_pmu->n_events)
			return -ENOENT;

		hwc->config |= event->attr.config;
		pr_debug("init raw event with idx %lld \'%s\'\n",
			 event->attr.config,
			 arc_pmu->raw_entry[event->attr.config].name);

		return 0;

	default:
		return -ENOENT;
	}
}

/* starts all counters */
static void arc_pmu_enable(struct pmu *pmu)
{
	u32 tmp;
	tmp = read_aux_reg(ARC_REG_PCT_CONTROL);
	write_aux_reg(ARC_REG_PCT_CONTROL, (tmp & 0xffff0000) | 0x1);
}

/* stops all counters */
static void arc_pmu_disable(struct pmu *pmu)
{
	u32 tmp;
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
	} else if (unlikely(left <= 0)) {
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
	write_aux_reg(ARC_REG_PCT_COUNTL, lower_32_bits(value));
	write_aux_reg(ARC_REG_PCT_COUNTH, upper_32_bits(value));

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
			      read_aux_reg(ARC_REG_PCT_INT_CTRL) | BIT(idx));

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
		write_aux_reg(ARC_REG_PCT_INT_ACT, BIT(idx));
		write_aux_reg(ARC_REG_PCT_INT_CTRL,
			      read_aux_reg(ARC_REG_PCT_INT_CTRL) & ~BIT(idx));
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

	idx = ffz(pmu_cpu->used_mask[0]);
	if (idx == arc_pmu->n_counters)
		return -EAGAIN;

	__set_bit(idx, pmu_cpu->used_mask);
	hwc->idx = idx;

	write_aux_reg(ARC_REG_PCT_INDEX, idx);

	pmu_cpu->act_counter[idx] = event;

	if (is_sampling_event(event)) {
		/* Mimic full counter overflow as other arches do */
		write_aux_reg(ARC_REG_PCT_INT_CNTL,
			      lower_32_bits(arc_pmu->max_period));
		write_aux_reg(ARC_REG_PCT_INT_CNTH,
			      upper_32_bits(arc_pmu->max_period));
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
	unsigned int active_ints;
	int idx;

	arc_pmu_disable(&arc_pmu->pmu);

	active_ints = read_aux_reg(ARC_REG_PCT_INT_ACT);
	if (!active_ints)
		goto done;

	regs = get_irq_regs();

	do {
		struct perf_event *event;
		struct hw_perf_event *hwc;

		idx = __ffs(active_ints);

		/* Reset interrupt flag by writing of 1 */
		write_aux_reg(ARC_REG_PCT_INT_ACT, BIT(idx));

		/*
		 * On reset of "interrupt active" bit corresponding
		 * "interrupt enable" bit gets automatically reset as well.
		 * Now we need to re-enable interrupt for the counter.
		 */
		write_aux_reg(ARC_REG_PCT_INT_CTRL,
			read_aux_reg(ARC_REG_PCT_INT_CTRL) | BIT(idx));

		event = pmu_cpu->act_counter[idx];
		hwc = &event->hw;

		WARN_ON_ONCE(hwc->idx != idx);

		arc_perf_event_update(event, &event->hw, event->hw.idx);
		perf_sample_data_init(&data, 0, hwc->last_period);
		if (arc_pmu_event_set_period(event)) {
			if (perf_event_overflow(event, &data, regs))
				arc_pmu_stop(event, 0);
		}

		active_ints &= ~BIT(idx);
	} while (active_ints);

done:
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

/* Event field occupies the bottom 15 bits of our config field */
PMU_FORMAT_ATTR(event, "config:0-14");
static struct attribute *arc_pmu_format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group arc_pmu_format_attr_gr = {
	.name = "format",
	.attrs = arc_pmu_format_attrs,
};

static ssize_t arc_pmu_events_sysfs_show(struct device *dev,
					 struct device_attribute *attr,
					 char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sprintf(page, "event=0x%04llx\n", pmu_attr->id);
}

/*
 * We don't add attrs here as we don't have pre-defined list of perf events.
 * We will generate and add attrs dynamically in probe() after we read HW
 * configuration.
 */
static struct attribute_group arc_pmu_events_attr_gr = {
	.name = "events",
};

static void arc_pmu_add_raw_event_attr(int j, char *str)
{
	memmove(arc_pmu->raw_entry[j].name, str, ARCPMU_EVENT_NAME_LEN - 1);
	arc_pmu->attr[j].attr.attr.name = arc_pmu->raw_entry[j].name;
	arc_pmu->attr[j].attr.attr.mode = VERIFY_OCTAL_PERMISSIONS(0444);
	arc_pmu->attr[j].attr.show = arc_pmu_events_sysfs_show;
	arc_pmu->attr[j].id = j;
	arc_pmu->attrs[j] = &(arc_pmu->attr[j].attr.attr);
}

static int arc_pmu_raw_alloc(struct device *dev)
{
	arc_pmu->attr = devm_kmalloc_array(dev, arc_pmu->n_events + 1,
		sizeof(*arc_pmu->attr), GFP_KERNEL | __GFP_ZERO);
	if (!arc_pmu->attr)
		return -ENOMEM;

	arc_pmu->attrs = devm_kmalloc_array(dev, arc_pmu->n_events + 1,
		sizeof(*arc_pmu->attrs), GFP_KERNEL | __GFP_ZERO);
	if (!arc_pmu->attrs)
		return -ENOMEM;

	arc_pmu->raw_entry = devm_kmalloc_array(dev, arc_pmu->n_events,
		sizeof(*arc_pmu->raw_entry), GFP_KERNEL | __GFP_ZERO);
	if (!arc_pmu->raw_entry)
		return -ENOMEM;

	return 0;
}

static inline bool event_in_hw_event_map(int i, char *name)
{
	if (!arc_pmu_ev_hw_map[i])
		return false;

	if (!strlen(arc_pmu_ev_hw_map[i]))
		return false;

	if (strcmp(arc_pmu_ev_hw_map[i], name))
		return false;

	return true;
}

static void arc_pmu_map_hw_event(int j, char *str)
{
	int i;

	/* See if HW condition has been mapped to a perf event_id */
	for (i = 0; i < ARRAY_SIZE(arc_pmu_ev_hw_map); i++) {
		if (event_in_hw_event_map(i, str)) {
			pr_debug("mapping perf event %2d to h/w event \'%8s\' (idx %d)\n",
				 i, str, j);
			arc_pmu->ev_hw_idx[i] = j;
		}
	}
}

static int arc_pmu_device_probe(struct platform_device *pdev)
{
	struct arc_reg_pct_build pct_bcr;
	struct arc_reg_cc_build cc_bcr;
	int i, has_interrupts, irq = -1;
	int counter_size;	/* in bits */

	union cc_name {
		struct {
			u32 word0, word1;
			char sentinel;
		} indiv;
		char str[ARCPMU_EVENT_NAME_LEN];
	} cc_name;


	READ_BCR(ARC_REG_PCT_BUILD, pct_bcr);
	if (!pct_bcr.v) {
		pr_err("This core does not have performance counters!\n");
		return -ENODEV;
	}
	BUILD_BUG_ON(ARC_PERF_MAX_COUNTERS > 32);
	if (WARN_ON(pct_bcr.c > ARC_PERF_MAX_COUNTERS))
		return -EINVAL;

	READ_BCR(ARC_REG_CC_BUILD, cc_bcr);
	if (WARN(!cc_bcr.v, "Counters exist but No countable conditions?"))
		return -EINVAL;

	arc_pmu = devm_kzalloc(&pdev->dev, sizeof(struct arc_pmu), GFP_KERNEL);
	if (!arc_pmu)
		return -ENOMEM;

	arc_pmu->n_events = cc_bcr.c;

	if (arc_pmu_raw_alloc(&pdev->dev))
		return -ENOMEM;

	has_interrupts = is_isa_arcv2() ? pct_bcr.i : 0;

	arc_pmu->n_counters = pct_bcr.c;
	counter_size = 32 + (pct_bcr.s << 4);

	arc_pmu->max_period = (1ULL << counter_size) / 2 - 1ULL;

	pr_info("ARC perf\t: %d counters (%d bits), %d conditions%s\n",
		arc_pmu->n_counters, counter_size, cc_bcr.c,
		has_interrupts ? ", [overflow IRQ support]" : "");

	cc_name.str[ARCPMU_EVENT_NAME_LEN - 1] = 0;
	for (i = 0; i < PERF_COUNT_ARC_HW_MAX; i++)
		arc_pmu->ev_hw_idx[i] = -1;

	/* loop thru all available h/w condition indexes */
	for (i = 0; i < cc_bcr.c; i++) {
		write_aux_reg(ARC_REG_CC_INDEX, i);
		cc_name.indiv.word0 = le32_to_cpu(read_aux_reg(ARC_REG_CC_NAME0));
		cc_name.indiv.word1 = le32_to_cpu(read_aux_reg(ARC_REG_CC_NAME1));

		arc_pmu_map_hw_event(i, cc_name.str);
		arc_pmu_add_raw_event_attr(i, cc_name.str);
	}

	arc_pmu_events_attr_gr.attrs = arc_pmu->attrs;
	arc_pmu->attr_groups[ARCPMU_ATTR_GR_EVENTS] = &arc_pmu_events_attr_gr;
	arc_pmu->attr_groups[ARCPMU_ATTR_GR_FORMATS] = &arc_pmu_format_attr_gr;

	arc_pmu->pmu = (struct pmu) {
		.pmu_enable	= arc_pmu_enable,
		.pmu_disable	= arc_pmu_disable,
		.event_init	= arc_pmu_event_init,
		.add		= arc_pmu_add,
		.del		= arc_pmu_del,
		.start		= arc_pmu_start,
		.stop		= arc_pmu_stop,
		.read		= arc_pmu_read,
		.attr_groups	= arc_pmu->attr_groups,
	};

	if (has_interrupts) {
		irq = platform_get_irq(pdev, 0);
		if (irq >= 0) {
			int ret;

			arc_pmu->irq = irq;

			/* intc map function ensures irq_set_percpu_devid() called */
			ret = request_percpu_irq(irq, arc_pmu_intr, "ARC perf counters",
						 this_cpu_ptr(&arc_pmu_cpu));

			if (!ret)
				on_each_cpu(arc_cpu_pmu_irq_init, &irq, 1);
			else
				irq = -1;
		}

	}

	if (irq == -1)
		arc_pmu->pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;

	/*
	 * perf parser doesn't really like '-' symbol in events name, so let's
	 * use '_' in arc pct name as it goes to kernel PMU event prefix.
	 */
	return perf_pmu_register(&arc_pmu->pmu, "arc_pct", PERF_TYPE_RAW);
}

static const struct of_device_id arc_pmu_match[] = {
	{ .compatible = "snps,arc700-pct" },
	{ .compatible = "snps,archs-pct" },
	{},
};
MODULE_DEVICE_TABLE(of, arc_pmu_match);

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
