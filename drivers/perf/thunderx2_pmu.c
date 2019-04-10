// SPDX-License-Identifier: GPL-2.0
/*
 * CAVIUM THUNDERX2 SoC PMU UNCORE
 * Copyright (C) 2018 Cavium Inc.
 * Author: Ganapatrao Kulkarni <gkulkarni@cavium.com>
 */

#include <linux/acpi.h>
#include <linux/cpuhotplug.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>

/* Each ThunderX2(TX2) Socket has a L3C and DMC UNCORE PMU device.
 * Each UNCORE PMU device consists of 4 independent programmable counters.
 * Counters are 32 bit and do not support overflow interrupt,
 * they need to be sampled before overflow(i.e, at every 2 seconds).
 */

#define TX2_PMU_MAX_COUNTERS		4
#define TX2_PMU_DMC_CHANNELS		8
#define TX2_PMU_L3_TILES		16

#define TX2_PMU_HRTIMER_INTERVAL	(2 * NSEC_PER_SEC)
#define GET_EVENTID(ev)			((ev->hw.config) & 0x1f)
#define GET_COUNTERID(ev)		((ev->hw.idx) & 0x3)
 /* 1 byte per counter(4 counters).
  * Event id is encoded in bits [5:1] of a byte,
  */
#define DMC_EVENT_CFG(idx, val)		((val) << (((idx) * 8) + 1))

#define L3C_COUNTER_CTL			0xA8
#define L3C_COUNTER_DATA		0xAC
#define DMC_COUNTER_CTL			0x234
#define DMC_COUNTER_DATA		0x240

/* L3C event IDs */
#define L3_EVENT_READ_REQ		0xD
#define L3_EVENT_WRITEBACK_REQ		0xE
#define L3_EVENT_INV_N_WRITE_REQ	0xF
#define L3_EVENT_INV_REQ		0x10
#define L3_EVENT_EVICT_REQ		0x13
#define L3_EVENT_INV_N_WRITE_HIT	0x14
#define L3_EVENT_INV_HIT		0x15
#define L3_EVENT_READ_HIT		0x17
#define L3_EVENT_MAX			0x18

/* DMC event IDs */
#define DMC_EVENT_COUNT_CYCLES		0x1
#define DMC_EVENT_WRITE_TXNS		0xB
#define DMC_EVENT_DATA_TRANSFERS	0xD
#define DMC_EVENT_READ_TXNS		0xF
#define DMC_EVENT_MAX			0x10

enum tx2_uncore_type {
	PMU_TYPE_L3C,
	PMU_TYPE_DMC,
	PMU_TYPE_INVALID,
};

/*
 * pmu on each socket has 2 uncore devices(dmc and l3c),
 * each device has 4 counters.
 */
struct tx2_uncore_pmu {
	struct hlist_node hpnode;
	struct list_head  entry;
	struct pmu pmu;
	char *name;
	int node;
	int cpu;
	u32 max_counters;
	u32 prorate_factor;
	u32 max_events;
	u64 hrtimer_interval;
	void __iomem *base;
	DECLARE_BITMAP(active_counters, TX2_PMU_MAX_COUNTERS);
	struct perf_event *events[TX2_PMU_MAX_COUNTERS];
	struct device *dev;
	struct hrtimer hrtimer;
	const struct attribute_group **attr_groups;
	enum tx2_uncore_type type;
	void (*init_cntr_base)(struct perf_event *event,
			struct tx2_uncore_pmu *tx2_pmu);
	void (*stop_event)(struct perf_event *event);
	void (*start_event)(struct perf_event *event, int flags);
};

static LIST_HEAD(tx2_pmus);

static inline struct tx2_uncore_pmu *pmu_to_tx2_pmu(struct pmu *pmu)
{
	return container_of(pmu, struct tx2_uncore_pmu, pmu);
}

PMU_FORMAT_ATTR(event,	"config:0-4");

static struct attribute *l3c_pmu_format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute *dmc_pmu_format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

static const struct attribute_group l3c_pmu_format_attr_group = {
	.name = "format",
	.attrs = l3c_pmu_format_attrs,
};

static const struct attribute_group dmc_pmu_format_attr_group = {
	.name = "format",
	.attrs = dmc_pmu_format_attrs,
};

/*
 * sysfs event attributes
 */
static ssize_t tx2_pmu_event_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	return sprintf(buf, "event=0x%lx\n", (unsigned long) eattr->var);
}

#define TX2_EVENT_ATTR(name, config) \
	PMU_EVENT_ATTR(name, tx2_pmu_event_attr_##name, \
			config, tx2_pmu_event_show)

TX2_EVENT_ATTR(read_request, L3_EVENT_READ_REQ);
TX2_EVENT_ATTR(writeback_request, L3_EVENT_WRITEBACK_REQ);
TX2_EVENT_ATTR(inv_nwrite_request, L3_EVENT_INV_N_WRITE_REQ);
TX2_EVENT_ATTR(inv_request, L3_EVENT_INV_REQ);
TX2_EVENT_ATTR(evict_request, L3_EVENT_EVICT_REQ);
TX2_EVENT_ATTR(inv_nwrite_hit, L3_EVENT_INV_N_WRITE_HIT);
TX2_EVENT_ATTR(inv_hit, L3_EVENT_INV_HIT);
TX2_EVENT_ATTR(read_hit, L3_EVENT_READ_HIT);

static struct attribute *l3c_pmu_events_attrs[] = {
	&tx2_pmu_event_attr_read_request.attr.attr,
	&tx2_pmu_event_attr_writeback_request.attr.attr,
	&tx2_pmu_event_attr_inv_nwrite_request.attr.attr,
	&tx2_pmu_event_attr_inv_request.attr.attr,
	&tx2_pmu_event_attr_evict_request.attr.attr,
	&tx2_pmu_event_attr_inv_nwrite_hit.attr.attr,
	&tx2_pmu_event_attr_inv_hit.attr.attr,
	&tx2_pmu_event_attr_read_hit.attr.attr,
	NULL,
};

TX2_EVENT_ATTR(cnt_cycles, DMC_EVENT_COUNT_CYCLES);
TX2_EVENT_ATTR(write_txns, DMC_EVENT_WRITE_TXNS);
TX2_EVENT_ATTR(data_transfers, DMC_EVENT_DATA_TRANSFERS);
TX2_EVENT_ATTR(read_txns, DMC_EVENT_READ_TXNS);

static struct attribute *dmc_pmu_events_attrs[] = {
	&tx2_pmu_event_attr_cnt_cycles.attr.attr,
	&tx2_pmu_event_attr_write_txns.attr.attr,
	&tx2_pmu_event_attr_data_transfers.attr.attr,
	&tx2_pmu_event_attr_read_txns.attr.attr,
	NULL,
};

static const struct attribute_group l3c_pmu_events_attr_group = {
	.name = "events",
	.attrs = l3c_pmu_events_attrs,
};

static const struct attribute_group dmc_pmu_events_attr_group = {
	.name = "events",
	.attrs = dmc_pmu_events_attrs,
};

/*
 * sysfs cpumask attributes
 */
static ssize_t cpumask_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct tx2_uncore_pmu *tx2_pmu;

	tx2_pmu = pmu_to_tx2_pmu(dev_get_drvdata(dev));
	return cpumap_print_to_pagebuf(true, buf, cpumask_of(tx2_pmu->cpu));
}
static DEVICE_ATTR_RO(cpumask);

static struct attribute *tx2_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group pmu_cpumask_attr_group = {
	.attrs = tx2_pmu_cpumask_attrs,
};

/*
 * Per PMU device attribute groups
 */
static const struct attribute_group *l3c_pmu_attr_groups[] = {
	&l3c_pmu_format_attr_group,
	&pmu_cpumask_attr_group,
	&l3c_pmu_events_attr_group,
	NULL
};

static const struct attribute_group *dmc_pmu_attr_groups[] = {
	&dmc_pmu_format_attr_group,
	&pmu_cpumask_attr_group,
	&dmc_pmu_events_attr_group,
	NULL
};

static inline u32 reg_readl(unsigned long addr)
{
	return readl((void __iomem *)addr);
}

static inline void reg_writel(u32 val, unsigned long addr)
{
	writel(val, (void __iomem *)addr);
}

static int alloc_counter(struct tx2_uncore_pmu *tx2_pmu)
{
	int counter;

	counter = find_first_zero_bit(tx2_pmu->active_counters,
				tx2_pmu->max_counters);
	if (counter == tx2_pmu->max_counters)
		return -ENOSPC;

	set_bit(counter, tx2_pmu->active_counters);
	return counter;
}

static inline void free_counter(struct tx2_uncore_pmu *tx2_pmu, int counter)
{
	clear_bit(counter, tx2_pmu->active_counters);
}

static void init_cntr_base_l3c(struct perf_event *event,
		struct tx2_uncore_pmu *tx2_pmu)
{
	struct hw_perf_event *hwc = &event->hw;

	/* counter ctrl/data reg offset at 8 */
	hwc->config_base = (unsigned long)tx2_pmu->base
		+ L3C_COUNTER_CTL + (8 * GET_COUNTERID(event));
	hwc->event_base =  (unsigned long)tx2_pmu->base
		+ L3C_COUNTER_DATA + (8 * GET_COUNTERID(event));
}

static void init_cntr_base_dmc(struct perf_event *event,
		struct tx2_uncore_pmu *tx2_pmu)
{
	struct hw_perf_event *hwc = &event->hw;

	hwc->config_base = (unsigned long)tx2_pmu->base
		+ DMC_COUNTER_CTL;
	/* counter data reg offset at 0xc */
	hwc->event_base = (unsigned long)tx2_pmu->base
		+ DMC_COUNTER_DATA + (0xc * GET_COUNTERID(event));
}

static void uncore_start_event_l3c(struct perf_event *event, int flags)
{
	u32 val;
	struct hw_perf_event *hwc = &event->hw;

	/* event id encoded in bits [07:03] */
	val = GET_EVENTID(event) << 3;
	reg_writel(val, hwc->config_base);
	local64_set(&hwc->prev_count, 0);
	reg_writel(0, hwc->event_base);
}

static inline void uncore_stop_event_l3c(struct perf_event *event)
{
	reg_writel(0, event->hw.config_base);
}

static void uncore_start_event_dmc(struct perf_event *event, int flags)
{
	u32 val;
	struct hw_perf_event *hwc = &event->hw;
	int idx = GET_COUNTERID(event);
	int event_id = GET_EVENTID(event);

	/* enable and start counters.
	 * 8 bits for each counter, bits[05:01] of a counter to set event type.
	 */
	val = reg_readl(hwc->config_base);
	val &= ~DMC_EVENT_CFG(idx, 0x1f);
	val |= DMC_EVENT_CFG(idx, event_id);
	reg_writel(val, hwc->config_base);
	local64_set(&hwc->prev_count, 0);
	reg_writel(0, hwc->event_base);
}

static void uncore_stop_event_dmc(struct perf_event *event)
{
	u32 val;
	struct hw_perf_event *hwc = &event->hw;
	int idx = GET_COUNTERID(event);

	/* clear event type(bits[05:01]) to stop counter */
	val = reg_readl(hwc->config_base);
	val &= ~DMC_EVENT_CFG(idx, 0x1f);
	reg_writel(val, hwc->config_base);
}

static void tx2_uncore_event_update(struct perf_event *event)
{
	s64 prev, delta, new = 0;
	struct hw_perf_event *hwc = &event->hw;
	struct tx2_uncore_pmu *tx2_pmu;
	enum tx2_uncore_type type;
	u32 prorate_factor;

	tx2_pmu = pmu_to_tx2_pmu(event->pmu);
	type = tx2_pmu->type;
	prorate_factor = tx2_pmu->prorate_factor;

	new = reg_readl(hwc->event_base);
	prev = local64_xchg(&hwc->prev_count, new);

	/* handles rollover of 32 bit counter */
	delta = (u32)(((1UL << 32) - prev) + new);

	/* DMC event data_transfers granularity is 16 Bytes, convert it to 64 */
	if (type == PMU_TYPE_DMC &&
			GET_EVENTID(event) == DMC_EVENT_DATA_TRANSFERS)
		delta = delta/4;

	/* L3C and DMC has 16 and 8 interleave channels respectively.
	 * The sampled value is for channel 0 and multiplied with
	 * prorate_factor to get the count for a device.
	 */
	local64_add(delta * prorate_factor, &event->count);
}

static enum tx2_uncore_type get_tx2_pmu_type(struct acpi_device *adev)
{
	int i = 0;
	struct acpi_tx2_pmu_device {
		__u8 id[ACPI_ID_LEN];
		enum tx2_uncore_type type;
	} devices[] = {
		{"CAV901D", PMU_TYPE_L3C},
		{"CAV901F", PMU_TYPE_DMC},
		{"", PMU_TYPE_INVALID}
	};

	while (devices[i].type != PMU_TYPE_INVALID) {
		if (!strcmp(acpi_device_hid(adev), devices[i].id))
			break;
		i++;
	}

	return devices[i].type;
}

static bool tx2_uncore_validate_event(struct pmu *pmu,
				  struct perf_event *event, int *counters)
{
	if (is_software_event(event))
		return true;
	/* Reject groups spanning multiple HW PMUs. */
	if (event->pmu != pmu)
		return false;

	*counters = *counters + 1;
	return true;
}

/*
 * Make sure the group of events can be scheduled at once
 * on the PMU.
 */
static bool tx2_uncore_validate_event_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	int counters = 0;

	if (event->group_leader == event)
		return true;

	if (!tx2_uncore_validate_event(event->pmu, leader, &counters))
		return false;

	for_each_sibling_event(sibling, leader) {
		if (!tx2_uncore_validate_event(event->pmu, sibling, &counters))
			return false;
	}

	if (!tx2_uncore_validate_event(event->pmu, event, &counters))
		return false;

	/*
	 * If the group requires more counters than the HW has,
	 * it cannot ever be scheduled.
	 */
	return counters <= TX2_PMU_MAX_COUNTERS;
}


static int tx2_uncore_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct tx2_uncore_pmu *tx2_pmu;

	/* Test the event attr type check for PMU enumeration */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/*
	 * SOC PMU counters are shared across all cores.
	 * Therefore, it does not support per-process mode.
	 * Also, it does not support event sampling mode.
	 */
	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	tx2_pmu = pmu_to_tx2_pmu(event->pmu);
	if (tx2_pmu->cpu >= nr_cpu_ids)
		return -EINVAL;
	event->cpu = tx2_pmu->cpu;

	if (event->attr.config >= tx2_pmu->max_events)
		return -EINVAL;

	/* store event id */
	hwc->config = event->attr.config;

	/* Validate the group */
	if (!tx2_uncore_validate_event_group(event))
		return -EINVAL;

	return 0;
}

static void tx2_uncore_event_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct tx2_uncore_pmu *tx2_pmu;

	hwc->state = 0;
	tx2_pmu = pmu_to_tx2_pmu(event->pmu);

	tx2_pmu->start_event(event, flags);
	perf_event_update_userpage(event);

	/* Start timer for first event */
	if (bitmap_weight(tx2_pmu->active_counters,
				tx2_pmu->max_counters) == 1) {
		hrtimer_start(&tx2_pmu->hrtimer,
			ns_to_ktime(tx2_pmu->hrtimer_interval),
			HRTIMER_MODE_REL_PINNED);
	}
}

static void tx2_uncore_event_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct tx2_uncore_pmu *tx2_pmu;

	if (hwc->state & PERF_HES_UPTODATE)
		return;

	tx2_pmu = pmu_to_tx2_pmu(event->pmu);
	tx2_pmu->stop_event(event);
	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;
	if (flags & PERF_EF_UPDATE) {
		tx2_uncore_event_update(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static int tx2_uncore_event_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct tx2_uncore_pmu *tx2_pmu;

	tx2_pmu = pmu_to_tx2_pmu(event->pmu);

	/* Allocate a free counter */
	hwc->idx  = alloc_counter(tx2_pmu);
	if (hwc->idx < 0)
		return -EAGAIN;

	tx2_pmu->events[hwc->idx] = event;
	/* set counter control and data registers base address */
	tx2_pmu->init_cntr_base(event, tx2_pmu);

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (flags & PERF_EF_START)
		tx2_uncore_event_start(event, flags);

	return 0;
}

static void tx2_uncore_event_del(struct perf_event *event, int flags)
{
	struct tx2_uncore_pmu *tx2_pmu = pmu_to_tx2_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	tx2_uncore_event_stop(event, PERF_EF_UPDATE);

	/* clear the assigned counter */
	free_counter(tx2_pmu, GET_COUNTERID(event));

	perf_event_update_userpage(event);
	tx2_pmu->events[hwc->idx] = NULL;
	hwc->idx = -1;
}

static void tx2_uncore_event_read(struct perf_event *event)
{
	tx2_uncore_event_update(event);
}

static enum hrtimer_restart tx2_hrtimer_callback(struct hrtimer *timer)
{
	struct tx2_uncore_pmu *tx2_pmu;
	int max_counters, idx;

	tx2_pmu = container_of(timer, struct tx2_uncore_pmu, hrtimer);
	max_counters = tx2_pmu->max_counters;

	if (bitmap_empty(tx2_pmu->active_counters, max_counters))
		return HRTIMER_NORESTART;

	for_each_set_bit(idx, tx2_pmu->active_counters, max_counters) {
		struct perf_event *event = tx2_pmu->events[idx];

		tx2_uncore_event_update(event);
	}
	hrtimer_forward_now(timer, ns_to_ktime(tx2_pmu->hrtimer_interval));
	return HRTIMER_RESTART;
}

static int tx2_uncore_pmu_register(
		struct tx2_uncore_pmu *tx2_pmu)
{
	struct device *dev = tx2_pmu->dev;
	char *name = tx2_pmu->name;

	/* Perf event registration */
	tx2_pmu->pmu = (struct pmu) {
		.module         = THIS_MODULE,
		.attr_groups	= tx2_pmu->attr_groups,
		.task_ctx_nr	= perf_invalid_context,
		.event_init	= tx2_uncore_event_init,
		.add		= tx2_uncore_event_add,
		.del		= tx2_uncore_event_del,
		.start		= tx2_uncore_event_start,
		.stop		= tx2_uncore_event_stop,
		.read		= tx2_uncore_event_read,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
	};

	tx2_pmu->pmu.name = devm_kasprintf(dev, GFP_KERNEL,
			"%s", name);

	return perf_pmu_register(&tx2_pmu->pmu, tx2_pmu->pmu.name, -1);
}

static int tx2_uncore_pmu_add_dev(struct tx2_uncore_pmu *tx2_pmu)
{
	int ret, cpu;

	cpu = cpumask_any_and(cpumask_of_node(tx2_pmu->node),
			cpu_online_mask);

	tx2_pmu->cpu = cpu;
	hrtimer_init(&tx2_pmu->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	tx2_pmu->hrtimer.function = tx2_hrtimer_callback;

	ret = tx2_uncore_pmu_register(tx2_pmu);
	if (ret) {
		dev_err(tx2_pmu->dev, "%s PMU: Failed to init driver\n",
				tx2_pmu->name);
		return -ENODEV;
	}

	/* register hotplug callback for the pmu */
	ret = cpuhp_state_add_instance(
			CPUHP_AP_PERF_ARM_CAVIUM_TX2_UNCORE_ONLINE,
			&tx2_pmu->hpnode);
	if (ret) {
		dev_err(tx2_pmu->dev, "Error %d registering hotplug", ret);
		return ret;
	}

	/* Add to list */
	list_add(&tx2_pmu->entry, &tx2_pmus);

	dev_dbg(tx2_pmu->dev, "%s PMU UNCORE registered\n",
			tx2_pmu->pmu.name);
	return ret;
}

static struct tx2_uncore_pmu *tx2_uncore_pmu_init_dev(struct device *dev,
		acpi_handle handle, struct acpi_device *adev, u32 type)
{
	struct tx2_uncore_pmu *tx2_pmu;
	void __iomem *base;
	struct resource res;
	struct resource_entry *rentry;
	struct list_head list;
	int ret;

	INIT_LIST_HEAD(&list);
	ret = acpi_dev_get_resources(adev, &list, NULL, NULL);
	if (ret <= 0) {
		dev_err(dev, "failed to parse _CRS method, error %d\n", ret);
		return NULL;
	}

	list_for_each_entry(rentry, &list, node) {
		if (resource_type(rentry->res) == IORESOURCE_MEM) {
			res = *rentry->res;
			break;
		}
	}

	if (!rentry->res)
		return NULL;

	acpi_dev_free_resource_list(&list);
	base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(base)) {
		dev_err(dev, "PMU type %d: Fail to map resource\n", type);
		return NULL;
	}

	tx2_pmu = devm_kzalloc(dev, sizeof(*tx2_pmu), GFP_KERNEL);
	if (!tx2_pmu)
		return NULL;

	tx2_pmu->dev = dev;
	tx2_pmu->type = type;
	tx2_pmu->base = base;
	tx2_pmu->node = dev_to_node(dev);
	INIT_LIST_HEAD(&tx2_pmu->entry);

	switch (tx2_pmu->type) {
	case PMU_TYPE_L3C:
		tx2_pmu->max_counters = TX2_PMU_MAX_COUNTERS;
		tx2_pmu->prorate_factor = TX2_PMU_L3_TILES;
		tx2_pmu->max_events = L3_EVENT_MAX;
		tx2_pmu->hrtimer_interval = TX2_PMU_HRTIMER_INTERVAL;
		tx2_pmu->attr_groups = l3c_pmu_attr_groups;
		tx2_pmu->name = devm_kasprintf(dev, GFP_KERNEL,
				"uncore_l3c_%d", tx2_pmu->node);
		tx2_pmu->init_cntr_base = init_cntr_base_l3c;
		tx2_pmu->start_event = uncore_start_event_l3c;
		tx2_pmu->stop_event = uncore_stop_event_l3c;
		break;
	case PMU_TYPE_DMC:
		tx2_pmu->max_counters = TX2_PMU_MAX_COUNTERS;
		tx2_pmu->prorate_factor = TX2_PMU_DMC_CHANNELS;
		tx2_pmu->max_events = DMC_EVENT_MAX;
		tx2_pmu->hrtimer_interval = TX2_PMU_HRTIMER_INTERVAL;
		tx2_pmu->attr_groups = dmc_pmu_attr_groups;
		tx2_pmu->name = devm_kasprintf(dev, GFP_KERNEL,
				"uncore_dmc_%d", tx2_pmu->node);
		tx2_pmu->init_cntr_base = init_cntr_base_dmc;
		tx2_pmu->start_event = uncore_start_event_dmc;
		tx2_pmu->stop_event = uncore_stop_event_dmc;
		break;
	case PMU_TYPE_INVALID:
		devm_kfree(dev, tx2_pmu);
		return NULL;
	}

	return tx2_pmu;
}

static acpi_status tx2_uncore_pmu_add(acpi_handle handle, u32 level,
				    void *data, void **return_value)
{
	struct tx2_uncore_pmu *tx2_pmu;
	struct acpi_device *adev;
	enum tx2_uncore_type type;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;
	if (acpi_bus_get_status(adev) || !adev->status.present)
		return AE_OK;

	type = get_tx2_pmu_type(adev);
	if (type == PMU_TYPE_INVALID)
		return AE_OK;

	tx2_pmu = tx2_uncore_pmu_init_dev((struct device *)data,
			handle, adev, type);

	if (!tx2_pmu)
		return AE_ERROR;

	if (tx2_uncore_pmu_add_dev(tx2_pmu)) {
		/* Can't add the PMU device, abort */
		return AE_ERROR;
	}
	return AE_OK;
}

static int tx2_uncore_pmu_online_cpu(unsigned int cpu,
		struct hlist_node *hpnode)
{
	struct tx2_uncore_pmu *tx2_pmu;

	tx2_pmu = hlist_entry_safe(hpnode,
			struct tx2_uncore_pmu, hpnode);

	/* Pick this CPU, If there is no CPU/PMU association and both are
	 * from same node.
	 */
	if ((tx2_pmu->cpu >= nr_cpu_ids) &&
		(tx2_pmu->node == cpu_to_node(cpu)))
		tx2_pmu->cpu = cpu;

	return 0;
}

static int tx2_uncore_pmu_offline_cpu(unsigned int cpu,
		struct hlist_node *hpnode)
{
	int new_cpu;
	struct tx2_uncore_pmu *tx2_pmu;
	struct cpumask cpu_online_mask_temp;

	tx2_pmu = hlist_entry_safe(hpnode,
			struct tx2_uncore_pmu, hpnode);

	if (cpu != tx2_pmu->cpu)
		return 0;

	hrtimer_cancel(&tx2_pmu->hrtimer);
	cpumask_copy(&cpu_online_mask_temp, cpu_online_mask);
	cpumask_clear_cpu(cpu, &cpu_online_mask_temp);
	new_cpu = cpumask_any_and(
			cpumask_of_node(tx2_pmu->node),
			&cpu_online_mask_temp);

	tx2_pmu->cpu = new_cpu;
	if (new_cpu >= nr_cpu_ids)
		return 0;
	perf_pmu_migrate_context(&tx2_pmu->pmu, cpu, new_cpu);

	return 0;
}

static const struct acpi_device_id tx2_uncore_acpi_match[] = {
	{"CAV901C", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, tx2_uncore_acpi_match);

static int tx2_uncore_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	acpi_handle handle;
	acpi_status status;

	set_dev_node(dev, acpi_get_node(ACPI_HANDLE(dev)));

	if (!has_acpi_companion(dev))
		return -ENODEV;

	handle = ACPI_HANDLE(dev);
	if (!handle)
		return -EINVAL;

	/* Walk through the tree for all PMU UNCORE devices */
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, 1,
				     tx2_uncore_pmu_add,
				     NULL, dev, NULL);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "failed to probe PMU devices\n");
		return_ACPI_STATUS(status);
	}

	dev_info(dev, "node%d: pmu uncore registered\n", dev_to_node(dev));
	return 0;
}

static int tx2_uncore_remove(struct platform_device *pdev)
{
	struct tx2_uncore_pmu *tx2_pmu, *temp;
	struct device *dev = &pdev->dev;

	if (!list_empty(&tx2_pmus)) {
		list_for_each_entry_safe(tx2_pmu, temp, &tx2_pmus, entry) {
			if (tx2_pmu->node == dev_to_node(dev)) {
				cpuhp_state_remove_instance_nocalls(
					CPUHP_AP_PERF_ARM_CAVIUM_TX2_UNCORE_ONLINE,
					&tx2_pmu->hpnode);
				perf_pmu_unregister(&tx2_pmu->pmu);
				list_del(&tx2_pmu->entry);
			}
		}
	}
	return 0;
}

static struct platform_driver tx2_uncore_driver = {
	.driver = {
		.name		= "tx2-uncore-pmu",
		.acpi_match_table = ACPI_PTR(tx2_uncore_acpi_match),
	},
	.probe = tx2_uncore_probe,
	.remove = tx2_uncore_remove,
};

static int __init tx2_uncore_driver_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_CAVIUM_TX2_UNCORE_ONLINE,
				      "perf/tx2/uncore:online",
				      tx2_uncore_pmu_online_cpu,
				      tx2_uncore_pmu_offline_cpu);
	if (ret) {
		pr_err("TX2 PMU: setup hotplug failed(%d)\n", ret);
		return ret;
	}
	ret = platform_driver_register(&tx2_uncore_driver);
	if (ret)
		cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_CAVIUM_TX2_UNCORE_ONLINE);

	return ret;
}
module_init(tx2_uncore_driver_init);

static void __exit tx2_uncore_driver_exit(void)
{
	platform_driver_unregister(&tx2_uncore_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_CAVIUM_TX2_UNCORE_ONLINE);
}
module_exit(tx2_uncore_driver_exit);

MODULE_DESCRIPTION("ThunderX2 UNCORE PMU driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ganapatrao Kulkarni <gkulkarni@cavium.com>");
