// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon PA uncore Hardware event counters support
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

/* PA register definition */
#define PA_PERF_CTRL			0x1c00
#define PA_EVENT_CTRL			0x1c04
#define PA_TT_CTRL			0x1c08
#define PA_TGTID_CTRL			0x1c14
#define PA_SRCID_CTRL			0x1c18
#define PA_INT_MASK			0x1c70
#define PA_INT_STATUS			0x1c78
#define PA_INT_CLEAR			0x1c7c
#define PA_EVENT_TYPE0			0x1c80
#define PA_PMU_VERSION			0x1cf0
#define PA_EVENT_CNT0_L			0x1d00

#define PA_EVTYPE_MASK			0xff
#define PA_NR_COUNTERS			0x8
#define PA_PERF_CTRL_EN			BIT(0)
#define PA_TRACETAG_EN			BIT(4)
#define PA_TGTID_EN			BIT(11)
#define PA_SRCID_EN			BIT(11)
#define PA_TGTID_NONE			0
#define PA_SRCID_NONE			0
#define PA_TGTID_MSK_SHIFT		12
#define PA_SRCID_MSK_SHIFT		12

HISI_PMU_EVENT_ATTR_EXTRACTOR(tgtid_cmd, config1, 10, 0);
HISI_PMU_EVENT_ATTR_EXTRACTOR(tgtid_msk, config1, 21, 11);
HISI_PMU_EVENT_ATTR_EXTRACTOR(srcid_cmd, config1, 32, 22);
HISI_PMU_EVENT_ATTR_EXTRACTOR(srcid_msk, config1, 43, 33);
HISI_PMU_EVENT_ATTR_EXTRACTOR(tracetag_en, config1, 44, 44);

static void hisi_pa_pmu_enable_tracetag(struct perf_event *event)
{
	struct hisi_pmu *pa_pmu = to_hisi_pmu(event->pmu);
	u32 tt_en = hisi_get_tracetag_en(event);

	if (tt_en) {
		u32 val;

		val = readl(pa_pmu->base + PA_TT_CTRL);
		val |= PA_TRACETAG_EN;
		writel(val, pa_pmu->base + PA_TT_CTRL);
	}
}

static void hisi_pa_pmu_clear_tracetag(struct perf_event *event)
{
	struct hisi_pmu *pa_pmu = to_hisi_pmu(event->pmu);
	u32 tt_en = hisi_get_tracetag_en(event);

	if (tt_en) {
		u32 val;

		val = readl(pa_pmu->base + PA_TT_CTRL);
		val &= ~PA_TRACETAG_EN;
		writel(val, pa_pmu->base + PA_TT_CTRL);
	}
}

static void hisi_pa_pmu_config_tgtid(struct perf_event *event)
{
	struct hisi_pmu *pa_pmu = to_hisi_pmu(event->pmu);
	u32 cmd = hisi_get_tgtid_cmd(event);

	if (cmd) {
		u32 msk = hisi_get_tgtid_msk(event);
		u32 val = cmd | PA_TGTID_EN | (msk << PA_TGTID_MSK_SHIFT);

		writel(val, pa_pmu->base + PA_TGTID_CTRL);
	}
}

static void hisi_pa_pmu_clear_tgtid(struct perf_event *event)
{
	struct hisi_pmu *pa_pmu = to_hisi_pmu(event->pmu);
	u32 cmd = hisi_get_tgtid_cmd(event);

	if (cmd)
		writel(PA_TGTID_NONE, pa_pmu->base + PA_TGTID_CTRL);
}

static void hisi_pa_pmu_config_srcid(struct perf_event *event)
{
	struct hisi_pmu *pa_pmu = to_hisi_pmu(event->pmu);
	u32 cmd = hisi_get_srcid_cmd(event);

	if (cmd) {
		u32 msk = hisi_get_srcid_msk(event);
		u32 val = cmd | PA_SRCID_EN | (msk << PA_SRCID_MSK_SHIFT);

		writel(val, pa_pmu->base + PA_SRCID_CTRL);
	}
}

static void hisi_pa_pmu_clear_srcid(struct perf_event *event)
{
	struct hisi_pmu *pa_pmu = to_hisi_pmu(event->pmu);
	u32 cmd = hisi_get_srcid_cmd(event);

	if (cmd)
		writel(PA_SRCID_NONE, pa_pmu->base + PA_SRCID_CTRL);
}

static void hisi_pa_pmu_enable_filter(struct perf_event *event)
{
	if (event->attr.config1 != 0x0) {
		hisi_pa_pmu_enable_tracetag(event);
		hisi_pa_pmu_config_srcid(event);
		hisi_pa_pmu_config_tgtid(event);
	}
}

static void hisi_pa_pmu_disable_filter(struct perf_event *event)
{
	if (event->attr.config1 != 0x0) {
		hisi_pa_pmu_clear_tgtid(event);
		hisi_pa_pmu_clear_srcid(event);
		hisi_pa_pmu_clear_tracetag(event);
	}
}

static u32 hisi_pa_pmu_get_counter_offset(int idx)
{
	return (PA_EVENT_CNT0_L + idx * 8);
}

static u64 hisi_pa_pmu_read_counter(struct hisi_pmu *pa_pmu,
				    struct hw_perf_event *hwc)
{
	return readq(pa_pmu->base + hisi_pa_pmu_get_counter_offset(hwc->idx));
}

static void hisi_pa_pmu_write_counter(struct hisi_pmu *pa_pmu,
				      struct hw_perf_event *hwc, u64 val)
{
	writeq(val, pa_pmu->base + hisi_pa_pmu_get_counter_offset(hwc->idx));
}

static void hisi_pa_pmu_write_evtype(struct hisi_pmu *pa_pmu, int idx,
				     u32 type)
{
	u32 reg, reg_idx, shift, val;

	/*
	 * Select the appropriate event select register(PA_EVENT_TYPE0/1).
	 * There are 2 event select registers for the 8 hardware counters.
	 * Event code is 8-bits and for the former 4 hardware counters,
	 * PA_EVENT_TYPE0 is chosen. For the latter 4 hardware counters,
	 * PA_EVENT_TYPE1 is chosen.
	 */
	reg = PA_EVENT_TYPE0 + (idx / 4) * 4;
	reg_idx = idx % 4;
	shift = 8 * reg_idx;

	/* Write event code to pa_EVENT_TYPEx Register */
	val = readl(pa_pmu->base + reg);
	val &= ~(PA_EVTYPE_MASK << shift);
	val |= (type << shift);
	writel(val, pa_pmu->base + reg);
}

static void hisi_pa_pmu_start_counters(struct hisi_pmu *pa_pmu)
{
	u32 val;

	val = readl(pa_pmu->base + PA_PERF_CTRL);
	val |= PA_PERF_CTRL_EN;
	writel(val, pa_pmu->base + PA_PERF_CTRL);
}

static void hisi_pa_pmu_stop_counters(struct hisi_pmu *pa_pmu)
{
	u32 val;

	val = readl(pa_pmu->base + PA_PERF_CTRL);
	val &= ~(PA_PERF_CTRL_EN);
	writel(val, pa_pmu->base + PA_PERF_CTRL);
}

static void hisi_pa_pmu_enable_counter(struct hisi_pmu *pa_pmu,
				       struct hw_perf_event *hwc)
{
	u32 val;

	/* Enable counter index in PA_EVENT_CTRL register */
	val = readl(pa_pmu->base + PA_EVENT_CTRL);
	val |= 1 << hwc->idx;
	writel(val, pa_pmu->base + PA_EVENT_CTRL);
}

static void hisi_pa_pmu_disable_counter(struct hisi_pmu *pa_pmu,
					struct hw_perf_event *hwc)
{
	u32 val;

	/* Clear counter index in PA_EVENT_CTRL register */
	val = readl(pa_pmu->base + PA_EVENT_CTRL);
	val &= ~(1 << hwc->idx);
	writel(val, pa_pmu->base + PA_EVENT_CTRL);
}

static void hisi_pa_pmu_enable_counter_int(struct hisi_pmu *pa_pmu,
					   struct hw_perf_event *hwc)
{
	u32 val;

	/* Write 0 to enable interrupt */
	val = readl(pa_pmu->base + PA_INT_MASK);
	val &= ~(1 << hwc->idx);
	writel(val, pa_pmu->base + PA_INT_MASK);
}

static void hisi_pa_pmu_disable_counter_int(struct hisi_pmu *pa_pmu,
					    struct hw_perf_event *hwc)
{
	u32 val;

	/* Write 1 to mask interrupt */
	val = readl(pa_pmu->base + PA_INT_MASK);
	val |= 1 << hwc->idx;
	writel(val, pa_pmu->base + PA_INT_MASK);
}

static u32 hisi_pa_pmu_get_int_status(struct hisi_pmu *pa_pmu)
{
	return readl(pa_pmu->base + PA_INT_STATUS);
}

static void hisi_pa_pmu_clear_int_status(struct hisi_pmu *pa_pmu, int idx)
{
	writel(1 << idx, pa_pmu->base + PA_INT_CLEAR);
}

static const struct acpi_device_id hisi_pa_pmu_acpi_match[] = {
	{ "HISI0273", },
	{}
};
MODULE_DEVICE_TABLE(acpi, hisi_pa_pmu_acpi_match);

static int hisi_pa_pmu_init_data(struct platform_device *pdev,
				   struct hisi_pmu *pa_pmu)
{
	/*
	 * As PA PMU is in a SICL, use the SICL_ID and the index ID
	 * to identify the PA PMU.
	 */
	if (device_property_read_u32(&pdev->dev, "hisilicon,scl-id",
				     &pa_pmu->sicl_id)) {
		dev_err(&pdev->dev, "Cannot read sicl-id!\n");
		return -EINVAL;
	}

	if (device_property_read_u32(&pdev->dev, "hisilicon,idx-id",
				     &pa_pmu->index_id)) {
		dev_err(&pdev->dev, "Cannot read idx-id!\n");
		return -EINVAL;
	}

	pa_pmu->ccl_id = -1;
	pa_pmu->sccl_id = -1;

	pa_pmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pa_pmu->base)) {
		dev_err(&pdev->dev, "ioremap failed for pa_pmu resource.\n");
		return PTR_ERR(pa_pmu->base);
	}

	pa_pmu->identifier = readl(pa_pmu->base + PA_PMU_VERSION);

	return 0;
}

static struct attribute *hisi_pa_pmu_v2_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	HISI_PMU_FORMAT_ATTR(tgtid_cmd, "config1:0-10"),
	HISI_PMU_FORMAT_ATTR(tgtid_msk, "config1:11-21"),
	HISI_PMU_FORMAT_ATTR(srcid_cmd, "config1:22-32"),
	HISI_PMU_FORMAT_ATTR(srcid_msk, "config1:33-43"),
	HISI_PMU_FORMAT_ATTR(tracetag_en, "config1:44"),
	NULL,
};

static const struct attribute_group hisi_pa_pmu_v2_format_group = {
	.name = "format",
	.attrs = hisi_pa_pmu_v2_format_attr,
};

static struct attribute *hisi_pa_pmu_v2_events_attr[] = {
	HISI_PMU_EVENT_ATTR(rx_req,		0x40),
	HISI_PMU_EVENT_ATTR(tx_req,             0x5c),
	HISI_PMU_EVENT_ATTR(cycle,		0x78),
	NULL
};

static const struct attribute_group hisi_pa_pmu_v2_events_group = {
	.name = "events",
	.attrs = hisi_pa_pmu_v2_events_attr,
};

static DEVICE_ATTR(cpumask, 0444, hisi_cpumask_sysfs_show, NULL);

static struct attribute *hisi_pa_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static const struct attribute_group hisi_pa_pmu_cpumask_attr_group = {
	.attrs = hisi_pa_pmu_cpumask_attrs,
};

static struct device_attribute hisi_pa_pmu_identifier_attr =
	__ATTR(identifier, 0444, hisi_uncore_pmu_identifier_attr_show, NULL);

static struct attribute *hisi_pa_pmu_identifier_attrs[] = {
	&hisi_pa_pmu_identifier_attr.attr,
	NULL
};

static const struct attribute_group hisi_pa_pmu_identifier_group = {
	.attrs = hisi_pa_pmu_identifier_attrs,
};

static const struct attribute_group *hisi_pa_pmu_v2_attr_groups[] = {
	&hisi_pa_pmu_v2_format_group,
	&hisi_pa_pmu_v2_events_group,
	&hisi_pa_pmu_cpumask_attr_group,
	&hisi_pa_pmu_identifier_group,
	NULL
};

static const struct hisi_uncore_ops hisi_uncore_pa_ops = {
	.write_evtype		= hisi_pa_pmu_write_evtype,
	.get_event_idx		= hisi_uncore_pmu_get_event_idx,
	.start_counters		= hisi_pa_pmu_start_counters,
	.stop_counters		= hisi_pa_pmu_stop_counters,
	.enable_counter		= hisi_pa_pmu_enable_counter,
	.disable_counter	= hisi_pa_pmu_disable_counter,
	.enable_counter_int	= hisi_pa_pmu_enable_counter_int,
	.disable_counter_int	= hisi_pa_pmu_disable_counter_int,
	.write_counter		= hisi_pa_pmu_write_counter,
	.read_counter		= hisi_pa_pmu_read_counter,
	.get_int_status		= hisi_pa_pmu_get_int_status,
	.clear_int_status	= hisi_pa_pmu_clear_int_status,
	.enable_filter		= hisi_pa_pmu_enable_filter,
	.disable_filter		= hisi_pa_pmu_disable_filter,
};

static int hisi_pa_pmu_dev_probe(struct platform_device *pdev,
				 struct hisi_pmu *pa_pmu)
{
	int ret;

	ret = hisi_pa_pmu_init_data(pdev, pa_pmu);
	if (ret)
		return ret;

	ret = hisi_uncore_pmu_init_irq(pa_pmu, pdev);
	if (ret)
		return ret;

	pa_pmu->pmu_events.attr_groups = hisi_pa_pmu_v2_attr_groups;
	pa_pmu->num_counters = PA_NR_COUNTERS;
	pa_pmu->ops = &hisi_uncore_pa_ops;
	pa_pmu->check_event = 0xB0;
	pa_pmu->counter_bits = 64;
	pa_pmu->dev = &pdev->dev;
	pa_pmu->on_cpu = -1;

	return 0;
}

static int hisi_pa_pmu_probe(struct platform_device *pdev)
{
	struct hisi_pmu *pa_pmu;
	char *name;
	int ret;

	pa_pmu = devm_kzalloc(&pdev->dev, sizeof(*pa_pmu), GFP_KERNEL);
	if (!pa_pmu)
		return -ENOMEM;

	ret = hisi_pa_pmu_dev_probe(pdev, pa_pmu);
	if (ret)
		return ret;

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hisi_sicl%u_pa%u",
			      pa_pmu->sicl_id, pa_pmu->index_id);
	if (!name)
		return -ENOMEM;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_HISI_PA_ONLINE,
				       &pa_pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug\n", ret);
		return ret;
	}

	hisi_pmu_init(&pa_pmu->pmu, name, pa_pmu->pmu_events.attr_groups, THIS_MODULE);
	ret = perf_pmu_register(&pa_pmu->pmu, name, -1);
	if (ret) {
		dev_err(pa_pmu->dev, "PMU register failed, ret = %d\n", ret);
		cpuhp_state_remove_instance(CPUHP_AP_PERF_ARM_HISI_PA_ONLINE,
					    &pa_pmu->node);
		return ret;
	}

	platform_set_drvdata(pdev, pa_pmu);
	return ret;
}

static int hisi_pa_pmu_remove(struct platform_device *pdev)
{
	struct hisi_pmu *pa_pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&pa_pmu->pmu);
	cpuhp_state_remove_instance_nocalls(CPUHP_AP_PERF_ARM_HISI_PA_ONLINE,
					    &pa_pmu->node);
	return 0;
}

static struct platform_driver hisi_pa_pmu_driver = {
	.driver = {
		.name = "hisi_pa_pmu",
		.acpi_match_table = hisi_pa_pmu_acpi_match,
		.suppress_bind_attrs = true,
	},
	.probe = hisi_pa_pmu_probe,
	.remove = hisi_pa_pmu_remove,
};

static int __init hisi_pa_pmu_module_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_HISI_PA_ONLINE,
				      "AP_PERF_ARM_HISI_PA_ONLINE",
				      hisi_uncore_pmu_online_cpu,
				      hisi_uncore_pmu_offline_cpu);
	if (ret) {
		pr_err("PA PMU: cpuhp state setup failed, ret = %d\n", ret);
		return ret;
	}

	ret = platform_driver_register(&hisi_pa_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_PA_ONLINE);

	return ret;
}
module_init(hisi_pa_pmu_module_init);

static void __exit hisi_pa_pmu_module_exit(void)
{
	platform_driver_unregister(&hisi_pa_pmu_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_PA_ONLINE);
}
module_exit(hisi_pa_pmu_module_exit);

MODULE_DESCRIPTION("HiSilicon Protocol Adapter uncore PMU driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shaokun Zhang <zhangshaokun@hisilicon.com>");
MODULE_AUTHOR("Qi Liu <liuqi115@huawei.com>");
