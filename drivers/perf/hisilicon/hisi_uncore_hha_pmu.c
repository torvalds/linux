// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SoC HHA uncore Hardware event counters support
 *
 * Copyright (C) 2017 HiSilicon Limited
 * Author: Shaokun Zhang <zhangshaokun@hisilicon.com>
 *         Anurup M <anurup.m@huawei.com>
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

/* HHA register definition */
#define HHA_INT_MASK		0x0804
#define HHA_INT_STATUS		0x0808
#define HHA_INT_CLEAR		0x080C
#define HHA_VERSION		0x1cf0
#define HHA_PERF_CTRL		0x1E00
#define HHA_EVENT_CTRL		0x1E04
#define HHA_SRCID_CTRL		0x1E08
#define HHA_DATSRC_CTRL		0x1BF0
#define HHA_EVENT_TYPE0		0x1E80
/*
 * If the HW version only supports a 48-bit counter, then
 * bits [63:48] are reserved, which are Read-As-Zero and
 * Writes-Ignored.
 */
#define HHA_CNT0_LOWER		0x1F00

/* HHA PMU v1 has 16 counters and v2 only has 8 counters */
#define HHA_V1_NR_COUNTERS	0x10
#define HHA_V2_NR_COUNTERS	0x8

#define HHA_PERF_CTRL_EN	0x1
#define HHA_TRACETAG_EN		BIT(31)
#define HHA_SRCID_EN		BIT(2)
#define HHA_SRCID_CMD_SHIFT	6
#define HHA_SRCID_MSK_SHIFT	20
#define HHA_SRCID_CMD		GENMASK(16, 6)
#define HHA_SRCID_MSK		GENMASK(30, 20)
#define HHA_DATSRC_SKT_EN	BIT(23)
#define HHA_EVTYPE_NONE		0xff
#define HHA_V1_NR_EVENT		0x65
#define HHA_V2_NR_EVENT		0xCE

HISI_PMU_EVENT_ATTR_EXTRACTOR(srcid_cmd, config1, 10, 0);
HISI_PMU_EVENT_ATTR_EXTRACTOR(srcid_msk, config1, 21, 11);
HISI_PMU_EVENT_ATTR_EXTRACTOR(tracetag_en, config1, 22, 22);
HISI_PMU_EVENT_ATTR_EXTRACTOR(datasrc_skt, config1, 23, 23);

static void hisi_hha_pmu_enable_tracetag(struct perf_event *event)
{
	struct hisi_pmu *hha_pmu = to_hisi_pmu(event->pmu);
	u32 tt_en = hisi_get_tracetag_en(event);

	if (tt_en) {
		u32 val;

		val = readl(hha_pmu->base + HHA_SRCID_CTRL);
		val |= HHA_TRACETAG_EN;
		writel(val, hha_pmu->base + HHA_SRCID_CTRL);
	}
}

static void hisi_hha_pmu_clear_tracetag(struct perf_event *event)
{
	struct hisi_pmu *hha_pmu = to_hisi_pmu(event->pmu);
	u32 val;

	val = readl(hha_pmu->base + HHA_SRCID_CTRL);
	val &= ~HHA_TRACETAG_EN;
	writel(val, hha_pmu->base + HHA_SRCID_CTRL);
}

static void hisi_hha_pmu_config_ds(struct perf_event *event)
{
	struct hisi_pmu *hha_pmu = to_hisi_pmu(event->pmu);
	u32 ds_skt = hisi_get_datasrc_skt(event);

	if (ds_skt) {
		u32 val;

		val = readl(hha_pmu->base + HHA_DATSRC_CTRL);
		val |= HHA_DATSRC_SKT_EN;
		writel(val, hha_pmu->base + HHA_DATSRC_CTRL);
	}
}

static void hisi_hha_pmu_clear_ds(struct perf_event *event)
{
	struct hisi_pmu *hha_pmu = to_hisi_pmu(event->pmu);
	u32 ds_skt = hisi_get_datasrc_skt(event);

	if (ds_skt) {
		u32 val;

		val = readl(hha_pmu->base + HHA_DATSRC_CTRL);
		val &= ~HHA_DATSRC_SKT_EN;
		writel(val, hha_pmu->base + HHA_DATSRC_CTRL);
	}
}

static void hisi_hha_pmu_config_srcid(struct perf_event *event)
{
	struct hisi_pmu *hha_pmu = to_hisi_pmu(event->pmu);
	u32 cmd = hisi_get_srcid_cmd(event);

	if (cmd) {
		u32 val, msk;

		msk = hisi_get_srcid_msk(event);
		val = readl(hha_pmu->base + HHA_SRCID_CTRL);
		val |= HHA_SRCID_EN | (cmd << HHA_SRCID_CMD_SHIFT) |
			(msk << HHA_SRCID_MSK_SHIFT);
		writel(val, hha_pmu->base + HHA_SRCID_CTRL);
	}
}

static void hisi_hha_pmu_disable_srcid(struct perf_event *event)
{
	struct hisi_pmu *hha_pmu = to_hisi_pmu(event->pmu);
	u32 cmd = hisi_get_srcid_cmd(event);

	if (cmd) {
		u32 val;

		val = readl(hha_pmu->base + HHA_SRCID_CTRL);
		val &= ~(HHA_SRCID_EN | HHA_SRCID_MSK | HHA_SRCID_CMD);
		writel(val, hha_pmu->base + HHA_SRCID_CTRL);
	}
}

static void hisi_hha_pmu_enable_filter(struct perf_event *event)
{
	if (event->attr.config1 != 0x0) {
		hisi_hha_pmu_enable_tracetag(event);
		hisi_hha_pmu_config_ds(event);
		hisi_hha_pmu_config_srcid(event);
	}
}

static void hisi_hha_pmu_disable_filter(struct perf_event *event)
{
	if (event->attr.config1 != 0x0) {
		hisi_hha_pmu_disable_srcid(event);
		hisi_hha_pmu_clear_ds(event);
		hisi_hha_pmu_clear_tracetag(event);
	}
}

/*
 * Select the counter register offset using the counter index
 * each counter is 48-bits.
 */
static u32 hisi_hha_pmu_get_counter_offset(int cntr_idx)
{
	return (HHA_CNT0_LOWER + (cntr_idx * 8));
}

static u64 hisi_hha_pmu_read_counter(struct hisi_pmu *hha_pmu,
				     struct hw_perf_event *hwc)
{
	/* Read 64 bits and like L3C, top 16 bits are RAZ */
	return readq(hha_pmu->base + hisi_hha_pmu_get_counter_offset(hwc->idx));
}

static void hisi_hha_pmu_write_counter(struct hisi_pmu *hha_pmu,
				       struct hw_perf_event *hwc, u64 val)
{
	/* Write 64 bits and like L3C, top 16 bits are WI */
	writeq(val, hha_pmu->base + hisi_hha_pmu_get_counter_offset(hwc->idx));
}

static void hisi_hha_pmu_write_evtype(struct hisi_pmu *hha_pmu, int idx,
				      u32 type)
{
	u32 reg, reg_idx, shift, val;

	/*
	 * Select the appropriate event select register(HHA_EVENT_TYPEx).
	 * There are 4 event select registers for the 16 hardware counters.
	 * Event code is 8-bits and for the first 4 hardware counters,
	 * HHA_EVENT_TYPE0 is chosen. For the next 4 hardware counters,
	 * HHA_EVENT_TYPE1 is chosen and so on.
	 */
	reg = HHA_EVENT_TYPE0 + 4 * (idx / 4);
	reg_idx = idx % 4;
	shift = 8 * reg_idx;

	/* Write event code to HHA_EVENT_TYPEx register */
	val = readl(hha_pmu->base + reg);
	val &= ~(HHA_EVTYPE_NONE << shift);
	val |= (type << shift);
	writel(val, hha_pmu->base + reg);
}

static void hisi_hha_pmu_start_counters(struct hisi_pmu *hha_pmu)
{
	u32 val;

	/*
	 * Set perf_enable bit in HHA_PERF_CTRL to start event
	 * counting for all enabled counters.
	 */
	val = readl(hha_pmu->base + HHA_PERF_CTRL);
	val |= HHA_PERF_CTRL_EN;
	writel(val, hha_pmu->base + HHA_PERF_CTRL);
}

static void hisi_hha_pmu_stop_counters(struct hisi_pmu *hha_pmu)
{
	u32 val;

	/*
	 * Clear perf_enable bit in HHA_PERF_CTRL to stop event
	 * counting for all enabled counters.
	 */
	val = readl(hha_pmu->base + HHA_PERF_CTRL);
	val &= ~(HHA_PERF_CTRL_EN);
	writel(val, hha_pmu->base + HHA_PERF_CTRL);
}

static void hisi_hha_pmu_enable_counter(struct hisi_pmu *hha_pmu,
					struct hw_perf_event *hwc)
{
	u32 val;

	/* Enable counter index in HHA_EVENT_CTRL register */
	val = readl(hha_pmu->base + HHA_EVENT_CTRL);
	val |= (1 << hwc->idx);
	writel(val, hha_pmu->base + HHA_EVENT_CTRL);
}

static void hisi_hha_pmu_disable_counter(struct hisi_pmu *hha_pmu,
					 struct hw_perf_event *hwc)
{
	u32 val;

	/* Clear counter index in HHA_EVENT_CTRL register */
	val = readl(hha_pmu->base + HHA_EVENT_CTRL);
	val &= ~(1 << hwc->idx);
	writel(val, hha_pmu->base + HHA_EVENT_CTRL);
}

static void hisi_hha_pmu_enable_counter_int(struct hisi_pmu *hha_pmu,
					    struct hw_perf_event *hwc)
{
	u32 val;

	/* Write 0 to enable interrupt */
	val = readl(hha_pmu->base + HHA_INT_MASK);
	val &= ~(1 << hwc->idx);
	writel(val, hha_pmu->base + HHA_INT_MASK);
}

static void hisi_hha_pmu_disable_counter_int(struct hisi_pmu *hha_pmu,
					     struct hw_perf_event *hwc)
{
	u32 val;

	/* Write 1 to mask interrupt */
	val = readl(hha_pmu->base + HHA_INT_MASK);
	val |= (1 << hwc->idx);
	writel(val, hha_pmu->base + HHA_INT_MASK);
}

static u32 hisi_hha_pmu_get_int_status(struct hisi_pmu *hha_pmu)
{
	return readl(hha_pmu->base + HHA_INT_STATUS);
}

static void hisi_hha_pmu_clear_int_status(struct hisi_pmu *hha_pmu, int idx)
{
	writel(1 << idx, hha_pmu->base + HHA_INT_CLEAR);
}

static const struct acpi_device_id hisi_hha_pmu_acpi_match[] = {
	{ "HISI0243", },
	{ "HISI0244", },
	{}
};
MODULE_DEVICE_TABLE(acpi, hisi_hha_pmu_acpi_match);

static int hisi_hha_pmu_init_data(struct platform_device *pdev,
				  struct hisi_pmu *hha_pmu)
{
	unsigned long long id;
	acpi_status status;

	/*
	 * Use SCCL_ID and UID to identify the HHA PMU, while
	 * SCCL_ID is in MPIDR[aff2].
	 */
	if (device_property_read_u32(&pdev->dev, "hisilicon,scl-id",
				     &hha_pmu->sccl_id)) {
		dev_err(&pdev->dev, "Can not read hha sccl-id!\n");
		return -EINVAL;
	}

	/*
	 * Early versions of BIOS support _UID by mistake, so we support
	 * both "hisilicon, idx-id" as preference, if available.
	 */
	if (device_property_read_u32(&pdev->dev, "hisilicon,idx-id",
				     &hha_pmu->index_id)) {
		status = acpi_evaluate_integer(ACPI_HANDLE(&pdev->dev),
					       "_UID", NULL, &id);
		if (ACPI_FAILURE(status)) {
			dev_err(&pdev->dev, "Cannot read idx-id!\n");
			return -EINVAL;
		}

		hha_pmu->index_id = id;
	}
	/* HHA PMUs only share the same SCCL */
	hha_pmu->ccl_id = -1;

	hha_pmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hha_pmu->base)) {
		dev_err(&pdev->dev, "ioremap failed for hha_pmu resource\n");
		return PTR_ERR(hha_pmu->base);
	}

	hha_pmu->identifier = readl(hha_pmu->base + HHA_VERSION);

	return 0;
}

static struct attribute *hisi_hha_pmu_v1_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	NULL,
};

static const struct attribute_group hisi_hha_pmu_v1_format_group = {
	.name = "format",
	.attrs = hisi_hha_pmu_v1_format_attr,
};

static struct attribute *hisi_hha_pmu_v2_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	HISI_PMU_FORMAT_ATTR(srcid_cmd, "config1:0-10"),
	HISI_PMU_FORMAT_ATTR(srcid_msk, "config1:11-21"),
	HISI_PMU_FORMAT_ATTR(tracetag_en, "config1:22"),
	HISI_PMU_FORMAT_ATTR(datasrc_skt, "config1:23"),
	NULL
};

static const struct attribute_group hisi_hha_pmu_v2_format_group = {
	.name = "format",
	.attrs = hisi_hha_pmu_v2_format_attr,
};

static struct attribute *hisi_hha_pmu_v1_events_attr[] = {
	HISI_PMU_EVENT_ATTR(rx_ops_num,		0x00),
	HISI_PMU_EVENT_ATTR(rx_outer,		0x01),
	HISI_PMU_EVENT_ATTR(rx_sccl,		0x02),
	HISI_PMU_EVENT_ATTR(rx_ccix,		0x03),
	HISI_PMU_EVENT_ATTR(rx_wbi,		0x04),
	HISI_PMU_EVENT_ATTR(rx_wbip,		0x05),
	HISI_PMU_EVENT_ATTR(rx_wtistash,	0x11),
	HISI_PMU_EVENT_ATTR(rd_ddr_64b,		0x1c),
	HISI_PMU_EVENT_ATTR(wr_ddr_64b,		0x1d),
	HISI_PMU_EVENT_ATTR(rd_ddr_128b,	0x1e),
	HISI_PMU_EVENT_ATTR(wr_ddr_128b,	0x1f),
	HISI_PMU_EVENT_ATTR(spill_num,		0x20),
	HISI_PMU_EVENT_ATTR(spill_success,	0x21),
	HISI_PMU_EVENT_ATTR(bi_num,		0x23),
	HISI_PMU_EVENT_ATTR(mediated_num,	0x32),
	HISI_PMU_EVENT_ATTR(tx_snp_num,		0x33),
	HISI_PMU_EVENT_ATTR(tx_snp_outer,	0x34),
	HISI_PMU_EVENT_ATTR(tx_snp_ccix,	0x35),
	HISI_PMU_EVENT_ATTR(rx_snprspdata,	0x38),
	HISI_PMU_EVENT_ATTR(rx_snprsp_outer,	0x3c),
	HISI_PMU_EVENT_ATTR(sdir-lookup,	0x40),
	HISI_PMU_EVENT_ATTR(edir-lookup,	0x41),
	HISI_PMU_EVENT_ATTR(sdir-hit,		0x42),
	HISI_PMU_EVENT_ATTR(edir-hit,		0x43),
	HISI_PMU_EVENT_ATTR(sdir-home-migrate,	0x4c),
	HISI_PMU_EVENT_ATTR(edir-home-migrate,  0x4d),
	NULL,
};

static const struct attribute_group hisi_hha_pmu_v1_events_group = {
	.name = "events",
	.attrs = hisi_hha_pmu_v1_events_attr,
};

static struct attribute *hisi_hha_pmu_v2_events_attr[] = {
	HISI_PMU_EVENT_ATTR(rx_ops_num,		0x00),
	HISI_PMU_EVENT_ATTR(rx_outer,		0x01),
	HISI_PMU_EVENT_ATTR(rx_sccl,		0x02),
	HISI_PMU_EVENT_ATTR(hha_retry,		0x2e),
	HISI_PMU_EVENT_ATTR(cycles,		0x55),
	NULL
};

static const struct attribute_group hisi_hha_pmu_v2_events_group = {
	.name = "events",
	.attrs = hisi_hha_pmu_v2_events_attr,
};

static DEVICE_ATTR(cpumask, 0444, hisi_cpumask_sysfs_show, NULL);

static struct attribute *hisi_hha_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group hisi_hha_pmu_cpumask_attr_group = {
	.attrs = hisi_hha_pmu_cpumask_attrs,
};

static struct device_attribute hisi_hha_pmu_identifier_attr =
	__ATTR(identifier, 0444, hisi_uncore_pmu_identifier_attr_show, NULL);

static struct attribute *hisi_hha_pmu_identifier_attrs[] = {
	&hisi_hha_pmu_identifier_attr.attr,
	NULL
};

static const struct attribute_group hisi_hha_pmu_identifier_group = {
	.attrs = hisi_hha_pmu_identifier_attrs,
};

static const struct attribute_group *hisi_hha_pmu_v1_attr_groups[] = {
	&hisi_hha_pmu_v1_format_group,
	&hisi_hha_pmu_v1_events_group,
	&hisi_hha_pmu_cpumask_attr_group,
	&hisi_hha_pmu_identifier_group,
	NULL,
};

static const struct attribute_group *hisi_hha_pmu_v2_attr_groups[] = {
	&hisi_hha_pmu_v2_format_group,
	&hisi_hha_pmu_v2_events_group,
	&hisi_hha_pmu_cpumask_attr_group,
	&hisi_hha_pmu_identifier_group,
	NULL
};

static const struct hisi_uncore_ops hisi_uncore_hha_ops = {
	.write_evtype		= hisi_hha_pmu_write_evtype,
	.get_event_idx		= hisi_uncore_pmu_get_event_idx,
	.start_counters		= hisi_hha_pmu_start_counters,
	.stop_counters		= hisi_hha_pmu_stop_counters,
	.enable_counter		= hisi_hha_pmu_enable_counter,
	.disable_counter	= hisi_hha_pmu_disable_counter,
	.enable_counter_int	= hisi_hha_pmu_enable_counter_int,
	.disable_counter_int	= hisi_hha_pmu_disable_counter_int,
	.write_counter		= hisi_hha_pmu_write_counter,
	.read_counter		= hisi_hha_pmu_read_counter,
	.get_int_status		= hisi_hha_pmu_get_int_status,
	.clear_int_status	= hisi_hha_pmu_clear_int_status,
	.enable_filter		= hisi_hha_pmu_enable_filter,
	.disable_filter		= hisi_hha_pmu_disable_filter,
};

static int hisi_hha_pmu_dev_probe(struct platform_device *pdev,
				  struct hisi_pmu *hha_pmu)
{
	int ret;

	ret = hisi_hha_pmu_init_data(pdev, hha_pmu);
	if (ret)
		return ret;

	ret = hisi_uncore_pmu_init_irq(hha_pmu, pdev);
	if (ret)
		return ret;

	if (hha_pmu->identifier >= HISI_PMU_V2) {
		hha_pmu->counter_bits = 64;
		hha_pmu->check_event = HHA_V2_NR_EVENT;
		hha_pmu->pmu_events.attr_groups = hisi_hha_pmu_v2_attr_groups;
		hha_pmu->num_counters = HHA_V2_NR_COUNTERS;
	} else {
		hha_pmu->counter_bits = 48;
		hha_pmu->check_event = HHA_V1_NR_EVENT;
		hha_pmu->pmu_events.attr_groups = hisi_hha_pmu_v1_attr_groups;
		hha_pmu->num_counters = HHA_V1_NR_COUNTERS;
	}
	hha_pmu->ops = &hisi_uncore_hha_ops;
	hha_pmu->dev = &pdev->dev;
	hha_pmu->on_cpu = -1;

	return 0;
}

static int hisi_hha_pmu_probe(struct platform_device *pdev)
{
	struct hisi_pmu *hha_pmu;
	char *name;
	int ret;

	hha_pmu = devm_kzalloc(&pdev->dev, sizeof(*hha_pmu), GFP_KERNEL);
	if (!hha_pmu)
		return -ENOMEM;

	platform_set_drvdata(pdev, hha_pmu);

	ret = hisi_hha_pmu_dev_probe(pdev, hha_pmu);
	if (ret)
		return ret;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_HISI_HHA_ONLINE,
				       &hha_pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug\n", ret);
		return ret;
	}

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hisi_sccl%u_hha%u",
			      hha_pmu->sccl_id, hha_pmu->index_id);
	hha_pmu->pmu = (struct pmu) {
		.name		= name,
		.module		= THIS_MODULE,
		.task_ctx_nr	= perf_invalid_context,
		.event_init	= hisi_uncore_pmu_event_init,
		.pmu_enable	= hisi_uncore_pmu_enable,
		.pmu_disable	= hisi_uncore_pmu_disable,
		.add		= hisi_uncore_pmu_add,
		.del		= hisi_uncore_pmu_del,
		.start		= hisi_uncore_pmu_start,
		.stop		= hisi_uncore_pmu_stop,
		.read		= hisi_uncore_pmu_read,
		.attr_groups	= hha_pmu->pmu_events.attr_groups,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
	};

	ret = perf_pmu_register(&hha_pmu->pmu, name, -1);
	if (ret) {
		dev_err(hha_pmu->dev, "HHA PMU register failed!\n");
		cpuhp_state_remove_instance_nocalls(
			CPUHP_AP_PERF_ARM_HISI_HHA_ONLINE, &hha_pmu->node);
	}

	return ret;
}

static int hisi_hha_pmu_remove(struct platform_device *pdev)
{
	struct hisi_pmu *hha_pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&hha_pmu->pmu);
	cpuhp_state_remove_instance_nocalls(CPUHP_AP_PERF_ARM_HISI_HHA_ONLINE,
					    &hha_pmu->node);
	return 0;
}

static struct platform_driver hisi_hha_pmu_driver = {
	.driver = {
		.name = "hisi_hha_pmu",
		.acpi_match_table = ACPI_PTR(hisi_hha_pmu_acpi_match),
		.suppress_bind_attrs = true,
	},
	.probe = hisi_hha_pmu_probe,
	.remove = hisi_hha_pmu_remove,
};

static int __init hisi_hha_pmu_module_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_HISI_HHA_ONLINE,
				      "AP_PERF_ARM_HISI_HHA_ONLINE",
				      hisi_uncore_pmu_online_cpu,
				      hisi_uncore_pmu_offline_cpu);
	if (ret) {
		pr_err("HHA PMU: Error setup hotplug, ret = %d;\n", ret);
		return ret;
	}

	ret = platform_driver_register(&hisi_hha_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_HHA_ONLINE);

	return ret;
}
module_init(hisi_hha_pmu_module_init);

static void __exit hisi_hha_pmu_module_exit(void)
{
	platform_driver_unregister(&hisi_hha_pmu_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_HHA_ONLINE);
}
module_exit(hisi_hha_pmu_module_exit);

MODULE_DESCRIPTION("HiSilicon SoC HHA uncore PMU driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shaokun Zhang <zhangshaokun@hisilicon.com>");
MODULE_AUTHOR("Anurup M <anurup.m@huawei.com>");
