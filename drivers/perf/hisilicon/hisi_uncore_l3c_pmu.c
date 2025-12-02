// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SoC L3C uncore Hardware event counters support
 *
 * Copyright (C) 2017 HiSilicon Limited
 * Author: Anurup M <anurup.m@huawei.com>
 *         Shaokun Zhang <zhangshaokun@hisilicon.com>
 *
 * This code is based on the uncore PMUs like arm-cci and arm-ccn.
 */
#include <linux/acpi.h>
#include <linux/bug.h>
#include <linux/cpuhotplug.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/smp.h>

#include "hisi_uncore_pmu.h"

/* L3C register definition */
#define L3C_PERF_CTRL		0x0408
#define L3C_INT_MASK		0x0800
#define L3C_INT_STATUS		0x0808
#define L3C_INT_CLEAR		0x080c
#define L3C_CORE_CTRL           0x1b04
#define L3C_TRACETAG_CTRL       0x1b20
#define L3C_DATSRC_TYPE         0x1b48
#define L3C_DATSRC_CTRL         0x1bf0
#define L3C_EVENT_CTRL	        0x1c00
#define L3C_VERSION		0x1cf0
#define L3C_EVENT_TYPE0		0x1d00
/*
 * If the HW version only supports a 48-bit counter, then
 * bits [63:48] are reserved, which are Read-As-Zero and
 * Writes-Ignored.
 */
#define L3C_CNTR0_LOWER		0x1e00

/* L3C has 8-counters */
#define L3C_NR_COUNTERS		0x8
#define L3C_MAX_EXT		2

#define L3C_PERF_CTRL_EN	0x10000
#define L3C_TRACETAG_EN		BIT(31)
#define L3C_TRACETAG_REQ_SHIFT	7
#define L3C_TRACETAG_MARK_EN	BIT(0)
#define L3C_TRACETAG_REQ_EN	(L3C_TRACETAG_MARK_EN | BIT(2))
#define L3C_TRACETAG_CORE_EN	(L3C_TRACETAG_MARK_EN | BIT(3))
#define L3C_CORE_EN		BIT(20)
#define L3C_COER_NONE		0x0
#define L3C_DATSRC_MASK		0xFF
#define L3C_DATSRC_SKT_EN	BIT(23)
#define L3C_DATSRC_NONE		0x0
#define L3C_EVTYPE_NONE		0xff
#define L3C_V1_NR_EVENTS	0x59
#define L3C_V2_NR_EVENTS	0xFF

HISI_PMU_EVENT_ATTR_EXTRACTOR(ext, config, 17, 16);
/*
 * Remain the config1:0-7 for backward compatibility if some existing users
 * hardcode the config1:0-7 directly without parsing the sysfs attribute.
 */
HISI_PMU_EVENT_ATTR_EXTRACTOR(tt_core_deprecated, config1, 7, 0);
HISI_PMU_EVENT_ATTR_EXTRACTOR(tt_req, config1, 10, 8);
HISI_PMU_EVENT_ATTR_EXTRACTOR(datasrc_cfg, config1, 15, 11);
HISI_PMU_EVENT_ATTR_EXTRACTOR(datasrc_skt, config1, 16, 16);
HISI_PMU_EVENT_ATTR_EXTRACTOR(tt_core, config2, 15, 0);

struct hisi_l3c_pmu {
	struct hisi_pmu l3c_pmu;

	/* MMIO and IRQ resources for extension events */
	void __iomem *ext_base[L3C_MAX_EXT];
	int ext_irq[L3C_MAX_EXT];
	int ext_num;
};

#define to_hisi_l3c_pmu(_l3c_pmu) \
	container_of(_l3c_pmu, struct hisi_l3c_pmu, l3c_pmu)

/*
 * The hardware counter idx used in counter enable/disable,
 * interrupt enable/disable and status check, etc.
 */
#define L3C_HW_IDX(_cntr_idx)		((_cntr_idx) % L3C_NR_COUNTERS)

/* Range of ext counters in used mask. */
#define L3C_CNTR_EXT_L(_ext)		(((_ext) + 1) * L3C_NR_COUNTERS)
#define L3C_CNTR_EXT_H(_ext)		(((_ext) + 2) * L3C_NR_COUNTERS)

struct hisi_l3c_pmu_ext {
	bool support_ext;
};

static bool support_ext(struct hisi_l3c_pmu *pmu)
{
	struct hisi_l3c_pmu_ext *l3c_pmu_ext = pmu->l3c_pmu.dev_info->private;

	return l3c_pmu_ext->support_ext;
}

/*
 * tt_core was extended to cover all the CPUs sharing the L3 and was moved from
 * config1:0-7 to config2:0-*. Try it first and fallback to tt_core_deprecated
 * if user's still using the deprecated one.
 */
static u32 hisi_l3c_pmu_get_tt_core(struct perf_event *event)
{
	u32 core = hisi_get_tt_core(event);

	if (core)
		return core;

	return hisi_get_tt_core_deprecated(event);
}

static int hisi_l3c_pmu_get_event_idx(struct perf_event *event)
{
	struct hisi_pmu *l3c_pmu = to_hisi_pmu(event->pmu);
	struct hisi_l3c_pmu *hisi_l3c_pmu = to_hisi_l3c_pmu(l3c_pmu);
	unsigned long *used_mask = l3c_pmu->pmu_events.used_mask;
	int ext = hisi_get_ext(event);
	int idx;

	/*
	 * For an L3C PMU that supports extension events, we can monitor
	 * maximum 2 * num_counters to 3 * num_counters events, depending on
	 * the number of ext regions supported by hardware. Thus use bit
	 * [0, num_counters - 1] for normal events and bit
	 * [ext * num_counters, (ext + 1) * num_counters - 1] for extension
	 * events. The idx allocation will keep unchanged for normal events and
	 * we can also use the idx to distinguish whether it's an extension
	 * event or not.
	 *
	 * Since normal events and extension events locates on the different
	 * address space, save the base address to the event->hw.event_base.
	 */
	if (ext && !support_ext(hisi_l3c_pmu))
		return -EOPNOTSUPP;

	if (ext)
		event->hw.event_base = (unsigned long)hisi_l3c_pmu->ext_base[ext - 1];
	else
		event->hw.event_base = (unsigned long)l3c_pmu->base;

	ext -= 1;
	idx = find_next_zero_bit(used_mask, L3C_CNTR_EXT_H(ext), L3C_CNTR_EXT_L(ext));

	if (idx >= L3C_CNTR_EXT_H(ext))
		return -EAGAIN;

	set_bit(idx, used_mask);

	return idx;
}

static u32 hisi_l3c_pmu_event_readl(struct hw_perf_event *hwc, u32 reg)
{
	return readl((void __iomem *)hwc->event_base + reg);
}

static void hisi_l3c_pmu_event_writel(struct hw_perf_event *hwc, u32 reg, u32 val)
{
	writel(val, (void __iomem *)hwc->event_base + reg);
}

static u64 hisi_l3c_pmu_event_readq(struct hw_perf_event *hwc, u32 reg)
{
	return readq((void __iomem *)hwc->event_base + reg);
}

static void hisi_l3c_pmu_event_writeq(struct hw_perf_event *hwc, u32 reg, u64 val)
{
	writeq(val, (void __iomem *)hwc->event_base + reg);
}

static void hisi_l3c_pmu_config_req_tracetag(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u32 tt_req = hisi_get_tt_req(event);

	if (tt_req) {
		u32 val;

		/* Set request-type for tracetag */
		val = hisi_l3c_pmu_event_readl(hwc, L3C_TRACETAG_CTRL);
		val |= tt_req << L3C_TRACETAG_REQ_SHIFT;
		val |= L3C_TRACETAG_REQ_EN;
		hisi_l3c_pmu_event_writel(hwc, L3C_TRACETAG_CTRL, val);

		/* Enable request-tracetag statistics */
		val = hisi_l3c_pmu_event_readl(hwc, L3C_PERF_CTRL);
		val |= L3C_TRACETAG_EN;
		hisi_l3c_pmu_event_writel(hwc, L3C_PERF_CTRL, val);
	}
}

static void hisi_l3c_pmu_clear_req_tracetag(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u32 tt_req = hisi_get_tt_req(event);

	if (tt_req) {
		u32 val;

		/* Clear request-type */
		val = hisi_l3c_pmu_event_readl(hwc, L3C_TRACETAG_CTRL);
		val &= ~(tt_req << L3C_TRACETAG_REQ_SHIFT);
		val &= ~L3C_TRACETAG_REQ_EN;
		hisi_l3c_pmu_event_writel(hwc, L3C_TRACETAG_CTRL, val);

		/* Disable request-tracetag statistics */
		val = hisi_l3c_pmu_event_readl(hwc, L3C_PERF_CTRL);
		val &= ~L3C_TRACETAG_EN;
		hisi_l3c_pmu_event_writel(hwc, L3C_PERF_CTRL, val);
	}
}

static void hisi_l3c_pmu_write_ds(struct perf_event *event, u32 ds_cfg)
{
	struct hw_perf_event *hwc = &event->hw;
	u32 reg, reg_idx, shift, val;
	int idx = L3C_HW_IDX(hwc->idx);

	/*
	 * Select the appropriate datasource register(L3C_DATSRC_TYPE0/1).
	 * There are 2 datasource ctrl register for the 8 hardware counters.
	 * Datasrc is 8-bits and for the former 4 hardware counters,
	 * L3C_DATSRC_TYPE0 is chosen. For the latter 4 hardware counters,
	 * L3C_DATSRC_TYPE1 is chosen.
	 */
	reg = L3C_DATSRC_TYPE + (idx / 4) * 4;
	reg_idx = idx % 4;
	shift = 8 * reg_idx;

	val = hisi_l3c_pmu_event_readl(hwc, reg);
	val &= ~(L3C_DATSRC_MASK << shift);
	val |= ds_cfg << shift;
	hisi_l3c_pmu_event_writel(hwc, reg, val);
}

static void hisi_l3c_pmu_config_ds(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u32 ds_cfg = hisi_get_datasrc_cfg(event);
	u32 ds_skt = hisi_get_datasrc_skt(event);

	if (ds_cfg)
		hisi_l3c_pmu_write_ds(event, ds_cfg);

	if (ds_skt) {
		u32 val;

		val = hisi_l3c_pmu_event_readl(hwc, L3C_DATSRC_CTRL);
		val |= L3C_DATSRC_SKT_EN;
		hisi_l3c_pmu_event_writel(hwc, L3C_DATSRC_CTRL, val);
	}
}

static void hisi_l3c_pmu_clear_ds(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u32 ds_cfg = hisi_get_datasrc_cfg(event);
	u32 ds_skt = hisi_get_datasrc_skt(event);

	if (ds_cfg)
		hisi_l3c_pmu_write_ds(event, L3C_DATSRC_NONE);

	if (ds_skt) {
		u32 val;

		val = hisi_l3c_pmu_event_readl(hwc, L3C_DATSRC_CTRL);
		val &= ~L3C_DATSRC_SKT_EN;
		hisi_l3c_pmu_event_writel(hwc, L3C_DATSRC_CTRL, val);
	}
}

static void hisi_l3c_pmu_config_core_tracetag(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u32 core = hisi_l3c_pmu_get_tt_core(event);

	if (core) {
		u32 val;

		/* Config and enable core information */
		hisi_l3c_pmu_event_writel(hwc, L3C_CORE_CTRL, core);
		val = hisi_l3c_pmu_event_readl(hwc, L3C_PERF_CTRL);
		val |= L3C_CORE_EN;
		hisi_l3c_pmu_event_writel(hwc, L3C_PERF_CTRL, val);

		/* Enable core-tracetag statistics */
		val = hisi_l3c_pmu_event_readl(hwc, L3C_TRACETAG_CTRL);
		val |= L3C_TRACETAG_CORE_EN;
		hisi_l3c_pmu_event_writel(hwc, L3C_TRACETAG_CTRL, val);
	}
}

static void hisi_l3c_pmu_clear_core_tracetag(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u32 core = hisi_l3c_pmu_get_tt_core(event);

	if (core) {
		u32 val;

		/* Clear core information */
		hisi_l3c_pmu_event_writel(hwc, L3C_CORE_CTRL, L3C_COER_NONE);
		val = hisi_l3c_pmu_event_readl(hwc, L3C_PERF_CTRL);
		val &= ~L3C_CORE_EN;
		hisi_l3c_pmu_event_writel(hwc, L3C_PERF_CTRL, val);

		/* Disable core-tracetag statistics */
		val = hisi_l3c_pmu_event_readl(hwc, L3C_TRACETAG_CTRL);
		val &= ~L3C_TRACETAG_CORE_EN;
		hisi_l3c_pmu_event_writel(hwc, L3C_TRACETAG_CTRL, val);
	}
}

static bool hisi_l3c_pmu_have_filter(struct perf_event *event)
{
	return hisi_get_tt_req(event) || hisi_l3c_pmu_get_tt_core(event) ||
	       hisi_get_datasrc_cfg(event) || hisi_get_datasrc_skt(event);
}

static void hisi_l3c_pmu_enable_filter(struct perf_event *event)
{
	if (hisi_l3c_pmu_have_filter(event)) {
		hisi_l3c_pmu_config_req_tracetag(event);
		hisi_l3c_pmu_config_core_tracetag(event);
		hisi_l3c_pmu_config_ds(event);
	}
}

static void hisi_l3c_pmu_disable_filter(struct perf_event *event)
{
	if (hisi_l3c_pmu_have_filter(event)) {
		hisi_l3c_pmu_clear_ds(event);
		hisi_l3c_pmu_clear_core_tracetag(event);
		hisi_l3c_pmu_clear_req_tracetag(event);
	}
}

static int hisi_l3c_pmu_check_filter(struct perf_event *event)
{
	struct hisi_pmu *l3c_pmu = to_hisi_pmu(event->pmu);
	struct hisi_l3c_pmu *hisi_l3c_pmu = to_hisi_l3c_pmu(l3c_pmu);
	int ext = hisi_get_ext(event);

	if (ext < 0 || ext > hisi_l3c_pmu->ext_num)
		return -EINVAL;

	if (hisi_get_tt_core(event) && hisi_get_tt_core_deprecated(event))
		return -EINVAL;

	return 0;
}

/*
 * Select the counter register offset using the counter index
 */
static u32 hisi_l3c_pmu_get_counter_offset(int cntr_idx)
{
	return L3C_CNTR0_LOWER + L3C_HW_IDX(cntr_idx) * 8;
}

static u64 hisi_l3c_pmu_read_counter(struct hisi_pmu *l3c_pmu,
				     struct hw_perf_event *hwc)
{
	return hisi_l3c_pmu_event_readq(hwc, hisi_l3c_pmu_get_counter_offset(hwc->idx));
}

static void hisi_l3c_pmu_write_counter(struct hisi_pmu *l3c_pmu,
				       struct hw_perf_event *hwc, u64 val)
{
	hisi_l3c_pmu_event_writeq(hwc, hisi_l3c_pmu_get_counter_offset(hwc->idx), val);
}

static void hisi_l3c_pmu_write_evtype(struct hisi_pmu *l3c_pmu, int idx,
				      u32 type)
{
	struct hw_perf_event *hwc = &l3c_pmu->pmu_events.hw_events[idx]->hw;
	u32 reg, reg_idx, shift, val;

	idx = L3C_HW_IDX(idx);

	/*
	 * Select the appropriate event select register(L3C_EVENT_TYPE0/1).
	 * There are 2 event select registers for the 8 hardware counters.
	 * Event code is 8-bits and for the former 4 hardware counters,
	 * L3C_EVENT_TYPE0 is chosen. For the latter 4 hardware counters,
	 * L3C_EVENT_TYPE1 is chosen.
	 */
	reg = L3C_EVENT_TYPE0 + (idx / 4) * 4;
	reg_idx = idx % 4;
	shift = 8 * reg_idx;

	/* Write event code to L3C_EVENT_TYPEx Register */
	val = hisi_l3c_pmu_event_readl(hwc, reg);
	val &= ~(L3C_EVTYPE_NONE << shift);
	val |= type << shift;
	hisi_l3c_pmu_event_writel(hwc, reg, val);
}

static void hisi_l3c_pmu_start_counters(struct hisi_pmu *l3c_pmu)
{
	struct hisi_l3c_pmu *hisi_l3c_pmu = to_hisi_l3c_pmu(l3c_pmu);
	unsigned long *used_mask = l3c_pmu->pmu_events.used_mask;
	unsigned long used_cntr = find_first_bit(used_mask, l3c_pmu->num_counters);
	u32 val;
	int i;

	/*
	 * Check if any counter belongs to the normal range (instead of ext
	 * range). If so, enable it.
	 */
	if (used_cntr < L3C_NR_COUNTERS) {
		val = readl(l3c_pmu->base + L3C_PERF_CTRL);
		val |= L3C_PERF_CTRL_EN;
		writel(val, l3c_pmu->base + L3C_PERF_CTRL);
	}

	/* If not, do enable it on ext ranges. */
	for (i = 0; i < hisi_l3c_pmu->ext_num; i++) {
		/* Find used counter in this ext range, skip the range if not. */
		used_cntr = find_next_bit(used_mask, L3C_CNTR_EXT_H(i), L3C_CNTR_EXT_L(i));
		if (used_cntr >= L3C_CNTR_EXT_H(i))
			continue;

		val = readl(hisi_l3c_pmu->ext_base[i] + L3C_PERF_CTRL);
		val |= L3C_PERF_CTRL_EN;
		writel(val, hisi_l3c_pmu->ext_base[i] + L3C_PERF_CTRL);
	}
}

static void hisi_l3c_pmu_stop_counters(struct hisi_pmu *l3c_pmu)
{
	struct hisi_l3c_pmu *hisi_l3c_pmu = to_hisi_l3c_pmu(l3c_pmu);
	unsigned long *used_mask = l3c_pmu->pmu_events.used_mask;
	unsigned long used_cntr = find_first_bit(used_mask, l3c_pmu->num_counters);
	u32 val;
	int i;

	/*
	 * Check if any counter belongs to the normal range (instead of ext
	 * range). If so, stop it.
	 */
	if (used_cntr < L3C_NR_COUNTERS) {
		val = readl(l3c_pmu->base + L3C_PERF_CTRL);
		val &= ~L3C_PERF_CTRL_EN;
		writel(val, l3c_pmu->base + L3C_PERF_CTRL);
	}

	/* If not, do stop it on ext ranges. */
	for (i = 0; i < hisi_l3c_pmu->ext_num; i++) {
		/* Find used counter in this ext range, skip the range if not. */
		used_cntr = find_next_bit(used_mask, L3C_CNTR_EXT_H(i), L3C_CNTR_EXT_L(i));
		if (used_cntr >= L3C_CNTR_EXT_H(i))
			continue;

		val = readl(hisi_l3c_pmu->ext_base[i] + L3C_PERF_CTRL);
		val &= ~L3C_PERF_CTRL_EN;
		writel(val, hisi_l3c_pmu->ext_base[i] + L3C_PERF_CTRL);
	}
}

static void hisi_l3c_pmu_enable_counter(struct hisi_pmu *l3c_pmu,
					struct hw_perf_event *hwc)
{
	u32 val;

	/* Enable counter index in L3C_EVENT_CTRL register */
	val = hisi_l3c_pmu_event_readl(hwc, L3C_EVENT_CTRL);
	val |= 1 << L3C_HW_IDX(hwc->idx);
	hisi_l3c_pmu_event_writel(hwc, L3C_EVENT_CTRL, val);
}

static void hisi_l3c_pmu_disable_counter(struct hisi_pmu *l3c_pmu,
					 struct hw_perf_event *hwc)
{
	u32 val;

	/* Clear counter index in L3C_EVENT_CTRL register */
	val = hisi_l3c_pmu_event_readl(hwc, L3C_EVENT_CTRL);
	val &= ~(1 << L3C_HW_IDX(hwc->idx));
	hisi_l3c_pmu_event_writel(hwc, L3C_EVENT_CTRL, val);
}

static void hisi_l3c_pmu_enable_counter_int(struct hisi_pmu *l3c_pmu,
					    struct hw_perf_event *hwc)
{
	u32 val;

	val = hisi_l3c_pmu_event_readl(hwc, L3C_INT_MASK);
	/* Write 0 to enable interrupt */
	val &= ~(1 << L3C_HW_IDX(hwc->idx));
	hisi_l3c_pmu_event_writel(hwc, L3C_INT_MASK, val);
}

static void hisi_l3c_pmu_disable_counter_int(struct hisi_pmu *l3c_pmu,
					     struct hw_perf_event *hwc)
{
	u32 val;

	val = hisi_l3c_pmu_event_readl(hwc, L3C_INT_MASK);
	/* Write 1 to mask interrupt */
	val |= 1 << L3C_HW_IDX(hwc->idx);
	hisi_l3c_pmu_event_writel(hwc, L3C_INT_MASK, val);
}

static u32 hisi_l3c_pmu_get_int_status(struct hisi_pmu *l3c_pmu)
{
	struct hisi_l3c_pmu *hisi_l3c_pmu = to_hisi_l3c_pmu(l3c_pmu);
	u32 ext_int, status, status_ext = 0;
	int i;

	status = readl(l3c_pmu->base + L3C_INT_STATUS);

	if (!support_ext(hisi_l3c_pmu))
		return status;

	for (i = 0; i < hisi_l3c_pmu->ext_num; i++) {
		ext_int = readl(hisi_l3c_pmu->ext_base[i] + L3C_INT_STATUS);
		status_ext |= ext_int << (L3C_NR_COUNTERS * i);
	}

	return status | (status_ext << L3C_NR_COUNTERS);
}

static void hisi_l3c_pmu_clear_int_status(struct hisi_pmu *l3c_pmu, int idx)
{
	struct hw_perf_event *hwc = &l3c_pmu->pmu_events.hw_events[idx]->hw;

	hisi_l3c_pmu_event_writel(hwc, L3C_INT_CLEAR, 1 << L3C_HW_IDX(idx));
}

static int hisi_l3c_pmu_init_data(struct platform_device *pdev,
				  struct hisi_pmu *l3c_pmu)
{
	hisi_uncore_pmu_init_topology(l3c_pmu, &pdev->dev);

	/*
	 * Use the SCCL_ID and CCL_ID to identify the L3C PMU, while
	 * SCCL_ID is in MPIDR[aff2] and CCL_ID is in MPIDR[aff1].
	 */
	if (l3c_pmu->topo.sccl_id < 0) {
		dev_err(&pdev->dev, "Can not read l3c sccl-id!\n");
		return -EINVAL;
	}

	if (l3c_pmu->topo.ccl_id < 0) {
		dev_err(&pdev->dev, "Can not read l3c ccl-id!\n");
		return -EINVAL;
	}

	l3c_pmu->dev_info = device_get_match_data(&pdev->dev);
	if (!l3c_pmu->dev_info)
		return -ENODEV;

	l3c_pmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(l3c_pmu->base)) {
		dev_err(&pdev->dev, "ioremap failed for l3c_pmu resource\n");
		return PTR_ERR(l3c_pmu->base);
	}

	l3c_pmu->identifier = readl(l3c_pmu->base + L3C_VERSION);

	return 0;
}

static int hisi_l3c_pmu_init_ext(struct hisi_pmu *l3c_pmu, struct platform_device *pdev)
{
	struct hisi_l3c_pmu *hisi_l3c_pmu = to_hisi_l3c_pmu(l3c_pmu);
	int ret, irq, ext_num, i;
	char *irqname;

	/* HiSilicon L3C PMU supporting ext should have more than 1 irq resources. */
	ext_num = platform_irq_count(pdev);
	if (ext_num < L3C_MAX_EXT)
		return -ENODEV;

	/*
	 * The number of ext supported equals the number of irq - 1, since one
	 * of the irqs belongs to the normal part of PMU.
	 */
	hisi_l3c_pmu->ext_num = ext_num - 1;

	for (i = 0; i < hisi_l3c_pmu->ext_num; i++) {
		hisi_l3c_pmu->ext_base[i] = devm_platform_ioremap_resource(pdev, i + 1);
		if (IS_ERR(hisi_l3c_pmu->ext_base[i]))
			return PTR_ERR(hisi_l3c_pmu->ext_base[i]);

		irq = platform_get_irq(pdev, i + 1);
		if (irq < 0)
			return irq;

		irqname = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s ext%d",
					 dev_name(&pdev->dev), i + 1);
		if (!irqname)
			return -ENOMEM;

		ret = devm_request_irq(&pdev->dev, irq, hisi_uncore_pmu_isr,
				       IRQF_NOBALANCING | IRQF_NO_THREAD,
				       irqname, l3c_pmu);
		if (ret < 0)
			return dev_err_probe(&pdev->dev, ret,
				"Fail to request EXT IRQ: %d.\n", irq);

		hisi_l3c_pmu->ext_irq[i] = irq;
	}

	return 0;
}

static struct attribute *hisi_l3c_pmu_v1_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	NULL,
};

static const struct attribute_group hisi_l3c_pmu_v1_format_group = {
	.name = "format",
	.attrs = hisi_l3c_pmu_v1_format_attr,
};

static struct attribute *hisi_l3c_pmu_v2_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	HISI_PMU_FORMAT_ATTR(tt_core_deprecated, "config1:0-7"),
	HISI_PMU_FORMAT_ATTR(tt_req, "config1:8-10"),
	HISI_PMU_FORMAT_ATTR(datasrc_cfg, "config1:11-15"),
	HISI_PMU_FORMAT_ATTR(datasrc_skt, "config1:16"),
	HISI_PMU_FORMAT_ATTR(tt_core, "config2:0-15"),
	NULL
};

static const struct attribute_group hisi_l3c_pmu_v2_format_group = {
	.name = "format",
	.attrs = hisi_l3c_pmu_v2_format_attr,
};

static struct attribute *hisi_l3c_pmu_v3_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	HISI_PMU_FORMAT_ATTR(ext, "config:16-17"),
	HISI_PMU_FORMAT_ATTR(tt_core_deprecated, "config1:0-7"),
	HISI_PMU_FORMAT_ATTR(tt_req, "config1:8-10"),
	HISI_PMU_FORMAT_ATTR(tt_core, "config2:0-15"),
	NULL
};

static const struct attribute_group hisi_l3c_pmu_v3_format_group = {
	.name = "format",
	.attrs = hisi_l3c_pmu_v3_format_attr,
};

static struct attribute *hisi_l3c_pmu_v1_events_attr[] = {
	HISI_PMU_EVENT_ATTR(rd_cpipe,		0x00),
	HISI_PMU_EVENT_ATTR(wr_cpipe,		0x01),
	HISI_PMU_EVENT_ATTR(rd_hit_cpipe,	0x02),
	HISI_PMU_EVENT_ATTR(wr_hit_cpipe,	0x03),
	HISI_PMU_EVENT_ATTR(victim_num,		0x04),
	HISI_PMU_EVENT_ATTR(rd_spipe,		0x20),
	HISI_PMU_EVENT_ATTR(wr_spipe,		0x21),
	HISI_PMU_EVENT_ATTR(rd_hit_spipe,	0x22),
	HISI_PMU_EVENT_ATTR(wr_hit_spipe,	0x23),
	HISI_PMU_EVENT_ATTR(back_invalid,	0x29),
	HISI_PMU_EVENT_ATTR(retry_cpu,		0x40),
	HISI_PMU_EVENT_ATTR(retry_ring,		0x41),
	HISI_PMU_EVENT_ATTR(prefetch_drop,	0x42),
	NULL,
};

static const struct attribute_group hisi_l3c_pmu_v1_events_group = {
	.name = "events",
	.attrs = hisi_l3c_pmu_v1_events_attr,
};

static struct attribute *hisi_l3c_pmu_v2_events_attr[] = {
	HISI_PMU_EVENT_ATTR(l3c_hit,		0x48),
	HISI_PMU_EVENT_ATTR(cycles,		0x7f),
	HISI_PMU_EVENT_ATTR(l3c_ref,		0xb8),
	HISI_PMU_EVENT_ATTR(dat_access,		0xb9),
	NULL
};

static const struct attribute_group hisi_l3c_pmu_v2_events_group = {
	.name = "events",
	.attrs = hisi_l3c_pmu_v2_events_attr,
};

static struct attribute *hisi_l3c_pmu_v3_events_attr[] = {
	HISI_PMU_EVENT_ATTR(rd_spipe,		0x18),
	HISI_PMU_EVENT_ATTR(rd_hit_spipe,	0x19),
	HISI_PMU_EVENT_ATTR(wr_spipe,		0x1a),
	HISI_PMU_EVENT_ATTR(wr_hit_spipe,	0x1b),
	HISI_PMU_EVENT_ATTR(io_rd_spipe,	0x1c),
	HISI_PMU_EVENT_ATTR(io_rd_hit_spipe,	0x1d),
	HISI_PMU_EVENT_ATTR(io_wr_spipe,	0x1e),
	HISI_PMU_EVENT_ATTR(io_wr_hit_spipe,	0x1f),
	HISI_PMU_EVENT_ATTR(cycles,		0x7f),
	HISI_PMU_EVENT_ATTR(l3c_ref,		0xbc),
	HISI_PMU_EVENT_ATTR(l3c2ring,		0xbd),
	NULL
};

static const struct attribute_group hisi_l3c_pmu_v3_events_group = {
	.name = "events",
	.attrs = hisi_l3c_pmu_v3_events_attr,
};

static const struct attribute_group *hisi_l3c_pmu_v1_attr_groups[] = {
	&hisi_l3c_pmu_v1_format_group,
	&hisi_l3c_pmu_v1_events_group,
	&hisi_pmu_cpumask_attr_group,
	&hisi_pmu_identifier_group,
	NULL,
};

static const struct attribute_group *hisi_l3c_pmu_v2_attr_groups[] = {
	&hisi_l3c_pmu_v2_format_group,
	&hisi_l3c_pmu_v2_events_group,
	&hisi_pmu_cpumask_attr_group,
	&hisi_pmu_identifier_group,
	NULL
};

static const struct attribute_group *hisi_l3c_pmu_v3_attr_groups[] = {
	&hisi_l3c_pmu_v3_format_group,
	&hisi_l3c_pmu_v3_events_group,
	&hisi_pmu_cpumask_attr_group,
	&hisi_pmu_identifier_group,
	NULL
};

static struct hisi_l3c_pmu_ext hisi_l3c_pmu_support_ext = {
	.support_ext = true,
};

static struct hisi_l3c_pmu_ext hisi_l3c_pmu_not_support_ext = {
	.support_ext = false,
};

static const struct hisi_pmu_dev_info hisi_l3c_pmu_v1 = {
	.attr_groups = hisi_l3c_pmu_v1_attr_groups,
	.counter_bits = 48,
	.check_event = L3C_V1_NR_EVENTS,
	.private = &hisi_l3c_pmu_not_support_ext,
};

static const struct hisi_pmu_dev_info hisi_l3c_pmu_v2 = {
	.attr_groups = hisi_l3c_pmu_v2_attr_groups,
	.counter_bits = 64,
	.check_event = L3C_V2_NR_EVENTS,
	.private = &hisi_l3c_pmu_not_support_ext,
};

static const struct hisi_pmu_dev_info hisi_l3c_pmu_v3 = {
	.attr_groups = hisi_l3c_pmu_v3_attr_groups,
	.counter_bits = 64,
	.check_event = L3C_V2_NR_EVENTS,
	.private = &hisi_l3c_pmu_support_ext,
};

static const struct hisi_uncore_ops hisi_uncore_l3c_ops = {
	.write_evtype		= hisi_l3c_pmu_write_evtype,
	.get_event_idx		= hisi_l3c_pmu_get_event_idx,
	.start_counters		= hisi_l3c_pmu_start_counters,
	.stop_counters		= hisi_l3c_pmu_stop_counters,
	.enable_counter		= hisi_l3c_pmu_enable_counter,
	.disable_counter	= hisi_l3c_pmu_disable_counter,
	.enable_counter_int	= hisi_l3c_pmu_enable_counter_int,
	.disable_counter_int	= hisi_l3c_pmu_disable_counter_int,
	.write_counter		= hisi_l3c_pmu_write_counter,
	.read_counter		= hisi_l3c_pmu_read_counter,
	.get_int_status		= hisi_l3c_pmu_get_int_status,
	.clear_int_status	= hisi_l3c_pmu_clear_int_status,
	.enable_filter		= hisi_l3c_pmu_enable_filter,
	.disable_filter		= hisi_l3c_pmu_disable_filter,
	.check_filter		= hisi_l3c_pmu_check_filter,
};

static int hisi_l3c_pmu_dev_probe(struct platform_device *pdev,
				  struct hisi_pmu *l3c_pmu)
{
	struct hisi_l3c_pmu *hisi_l3c_pmu = to_hisi_l3c_pmu(l3c_pmu);
	struct hisi_l3c_pmu_ext *l3c_pmu_dev_ext;
	int ret;

	ret = hisi_l3c_pmu_init_data(pdev, l3c_pmu);
	if (ret)
		return ret;

	ret = hisi_uncore_pmu_init_irq(l3c_pmu, pdev);
	if (ret)
		return ret;

	l3c_pmu->pmu_events.attr_groups = l3c_pmu->dev_info->attr_groups;
	l3c_pmu->counter_bits = l3c_pmu->dev_info->counter_bits;
	l3c_pmu->check_event = l3c_pmu->dev_info->check_event;
	l3c_pmu->num_counters = L3C_NR_COUNTERS;
	l3c_pmu->ops = &hisi_uncore_l3c_ops;
	l3c_pmu->dev = &pdev->dev;
	l3c_pmu->on_cpu = -1;

	l3c_pmu_dev_ext = l3c_pmu->dev_info->private;
	if (l3c_pmu_dev_ext->support_ext) {
		ret = hisi_l3c_pmu_init_ext(l3c_pmu, pdev);
		if (ret)
			return ret;
		/*
		 * The extension events have their own counters with the
		 * same number of the normal events counters. So we can
		 * have at maximum num_counters * ext events monitored.
		 */
		l3c_pmu->num_counters += hisi_l3c_pmu->ext_num * L3C_NR_COUNTERS;
	}

	return 0;
}

static int hisi_l3c_pmu_probe(struct platform_device *pdev)
{
	struct hisi_l3c_pmu *hisi_l3c_pmu;
	struct hisi_pmu *l3c_pmu;
	char *name;
	int ret;

	hisi_l3c_pmu = devm_kzalloc(&pdev->dev, sizeof(*hisi_l3c_pmu), GFP_KERNEL);
	if (!hisi_l3c_pmu)
		return -ENOMEM;

	l3c_pmu = &hisi_l3c_pmu->l3c_pmu;
	platform_set_drvdata(pdev, l3c_pmu);

	ret = hisi_l3c_pmu_dev_probe(pdev, l3c_pmu);
	if (ret)
		return ret;

	if (l3c_pmu->topo.sub_id >= 0)
		name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hisi_sccl%d_l3c%d_%d",
				      l3c_pmu->topo.sccl_id, l3c_pmu->topo.ccl_id,
				      l3c_pmu->topo.sub_id);
	else
		name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hisi_sccl%d_l3c%d",
				      l3c_pmu->topo.sccl_id, l3c_pmu->topo.ccl_id);
	if (!name)
		return -ENOMEM;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_HISI_L3_ONLINE,
				       &l3c_pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug\n", ret);
		return ret;
	}

	hisi_pmu_init(l3c_pmu, THIS_MODULE);

	ret = perf_pmu_register(&l3c_pmu->pmu, name, -1);
	if (ret) {
		dev_err(l3c_pmu->dev, "L3C PMU register failed!\n");
		cpuhp_state_remove_instance_nocalls(
			CPUHP_AP_PERF_ARM_HISI_L3_ONLINE, &l3c_pmu->node);
	}

	return ret;
}

static void hisi_l3c_pmu_remove(struct platform_device *pdev)
{
	struct hisi_pmu *l3c_pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&l3c_pmu->pmu);
	cpuhp_state_remove_instance_nocalls(CPUHP_AP_PERF_ARM_HISI_L3_ONLINE,
					    &l3c_pmu->node);
}

static const struct acpi_device_id hisi_l3c_pmu_acpi_match[] = {
	{ "HISI0213", (kernel_ulong_t)&hisi_l3c_pmu_v1 },
	{ "HISI0214", (kernel_ulong_t)&hisi_l3c_pmu_v2 },
	{ "HISI0215", (kernel_ulong_t)&hisi_l3c_pmu_v3 },
	{}
};
MODULE_DEVICE_TABLE(acpi, hisi_l3c_pmu_acpi_match);

static struct platform_driver hisi_l3c_pmu_driver = {
	.driver = {
		.name = "hisi_l3c_pmu",
		.acpi_match_table = ACPI_PTR(hisi_l3c_pmu_acpi_match),
		.suppress_bind_attrs = true,
	},
	.probe = hisi_l3c_pmu_probe,
	.remove = hisi_l3c_pmu_remove,
};

static int hisi_l3c_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_pmu *l3c_pmu = hlist_entry_safe(node, struct hisi_pmu, node);
	struct hisi_l3c_pmu *hisi_l3c_pmu = to_hisi_l3c_pmu(l3c_pmu);
	int ret, i;

	ret = hisi_uncore_pmu_online_cpu(cpu, node);
	if (ret)
		return ret;

	/* Avoid L3C pmu not supporting ext from ext irq migrating. */
	if (!support_ext(hisi_l3c_pmu))
		return 0;

	for (i = 0; i < hisi_l3c_pmu->ext_num; i++)
		WARN_ON(irq_set_affinity(hisi_l3c_pmu->ext_irq[i],
					 cpumask_of(l3c_pmu->on_cpu)));

	return 0;
}

static int hisi_l3c_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_pmu *l3c_pmu = hlist_entry_safe(node, struct hisi_pmu, node);
	struct hisi_l3c_pmu *hisi_l3c_pmu = to_hisi_l3c_pmu(l3c_pmu);
	int ret, i;

	ret = hisi_uncore_pmu_offline_cpu(cpu, node);
	if (ret)
		return ret;

	/* If failed to find any available CPU, skip irq migration. */
	if (l3c_pmu->on_cpu < 0)
		return 0;

	/* Avoid L3C pmu not supporting ext from ext irq migrating. */
	if (!support_ext(hisi_l3c_pmu))
		return 0;

	for (i = 0; i < hisi_l3c_pmu->ext_num; i++)
		WARN_ON(irq_set_affinity(hisi_l3c_pmu->ext_irq[i],
					 cpumask_of(l3c_pmu->on_cpu)));

	return 0;
}

static int __init hisi_l3c_pmu_module_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_HISI_L3_ONLINE,
				      "AP_PERF_ARM_HISI_L3_ONLINE",
				      hisi_l3c_pmu_online_cpu,
				      hisi_l3c_pmu_offline_cpu);
	if (ret) {
		pr_err("L3C PMU: Error setup hotplug, ret = %d\n", ret);
		return ret;
	}

	ret = platform_driver_register(&hisi_l3c_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_L3_ONLINE);

	return ret;
}
module_init(hisi_l3c_pmu_module_init);

static void __exit hisi_l3c_pmu_module_exit(void)
{
	platform_driver_unregister(&hisi_l3c_pmu_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_L3_ONLINE);
}
module_exit(hisi_l3c_pmu_module_exit);

MODULE_IMPORT_NS("HISI_PMU");
MODULE_DESCRIPTION("HiSilicon SoC L3C uncore PMU driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Anurup M <anurup.m@huawei.com>");
MODULE_AUTHOR("Shaokun Zhang <zhangshaokun@hisilicon.com>");
