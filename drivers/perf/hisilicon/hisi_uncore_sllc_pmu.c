// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SLLC uncore Hardware event counters support
 *
 * Copyright (C) 2020 HiSilicon Limited
 * Author: Shaokun Zhang <zhangshaokun@hisilicon.com>
 *
 * This code is based on the uncore PMUs like arm-cci and arm-ccn.
 */
#include <linux/acpi.h>
#include <linux/cpuhotplug.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/smp.h>

#include "hisi_uncore_pmu.h"

/* SLLC register definition */
#define SLLC_INT_MASK			0x0814
#define SLLC_INT_STATUS			0x0818
#define SLLC_INT_CLEAR			0x081c
#define SLLC_PERF_CTRL			0x1c00
#define SLLC_SRCID_CTRL			0x1c04
#define SLLC_TGTID_CTRL			0x1c08
#define SLLC_EVENT_CTRL			0x1c14
#define SLLC_EVENT_TYPE0		0x1c18
#define SLLC_VERSION			0x1cf0
#define SLLC_EVENT_CNT0_L		0x1d00

#define SLLC_EVTYPE_MASK		0xff
#define SLLC_PERF_CTRL_EN		BIT(0)
#define SLLC_FILT_EN			BIT(1)
#define SLLC_TRACETAG_EN		BIT(2)
#define SLLC_SRCID_EN			BIT(4)
#define SLLC_SRCID_NONE			0x0
#define SLLC_TGTID_EN			BIT(5)
#define SLLC_TGTID_NONE			0x0
#define SLLC_TGTID_MIN_SHIFT		1
#define SLLC_TGTID_MAX_SHIFT		12
#define SLLC_SRCID_CMD_SHIFT		1
#define SLLC_SRCID_MSK_SHIFT		12
#define SLLC_NR_EVENTS			0x80

HISI_PMU_EVENT_ATTR_EXTRACTOR(tgtid_min, config1, 10, 0);
HISI_PMU_EVENT_ATTR_EXTRACTOR(tgtid_max, config1, 21, 11);
HISI_PMU_EVENT_ATTR_EXTRACTOR(srcid_cmd, config1, 32, 22);
HISI_PMU_EVENT_ATTR_EXTRACTOR(srcid_msk, config1, 43, 33);
HISI_PMU_EVENT_ATTR_EXTRACTOR(tracetag_en, config1, 44, 44);

static bool tgtid_is_valid(u32 max, u32 min)
{
	return max > 0 && max >= min;
}

static void hisi_sllc_pmu_enable_tracetag(struct perf_event *event)
{
	struct hisi_pmu *sllc_pmu = to_hisi_pmu(event->pmu);
	u32 tt_en = hisi_get_tracetag_en(event);

	if (tt_en) {
		u32 val;

		val = readl(sllc_pmu->base + SLLC_PERF_CTRL);
		val |= SLLC_TRACETAG_EN | SLLC_FILT_EN;
		writel(val, sllc_pmu->base + SLLC_PERF_CTRL);
	}
}

static void hisi_sllc_pmu_disable_tracetag(struct perf_event *event)
{
	struct hisi_pmu *sllc_pmu = to_hisi_pmu(event->pmu);
	u32 tt_en = hisi_get_tracetag_en(event);

	if (tt_en) {
		u32 val;

		val = readl(sllc_pmu->base + SLLC_PERF_CTRL);
		val &= ~(SLLC_TRACETAG_EN | SLLC_FILT_EN);
		writel(val, sllc_pmu->base + SLLC_PERF_CTRL);
	}
}

static void hisi_sllc_pmu_config_tgtid(struct perf_event *event)
{
	struct hisi_pmu *sllc_pmu = to_hisi_pmu(event->pmu);
	u32 min = hisi_get_tgtid_min(event);
	u32 max = hisi_get_tgtid_max(event);

	if (tgtid_is_valid(max, min)) {
		u32 val = (max << SLLC_TGTID_MAX_SHIFT) | (min << SLLC_TGTID_MIN_SHIFT);

		writel(val, sllc_pmu->base + SLLC_TGTID_CTRL);
		/* Enable the tgtid */
		val = readl(sllc_pmu->base + SLLC_PERF_CTRL);
		val |= SLLC_TGTID_EN | SLLC_FILT_EN;
		writel(val, sllc_pmu->base + SLLC_PERF_CTRL);
	}
}

static void hisi_sllc_pmu_clear_tgtid(struct perf_event *event)
{
	struct hisi_pmu *sllc_pmu = to_hisi_pmu(event->pmu);
	u32 min = hisi_get_tgtid_min(event);
	u32 max = hisi_get_tgtid_max(event);

	if (tgtid_is_valid(max, min)) {
		u32 val;

		writel(SLLC_TGTID_NONE, sllc_pmu->base + SLLC_TGTID_CTRL);
		/* Disable the tgtid */
		val = readl(sllc_pmu->base + SLLC_PERF_CTRL);
		val &= ~(SLLC_TGTID_EN | SLLC_FILT_EN);
		writel(val, sllc_pmu->base + SLLC_PERF_CTRL);
	}
}

static void hisi_sllc_pmu_config_srcid(struct perf_event *event)
{
	struct hisi_pmu *sllc_pmu = to_hisi_pmu(event->pmu);
	u32 cmd = hisi_get_srcid_cmd(event);

	if (cmd) {
		u32 val, msk;

		msk = hisi_get_srcid_msk(event);
		val = (cmd << SLLC_SRCID_CMD_SHIFT) | (msk << SLLC_SRCID_MSK_SHIFT);
		writel(val, sllc_pmu->base + SLLC_SRCID_CTRL);
		/* Enable the srcid */
		val = readl(sllc_pmu->base + SLLC_PERF_CTRL);
		val |= SLLC_SRCID_EN | SLLC_FILT_EN;
		writel(val, sllc_pmu->base + SLLC_PERF_CTRL);
	}
}

static void hisi_sllc_pmu_clear_srcid(struct perf_event *event)
{
	struct hisi_pmu *sllc_pmu = to_hisi_pmu(event->pmu);
	u32 cmd = hisi_get_srcid_cmd(event);

	if (cmd) {
		u32 val;

		writel(SLLC_SRCID_NONE, sllc_pmu->base + SLLC_SRCID_CTRL);
		/* Disable the srcid */
		val = readl(sllc_pmu->base + SLLC_PERF_CTRL);
		val &= ~(SLLC_SRCID_EN | SLLC_FILT_EN);
		writel(val, sllc_pmu->base + SLLC_PERF_CTRL);
	}
}

static void hisi_sllc_pmu_enable_filter(struct perf_event *event)
{
	if (event->attr.config1 != 0x0) {
		hisi_sllc_pmu_enable_tracetag(event);
		hisi_sllc_pmu_config_srcid(event);
		hisi_sllc_pmu_config_tgtid(event);
	}
}

static void hisi_sllc_pmu_clear_filter(struct perf_event *event)
{
	if (event->attr.config1 != 0x0) {
		hisi_sllc_pmu_disable_tracetag(event);
		hisi_sllc_pmu_clear_srcid(event);
		hisi_sllc_pmu_clear_tgtid(event);
	}
}

static u32 hisi_sllc_pmu_get_counter_offset(int idx)
{
	return (SLLC_EVENT_CNT0_L + idx * 8);
}

static u64 hisi_sllc_pmu_read_counter(struct hisi_pmu *sllc_pmu,
				      struct hw_perf_event *hwc)
{
	return readq(sllc_pmu->base +
		     hisi_sllc_pmu_get_counter_offset(hwc->idx));
}

static void hisi_sllc_pmu_write_counter(struct hisi_pmu *sllc_pmu,
					struct hw_perf_event *hwc, u64 val)
{
	writeq(val, sllc_pmu->base +
	       hisi_sllc_pmu_get_counter_offset(hwc->idx));
}

static void hisi_sllc_pmu_write_evtype(struct hisi_pmu *sllc_pmu, int idx,
				       u32 type)
{
	u32 reg, reg_idx, shift, val;

	/*
	 * Select the appropriate event select register(SLLC_EVENT_TYPE0/1).
	 * There are 2 event select registers for the 8 hardware counters.
	 * Event code is 8-bits and for the former 4 hardware counters,
	 * SLLC_EVENT_TYPE0 is chosen. For the latter 4 hardware counters,
	 * SLLC_EVENT_TYPE1 is chosen.
	 */
	reg = SLLC_EVENT_TYPE0 + (idx / 4) * 4;
	reg_idx = idx % 4;
	shift = 8 * reg_idx;

	/* Write event code to SLLC_EVENT_TYPEx Register */
	val = readl(sllc_pmu->base + reg);
	val &= ~(SLLC_EVTYPE_MASK << shift);
	val |= (type << shift);
	writel(val, sllc_pmu->base + reg);
}

static void hisi_sllc_pmu_start_counters(struct hisi_pmu *sllc_pmu)
{
	u32 val;

	val = readl(sllc_pmu->base + SLLC_PERF_CTRL);
	val |= SLLC_PERF_CTRL_EN;
	writel(val, sllc_pmu->base + SLLC_PERF_CTRL);
}

static void hisi_sllc_pmu_stop_counters(struct hisi_pmu *sllc_pmu)
{
	u32 val;

	val = readl(sllc_pmu->base + SLLC_PERF_CTRL);
	val &= ~(SLLC_PERF_CTRL_EN);
	writel(val, sllc_pmu->base + SLLC_PERF_CTRL);
}

static void hisi_sllc_pmu_enable_counter(struct hisi_pmu *sllc_pmu,
					 struct hw_perf_event *hwc)
{
	u32 val;

	val = readl(sllc_pmu->base + SLLC_EVENT_CTRL);
	val |= 1 << hwc->idx;
	writel(val, sllc_pmu->base + SLLC_EVENT_CTRL);
}

static void hisi_sllc_pmu_disable_counter(struct hisi_pmu *sllc_pmu,
					  struct hw_perf_event *hwc)
{
	u32 val;

	val = readl(sllc_pmu->base + SLLC_EVENT_CTRL);
	val &= ~(1 << hwc->idx);
	writel(val, sllc_pmu->base + SLLC_EVENT_CTRL);
}

static void hisi_sllc_pmu_enable_counter_int(struct hisi_pmu *sllc_pmu,
					     struct hw_perf_event *hwc)
{
	u32 val;

	val = readl(sllc_pmu->base + SLLC_INT_MASK);
	/* Write 0 to enable interrupt */
	val &= ~(1 << hwc->idx);
	writel(val, sllc_pmu->base + SLLC_INT_MASK);
}

static void hisi_sllc_pmu_disable_counter_int(struct hisi_pmu *sllc_pmu,
					      struct hw_perf_event *hwc)
{
	u32 val;

	val = readl(sllc_pmu->base + SLLC_INT_MASK);
	/* Write 1 to mask interrupt */
	val |= 1 << hwc->idx;
	writel(val, sllc_pmu->base + SLLC_INT_MASK);
}

static u32 hisi_sllc_pmu_get_int_status(struct hisi_pmu *sllc_pmu)
{
	return readl(sllc_pmu->base + SLLC_INT_STATUS);
}

static void hisi_sllc_pmu_clear_int_status(struct hisi_pmu *sllc_pmu, int idx)
{
	writel(1 << idx, sllc_pmu->base + SLLC_INT_CLEAR);
}

static const struct acpi_device_id hisi_sllc_pmu_acpi_match[] = {
	{ "HISI0263", },
	{}
};
MODULE_DEVICE_TABLE(acpi, hisi_sllc_pmu_acpi_match);

static int hisi_sllc_pmu_init_data(struct platform_device *pdev,
				   struct hisi_pmu *sllc_pmu)
{
	/*
	 * Use the SCCL_ID and the index ID to identify the SLLC PMU,
	 * while SCCL_ID is from MPIDR_EL1 by CPU.
	 */
	if (device_property_read_u32(&pdev->dev, "hisilicon,scl-id",
				     &sllc_pmu->sccl_id)) {
		dev_err(&pdev->dev, "Cannot read sccl-id!\n");
		return -EINVAL;
	}

	if (device_property_read_u32(&pdev->dev, "hisilicon,idx-id",
				     &sllc_pmu->index_id)) {
		dev_err(&pdev->dev, "Cannot read idx-id!\n");
		return -EINVAL;
	}

	/* SLLC PMUs only share the same SCCL */
	sllc_pmu->ccl_id = -1;

	sllc_pmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sllc_pmu->base)) {
		dev_err(&pdev->dev, "ioremap failed for sllc_pmu resource.\n");
		return PTR_ERR(sllc_pmu->base);
	}

	sllc_pmu->identifier = readl(sllc_pmu->base + SLLC_VERSION);

	return 0;
}

static struct attribute *hisi_sllc_pmu_v2_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	HISI_PMU_FORMAT_ATTR(tgtid_min, "config1:0-10"),
	HISI_PMU_FORMAT_ATTR(tgtid_max, "config1:11-21"),
	HISI_PMU_FORMAT_ATTR(srcid_cmd, "config1:22-32"),
	HISI_PMU_FORMAT_ATTR(srcid_msk, "config1:33-43"),
	HISI_PMU_FORMAT_ATTR(tracetag_en, "config1:44"),
	NULL
};

static const struct attribute_group hisi_sllc_pmu_v2_format_group = {
	.name = "format",
	.attrs = hisi_sllc_pmu_v2_format_attr,
};

static struct attribute *hisi_sllc_pmu_v2_events_attr[] = {
	HISI_PMU_EVENT_ATTR(rx_req,             0x30),
	HISI_PMU_EVENT_ATTR(rx_data,            0x31),
	HISI_PMU_EVENT_ATTR(tx_req,             0x34),
	HISI_PMU_EVENT_ATTR(tx_data,            0x35),
	HISI_PMU_EVENT_ATTR(cycles,             0x09),
	NULL
};

static const struct attribute_group hisi_sllc_pmu_v2_events_group = {
	.name = "events",
	.attrs = hisi_sllc_pmu_v2_events_attr,
};

static DEVICE_ATTR(cpumask, 0444, hisi_cpumask_sysfs_show, NULL);

static struct attribute *hisi_sllc_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static const struct attribute_group hisi_sllc_pmu_cpumask_attr_group = {
	.attrs = hisi_sllc_pmu_cpumask_attrs,
};

static struct device_attribute hisi_sllc_pmu_identifier_attr =
	__ATTR(identifier, 0444, hisi_uncore_pmu_identifier_attr_show, NULL);

static struct attribute *hisi_sllc_pmu_identifier_attrs[] = {
	&hisi_sllc_pmu_identifier_attr.attr,
	NULL
};

static const struct attribute_group hisi_sllc_pmu_identifier_group = {
	.attrs = hisi_sllc_pmu_identifier_attrs,
};

static const struct attribute_group *hisi_sllc_pmu_v2_attr_groups[] = {
	&hisi_sllc_pmu_v2_format_group,
	&hisi_sllc_pmu_v2_events_group,
	&hisi_sllc_pmu_cpumask_attr_group,
	&hisi_sllc_pmu_identifier_group,
	NULL
};

static const struct hisi_uncore_ops hisi_uncore_sllc_ops = {
	.write_evtype		= hisi_sllc_pmu_write_evtype,
	.get_event_idx		= hisi_uncore_pmu_get_event_idx,
	.start_counters		= hisi_sllc_pmu_start_counters,
	.stop_counters		= hisi_sllc_pmu_stop_counters,
	.enable_counter		= hisi_sllc_pmu_enable_counter,
	.disable_counter	= hisi_sllc_pmu_disable_counter,
	.enable_counter_int	= hisi_sllc_pmu_enable_counter_int,
	.disable_counter_int	= hisi_sllc_pmu_disable_counter_int,
	.write_counter		= hisi_sllc_pmu_write_counter,
	.read_counter		= hisi_sllc_pmu_read_counter,
	.get_int_status		= hisi_sllc_pmu_get_int_status,
	.clear_int_status	= hisi_sllc_pmu_clear_int_status,
	.enable_filter		= hisi_sllc_pmu_enable_filter,
	.disable_filter		= hisi_sllc_pmu_clear_filter,
};

static int hisi_sllc_pmu_dev_probe(struct platform_device *pdev,
				   struct hisi_pmu *sllc_pmu)
{
	int ret;

	ret = hisi_sllc_pmu_init_data(pdev, sllc_pmu);
	if (ret)
		return ret;

	ret = hisi_uncore_pmu_init_irq(sllc_pmu, pdev);
	if (ret)
		return ret;

	sllc_pmu->pmu_events.attr_groups = hisi_sllc_pmu_v2_attr_groups;
	sllc_pmu->ops = &hisi_uncore_sllc_ops;
	sllc_pmu->check_event = SLLC_NR_EVENTS;
	sllc_pmu->counter_bits = 64;
	sllc_pmu->num_counters = 8;
	sllc_pmu->dev = &pdev->dev;
	sllc_pmu->on_cpu = -1;

	return 0;
}

static int hisi_sllc_pmu_probe(struct platform_device *pdev)
{
	struct hisi_pmu *sllc_pmu;
	char *name;
	int ret;

	sllc_pmu = devm_kzalloc(&pdev->dev, sizeof(*sllc_pmu), GFP_KERNEL);
	if (!sllc_pmu)
		return -ENOMEM;

	ret = hisi_sllc_pmu_dev_probe(pdev, sllc_pmu);
	if (ret)
		return ret;

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hisi_sccl%u_sllc%u",
			      sllc_pmu->sccl_id, sllc_pmu->index_id);
	if (!name)
		return -ENOMEM;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_HISI_SLLC_ONLINE,
				       &sllc_pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug\n", ret);
		return ret;
	}

	hisi_pmu_init(sllc_pmu, THIS_MODULE);

	ret = perf_pmu_register(&sllc_pmu->pmu, name, -1);
	if (ret) {
		dev_err(sllc_pmu->dev, "PMU register failed, ret = %d\n", ret);
		cpuhp_state_remove_instance_nocalls(CPUHP_AP_PERF_ARM_HISI_SLLC_ONLINE,
						    &sllc_pmu->node);
		return ret;
	}

	platform_set_drvdata(pdev, sllc_pmu);

	return ret;
}

static int hisi_sllc_pmu_remove(struct platform_device *pdev)
{
	struct hisi_pmu *sllc_pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&sllc_pmu->pmu);
	cpuhp_state_remove_instance_nocalls(CPUHP_AP_PERF_ARM_HISI_SLLC_ONLINE,
					    &sllc_pmu->node);
	return 0;
}

static struct platform_driver hisi_sllc_pmu_driver = {
	.driver = {
		.name = "hisi_sllc_pmu",
		.acpi_match_table = hisi_sllc_pmu_acpi_match,
		.suppress_bind_attrs = true,
	},
	.probe = hisi_sllc_pmu_probe,
	.remove = hisi_sllc_pmu_remove,
};

static int __init hisi_sllc_pmu_module_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_HISI_SLLC_ONLINE,
				      "AP_PERF_ARM_HISI_SLLC_ONLINE",
				      hisi_uncore_pmu_online_cpu,
				      hisi_uncore_pmu_offline_cpu);
	if (ret) {
		pr_err("SLLC PMU: cpuhp state setup failed, ret = %d\n", ret);
		return ret;
	}

	ret = platform_driver_register(&hisi_sllc_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_SLLC_ONLINE);

	return ret;
}
module_init(hisi_sllc_pmu_module_init);

static void __exit hisi_sllc_pmu_module_exit(void)
{
	platform_driver_unregister(&hisi_sllc_pmu_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_SLLC_ONLINE);
}
module_exit(hisi_sllc_pmu_module_exit);

MODULE_DESCRIPTION("HiSilicon SLLC uncore PMU driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shaokun Zhang <zhangshaokun@hisilicon.com>");
MODULE_AUTHOR("Qi Liu <liuqi115@huawei.com>");
