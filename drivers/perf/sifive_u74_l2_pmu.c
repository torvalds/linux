// SPDX-License-Identifier: GPL-2.0
/*
 * SiFive private L2 cache controller PMU Driver
 *
 * Copyright (C) 2018-2023 SiFive, Inc.
 */

#define pr_fmt(fmt) "pL2CACHE_PMU: " fmt

#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/mutex.h>
#include <linux/bitmap.h>
#include <linux/perf_event.h>
#include <linux/atomic.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/cpu_pm.h>

#define SIFIVE_PL2_PMU_MAX_COUNTERS	64
#define SIFIVE_PL2_SELECT_BASE_OFFSET	0x2000
#define SIFIVE_PL2_COUNTER_BASE_OFFSET	0x3000

#define SIFIVE_PL2_COUNTER_MASK   GENMASK_ULL(63, 0)

struct sifive_u74_l2_pmu_event {
	struct perf_event **events;
	void __iomem *event_counter_base;
	void __iomem *event_select_base;
	u32 counters;
	DECLARE_BITMAP(used_mask, SIFIVE_PL2_PMU_MAX_COUNTERS);
};

struct sifive_u74_l2_pmu {
	struct pmu *pmu;
};

static bool pl2pmu_init_done;
static struct sifive_u74_l2_pmu sifive_u74_l2_pmu;
static DEFINE_PER_CPU(struct sifive_u74_l2_pmu_event, sifive_u74_l2_pmu_event);

#ifndef readq
static inline unsigned long long readq(void __iomem *addr)
{
	return readl(addr) | (((unsigned long long)readl(addr + 4)) << 32LL);
}
#endif

#ifndef writeq
static inline void writeq(unsigned long long v, void __iomem *addr)
{
	writel(lower_32_bits(v), addr);
	writel(upper_32_bits(v), addr + 4);
}
#endif

/* formats */
static ssize_t sifive_u74_l2_pmu_format_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	return sysfs_emit(buf, "%s\n", (char *)eattr->var);
}

#define SIFIVE_PL2_PMU_PMU_FORMAT_ATTR(_name, _config)				\
	(&((struct dev_ext_attribute[]) {					\
		{ .attr = __ATTR(_name, 0444, sifive_u74_l2_pmu_format_show, NULL),\
		  .var = (void *)_config, }					\
	})[0].attr.attr)

static struct attribute *sifive_u74_l2_pmu_formats[] = {
	SIFIVE_PL2_PMU_PMU_FORMAT_ATTR(event, "config:0-63"),
	NULL,
};

static struct attribute_group sifive_u74_l2_pmu_format_group = {
	.name = "format",
	.attrs = sifive_u74_l2_pmu_formats,
};

/* events */

static ssize_t sifive_u74_l2_pmu_event_show(struct device *dev,
					 struct device_attribute *attr,
					 char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(page, "event=0x%02llx\n", pmu_attr->id);
}

#define SET_EVENT_SELECT(_event, _set)	(((u64)1 << ((_event) + 8)) | (_set))
#define PL2_PMU_EVENT_ATTR(_name, _event, _set)			\
	PMU_EVENT_ATTR_ID(_name, sifive_u74_l2_pmu_event_show,	\
	SET_EVENT_SELECT(_event, _set))

enum pl2_pmu_event_set1 {
	INNER_PUT_FULL_DATA = 0,
	INNER_PUT_PARTIAL_DATA,
	INNER_ARITHMETIC_DATA,
	INNER_GET,
	INNER_PREFETCH_READ,
	INNER_PREFETCH_WRITE,
	INNER_ACQUIRE_BLOCK_NTOB,
	INNER_ACQUIRE_BLOCK_NTOT,
	INNER_ACQUIRE_BLOCK_BTOT,
	INNER_ACQUIRE_PERM_NTOT,
	INNER_ACQUIRE_PERM_BTOT,
	INNER_RELEASE_TTOB,
	INNER_RELEASE_TTON,
	INNER_RELEASE_BTON,
	INNER_RELEASE_DATA_TTOB,
	INNER_RELEASE_DATA_TTON,
	INNER_RELEASE_DATA_BTON,
	OUTER_PROBE_BLOCK_TOT,
	OUTER_PROBE_BLOCK_TOB,
	OUTER_PROBE_BLOCK_TON,
	PL2_PMU_MAX_EVENT1_IDX
};

enum pl2_pmu_event_set2 {
	INNER_PUT_FULL_DATA_HIT = 0,
	INNER_PUT_PARTIAL_DATA_HIT,
	INNER_ARITHMETIC_DATA_HIT,
	INNER_GET_HIT,
	INNER_PREFETCH_READ_HIT,
	INNER_ACQUIRE_BLOCK_NTOB_HIT,
	INNER_ACQUIRE_PERM_NTOT_HIT,
	INNER_RELEASE_TTOB_HIT,
	INNER_RELEASE_DATA_TTOB_HIT,
	OUTER_PROBE_BLOCK_TOT_HIT,
	INNER_PUT_FULL_DATA_HIT_SHARED,
	INNER_PUT_PARTIAL_DATA_HIT_SHARED,
	INNER_ARITHMETIC_DATA_HIT_SHARED,
	INNER_GET_HIT_SHARED,
	INNER_PREFETCH_READ_HIT_SHARED,
	INNER_ACQUIRE_BLOCK_HIT_SHARED,
	INNER_ACQUIRE_PERM_NTOT_HIT_SHARED,
	OUTER_PROBE_BLOCK_TOT_HIT_SHARED,
	OUTER_PROBE_BLOCK_TOT_HIT_DIRTY,
	PL2_PMU_MAX_EVENT2_IDX
};

enum pl2_pmu_event_set3 {
	OUTER_ACQUIRE_BLOCK_NTOB,
	OUTER_ACQUIRE_BLOCK_NTOT,
	OUTER_ACQUIRE_BLOCK_BTOT,
	OUTER_ACQUIRE_PERM_NTOT,
	OUTER_ACQUIRE_PERM_BTOT,
	OUTER_RELEARE_TTOB,
	OUTER_RELEARE_TTON,
	OUTER_RELEARE_BTON,
	OUTER_RELEARE_DATA_TTOB,
	OUTER_RELEARE_DATA_TTON,
	OUTER_RELEARE_DATA_BTON,
	INNER_PROBE_BLOCK_TOT,
	INNER_PROBE_BLOCK_TOB,
	INNER_PROBE_BLOCK_TON,
	PL2_PMU_MAX_EVENT3_IDX
};

enum pl2_pmu_event_set4 {
	INNER_HINT_HITS_MSHR = 0,
	PL2_PMU_MAX_EVENT4_IDX
};

static struct attribute *sifive_u74_l2_pmu_events[] = {
	PL2_PMU_EVENT_ATTR(inner_put_full_data, INNER_PUT_FULL_DATA, 1),
	PL2_PMU_EVENT_ATTR(inner_put_partial_data, INNER_PUT_PARTIAL_DATA, 1),
	PL2_PMU_EVENT_ATTR(inner_arithmetic_data, INNER_ARITHMETIC_DATA, 1),
	PL2_PMU_EVENT_ATTR(inner_get, INNER_GET, 1),
	PL2_PMU_EVENT_ATTR(inner_prefetch_read, INNER_PREFETCH_READ, 1),
	PL2_PMU_EVENT_ATTR(inner_prefetch_write, INNER_PREFETCH_WRITE, 1),
	PL2_PMU_EVENT_ATTR(inner_acquire_block_ntob, INNER_ACQUIRE_BLOCK_NTOB, 1),
	PL2_PMU_EVENT_ATTR(inner_acquire_block_ntot, INNER_ACQUIRE_BLOCK_NTOT, 1),
	PL2_PMU_EVENT_ATTR(inner_acquire_block_btot, INNER_ACQUIRE_BLOCK_BTOT, 1),
	PL2_PMU_EVENT_ATTR(inner_acquire_perm_ntot, INNER_ACQUIRE_PERM_NTOT, 1),
	PL2_PMU_EVENT_ATTR(inner_acquire_perm_btot, INNER_ACQUIRE_PERM_BTOT, 1),
	PL2_PMU_EVENT_ATTR(inner_release_ttob, INNER_RELEASE_TTOB, 1),
	PL2_PMU_EVENT_ATTR(inner_release_tton, INNER_RELEASE_TTON, 1),
	PL2_PMU_EVENT_ATTR(inner_release_bton, INNER_RELEASE_BTON, 1),
	PL2_PMU_EVENT_ATTR(inner_release_data_ttob, INNER_RELEASE_DATA_TTOB, 1),
	PL2_PMU_EVENT_ATTR(inner_release_data_tton, INNER_RELEASE_DATA_TTON, 1),
	PL2_PMU_EVENT_ATTR(inner_release_data_bton, INNER_RELEASE_DATA_BTON, 1),
	PL2_PMU_EVENT_ATTR(outer_probe_block_tot, OUTER_PROBE_BLOCK_TOT, 1),
	PL2_PMU_EVENT_ATTR(outer_probe_block_tob, OUTER_PROBE_BLOCK_TOB, 1),
	PL2_PMU_EVENT_ATTR(outer_probe_block_ton, OUTER_PROBE_BLOCK_TON, 1),

	PL2_PMU_EVENT_ATTR(inner_put_full_data_hit, INNER_PUT_FULL_DATA_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_put_partial_data_hit, INNER_PUT_PARTIAL_DATA_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_arithmetic_data_hit, INNER_ARITHMETIC_DATA_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_get_hit, INNER_GET_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_prefetch_read_hit, INNER_PREFETCH_READ_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_acquire_block_ntob_hit, INNER_ACQUIRE_BLOCK_NTOB_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_acquire_perm_ntot_hit, INNER_ACQUIRE_PERM_NTOT_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_release_ttob_hit, INNER_RELEASE_TTOB_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_release_data_ttob_hit, INNER_RELEASE_DATA_TTOB_HIT, 2),
	PL2_PMU_EVENT_ATTR(outer_probe_block_tot_hit, OUTER_PROBE_BLOCK_TOT_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_put_full_data_hit_shared, INNER_PUT_FULL_DATA_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_put_partial_data_hit_shared, INNER_PUT_PARTIAL_DATA_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_arithmetic_data_hit_shared, INNER_ARITHMETIC_DATA_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_get_hit_shared, INNER_GET_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_prefetch_read_hit_shared, INNER_PREFETCH_READ_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_acquire_block_hit_shared, INNER_ACQUIRE_BLOCK_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_acquire_perm_hit_shared, INNER_ACQUIRE_PERM_NTOT_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(outer_probe_block_tot_hit_shared, OUTER_PROBE_BLOCK_TOT_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(outer_probe_block_tot_hit_dirty, OUTER_PROBE_BLOCK_TOT_HIT_DIRTY, 2),

	PL2_PMU_EVENT_ATTR(outer_acquire_block_ntob, OUTER_ACQUIRE_BLOCK_NTOB, 3),
	PL2_PMU_EVENT_ATTR(outer_acquire_block_ntot, OUTER_ACQUIRE_BLOCK_NTOT, 3),
	PL2_PMU_EVENT_ATTR(outer_acquire_block_btot, OUTER_ACQUIRE_BLOCK_BTOT, 3),
	PL2_PMU_EVENT_ATTR(outer_acquire_perm_ntot, OUTER_ACQUIRE_PERM_NTOT, 3),
	PL2_PMU_EVENT_ATTR(outer_acquire_perm_btot, OUTER_ACQUIRE_PERM_BTOT, 3),
	PL2_PMU_EVENT_ATTR(outer_release_ttob, OUTER_RELEARE_TTOB, 3),
	PL2_PMU_EVENT_ATTR(outer_release_tton, OUTER_RELEARE_TTON, 3),
	PL2_PMU_EVENT_ATTR(outer_release_bton, OUTER_RELEARE_BTON, 3),
	PL2_PMU_EVENT_ATTR(outer_release_data_ttob, OUTER_RELEARE_DATA_TTOB, 3),
	PL2_PMU_EVENT_ATTR(outer_release_data_tton, OUTER_RELEARE_DATA_TTON, 3),
	PL2_PMU_EVENT_ATTR(outer_release_data_bton, OUTER_RELEARE_DATA_BTON, 3),
	PL2_PMU_EVENT_ATTR(inner_probe_block_tot, INNER_PROBE_BLOCK_TOT, 3),
	PL2_PMU_EVENT_ATTR(inner_probe_block_tob, INNER_PROBE_BLOCK_TOB, 3),
	PL2_PMU_EVENT_ATTR(inner_probe_block_ton, INNER_PROBE_BLOCK_TON, 3),

	PL2_PMU_EVENT_ATTR(inner_hint_hits_mshr, INNER_HINT_HITS_MSHR, 4),
	NULL
};

static struct attribute_group sifive_u74_l2_pmu_events_group = {
	.name = "events",
	.attrs = sifive_u74_l2_pmu_events,
};

/*
 * Per PMU device attribute groups
 */

static const struct attribute_group *sifive_u74_l2_pmu_attr_grps[] = {
	&sifive_u74_l2_pmu_format_group,
	&sifive_u74_l2_pmu_events_group,
	NULL,
};

/*
 * Low-level functions: reading and writing counters
 */

static inline u64 read_counter(int idx)
{
	struct sifive_u74_l2_pmu_event *ptr = this_cpu_ptr(&sifive_u74_l2_pmu_event);

	if (WARN_ON_ONCE(idx < 0 || idx > ptr->counters))
		return -EINVAL;

	return readq(ptr->event_counter_base + idx * 8);
}

static inline void write_counter(int idx, u64 val)
{
	struct sifive_u74_l2_pmu_event *ptr = this_cpu_ptr(&sifive_u74_l2_pmu_event);

	writeq(val, ptr->event_counter_base + idx * 8);
}

/*
 * pmu->read: read and update the counter
 */
static void sifive_u74_l2_pmu_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;
	u64 oldval;
	int idx = hwc->idx;
	u64 delta;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = read_counter(idx);

		oldval = local64_cmpxchg(&hwc->prev_count, prev_raw_count,
					 new_raw_count);
	} while (oldval != prev_raw_count);

	/* delta is the value to update the counter we maintain in the kernel. */
	delta = (new_raw_count - prev_raw_count) & SIFIVE_PL2_COUNTER_MASK;
	local64_add(delta, &event->count);
}

/*
 * State transition functions:
 *
 * stop()/start() & add()/del()
 */

/*
 * pmu->stop: stop the counter
 */
static void sifive_u74_l2_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct sifive_u74_l2_pmu_event *ptr = this_cpu_ptr(&sifive_u74_l2_pmu_event);

	/* Disable this counter to count events */
	writeq(0, ptr->event_select_base + (hwc->idx * 8));

	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		sifive_u74_l2_pmu_read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

/*
 * pmu->start: start the event.
 */
static void sifive_u74_l2_pmu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct sifive_u74_l2_pmu_event *ptr = this_cpu_ptr(&sifive_u74_l2_pmu_event);

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	hwc->state = 0;
	perf_event_update_userpage(event);

	/* Set initial value 0 */
	local64_set(&hwc->prev_count, 0);
	write_counter(hwc->idx, 0);

	/* Enable counter to count these events */
	writeq(hwc->config, ptr->event_select_base + (hwc->idx * 8));
}

/*
 * pmu->add: add the event to PMU.
 */
static int sifive_u74_l2_pmu_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct sifive_u74_l2_pmu_event *ptr = this_cpu_ptr(&sifive_u74_l2_pmu_event);
	int idx;
	u64 config = event->attr.config;
	u64 set = config & 0xff;
	u64 ev_type = config >> 8;

	/* Check if this is a valid set and event. */
	switch (set) {
	case 1:
		if (ev_type >= (BIT_ULL(PL2_PMU_MAX_EVENT1_IDX)))
			return -ENOENT;
		break;
	case 2:
		if (ev_type >= (BIT_ULL(PL2_PMU_MAX_EVENT2_IDX)))
			return -ENOENT;
		break;
	case 3:
		if (ev_type >= (BIT_ULL(PL2_PMU_MAX_EVENT3_IDX)))
			return -ENOENT;
		break;
	case 4:
		if (ev_type >= (BIT_ULL(PL2_PMU_MAX_EVENT4_IDX)))
			return -ENOENT;
		break;
	case 0:
	default:
		return -ENOENT;
	}

	idx = find_first_zero_bit(ptr->used_mask, ptr->counters);
	/* The counters are all in use. */
	if (idx == ptr->counters)
		return -EAGAIN;

	set_bit(idx, ptr->used_mask);

	/* Found an available counter idx for this event. */
	hwc->idx = idx;
	ptr->events[hwc->idx] = event;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		sifive_u74_l2_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	return 0;
}

/*
 * pmu->del: delete the event from PMU.
 */
static void sifive_u74_l2_pmu_del(struct perf_event *event, int flags)
{
	struct sifive_u74_l2_pmu_event *ptr = this_cpu_ptr(&sifive_u74_l2_pmu_event);
	struct hw_perf_event *hwc = &event->hw;

	/* Stop the counter and release this counter. */
	ptr->events[hwc->idx] = NULL;
	sifive_u74_l2_pmu_stop(event, PERF_EF_UPDATE);
	clear_bit(hwc->idx, ptr->used_mask);
	perf_event_update_userpage(event);
}

/*
 * Event Initialization/Finalization
 */

static int sifive_u74_l2_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/* Don't allocate hw counter yet. */
	hwc->idx = -1;
	hwc->config = event->attr.config;

	return 0;
}

/*
 * Initialization
 */

static struct pmu sifive_u74_l2_generic_pmu = {
	.name		= "sifive_u74_l2_pmu",
	.task_ctx_nr	= perf_sw_context,
	.event_init	= sifive_u74_l2_pmu_event_init,
	.add		= sifive_u74_l2_pmu_add,
	.del		= sifive_u74_l2_pmu_del,
	.start		= sifive_u74_l2_pmu_start,
	.stop		= sifive_u74_l2_pmu_stop,
	.read		= sifive_u74_l2_pmu_read,
	.attr_groups	= sifive_u74_l2_pmu_attr_grps,
	.capabilities   = PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
};

static struct sifive_u74_l2_pmu sifive_u74_l2_pmu = {
	.pmu = &sifive_u74_l2_generic_pmu,
};

/*
 *  PM notifer for suspend to ram
 */
#ifdef CONFIG_CPU_PM
static int sifive_u74_l2_pmu_pm_notify(struct notifier_block *b, unsigned long cmd,
				    void *v)
{
	struct sifive_u74_l2_pmu_event *ptr = this_cpu_ptr(&sifive_u74_l2_pmu_event);
	struct perf_event *event;
	int idx;
	int enabled_event = bitmap_weight(ptr->used_mask, ptr->counters);

	if (!enabled_event)
		return NOTIFY_OK;

	for (idx = 0; idx < ptr->counters; idx++) {
		event = ptr->events[idx];
		if (!event)
			continue;

		switch (cmd) {
		case CPU_PM_ENTER:
			/* Stop and update the counter */
			sifive_u74_l2_pmu_stop(event, PERF_EF_UPDATE);
			break;
		case CPU_PM_ENTER_FAILED:
		case CPU_PM_EXIT:
			 /* Restore and enable the counter */
			sifive_u74_l2_pmu_start(event, PERF_EF_RELOAD);
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block sifive_u74_l2_pmu_pm_notifier_block = {
	.notifier_call = sifive_u74_l2_pmu_pm_notify,
};

static inline void sifive_u74_l2_pmu_pm_init(void)
{
	cpu_pm_register_notifier(&sifive_u74_l2_pmu_pm_notifier_block);
}

#else
static inline void sifive_u74_l2_pmu_pm_init(void) { }
#endif /* CONFIG_CPU_PM */

int sifive_u74_l2_pmu_probe(struct device_node	*pl2_node,
			 void __iomem *pl2_base, int cpu)
{
	struct sifive_u74_l2_pmu_event *ptr = per_cpu_ptr(&sifive_u74_l2_pmu_event, cpu);
	int ret;

	/* Get counter numbers. */
	ptr->counters = SIFIVE_PL2_PMU_MAX_COUNTERS;
	pr_info("perfmon-counters: 64 for CPU %d\n", cpu);

	/* Allocate perf_event. */
	ptr->events = kcalloc(ptr->counters, sizeof(struct perf_event), GFP_KERNEL);
	if (!ptr->events)
		return -ENOMEM;

	ptr->event_select_base = pl2_base + SIFIVE_PL2_SELECT_BASE_OFFSET;
	ptr->event_counter_base = pl2_base + SIFIVE_PL2_COUNTER_BASE_OFFSET;

	if (!pl2pmu_init_done) {
		ret = perf_pmu_register(sifive_u74_l2_pmu.pmu, sifive_u74_l2_pmu.pmu->name, -1);
		if (ret) {
			pr_err("Failed to register sifive_u74_l2_pmu.pmu: %d\n", ret);
			return ret;
		}
		sifive_u74_l2_pmu_pm_init();
		pl2pmu_init_done = true;
	}

	return 0;
}
EXPORT_SYMBOL(sifive_u74_l2_pmu_probe);
