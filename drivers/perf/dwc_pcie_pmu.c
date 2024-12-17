// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe PMU driver
 *
 * Copyright (C) 2021-2023 Alibaba Inc.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/perf_event.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/smp.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#define DWC_PCIE_VSEC_RAS_DES_ID		0x02
#define DWC_PCIE_EVENT_CNT_CTL			0x8

/*
 * Event Counter Data Select includes two parts:
 * - 27-24: Group number(4-bit: 0..0x7)
 * - 23-16: Event number(8-bit: 0..0x13) within the Group
 *
 * Put them together as in TRM.
 */
#define DWC_PCIE_CNT_EVENT_SEL			GENMASK(27, 16)
#define DWC_PCIE_CNT_LANE_SEL			GENMASK(11, 8)
#define DWC_PCIE_CNT_STATUS			BIT(7)
#define DWC_PCIE_CNT_ENABLE			GENMASK(4, 2)
#define DWC_PCIE_PER_EVENT_OFF			0x1
#define DWC_PCIE_PER_EVENT_ON			0x3
#define DWC_PCIE_EVENT_CLEAR			GENMASK(1, 0)
#define DWC_PCIE_EVENT_PER_CLEAR		0x1

#define DWC_PCIE_EVENT_CNT_DATA			0xC

#define DWC_PCIE_TIME_BASED_ANAL_CTL		0x10
#define DWC_PCIE_TIME_BASED_REPORT_SEL		GENMASK(31, 24)
#define DWC_PCIE_TIME_BASED_DURATION_SEL	GENMASK(15, 8)
#define DWC_PCIE_DURATION_MANUAL_CTL		0x0
#define DWC_PCIE_DURATION_1MS			0x1
#define DWC_PCIE_DURATION_10MS			0x2
#define DWC_PCIE_DURATION_100MS			0x3
#define DWC_PCIE_DURATION_1S			0x4
#define DWC_PCIE_DURATION_2S			0x5
#define DWC_PCIE_DURATION_4S			0x6
#define DWC_PCIE_DURATION_4US			0xFF
#define DWC_PCIE_TIME_BASED_TIMER_START		BIT(0)
#define DWC_PCIE_TIME_BASED_CNT_ENABLE		0x1

#define DWC_PCIE_TIME_BASED_ANAL_DATA_REG_LOW	0x14
#define DWC_PCIE_TIME_BASED_ANAL_DATA_REG_HIGH	0x18

/* Event attributes */
#define DWC_PCIE_CONFIG_EVENTID			GENMASK(15, 0)
#define DWC_PCIE_CONFIG_TYPE			GENMASK(19, 16)
#define DWC_PCIE_CONFIG_LANE			GENMASK(27, 20)

#define DWC_PCIE_EVENT_ID(event)	FIELD_GET(DWC_PCIE_CONFIG_EVENTID, (event)->attr.config)
#define DWC_PCIE_EVENT_TYPE(event)	FIELD_GET(DWC_PCIE_CONFIG_TYPE, (event)->attr.config)
#define DWC_PCIE_EVENT_LANE(event)	FIELD_GET(DWC_PCIE_CONFIG_LANE, (event)->attr.config)

enum dwc_pcie_event_type {
	DWC_PCIE_TIME_BASE_EVENT,
	DWC_PCIE_LANE_EVENT,
	DWC_PCIE_EVENT_TYPE_MAX,
};

#define DWC_PCIE_LANE_EVENT_MAX_PERIOD		GENMASK_ULL(31, 0)
#define DWC_PCIE_MAX_PERIOD			GENMASK_ULL(63, 0)

struct dwc_pcie_pmu {
	struct pmu		pmu;
	struct pci_dev		*pdev;		/* Root Port device */
	u16			ras_des_offset;
	u32			nr_lanes;

	struct hlist_node	cpuhp_node;
	struct perf_event	*event[DWC_PCIE_EVENT_TYPE_MAX];
	int			on_cpu;
};

#define to_dwc_pcie_pmu(p) (container_of(p, struct dwc_pcie_pmu, pmu))

static int dwc_pcie_pmu_hp_state;
static struct list_head dwc_pcie_dev_info_head =
				LIST_HEAD_INIT(dwc_pcie_dev_info_head);
static bool notify;

struct dwc_pcie_dev_info {
	struct platform_device *plat_dev;
	struct pci_dev *pdev;
	struct list_head dev_node;
};

struct dwc_pcie_vendor_id {
	int vendor_id;
};

static const struct dwc_pcie_vendor_id dwc_pcie_vendor_ids[] = {
	{.vendor_id = PCI_VENDOR_ID_ALIBABA },
	{.vendor_id = PCI_VENDOR_ID_AMPERE },
	{.vendor_id = PCI_VENDOR_ID_QCOM },
	{} /* terminator */
};

static ssize_t cpumask_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct dwc_pcie_pmu *pcie_pmu = to_dwc_pcie_pmu(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(pcie_pmu->on_cpu));
}
static DEVICE_ATTR_RO(cpumask);

static struct attribute *dwc_pcie_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static struct attribute_group dwc_pcie_cpumask_attr_group = {
	.attrs = dwc_pcie_pmu_cpumask_attrs,
};

struct dwc_pcie_format_attr {
	struct device_attribute attr;
	u64 field;
	int config;
};

PMU_FORMAT_ATTR(eventid, "config:0-15");
PMU_FORMAT_ATTR(type, "config:16-19");
PMU_FORMAT_ATTR(lane, "config:20-27");

static struct attribute *dwc_pcie_format_attrs[] = {
	&format_attr_type.attr,
	&format_attr_eventid.attr,
	&format_attr_lane.attr,
	NULL,
};

static struct attribute_group dwc_pcie_format_attrs_group = {
	.name = "format",
	.attrs = dwc_pcie_format_attrs,
};

struct dwc_pcie_event_attr {
	struct device_attribute attr;
	enum dwc_pcie_event_type type;
	u16 eventid;
	u8 lane;
};

static ssize_t dwc_pcie_event_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct dwc_pcie_event_attr *eattr;

	eattr = container_of(attr, typeof(*eattr), attr);

	if (eattr->type == DWC_PCIE_LANE_EVENT)
		return sysfs_emit(buf, "eventid=0x%x,type=0x%x,lane=?\n",
				  eattr->eventid, eattr->type);
	else if (eattr->type == DWC_PCIE_TIME_BASE_EVENT)
		return sysfs_emit(buf, "eventid=0x%x,type=0x%x\n",
				  eattr->eventid, eattr->type);

	return 0;
}

#define DWC_PCIE_EVENT_ATTR(_name, _type, _eventid, _lane)		\
	(&((struct dwc_pcie_event_attr[]) {{				\
		.attr = __ATTR(_name, 0444, dwc_pcie_event_show, NULL),	\
		.type = _type,						\
		.eventid = _eventid,					\
		.lane = _lane,						\
	}})[0].attr.attr)

#define DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(_name, _eventid)		\
	DWC_PCIE_EVENT_ATTR(_name, DWC_PCIE_TIME_BASE_EVENT, _eventid, 0)
#define DWC_PCIE_PMU_LANE_EVENT_ATTR(_name, _eventid)			\
	DWC_PCIE_EVENT_ATTR(_name, DWC_PCIE_LANE_EVENT, _eventid, 0)

static struct attribute *dwc_pcie_pmu_time_event_attrs[] = {
	/* Group #0 */
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(one_cycle, 0x00),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(TX_L0S, 0x01),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(RX_L0S, 0x02),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(L0, 0x03),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(L1, 0x04),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(L1_1, 0x05),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(L1_2, 0x06),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(CFG_RCVRY, 0x07),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(TX_RX_L0S, 0x08),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(L1_AUX, 0x09),

	/* Group #1 */
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(tx_pcie_tlp_data_payload, 0x20),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(rx_pcie_tlp_data_payload, 0x21),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(tx_ccix_tlp_data_payload, 0x22),
	DWC_PCIE_PMU_TIME_BASE_EVENT_ATTR(rx_ccix_tlp_data_payload, 0x23),

	/*
	 * Leave it to the user to specify the lane ID to avoid generating
	 * a list of hundreds of events.
	 */
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_ack_dllp, 0x600),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_update_fc_dllp, 0x601),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_ack_dllp, 0x602),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_update_fc_dllp, 0x603),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_nullified_tlp, 0x604),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_nullified_tlp, 0x605),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_duplicate_tlp, 0x606),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_memory_write, 0x700),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_memory_read, 0x701),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_configuration_write, 0x702),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_configuration_read, 0x703),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_io_write, 0x704),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_io_read, 0x705),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_completion_without_data, 0x706),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_completion_with_data, 0x707),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_message_tlp, 0x708),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_atomic, 0x709),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_tlp_with_prefix, 0x70A),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_memory_write, 0x70B),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_memory_read, 0x70C),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_io_write, 0x70F),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_io_read, 0x710),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_completion_without_data, 0x711),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_completion_with_data, 0x712),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_message_tlp, 0x713),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_atomic, 0x714),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_tlp_with_prefix, 0x715),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(tx_ccix_tlp, 0x716),
	DWC_PCIE_PMU_LANE_EVENT_ATTR(rx_ccix_tlp, 0x717),
	NULL
};

static const struct attribute_group dwc_pcie_event_attrs_group = {
	.name = "events",
	.attrs = dwc_pcie_pmu_time_event_attrs,
};

static const struct attribute_group *dwc_pcie_attr_groups[] = {
	&dwc_pcie_event_attrs_group,
	&dwc_pcie_format_attrs_group,
	&dwc_pcie_cpumask_attr_group,
	NULL
};

static void dwc_pcie_pmu_lane_event_enable(struct dwc_pcie_pmu *pcie_pmu,
					   bool enable)
{
	struct pci_dev *pdev = pcie_pmu->pdev;
	u16 ras_des_offset = pcie_pmu->ras_des_offset;

	if (enable)
		pci_clear_and_set_config_dword(pdev,
					ras_des_offset + DWC_PCIE_EVENT_CNT_CTL,
					DWC_PCIE_CNT_ENABLE, DWC_PCIE_PER_EVENT_ON);
	else
		pci_clear_and_set_config_dword(pdev,
					ras_des_offset + DWC_PCIE_EVENT_CNT_CTL,
					DWC_PCIE_CNT_ENABLE, DWC_PCIE_PER_EVENT_OFF);
}

static void dwc_pcie_pmu_time_based_event_enable(struct dwc_pcie_pmu *pcie_pmu,
					  bool enable)
{
	struct pci_dev *pdev = pcie_pmu->pdev;
	u16 ras_des_offset = pcie_pmu->ras_des_offset;

	pci_clear_and_set_config_dword(pdev,
				       ras_des_offset + DWC_PCIE_TIME_BASED_ANAL_CTL,
				       DWC_PCIE_TIME_BASED_TIMER_START, enable);
}

static u64 dwc_pcie_pmu_read_lane_event_counter(struct perf_event *event)
{
	struct dwc_pcie_pmu *pcie_pmu = to_dwc_pcie_pmu(event->pmu);
	struct pci_dev *pdev = pcie_pmu->pdev;
	u16 ras_des_offset = pcie_pmu->ras_des_offset;
	u32 val;

	pci_read_config_dword(pdev, ras_des_offset + DWC_PCIE_EVENT_CNT_DATA, &val);

	return val;
}

static u64 dwc_pcie_pmu_read_time_based_counter(struct perf_event *event)
{
	struct dwc_pcie_pmu *pcie_pmu = to_dwc_pcie_pmu(event->pmu);
	struct pci_dev *pdev = pcie_pmu->pdev;
	int event_id = DWC_PCIE_EVENT_ID(event);
	u16 ras_des_offset = pcie_pmu->ras_des_offset;
	u32 lo, hi, ss;
	u64 val;

	/*
	 * The 64-bit value of the data counter is spread across two
	 * registers that are not synchronized. In order to read them
	 * atomically, ensure that the high 32 bits match before and after
	 * reading the low 32 bits.
	 */
	pci_read_config_dword(pdev,
		ras_des_offset + DWC_PCIE_TIME_BASED_ANAL_DATA_REG_HIGH, &hi);
	do {
		/* snapshot the high 32 bits */
		ss = hi;

		pci_read_config_dword(
			pdev, ras_des_offset + DWC_PCIE_TIME_BASED_ANAL_DATA_REG_LOW,
			&lo);
		pci_read_config_dword(
			pdev, ras_des_offset + DWC_PCIE_TIME_BASED_ANAL_DATA_REG_HIGH,
			&hi);
	} while (hi != ss);

	val = ((u64)hi << 32) | lo;
	/*
	 * The Group#1 event measures the amount of data processed in 16-byte
	 * units. Simplify the end-user interface by multiplying the counter
	 * at the point of read.
	 */
	if (event_id >= 0x20 && event_id <= 0x23)
		val *= 16;

	return val;
}

static void dwc_pcie_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	enum dwc_pcie_event_type type = DWC_PCIE_EVENT_TYPE(event);
	u64 delta, prev, now = 0;

	do {
		prev = local64_read(&hwc->prev_count);

		if (type == DWC_PCIE_LANE_EVENT)
			now = dwc_pcie_pmu_read_lane_event_counter(event);
		else if (type == DWC_PCIE_TIME_BASE_EVENT)
			now = dwc_pcie_pmu_read_time_based_counter(event);

	} while (local64_cmpxchg(&hwc->prev_count, prev, now) != prev);

	delta = (now - prev) & DWC_PCIE_MAX_PERIOD;
	/* 32-bit counter for Lane Event Counting */
	if (type == DWC_PCIE_LANE_EVENT)
		delta &= DWC_PCIE_LANE_EVENT_MAX_PERIOD;

	local64_add(delta, &event->count);
}

static int dwc_pcie_pmu_event_init(struct perf_event *event)
{
	struct dwc_pcie_pmu *pcie_pmu = to_dwc_pcie_pmu(event->pmu);
	enum dwc_pcie_event_type type = DWC_PCIE_EVENT_TYPE(event);
	struct perf_event *sibling;
	u32 lane;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* We don't support sampling */
	if (is_sampling_event(event))
		return -EINVAL;

	/* We cannot support task bound events */
	if (event->cpu < 0 || event->attach_state & PERF_ATTACH_TASK)
		return -EINVAL;

	if (event->group_leader != event &&
	    !is_software_event(event->group_leader))
		return -EINVAL;

	for_each_sibling_event(sibling, event->group_leader) {
		if (sibling->pmu != event->pmu && !is_software_event(sibling))
			return -EINVAL;
	}

	if (type < 0 || type >= DWC_PCIE_EVENT_TYPE_MAX)
		return -EINVAL;

	if (type == DWC_PCIE_LANE_EVENT) {
		lane = DWC_PCIE_EVENT_LANE(event);
		if (lane < 0 || lane >= pcie_pmu->nr_lanes)
			return -EINVAL;
	}

	event->cpu = pcie_pmu->on_cpu;

	return 0;
}

static void dwc_pcie_pmu_event_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct dwc_pcie_pmu *pcie_pmu = to_dwc_pcie_pmu(event->pmu);
	enum dwc_pcie_event_type type = DWC_PCIE_EVENT_TYPE(event);

	hwc->state = 0;
	local64_set(&hwc->prev_count, 0);

	if (type == DWC_PCIE_LANE_EVENT)
		dwc_pcie_pmu_lane_event_enable(pcie_pmu, true);
	else if (type == DWC_PCIE_TIME_BASE_EVENT)
		dwc_pcie_pmu_time_based_event_enable(pcie_pmu, true);
}

static void dwc_pcie_pmu_event_stop(struct perf_event *event, int flags)
{
	struct dwc_pcie_pmu *pcie_pmu = to_dwc_pcie_pmu(event->pmu);
	enum dwc_pcie_event_type type = DWC_PCIE_EVENT_TYPE(event);
	struct hw_perf_event *hwc = &event->hw;

	if (event->hw.state & PERF_HES_STOPPED)
		return;

	if (type == DWC_PCIE_LANE_EVENT)
		dwc_pcie_pmu_lane_event_enable(pcie_pmu, false);
	else if (type == DWC_PCIE_TIME_BASE_EVENT)
		dwc_pcie_pmu_time_based_event_enable(pcie_pmu, false);

	dwc_pcie_pmu_event_update(event);
	hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static int dwc_pcie_pmu_event_add(struct perf_event *event, int flags)
{
	struct dwc_pcie_pmu *pcie_pmu = to_dwc_pcie_pmu(event->pmu);
	struct pci_dev *pdev = pcie_pmu->pdev;
	struct hw_perf_event *hwc = &event->hw;
	enum dwc_pcie_event_type type = DWC_PCIE_EVENT_TYPE(event);
	int event_id = DWC_PCIE_EVENT_ID(event);
	int lane = DWC_PCIE_EVENT_LANE(event);
	u16 ras_des_offset = pcie_pmu->ras_des_offset;
	u32 ctrl;

	/* one counter for each type and it is in use */
	if (pcie_pmu->event[type])
		return -ENOSPC;

	pcie_pmu->event[type] = event;
	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	if (type == DWC_PCIE_LANE_EVENT) {
		/* EVENT_COUNTER_DATA_REG needs clear manually */
		ctrl = FIELD_PREP(DWC_PCIE_CNT_EVENT_SEL, event_id) |
			FIELD_PREP(DWC_PCIE_CNT_LANE_SEL, lane) |
			FIELD_PREP(DWC_PCIE_CNT_ENABLE, DWC_PCIE_PER_EVENT_OFF) |
			FIELD_PREP(DWC_PCIE_EVENT_CLEAR, DWC_PCIE_EVENT_PER_CLEAR);
		pci_write_config_dword(pdev, ras_des_offset + DWC_PCIE_EVENT_CNT_CTL,
				       ctrl);
	} else if (type == DWC_PCIE_TIME_BASE_EVENT) {
		/*
		 * TIME_BASED_ANAL_DATA_REG is a 64 bit register, we can safely
		 * use it with any manually controlled duration. And it is
		 * cleared when next measurement starts.
		 */
		ctrl = FIELD_PREP(DWC_PCIE_TIME_BASED_REPORT_SEL, event_id) |
			FIELD_PREP(DWC_PCIE_TIME_BASED_DURATION_SEL,
				   DWC_PCIE_DURATION_MANUAL_CTL) |
			DWC_PCIE_TIME_BASED_CNT_ENABLE;
		pci_write_config_dword(
			pdev, ras_des_offset + DWC_PCIE_TIME_BASED_ANAL_CTL, ctrl);
	}

	if (flags & PERF_EF_START)
		dwc_pcie_pmu_event_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);

	return 0;
}

static void dwc_pcie_pmu_event_del(struct perf_event *event, int flags)
{
	struct dwc_pcie_pmu *pcie_pmu = to_dwc_pcie_pmu(event->pmu);
	enum dwc_pcie_event_type type = DWC_PCIE_EVENT_TYPE(event);

	dwc_pcie_pmu_event_stop(event, flags | PERF_EF_UPDATE);
	perf_event_update_userpage(event);
	pcie_pmu->event[type] = NULL;
}

static void dwc_pcie_pmu_remove_cpuhp_instance(void *hotplug_node)
{
	cpuhp_state_remove_instance_nocalls(dwc_pcie_pmu_hp_state, hotplug_node);
}

/*
 * Find the binded DES capability device info of a PCI device.
 * @pdev: The PCI device.
 */
static struct dwc_pcie_dev_info *dwc_pcie_find_dev_info(struct pci_dev *pdev)
{
	struct dwc_pcie_dev_info *dev_info;

	list_for_each_entry(dev_info, &dwc_pcie_dev_info_head, dev_node)
		if (dev_info->pdev == pdev)
			return dev_info;

	return NULL;
}

static void dwc_pcie_unregister_pmu(void *data)
{
	struct dwc_pcie_pmu *pcie_pmu = data;

	perf_pmu_unregister(&pcie_pmu->pmu);
}

static bool dwc_pcie_match_des_cap(struct pci_dev *pdev)
{
	const struct dwc_pcie_vendor_id *vid;
	u16 vsec = 0;
	u32 val;

	if (!pci_is_pcie(pdev) || !(pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT))
		return false;

	for (vid = dwc_pcie_vendor_ids; vid->vendor_id; vid++) {
		vsec = pci_find_vsec_capability(pdev, vid->vendor_id,
						DWC_PCIE_VSEC_RAS_DES_ID);
		if (vsec)
			break;
	}
	if (!vsec)
		return false;

	pci_read_config_dword(pdev, vsec + PCI_VNDR_HEADER, &val);
	if (PCI_VNDR_HEADER_REV(val) != 0x04)
		return false;

	pci_dbg(pdev,
		"Detected PCIe Vendor-Specific Extended Capability RAS DES\n");
	return true;
}

static void dwc_pcie_unregister_dev(struct dwc_pcie_dev_info *dev_info)
{
	platform_device_unregister(dev_info->plat_dev);
	list_del(&dev_info->dev_node);
	kfree(dev_info);
}

static int dwc_pcie_register_dev(struct pci_dev *pdev)
{
	struct platform_device *plat_dev;
	struct dwc_pcie_dev_info *dev_info;
	u32 sbdf;

	sbdf = (pci_domain_nr(pdev->bus) << 16) | PCI_DEVID(pdev->bus->number, pdev->devfn);
	plat_dev = platform_device_register_data(NULL, "dwc_pcie_pmu", sbdf,
						 pdev, sizeof(*pdev));

	if (IS_ERR(plat_dev))
		return PTR_ERR(plat_dev);

	dev_info = kzalloc(sizeof(*dev_info), GFP_KERNEL);
	if (!dev_info)
		return -ENOMEM;

	/* Cache platform device to handle pci device hotplug */
	dev_info->plat_dev = plat_dev;
	dev_info->pdev = pdev;
	list_add(&dev_info->dev_node, &dwc_pcie_dev_info_head);

	return 0;
}

static int dwc_pcie_pmu_notifier(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct device *dev = data;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct dwc_pcie_dev_info *dev_info;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		if (!dwc_pcie_match_des_cap(pdev))
			return NOTIFY_DONE;
		if (dwc_pcie_register_dev(pdev))
			return NOTIFY_BAD;
		break;
	case BUS_NOTIFY_DEL_DEVICE:
		dev_info = dwc_pcie_find_dev_info(pdev);
		if (!dev_info)
			return NOTIFY_DONE;
		dwc_pcie_unregister_dev(dev_info);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block dwc_pcie_pmu_nb = {
	.notifier_call = dwc_pcie_pmu_notifier,
};

static int dwc_pcie_pmu_probe(struct platform_device *plat_dev)
{
	struct pci_dev *pdev = plat_dev->dev.platform_data;
	struct dwc_pcie_pmu *pcie_pmu;
	char *name;
	u32 sbdf, val;
	u16 vsec;
	int ret;

	vsec = pci_find_vsec_capability(pdev, pdev->vendor,
					DWC_PCIE_VSEC_RAS_DES_ID);
	pci_read_config_dword(pdev, vsec + PCI_VNDR_HEADER, &val);
	sbdf = plat_dev->id;
	name = devm_kasprintf(&plat_dev->dev, GFP_KERNEL, "dwc_rootport_%x", sbdf);
	if (!name)
		return -ENOMEM;

	pcie_pmu = devm_kzalloc(&plat_dev->dev, sizeof(*pcie_pmu), GFP_KERNEL);
	if (!pcie_pmu)
		return -ENOMEM;

	pcie_pmu->pdev = pdev;
	pcie_pmu->ras_des_offset = vsec;
	pcie_pmu->nr_lanes = pcie_get_width_cap(pdev);
	pcie_pmu->on_cpu = -1;
	pcie_pmu->pmu = (struct pmu){
		.name		= name,
		.parent		= &pdev->dev,
		.module		= THIS_MODULE,
		.attr_groups	= dwc_pcie_attr_groups,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
		.task_ctx_nr	= perf_invalid_context,
		.event_init	= dwc_pcie_pmu_event_init,
		.add		= dwc_pcie_pmu_event_add,
		.del		= dwc_pcie_pmu_event_del,
		.start		= dwc_pcie_pmu_event_start,
		.stop		= dwc_pcie_pmu_event_stop,
		.read		= dwc_pcie_pmu_event_update,
	};

	/* Add this instance to the list used by the offline callback */
	ret = cpuhp_state_add_instance(dwc_pcie_pmu_hp_state,
				       &pcie_pmu->cpuhp_node);
	if (ret) {
		pci_err(pdev, "Error %d registering hotplug @%x\n", ret, sbdf);
		return ret;
	}

	/* Unwind when platform driver removes */
	ret = devm_add_action_or_reset(&plat_dev->dev,
				       dwc_pcie_pmu_remove_cpuhp_instance,
				       &pcie_pmu->cpuhp_node);
	if (ret)
		return ret;

	ret = perf_pmu_register(&pcie_pmu->pmu, name, -1);
	if (ret) {
		pci_err(pdev, "Error %d registering PMU @%x\n", ret, sbdf);
		return ret;
	}
	ret = devm_add_action_or_reset(&plat_dev->dev, dwc_pcie_unregister_pmu,
				       pcie_pmu);
	if (ret)
		return ret;

	return 0;
}

static int dwc_pcie_pmu_online_cpu(unsigned int cpu, struct hlist_node *cpuhp_node)
{
	struct dwc_pcie_pmu *pcie_pmu;

	pcie_pmu = hlist_entry_safe(cpuhp_node, struct dwc_pcie_pmu, cpuhp_node);
	if (pcie_pmu->on_cpu == -1)
		pcie_pmu->on_cpu = cpumask_local_spread(
			0, dev_to_node(&pcie_pmu->pdev->dev));

	return 0;
}

static int dwc_pcie_pmu_offline_cpu(unsigned int cpu, struct hlist_node *cpuhp_node)
{
	struct dwc_pcie_pmu *pcie_pmu;
	struct pci_dev *pdev;
	unsigned int target;
	int node;

	pcie_pmu = hlist_entry_safe(cpuhp_node, struct dwc_pcie_pmu, cpuhp_node);
	/* Nothing to do if this CPU doesn't own the PMU */
	if (cpu != pcie_pmu->on_cpu)
		return 0;

	pcie_pmu->on_cpu = -1;
	pdev = pcie_pmu->pdev;
	node = dev_to_node(&pdev->dev);

	target = cpumask_any_and_but(cpumask_of_node(node), cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		target = cpumask_any_but(cpu_online_mask, cpu);

	if (target >= nr_cpu_ids) {
		pci_err(pdev, "There is no CPU to set\n");
		return 0;
	}

	/* This PMU does NOT support interrupt, just migrate context. */
	perf_pmu_migrate_context(&pcie_pmu->pmu, cpu, target);
	pcie_pmu->on_cpu = target;

	return 0;
}

static struct platform_driver dwc_pcie_pmu_driver = {
	.probe = dwc_pcie_pmu_probe,
	.driver = {.name = "dwc_pcie_pmu",},
};

static int __init dwc_pcie_pmu_init(void)
{
	struct pci_dev *pdev = NULL;
	int ret;

	for_each_pci_dev(pdev) {
		if (!dwc_pcie_match_des_cap(pdev))
			continue;

		ret = dwc_pcie_register_dev(pdev);
		if (ret) {
			pci_dev_put(pdev);
			return ret;
		}
	}

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "perf/dwc_pcie_pmu:online",
				      dwc_pcie_pmu_online_cpu,
				      dwc_pcie_pmu_offline_cpu);
	if (ret < 0)
		return ret;

	dwc_pcie_pmu_hp_state = ret;

	ret = platform_driver_register(&dwc_pcie_pmu_driver);
	if (ret)
		goto platform_driver_register_err;

	ret = bus_register_notifier(&pci_bus_type, &dwc_pcie_pmu_nb);
	if (ret)
		goto platform_driver_register_err;
	notify = true;

	return 0;

platform_driver_register_err:
	cpuhp_remove_multi_state(dwc_pcie_pmu_hp_state);

	return ret;
}

static void __exit dwc_pcie_pmu_exit(void)
{
	struct dwc_pcie_dev_info *dev_info, *tmp;

	if (notify)
		bus_unregister_notifier(&pci_bus_type, &dwc_pcie_pmu_nb);
	list_for_each_entry_safe(dev_info, tmp, &dwc_pcie_dev_info_head, dev_node)
		dwc_pcie_unregister_dev(dev_info);
	platform_driver_unregister(&dwc_pcie_pmu_driver);
	cpuhp_remove_multi_state(dwc_pcie_pmu_hp_state);
}

module_init(dwc_pcie_pmu_init);
module_exit(dwc_pcie_pmu_exit);

MODULE_DESCRIPTION("PMU driver for DesignWare Cores PCI Express Controller");
MODULE_AUTHOR("Shuai Xue <xueshuai@linux.alibaba.com>");
MODULE_LICENSE("GPL v2");
