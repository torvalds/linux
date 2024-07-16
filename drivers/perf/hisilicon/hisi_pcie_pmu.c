// SPDX-License-Identifier: GPL-2.0-only
/*
 * This driver adds support for PCIe PMU RCiEP device. Related
 * perf events are bandwidth, latency etc.
 *
 * Copyright (C) 2021 HiSilicon Limited
 * Author: Qi Liu <liuqi115@huawei.com>
 */
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bug.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/perf_event.h>

#define DRV_NAME "hisi_pcie_pmu"
/* Define registers */
#define HISI_PCIE_GLOBAL_CTRL		0x00
#define HISI_PCIE_EVENT_CTRL		0x010
#define HISI_PCIE_CNT			0x090
#define HISI_PCIE_EXT_CNT		0x110
#define HISI_PCIE_INT_STAT		0x150
#define HISI_PCIE_INT_MASK		0x154
#define HISI_PCIE_REG_BDF		0xfe0
#define HISI_PCIE_REG_VERSION		0xfe4
#define HISI_PCIE_REG_INFO		0xfe8

/* Define command in HISI_PCIE_GLOBAL_CTRL */
#define HISI_PCIE_GLOBAL_EN		0x01
#define HISI_PCIE_GLOBAL_NONE		0

/* Define command in HISI_PCIE_EVENT_CTRL */
#define HISI_PCIE_EVENT_EN		BIT_ULL(20)
#define HISI_PCIE_RESET_CNT		BIT_ULL(22)
#define HISI_PCIE_INIT_SET		BIT_ULL(34)
#define HISI_PCIE_THR_EN		BIT_ULL(26)
#define HISI_PCIE_TARGET_EN		BIT_ULL(32)
#define HISI_PCIE_TRIG_EN		BIT_ULL(52)

/* Define offsets in HISI_PCIE_EVENT_CTRL */
#define HISI_PCIE_EVENT_M		GENMASK_ULL(15, 0)
#define HISI_PCIE_THR_MODE_M		GENMASK_ULL(27, 27)
#define HISI_PCIE_THR_M			GENMASK_ULL(31, 28)
#define HISI_PCIE_LEN_M			GENMASK_ULL(35, 34)
#define HISI_PCIE_TARGET_M		GENMASK_ULL(52, 36)
#define HISI_PCIE_TRIG_MODE_M		GENMASK_ULL(53, 53)
#define HISI_PCIE_TRIG_M		GENMASK_ULL(59, 56)

/* Default config of TLP length mode, will count both TLP headers and payloads */
#define HISI_PCIE_LEN_M_DEFAULT		3ULL

#define HISI_PCIE_MAX_COUNTERS		8
#define HISI_PCIE_REG_STEP		8
#define HISI_PCIE_THR_MAX_VAL		10
#define HISI_PCIE_TRIG_MAX_VAL		10
#define HISI_PCIE_MAX_PERIOD		(GENMASK_ULL(63, 0))
#define HISI_PCIE_INIT_VAL		BIT_ULL(63)

struct hisi_pcie_pmu {
	struct perf_event *hw_events[HISI_PCIE_MAX_COUNTERS];
	struct hlist_node node;
	struct pci_dev *pdev;
	struct pmu pmu;
	void __iomem *base;
	int irq;
	u32 identifier;
	/* Minimum and maximum BDF of root ports monitored by PMU */
	u16 bdf_min;
	u16 bdf_max;
	int on_cpu;
};

struct hisi_pcie_reg_pair {
	u16 lo;
	u16 hi;
};

#define to_pcie_pmu(p)  (container_of((p), struct hisi_pcie_pmu, pmu))
#define GET_PCI_DEVFN(bdf)  ((bdf) & 0xff)

#define HISI_PCIE_PMU_FILTER_ATTR(_name, _config, _hi, _lo)		  \
	static u64 hisi_pcie_get_##_name(struct perf_event *event)	  \
	{								  \
		return FIELD_GET(GENMASK(_hi, _lo), event->attr._config); \
	}								  \

HISI_PCIE_PMU_FILTER_ATTR(event, config, 16, 0);
HISI_PCIE_PMU_FILTER_ATTR(thr_len, config1, 3, 0);
HISI_PCIE_PMU_FILTER_ATTR(thr_mode, config1, 4, 4);
HISI_PCIE_PMU_FILTER_ATTR(trig_len, config1, 8, 5);
HISI_PCIE_PMU_FILTER_ATTR(trig_mode, config1, 9, 9);
HISI_PCIE_PMU_FILTER_ATTR(len_mode, config1, 11, 10);
HISI_PCIE_PMU_FILTER_ATTR(port, config2, 15, 0);
HISI_PCIE_PMU_FILTER_ATTR(bdf, config2, 31, 16);

static ssize_t hisi_pcie_event_sysfs_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	struct perf_pmu_events_attr *pmu_attr =
		container_of(attr, struct perf_pmu_events_attr, attr);

	return sysfs_emit(buf, "config=0x%llx\n", pmu_attr->id);
}

#define HISI_PCIE_PMU_FORMAT_ATTR(_name, _format)                              \
	(&((struct dev_ext_attribute[]){                                       \
		{ .attr = __ATTR(_name, 0444, device_show_string, NULL),       \
		  .var = (void *)_format }                                     \
	})[0].attr.attr)

#define HISI_PCIE_PMU_EVENT_ATTR(_name, _id)			\
	PMU_EVENT_ATTR_ID(_name, hisi_pcie_event_sysfs_show, _id)

static ssize_t cpumask_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(pcie_pmu->on_cpu));
}
static DEVICE_ATTR_RO(cpumask);

static ssize_t identifier_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "%#x\n", pcie_pmu->identifier);
}
static DEVICE_ATTR_RO(identifier);

static ssize_t bus_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "%#04x\n", PCI_BUS_NUM(pcie_pmu->bdf_min));
}
static DEVICE_ATTR_RO(bus);

static struct hisi_pcie_reg_pair
hisi_pcie_parse_reg_value(struct hisi_pcie_pmu *pcie_pmu, u32 reg_off)
{
	u32 val = readl_relaxed(pcie_pmu->base + reg_off);
	struct hisi_pcie_reg_pair regs = {
		.lo = val,
		.hi = val >> 16,
	};

	return regs;
}

/*
 * Hardware counter and ext_counter work together for bandwidth, latency, bus
 * utilization and buffer occupancy events. For example, RX memory write latency
 * events(index = 0x0010), counter counts total delay cycles and ext_counter
 * counts RX memory write PCIe packets number.
 *
 * As we don't want PMU driver to process these two data, "delay cycles" can
 * be treated as an independent event(index = 0x0010), "RX memory write packets
 * number" as another(index = 0x10010). BIT 16 is used to distinguish and 0-15
 * bits are "real" event index, which can be used to set HISI_PCIE_EVENT_CTRL.
 */
#define EXT_COUNTER_IS_USED(idx)		((idx) & BIT(16))

static u32 hisi_pcie_get_real_event(struct perf_event *event)
{
	return hisi_pcie_get_event(event) & GENMASK(15, 0);
}

static u32 hisi_pcie_pmu_get_offset(u32 offset, u32 idx)
{
	return offset + HISI_PCIE_REG_STEP * idx;
}

static u32 hisi_pcie_pmu_readl(struct hisi_pcie_pmu *pcie_pmu, u32 reg_offset,
			       u32 idx)
{
	u32 offset = hisi_pcie_pmu_get_offset(reg_offset, idx);

	return readl_relaxed(pcie_pmu->base + offset);
}

static void hisi_pcie_pmu_writel(struct hisi_pcie_pmu *pcie_pmu, u32 reg_offset, u32 idx, u32 val)
{
	u32 offset = hisi_pcie_pmu_get_offset(reg_offset, idx);

	writel_relaxed(val, pcie_pmu->base + offset);
}

static u64 hisi_pcie_pmu_readq(struct hisi_pcie_pmu *pcie_pmu, u32 reg_offset, u32 idx)
{
	u32 offset = hisi_pcie_pmu_get_offset(reg_offset, idx);

	return readq_relaxed(pcie_pmu->base + offset);
}

static void hisi_pcie_pmu_writeq(struct hisi_pcie_pmu *pcie_pmu, u32 reg_offset, u32 idx, u64 val)
{
	u32 offset = hisi_pcie_pmu_get_offset(reg_offset, idx);

	writeq_relaxed(val, pcie_pmu->base + offset);
}

static u64 hisi_pcie_pmu_get_event_ctrl_val(struct perf_event *event)
{
	u64 port, trig_len, thr_len, len_mode;
	u64 reg = HISI_PCIE_INIT_SET;

	/* Config HISI_PCIE_EVENT_CTRL according to event. */
	reg |= FIELD_PREP(HISI_PCIE_EVENT_M, hisi_pcie_get_real_event(event));

	/* Config HISI_PCIE_EVENT_CTRL according to root port or EP device. */
	port = hisi_pcie_get_port(event);
	if (port)
		reg |= FIELD_PREP(HISI_PCIE_TARGET_M, port);
	else
		reg |= HISI_PCIE_TARGET_EN |
		       FIELD_PREP(HISI_PCIE_TARGET_M, hisi_pcie_get_bdf(event));

	/* Config HISI_PCIE_EVENT_CTRL according to trigger condition. */
	trig_len = hisi_pcie_get_trig_len(event);
	if (trig_len) {
		reg |= FIELD_PREP(HISI_PCIE_TRIG_M, trig_len);
		reg |= FIELD_PREP(HISI_PCIE_TRIG_MODE_M, hisi_pcie_get_trig_mode(event));
		reg |= HISI_PCIE_TRIG_EN;
	}

	/* Config HISI_PCIE_EVENT_CTRL according to threshold condition. */
	thr_len = hisi_pcie_get_thr_len(event);
	if (thr_len) {
		reg |= FIELD_PREP(HISI_PCIE_THR_M, thr_len);
		reg |= FIELD_PREP(HISI_PCIE_THR_MODE_M, hisi_pcie_get_thr_mode(event));
		reg |= HISI_PCIE_THR_EN;
	}

	len_mode = hisi_pcie_get_len_mode(event);
	if (len_mode)
		reg |= FIELD_PREP(HISI_PCIE_LEN_M, len_mode);
	else
		reg |= FIELD_PREP(HISI_PCIE_LEN_M, HISI_PCIE_LEN_M_DEFAULT);

	return reg;
}

static void hisi_pcie_pmu_config_event_ctrl(struct perf_event *event)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 reg = hisi_pcie_pmu_get_event_ctrl_val(event);

	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, hwc->idx, reg);
}

static void hisi_pcie_pmu_clear_event_ctrl(struct perf_event *event)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, hwc->idx, HISI_PCIE_INIT_SET);
}

static bool hisi_pcie_pmu_valid_requester_id(struct hisi_pcie_pmu *pcie_pmu, u32 bdf)
{
	struct pci_dev *root_port, *pdev;
	u16 rp_bdf;

	pdev = pci_get_domain_bus_and_slot(pci_domain_nr(pcie_pmu->pdev->bus), PCI_BUS_NUM(bdf),
					   GET_PCI_DEVFN(bdf));
	if (!pdev)
		return false;

	root_port = pcie_find_root_port(pdev);
	if (!root_port) {
		pci_dev_put(pdev);
		return false;
	}

	pci_dev_put(pdev);
	rp_bdf = pci_dev_id(root_port);
	return rp_bdf >= pcie_pmu->bdf_min && rp_bdf <= pcie_pmu->bdf_max;
}

static bool hisi_pcie_pmu_valid_filter(struct perf_event *event,
				       struct hisi_pcie_pmu *pcie_pmu)
{
	u32 requester_id = hisi_pcie_get_bdf(event);

	if (hisi_pcie_get_thr_len(event) > HISI_PCIE_THR_MAX_VAL)
		return false;

	if (hisi_pcie_get_trig_len(event) > HISI_PCIE_TRIG_MAX_VAL)
		return false;

	/* Need to explicitly set filter of "port" or "bdf" */
	if (!hisi_pcie_get_port(event) &&
	    !hisi_pcie_pmu_valid_requester_id(pcie_pmu, requester_id))
		return false;

	return true;
}

/*
 * Check Whether two events share the same config. The same config means not
 * only the event code, but also the filter settings of the two events are
 * the same.
 */
static bool hisi_pcie_pmu_cmp_event(struct perf_event *target,
					struct perf_event *event)
{
	return hisi_pcie_pmu_get_event_ctrl_val(target) ==
	       hisi_pcie_pmu_get_event_ctrl_val(event);
}

static bool hisi_pcie_pmu_validate_event_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct perf_event *event_group[HISI_PCIE_MAX_COUNTERS];
	int counters = 1;
	int num;

	event_group[0] = leader;
	if (!is_software_event(leader)) {
		if (leader->pmu != event->pmu)
			return false;

		if (leader != event && !hisi_pcie_pmu_cmp_event(leader, event))
			event_group[counters++] = event;
	}

	for_each_sibling_event(sibling, event->group_leader) {
		if (is_software_event(sibling))
			continue;

		if (sibling->pmu != event->pmu)
			return false;

		for (num = 0; num < counters; num++) {
			/*
			 * If we find a related event, then it's a valid group
			 * since we don't need to allocate a new counter for it.
			 */
			if (hisi_pcie_pmu_cmp_event(event_group[num], sibling))
				break;
		}

		/*
		 * Otherwise it's a new event but if there's no available counter,
		 * fail the check since we cannot schedule all the events in
		 * the group simultaneously.
		 */
		if (num == HISI_PCIE_MAX_COUNTERS)
			return false;

		if (num == counters)
			event_group[counters++] = sibling;
	}

	return true;
}

static int hisi_pcie_pmu_event_init(struct perf_event *event)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	/* Check the type first before going on, otherwise it's not our event */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (EXT_COUNTER_IS_USED(hisi_pcie_get_event(event)))
		hwc->event_base = HISI_PCIE_EXT_CNT;
	else
		hwc->event_base = HISI_PCIE_CNT;

	/* Sampling is not supported. */
	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	if (!hisi_pcie_pmu_valid_filter(event, pcie_pmu))
		return -EINVAL;

	if (!hisi_pcie_pmu_validate_event_group(event))
		return -EINVAL;

	event->cpu = pcie_pmu->on_cpu;

	return 0;
}

static u64 hisi_pcie_pmu_read_counter(struct perf_event *event)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	u32 idx = event->hw.idx;

	return hisi_pcie_pmu_readq(pcie_pmu, event->hw.event_base, idx);
}

/*
 * Check all work events, if a relevant event is found then we return it
 * first, otherwise return the first idle counter (need to reset).
 */
static int hisi_pcie_pmu_get_event_idx(struct hisi_pcie_pmu *pcie_pmu,
					struct perf_event *event)
{
	int first_idle = -EAGAIN;
	struct perf_event *sibling;
	int idx;

	for (idx = 0; idx < HISI_PCIE_MAX_COUNTERS; idx++) {
		sibling = pcie_pmu->hw_events[idx];
		if (!sibling) {
			if (first_idle == -EAGAIN)
				first_idle = idx;
			continue;
		}

		/* Related events must be used in group */
		if (hisi_pcie_pmu_cmp_event(sibling, event) &&
		    sibling->group_leader == event->group_leader)
			return idx;
	}

	return first_idle;
}

static void hisi_pcie_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 new_cnt, prev_cnt, delta;

	do {
		prev_cnt = local64_read(&hwc->prev_count);
		new_cnt = hisi_pcie_pmu_read_counter(event);
	} while (local64_cmpxchg(&hwc->prev_count, prev_cnt,
				 new_cnt) != prev_cnt);

	delta = (new_cnt - prev_cnt) & HISI_PCIE_MAX_PERIOD;
	local64_add(delta, &event->count);
}

static void hisi_pcie_pmu_read(struct perf_event *event)
{
	hisi_pcie_pmu_event_update(event);
}

static void hisi_pcie_pmu_set_period(struct perf_event *event)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	local64_set(&hwc->prev_count, HISI_PCIE_INIT_VAL);
	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_CNT, idx, HISI_PCIE_INIT_VAL);
	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EXT_CNT, idx, HISI_PCIE_INIT_VAL);
}

static void hisi_pcie_pmu_enable_counter(struct hisi_pcie_pmu *pcie_pmu, struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;
	u64 val;

	val = hisi_pcie_pmu_readq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx);
	val |= HISI_PCIE_EVENT_EN;
	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx, val);
}

static void hisi_pcie_pmu_disable_counter(struct hisi_pcie_pmu *pcie_pmu, struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;
	u64 val;

	val = hisi_pcie_pmu_readq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx);
	val &= ~HISI_PCIE_EVENT_EN;
	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx, val);
}

static void hisi_pcie_pmu_enable_int(struct hisi_pcie_pmu *pcie_pmu, struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;

	hisi_pcie_pmu_writel(pcie_pmu, HISI_PCIE_INT_MASK, idx, 0);
}

static void hisi_pcie_pmu_disable_int(struct hisi_pcie_pmu *pcie_pmu, struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;

	hisi_pcie_pmu_writel(pcie_pmu, HISI_PCIE_INT_MASK, idx, 1);
}

static void hisi_pcie_pmu_reset_counter(struct hisi_pcie_pmu *pcie_pmu, int idx)
{
	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx, HISI_PCIE_RESET_CNT);
	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx, HISI_PCIE_INIT_SET);
}

static void hisi_pcie_pmu_start(struct perf_event *event, int flags)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	u64 prev_cnt;

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
	hwc->state = 0;

	hisi_pcie_pmu_config_event_ctrl(event);
	hisi_pcie_pmu_enable_counter(pcie_pmu, hwc);
	hisi_pcie_pmu_enable_int(pcie_pmu, hwc);
	hisi_pcie_pmu_set_period(event);

	if (flags & PERF_EF_RELOAD) {
		prev_cnt = local64_read(&hwc->prev_count);
		hisi_pcie_pmu_writeq(pcie_pmu, hwc->event_base, idx, prev_cnt);
	}

	perf_event_update_userpage(event);
}

static void hisi_pcie_pmu_stop(struct perf_event *event, int flags)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hisi_pcie_pmu_event_update(event);
	hisi_pcie_pmu_disable_int(pcie_pmu, hwc);
	hisi_pcie_pmu_disable_counter(pcie_pmu, hwc);
	hisi_pcie_pmu_clear_event_ctrl(event);
	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if (hwc->state & PERF_HES_UPTODATE)
		return;

	hwc->state |= PERF_HES_UPTODATE;
}

static int hisi_pcie_pmu_add(struct perf_event *event, int flags)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	idx = hisi_pcie_pmu_get_event_idx(pcie_pmu, event);
	if (idx < 0)
		return idx;

	hwc->idx = idx;

	/* No enabled counter found with related event, reset it */
	if (!pcie_pmu->hw_events[idx]) {
		hisi_pcie_pmu_reset_counter(pcie_pmu, idx);
		pcie_pmu->hw_events[idx] = event;
	}

	if (flags & PERF_EF_START)
		hisi_pcie_pmu_start(event, PERF_EF_RELOAD);

	return 0;
}

static void hisi_pcie_pmu_del(struct perf_event *event, int flags)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hisi_pcie_pmu_stop(event, PERF_EF_UPDATE);
	pcie_pmu->hw_events[hwc->idx] = NULL;
	perf_event_update_userpage(event);
}

static void hisi_pcie_pmu_enable(struct pmu *pmu)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(pmu);
	int num;

	for (num = 0; num < HISI_PCIE_MAX_COUNTERS; num++) {
		if (pcie_pmu->hw_events[num])
			break;
	}

	if (num == HISI_PCIE_MAX_COUNTERS)
		return;

	writel(HISI_PCIE_GLOBAL_EN, pcie_pmu->base + HISI_PCIE_GLOBAL_CTRL);
}

static void hisi_pcie_pmu_disable(struct pmu *pmu)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(pmu);

	writel(HISI_PCIE_GLOBAL_NONE, pcie_pmu->base + HISI_PCIE_GLOBAL_CTRL);
}

static irqreturn_t hisi_pcie_pmu_irq(int irq, void *data)
{
	struct hisi_pcie_pmu *pcie_pmu = data;
	irqreturn_t ret = IRQ_NONE;
	struct perf_event *event;
	u32 overflown;
	int idx;

	for (idx = 0; idx < HISI_PCIE_MAX_COUNTERS; idx++) {
		overflown = hisi_pcie_pmu_readl(pcie_pmu, HISI_PCIE_INT_STAT, idx);
		if (!overflown)
			continue;

		/* Clear status of interrupt. */
		hisi_pcie_pmu_writel(pcie_pmu, HISI_PCIE_INT_STAT, idx, 1);
		event = pcie_pmu->hw_events[idx];
		if (!event)
			continue;

		hisi_pcie_pmu_event_update(event);
		hisi_pcie_pmu_set_period(event);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int hisi_pcie_pmu_irq_register(struct pci_dev *pdev, struct hisi_pcie_pmu *pcie_pmu)
{
	int irq, ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret < 0) {
		pci_err(pdev, "Failed to enable MSI vectors: %d\n", ret);
		return ret;
	}

	irq = pci_irq_vector(pdev, 0);
	ret = request_irq(irq, hisi_pcie_pmu_irq, IRQF_NOBALANCING | IRQF_NO_THREAD, DRV_NAME,
			  pcie_pmu);
	if (ret) {
		pci_err(pdev, "Failed to register IRQ: %d\n", ret);
		pci_free_irq_vectors(pdev);
		return ret;
	}

	pcie_pmu->irq = irq;

	return 0;
}

static void hisi_pcie_pmu_irq_unregister(struct pci_dev *pdev, struct hisi_pcie_pmu *pcie_pmu)
{
	free_irq(pcie_pmu->irq, pcie_pmu);
	pci_free_irq_vectors(pdev);
}

static int hisi_pcie_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_pcie_pmu *pcie_pmu = hlist_entry_safe(node, struct hisi_pcie_pmu, node);

	if (pcie_pmu->on_cpu == -1) {
		pcie_pmu->on_cpu = cpumask_local_spread(0, dev_to_node(&pcie_pmu->pdev->dev));
		WARN_ON(irq_set_affinity(pcie_pmu->irq, cpumask_of(pcie_pmu->on_cpu)));
	}

	return 0;
}

static int hisi_pcie_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_pcie_pmu *pcie_pmu = hlist_entry_safe(node, struct hisi_pcie_pmu, node);
	unsigned int target;
	int numa_node;

	/* Nothing to do if this CPU doesn't own the PMU */
	if (pcie_pmu->on_cpu != cpu)
		return 0;

	pcie_pmu->on_cpu = -1;

	/* Choose a local CPU from all online cpus. */
	numa_node = dev_to_node(&pcie_pmu->pdev->dev);

	target = cpumask_any_and_but(cpumask_of_node(numa_node),
				     cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		target = cpumask_any_but(cpu_online_mask, cpu);

	if (target >= nr_cpu_ids) {
		pci_err(pcie_pmu->pdev, "There is no CPU to set\n");
		return 0;
	}

	perf_pmu_migrate_context(&pcie_pmu->pmu, cpu, target);
	/* Use this CPU for event counting */
	pcie_pmu->on_cpu = target;
	WARN_ON(irq_set_affinity(pcie_pmu->irq, cpumask_of(target)));

	return 0;
}

static struct attribute *hisi_pcie_pmu_events_attr[] = {
	HISI_PCIE_PMU_EVENT_ATTR(rx_mwr_latency, 0x0010),
	HISI_PCIE_PMU_EVENT_ATTR(rx_mwr_cnt, 0x10010),
	HISI_PCIE_PMU_EVENT_ATTR(rx_mrd_latency, 0x0210),
	HISI_PCIE_PMU_EVENT_ATTR(rx_mrd_cnt, 0x10210),
	HISI_PCIE_PMU_EVENT_ATTR(tx_mrd_latency, 0x0011),
	HISI_PCIE_PMU_EVENT_ATTR(tx_mrd_cnt, 0x10011),
	HISI_PCIE_PMU_EVENT_ATTR(rx_mwr_flux, 0x0104),
	HISI_PCIE_PMU_EVENT_ATTR(rx_mwr_time, 0x10104),
	HISI_PCIE_PMU_EVENT_ATTR(rx_mrd_flux, 0x0804),
	HISI_PCIE_PMU_EVENT_ATTR(rx_mrd_time, 0x10804),
	HISI_PCIE_PMU_EVENT_ATTR(rx_cpl_flux, 0x2004),
	HISI_PCIE_PMU_EVENT_ATTR(rx_cpl_time, 0x12004),
	HISI_PCIE_PMU_EVENT_ATTR(tx_mwr_flux, 0x0105),
	HISI_PCIE_PMU_EVENT_ATTR(tx_mwr_time, 0x10105),
	HISI_PCIE_PMU_EVENT_ATTR(tx_mrd_flux, 0x0405),
	HISI_PCIE_PMU_EVENT_ATTR(tx_mrd_time, 0x10405),
	HISI_PCIE_PMU_EVENT_ATTR(tx_cpl_flux, 0x1005),
	HISI_PCIE_PMU_EVENT_ATTR(tx_cpl_time, 0x11005),
	NULL
};

static struct attribute_group hisi_pcie_pmu_events_group = {
	.name = "events",
	.attrs = hisi_pcie_pmu_events_attr,
};

static struct attribute *hisi_pcie_pmu_format_attr[] = {
	HISI_PCIE_PMU_FORMAT_ATTR(event, "config:0-16"),
	HISI_PCIE_PMU_FORMAT_ATTR(thr_len, "config1:0-3"),
	HISI_PCIE_PMU_FORMAT_ATTR(thr_mode, "config1:4"),
	HISI_PCIE_PMU_FORMAT_ATTR(trig_len, "config1:5-8"),
	HISI_PCIE_PMU_FORMAT_ATTR(trig_mode, "config1:9"),
	HISI_PCIE_PMU_FORMAT_ATTR(len_mode, "config1:10-11"),
	HISI_PCIE_PMU_FORMAT_ATTR(port, "config2:0-15"),
	HISI_PCIE_PMU_FORMAT_ATTR(bdf, "config2:16-31"),
	NULL
};

static const struct attribute_group hisi_pcie_pmu_format_group = {
	.name = "format",
	.attrs = hisi_pcie_pmu_format_attr,
};

static struct attribute *hisi_pcie_pmu_bus_attrs[] = {
	&dev_attr_bus.attr,
	NULL
};

static const struct attribute_group hisi_pcie_pmu_bus_attr_group = {
	.attrs = hisi_pcie_pmu_bus_attrs,
};

static struct attribute *hisi_pcie_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static const struct attribute_group hisi_pcie_pmu_cpumask_attr_group = {
	.attrs = hisi_pcie_pmu_cpumask_attrs,
};

static struct attribute *hisi_pcie_pmu_identifier_attrs[] = {
	&dev_attr_identifier.attr,
	NULL
};

static const struct attribute_group hisi_pcie_pmu_identifier_attr_group = {
	.attrs = hisi_pcie_pmu_identifier_attrs,
};

static const struct attribute_group *hisi_pcie_pmu_attr_groups[] = {
	&hisi_pcie_pmu_events_group,
	&hisi_pcie_pmu_format_group,
	&hisi_pcie_pmu_bus_attr_group,
	&hisi_pcie_pmu_cpumask_attr_group,
	&hisi_pcie_pmu_identifier_attr_group,
	NULL
};

static int hisi_pcie_alloc_pmu(struct pci_dev *pdev, struct hisi_pcie_pmu *pcie_pmu)
{
	struct hisi_pcie_reg_pair regs;
	u16 sicl_id, core_id;
	char *name;

	regs = hisi_pcie_parse_reg_value(pcie_pmu, HISI_PCIE_REG_BDF);
	pcie_pmu->bdf_min = regs.lo;
	pcie_pmu->bdf_max = regs.hi;

	regs = hisi_pcie_parse_reg_value(pcie_pmu, HISI_PCIE_REG_INFO);
	sicl_id = regs.hi;
	core_id = regs.lo;

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hisi_pcie%u_core%u", sicl_id, core_id);
	if (!name)
		return -ENOMEM;

	pcie_pmu->pdev = pdev;
	pcie_pmu->on_cpu = -1;
	pcie_pmu->identifier = readl(pcie_pmu->base + HISI_PCIE_REG_VERSION);
	pcie_pmu->pmu = (struct pmu) {
		.name		= name,
		.module		= THIS_MODULE,
		.parent		= &pdev->dev,
		.event_init	= hisi_pcie_pmu_event_init,
		.pmu_enable	= hisi_pcie_pmu_enable,
		.pmu_disable	= hisi_pcie_pmu_disable,
		.add		= hisi_pcie_pmu_add,
		.del		= hisi_pcie_pmu_del,
		.start		= hisi_pcie_pmu_start,
		.stop		= hisi_pcie_pmu_stop,
		.read		= hisi_pcie_pmu_read,
		.task_ctx_nr	= perf_invalid_context,
		.attr_groups	= hisi_pcie_pmu_attr_groups,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
	};

	return 0;
}

static int hisi_pcie_init_pmu(struct pci_dev *pdev, struct hisi_pcie_pmu *pcie_pmu)
{
	int ret;

	pcie_pmu->base = pci_ioremap_bar(pdev, 2);
	if (!pcie_pmu->base) {
		pci_err(pdev, "Ioremap failed for pcie_pmu resource\n");
		return -ENOMEM;
	}

	ret = hisi_pcie_alloc_pmu(pdev, pcie_pmu);
	if (ret)
		goto err_iounmap;

	ret = hisi_pcie_pmu_irq_register(pdev, pcie_pmu);
	if (ret)
		goto err_iounmap;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE, &pcie_pmu->node);
	if (ret) {
		pci_err(pdev, "Failed to register hotplug: %d\n", ret);
		goto err_irq_unregister;
	}

	ret = perf_pmu_register(&pcie_pmu->pmu, pcie_pmu->pmu.name, -1);
	if (ret) {
		pci_err(pdev, "Failed to register PCIe PMU: %d\n", ret);
		goto err_hotplug_unregister;
	}

	return ret;

err_hotplug_unregister:
	cpuhp_state_remove_instance_nocalls(
		CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE, &pcie_pmu->node);

err_irq_unregister:
	hisi_pcie_pmu_irq_unregister(pdev, pcie_pmu);

err_iounmap:
	iounmap(pcie_pmu->base);

	return ret;
}

static void hisi_pcie_uninit_pmu(struct pci_dev *pdev)
{
	struct hisi_pcie_pmu *pcie_pmu = pci_get_drvdata(pdev);

	perf_pmu_unregister(&pcie_pmu->pmu);
	cpuhp_state_remove_instance_nocalls(
		CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE, &pcie_pmu->node);
	hisi_pcie_pmu_irq_unregister(pdev, pcie_pmu);
	iounmap(pcie_pmu->base);
}

static int hisi_pcie_init_dev(struct pci_dev *pdev)
{
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret) {
		pci_err(pdev, "Failed to enable PCI device: %d\n", ret);
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(2), DRV_NAME);
	if (ret < 0) {
		pci_err(pdev, "Failed to request PCI mem regions: %d\n", ret);
		return ret;
	}

	pci_set_master(pdev);

	return 0;
}

static int hisi_pcie_pmu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct hisi_pcie_pmu *pcie_pmu;
	int ret;

	pcie_pmu = devm_kzalloc(&pdev->dev, sizeof(*pcie_pmu), GFP_KERNEL);
	if (!pcie_pmu)
		return -ENOMEM;

	ret = hisi_pcie_init_dev(pdev);
	if (ret)
		return ret;

	ret = hisi_pcie_init_pmu(pdev, pcie_pmu);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, pcie_pmu);

	return ret;
}

static void hisi_pcie_pmu_remove(struct pci_dev *pdev)
{
	hisi_pcie_uninit_pmu(pdev);
	pci_set_drvdata(pdev, NULL);
}

static const struct pci_device_id hisi_pcie_pmu_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, 0xa12d) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, hisi_pcie_pmu_ids);

static struct pci_driver hisi_pcie_pmu_driver = {
	.name = DRV_NAME,
	.id_table = hisi_pcie_pmu_ids,
	.probe = hisi_pcie_pmu_probe,
	.remove = hisi_pcie_pmu_remove,
};

static int __init hisi_pcie_module_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE,
				      "AP_PERF_ARM_HISI_PCIE_PMU_ONLINE",
				      hisi_pcie_pmu_online_cpu,
				      hisi_pcie_pmu_offline_cpu);
	if (ret) {
		pr_err("Failed to setup PCIe PMU hotplug: %d\n", ret);
		return ret;
	}

	ret = pci_register_driver(&hisi_pcie_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE);

	return ret;
}
module_init(hisi_pcie_module_init);

static void __exit hisi_pcie_module_exit(void)
{
	pci_unregister_driver(&hisi_pcie_pmu_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE);
}
module_exit(hisi_pcie_module_exit);

MODULE_DESCRIPTION("HiSilicon PCIe PMU driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Qi Liu <liuqi115@huawei.com>");
