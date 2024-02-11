// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SoC UC (unified cache) uncore Hardware event counters support
 *
 * Copyright (C) 2023 HiSilicon Limited
 *
 * This code is based on the uncore PMUs like hisi_uncore_l3c_pmu.
 */
#include <linux/cpuhotplug.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>

#include "hisi_uncore_pmu.h"

/* Dynamic CPU hotplug state used by UC PMU */
static enum cpuhp_state hisi_uc_pmu_online;

/* UC register definition */
#define HISI_UC_INT_MASK_REG		0x0800
#define HISI_UC_INT_STS_REG		0x0808
#define HISI_UC_INT_CLEAR_REG		0x080c
#define HISI_UC_TRACETAG_CTRL_REG	0x1b2c
#define HISI_UC_TRACETAG_REQ_MSK	GENMASK(9, 7)
#define HISI_UC_TRACETAG_MARK_EN	BIT(0)
#define HISI_UC_TRACETAG_REQ_EN		(HISI_UC_TRACETAG_MARK_EN | BIT(2))
#define HISI_UC_TRACETAG_SRCID_EN	BIT(3)
#define HISI_UC_SRCID_CTRL_REG		0x1b40
#define HISI_UC_SRCID_MSK		GENMASK(14, 1)
#define HISI_UC_EVENT_CTRL_REG		0x1c00
#define HISI_UC_EVENT_TRACETAG_EN	BIT(29)
#define HISI_UC_EVENT_URING_MSK		GENMASK(28, 27)
#define HISI_UC_EVENT_GLB_EN		BIT(26)
#define HISI_UC_VERSION_REG		0x1cf0
#define HISI_UC_EVTYPE_REGn(n)		(0x1d00 + (n) * 4)
#define HISI_UC_EVTYPE_MASK		GENMASK(7, 0)
#define HISI_UC_CNTR_REGn(n)		(0x1e00 + (n) * 8)

#define HISI_UC_NR_COUNTERS		0x8
#define HISI_UC_V2_NR_EVENTS		0xFF
#define HISI_UC_CNTR_REG_BITS		64

#define HISI_UC_RD_REQ_TRACETAG		0x4
#define HISI_UC_URING_EVENT_MIN		0x47
#define HISI_UC_URING_EVENT_MAX		0x59

HISI_PMU_EVENT_ATTR_EXTRACTOR(rd_req_en, config1, 0, 0);
HISI_PMU_EVENT_ATTR_EXTRACTOR(uring_channel, config1, 5, 4);
HISI_PMU_EVENT_ATTR_EXTRACTOR(srcid, config1, 19, 6);
HISI_PMU_EVENT_ATTR_EXTRACTOR(srcid_en, config1, 20, 20);

static int hisi_uc_pmu_check_filter(struct perf_event *event)
{
	struct hisi_pmu *uc_pmu = to_hisi_pmu(event->pmu);

	if (hisi_get_srcid_en(event) && !hisi_get_rd_req_en(event)) {
		dev_err(uc_pmu->dev,
			"rcid_en depends on rd_req_en being enabled!\n");
		return -EINVAL;
	}

	if (!hisi_get_uring_channel(event))
		return 0;

	if ((HISI_GET_EVENTID(event) < HISI_UC_URING_EVENT_MIN) ||
	    (HISI_GET_EVENTID(event) > HISI_UC_URING_EVENT_MAX))
		dev_warn(uc_pmu->dev,
			 "Only events: [%#x ~ %#x] support channel filtering!",
			 HISI_UC_URING_EVENT_MIN, HISI_UC_URING_EVENT_MAX);

	return 0;
}

static void hisi_uc_pmu_config_req_tracetag(struct perf_event *event)
{
	struct hisi_pmu *uc_pmu = to_hisi_pmu(event->pmu);
	u32 val;

	if (!hisi_get_rd_req_en(event))
		return;

	val = readl(uc_pmu->base + HISI_UC_TRACETAG_CTRL_REG);

	/* The request-type has been configured */
	if (FIELD_GET(HISI_UC_TRACETAG_REQ_MSK, val) == HISI_UC_RD_REQ_TRACETAG)
		return;

	/* Set request-type for tracetag, only read request is supported! */
	val &= ~HISI_UC_TRACETAG_REQ_MSK;
	val |= FIELD_PREP(HISI_UC_TRACETAG_REQ_MSK, HISI_UC_RD_REQ_TRACETAG);
	val |= HISI_UC_TRACETAG_REQ_EN;
	writel(val, uc_pmu->base + HISI_UC_TRACETAG_CTRL_REG);
}

static void hisi_uc_pmu_clear_req_tracetag(struct perf_event *event)
{
	struct hisi_pmu *uc_pmu = to_hisi_pmu(event->pmu);
	u32 val;

	if (!hisi_get_rd_req_en(event))
		return;

	val = readl(uc_pmu->base + HISI_UC_TRACETAG_CTRL_REG);

	/* Do nothing, the request-type tracetag has been cleaned up */
	if (FIELD_GET(HISI_UC_TRACETAG_REQ_MSK, val) == 0)
		return;

	/* Clear request-type */
	val &= ~HISI_UC_TRACETAG_REQ_MSK;
	val &= ~HISI_UC_TRACETAG_REQ_EN;
	writel(val, uc_pmu->base + HISI_UC_TRACETAG_CTRL_REG);
}

static void hisi_uc_pmu_config_srcid_tracetag(struct perf_event *event)
{
	struct hisi_pmu *uc_pmu = to_hisi_pmu(event->pmu);
	u32 val;

	if (!hisi_get_srcid_en(event))
		return;

	val = readl(uc_pmu->base + HISI_UC_TRACETAG_CTRL_REG);

	/* Do nothing, the source id has been configured */
	if (FIELD_GET(HISI_UC_TRACETAG_SRCID_EN, val))
		return;

	/* Enable source id tracetag */
	val |= HISI_UC_TRACETAG_SRCID_EN;
	writel(val, uc_pmu->base + HISI_UC_TRACETAG_CTRL_REG);

	val = readl(uc_pmu->base + HISI_UC_SRCID_CTRL_REG);
	val &= ~HISI_UC_SRCID_MSK;
	val |= FIELD_PREP(HISI_UC_SRCID_MSK, hisi_get_srcid(event));
	writel(val, uc_pmu->base + HISI_UC_SRCID_CTRL_REG);

	/* Depend on request-type tracetag enabled */
	hisi_uc_pmu_config_req_tracetag(event);
}

static void hisi_uc_pmu_clear_srcid_tracetag(struct perf_event *event)
{
	struct hisi_pmu *uc_pmu = to_hisi_pmu(event->pmu);
	u32 val;

	if (!hisi_get_srcid_en(event))
		return;

	val = readl(uc_pmu->base + HISI_UC_TRACETAG_CTRL_REG);

	/* Do nothing, the source id has been cleaned up */
	if (FIELD_GET(HISI_UC_TRACETAG_SRCID_EN, val) == 0)
		return;

	hisi_uc_pmu_clear_req_tracetag(event);

	/* Disable source id tracetag */
	val &= ~HISI_UC_TRACETAG_SRCID_EN;
	writel(val, uc_pmu->base + HISI_UC_TRACETAG_CTRL_REG);

	val = readl(uc_pmu->base + HISI_UC_SRCID_CTRL_REG);
	val &= ~HISI_UC_SRCID_MSK;
	writel(val, uc_pmu->base + HISI_UC_SRCID_CTRL_REG);
}

static void hisi_uc_pmu_config_uring_channel(struct perf_event *event)
{
	struct hisi_pmu *uc_pmu = to_hisi_pmu(event->pmu);
	u32 uring_channel = hisi_get_uring_channel(event);
	u32 val;

	/* Do nothing if not being set or is set explicitly to zero (default) */
	if (uring_channel == 0)
		return;

	val = readl(uc_pmu->base + HISI_UC_EVENT_CTRL_REG);

	/* Do nothing, the uring_channel has been configured */
	if (uring_channel == FIELD_GET(HISI_UC_EVENT_URING_MSK, val))
		return;

	val &= ~HISI_UC_EVENT_URING_MSK;
	val |= FIELD_PREP(HISI_UC_EVENT_URING_MSK, uring_channel);
	writel(val, uc_pmu->base + HISI_UC_EVENT_CTRL_REG);
}

static void hisi_uc_pmu_clear_uring_channel(struct perf_event *event)
{
	struct hisi_pmu *uc_pmu = to_hisi_pmu(event->pmu);
	u32 val;

	/* Do nothing if not being set or is set explicitly to zero (default) */
	if (hisi_get_uring_channel(event) == 0)
		return;

	val = readl(uc_pmu->base + HISI_UC_EVENT_CTRL_REG);

	/* Do nothing, the uring_channel has been cleaned up */
	if (FIELD_GET(HISI_UC_EVENT_URING_MSK, val) == 0)
		return;

	val &= ~HISI_UC_EVENT_URING_MSK;
	writel(val, uc_pmu->base + HISI_UC_EVENT_CTRL_REG);
}

static void hisi_uc_pmu_enable_filter(struct perf_event *event)
{
	if (event->attr.config1 == 0)
		return;

	hisi_uc_pmu_config_uring_channel(event);
	hisi_uc_pmu_config_req_tracetag(event);
	hisi_uc_pmu_config_srcid_tracetag(event);
}

static void hisi_uc_pmu_disable_filter(struct perf_event *event)
{
	if (event->attr.config1 == 0)
		return;

	hisi_uc_pmu_clear_srcid_tracetag(event);
	hisi_uc_pmu_clear_req_tracetag(event);
	hisi_uc_pmu_clear_uring_channel(event);
}

static void hisi_uc_pmu_write_evtype(struct hisi_pmu *uc_pmu, int idx, u32 type)
{
	u32 val;

	/*
	 * Select the appropriate event select register.
	 * There are 2 32-bit event select registers for the
	 * 8 hardware counters, each event code is 8-bit wide.
	 */
	val = readl(uc_pmu->base + HISI_UC_EVTYPE_REGn(idx / 4));
	val &= ~(HISI_UC_EVTYPE_MASK << HISI_PMU_EVTYPE_SHIFT(idx));
	val |= (type << HISI_PMU_EVTYPE_SHIFT(idx));
	writel(val, uc_pmu->base + HISI_UC_EVTYPE_REGn(idx / 4));
}

static void hisi_uc_pmu_start_counters(struct hisi_pmu *uc_pmu)
{
	u32 val;

	val = readl(uc_pmu->base + HISI_UC_EVENT_CTRL_REG);
	val |= HISI_UC_EVENT_GLB_EN;
	writel(val, uc_pmu->base + HISI_UC_EVENT_CTRL_REG);
}

static void hisi_uc_pmu_stop_counters(struct hisi_pmu *uc_pmu)
{
	u32 val;

	val = readl(uc_pmu->base + HISI_UC_EVENT_CTRL_REG);
	val &= ~HISI_UC_EVENT_GLB_EN;
	writel(val, uc_pmu->base + HISI_UC_EVENT_CTRL_REG);
}

static void hisi_uc_pmu_enable_counter(struct hisi_pmu *uc_pmu,
					struct hw_perf_event *hwc)
{
	u32 val;

	/* Enable counter index */
	val = readl(uc_pmu->base + HISI_UC_EVENT_CTRL_REG);
	val |= (1 << hwc->idx);
	writel(val, uc_pmu->base + HISI_UC_EVENT_CTRL_REG);
}

static void hisi_uc_pmu_disable_counter(struct hisi_pmu *uc_pmu,
					struct hw_perf_event *hwc)
{
	u32 val;

	/* Clear counter index */
	val = readl(uc_pmu->base + HISI_UC_EVENT_CTRL_REG);
	val &= ~(1 << hwc->idx);
	writel(val, uc_pmu->base + HISI_UC_EVENT_CTRL_REG);
}

static u64 hisi_uc_pmu_read_counter(struct hisi_pmu *uc_pmu,
				    struct hw_perf_event *hwc)
{
	return readq(uc_pmu->base + HISI_UC_CNTR_REGn(hwc->idx));
}

static void hisi_uc_pmu_write_counter(struct hisi_pmu *uc_pmu,
				      struct hw_perf_event *hwc, u64 val)
{
	writeq(val, uc_pmu->base + HISI_UC_CNTR_REGn(hwc->idx));
}

static void hisi_uc_pmu_enable_counter_int(struct hisi_pmu *uc_pmu,
					   struct hw_perf_event *hwc)
{
	u32 val;

	val = readl(uc_pmu->base + HISI_UC_INT_MASK_REG);
	val &= ~(1 << hwc->idx);
	writel(val, uc_pmu->base + HISI_UC_INT_MASK_REG);
}

static void hisi_uc_pmu_disable_counter_int(struct hisi_pmu *uc_pmu,
					    struct hw_perf_event *hwc)
{
	u32 val;

	val = readl(uc_pmu->base + HISI_UC_INT_MASK_REG);
	val |= (1 << hwc->idx);
	writel(val, uc_pmu->base + HISI_UC_INT_MASK_REG);
}

static u32 hisi_uc_pmu_get_int_status(struct hisi_pmu *uc_pmu)
{
	return readl(uc_pmu->base + HISI_UC_INT_STS_REG);
}

static void hisi_uc_pmu_clear_int_status(struct hisi_pmu *uc_pmu, int idx)
{
	writel(1 << idx, uc_pmu->base + HISI_UC_INT_CLEAR_REG);
}

static int hisi_uc_pmu_init_data(struct platform_device *pdev,
				 struct hisi_pmu *uc_pmu)
{
	/*
	 * Use SCCL (Super CPU Cluster) ID and CCL (CPU Cluster) ID to
	 * identify the topology information of UC PMU devices in the chip.
	 * They have some CCLs per SCCL and then 4 UC PMU per CCL.
	 */
	if (device_property_read_u32(&pdev->dev, "hisilicon,scl-id",
				     &uc_pmu->sccl_id)) {
		dev_err(&pdev->dev, "Can not read uc sccl-id!\n");
		return -EINVAL;
	}

	if (device_property_read_u32(&pdev->dev, "hisilicon,ccl-id",
				     &uc_pmu->ccl_id)) {
		dev_err(&pdev->dev, "Can not read uc ccl-id!\n");
		return -EINVAL;
	}

	if (device_property_read_u32(&pdev->dev, "hisilicon,sub-id",
				     &uc_pmu->sub_id)) {
		dev_err(&pdev->dev, "Can not read sub-id!\n");
		return -EINVAL;
	}

	uc_pmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(uc_pmu->base)) {
		dev_err(&pdev->dev, "ioremap failed for uc_pmu resource\n");
		return PTR_ERR(uc_pmu->base);
	}

	uc_pmu->identifier = readl(uc_pmu->base + HISI_UC_VERSION_REG);

	return 0;
}

static struct attribute *hisi_uc_pmu_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	HISI_PMU_FORMAT_ATTR(rd_req_en, "config1:0-0"),
	HISI_PMU_FORMAT_ATTR(uring_channel, "config1:4-5"),
	HISI_PMU_FORMAT_ATTR(srcid, "config1:6-19"),
	HISI_PMU_FORMAT_ATTR(srcid_en, "config1:20-20"),
	NULL
};

static const struct attribute_group hisi_uc_pmu_format_group = {
	.name = "format",
	.attrs = hisi_uc_pmu_format_attr,
};

static struct attribute *hisi_uc_pmu_events_attr[] = {
	HISI_PMU_EVENT_ATTR(sq_time,		0x00),
	HISI_PMU_EVENT_ATTR(pq_time,		0x01),
	HISI_PMU_EVENT_ATTR(hbm_time,		0x02),
	HISI_PMU_EVENT_ATTR(iq_comp_time_cring,	0x03),
	HISI_PMU_EVENT_ATTR(iq_comp_time_uring,	0x05),
	HISI_PMU_EVENT_ATTR(cpu_rd,		0x10),
	HISI_PMU_EVENT_ATTR(cpu_rd64,		0x17),
	HISI_PMU_EVENT_ATTR(cpu_rs64,		0x19),
	HISI_PMU_EVENT_ATTR(cpu_mru,		0x1c),
	HISI_PMU_EVENT_ATTR(cycles,		0x95),
	HISI_PMU_EVENT_ATTR(spipe_hit,		0xb3),
	HISI_PMU_EVENT_ATTR(hpipe_hit,		0xdb),
	HISI_PMU_EVENT_ATTR(cring_rxdat_cnt,	0xfa),
	HISI_PMU_EVENT_ATTR(cring_txdat_cnt,	0xfb),
	HISI_PMU_EVENT_ATTR(uring_rxdat_cnt,	0xfc),
	HISI_PMU_EVENT_ATTR(uring_txdat_cnt,	0xfd),
	NULL
};

static const struct attribute_group hisi_uc_pmu_events_group = {
	.name = "events",
	.attrs = hisi_uc_pmu_events_attr,
};

static DEVICE_ATTR(cpumask, 0444, hisi_cpumask_sysfs_show, NULL);

static struct attribute *hisi_uc_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group hisi_uc_pmu_cpumask_attr_group = {
	.attrs = hisi_uc_pmu_cpumask_attrs,
};

static struct device_attribute hisi_uc_pmu_identifier_attr =
	__ATTR(identifier, 0444, hisi_uncore_pmu_identifier_attr_show, NULL);

static struct attribute *hisi_uc_pmu_identifier_attrs[] = {
	&hisi_uc_pmu_identifier_attr.attr,
	NULL
};

static const struct attribute_group hisi_uc_pmu_identifier_group = {
	.attrs = hisi_uc_pmu_identifier_attrs,
};

static const struct attribute_group *hisi_uc_pmu_attr_groups[] = {
	&hisi_uc_pmu_format_group,
	&hisi_uc_pmu_events_group,
	&hisi_uc_pmu_cpumask_attr_group,
	&hisi_uc_pmu_identifier_group,
	NULL
};

static const struct hisi_uncore_ops hisi_uncore_uc_pmu_ops = {
	.check_filter		= hisi_uc_pmu_check_filter,
	.write_evtype		= hisi_uc_pmu_write_evtype,
	.get_event_idx		= hisi_uncore_pmu_get_event_idx,
	.start_counters		= hisi_uc_pmu_start_counters,
	.stop_counters		= hisi_uc_pmu_stop_counters,
	.enable_counter		= hisi_uc_pmu_enable_counter,
	.disable_counter	= hisi_uc_pmu_disable_counter,
	.enable_counter_int	= hisi_uc_pmu_enable_counter_int,
	.disable_counter_int	= hisi_uc_pmu_disable_counter_int,
	.write_counter		= hisi_uc_pmu_write_counter,
	.read_counter		= hisi_uc_pmu_read_counter,
	.get_int_status		= hisi_uc_pmu_get_int_status,
	.clear_int_status	= hisi_uc_pmu_clear_int_status,
	.enable_filter		= hisi_uc_pmu_enable_filter,
	.disable_filter		= hisi_uc_pmu_disable_filter,
};

static int hisi_uc_pmu_dev_probe(struct platform_device *pdev,
				 struct hisi_pmu *uc_pmu)
{
	int ret;

	ret = hisi_uc_pmu_init_data(pdev, uc_pmu);
	if (ret)
		return ret;

	ret = hisi_uncore_pmu_init_irq(uc_pmu, pdev);
	if (ret)
		return ret;

	uc_pmu->pmu_events.attr_groups = hisi_uc_pmu_attr_groups;
	uc_pmu->check_event = HISI_UC_EVTYPE_MASK;
	uc_pmu->ops = &hisi_uncore_uc_pmu_ops;
	uc_pmu->counter_bits = HISI_UC_CNTR_REG_BITS;
	uc_pmu->num_counters = HISI_UC_NR_COUNTERS;
	uc_pmu->dev = &pdev->dev;
	uc_pmu->on_cpu = -1;

	return 0;
}

static void hisi_uc_pmu_remove_cpuhp_instance(void *hotplug_node)
{
	cpuhp_state_remove_instance_nocalls(hisi_uc_pmu_online, hotplug_node);
}

static void hisi_uc_pmu_unregister_pmu(void *pmu)
{
	perf_pmu_unregister(pmu);
}

static int hisi_uc_pmu_probe(struct platform_device *pdev)
{
	struct hisi_pmu *uc_pmu;
	char *name;
	int ret;

	uc_pmu = devm_kzalloc(&pdev->dev, sizeof(*uc_pmu), GFP_KERNEL);
	if (!uc_pmu)
		return -ENOMEM;

	platform_set_drvdata(pdev, uc_pmu);

	ret = hisi_uc_pmu_dev_probe(pdev, uc_pmu);
	if (ret)
		return ret;

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hisi_sccl%d_uc%d_%u",
			      uc_pmu->sccl_id, uc_pmu->ccl_id, uc_pmu->sub_id);
	if (!name)
		return -ENOMEM;

	ret = cpuhp_state_add_instance(hisi_uc_pmu_online, &uc_pmu->node);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Error registering hotplug\n");

	ret = devm_add_action_or_reset(&pdev->dev,
				       hisi_uc_pmu_remove_cpuhp_instance,
				       &uc_pmu->node);
	if (ret)
		return ret;

	hisi_pmu_init(uc_pmu, THIS_MODULE);

	ret = perf_pmu_register(&uc_pmu->pmu, name, -1);
	if (ret)
		return ret;

	return devm_add_action_or_reset(&pdev->dev,
					hisi_uc_pmu_unregister_pmu,
					&uc_pmu->pmu);
}

static const struct acpi_device_id hisi_uc_pmu_acpi_match[] = {
	{ "HISI0291", },
	{}
};
MODULE_DEVICE_TABLE(acpi, hisi_uc_pmu_acpi_match);

static struct platform_driver hisi_uc_pmu_driver = {
	.driver = {
		.name = "hisi_uc_pmu",
		.acpi_match_table = hisi_uc_pmu_acpi_match,
		/*
		 * We have not worked out a safe bind/unbind process,
		 * Forcefully unbinding during sampling will lead to a
		 * kernel panic, so this is not supported yet.
		 */
		.suppress_bind_attrs = true,
	},
	.probe = hisi_uc_pmu_probe,
};

static int __init hisi_uc_pmu_module_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "perf/hisi/uc:online",
				      hisi_uncore_pmu_online_cpu,
				      hisi_uncore_pmu_offline_cpu);
	if (ret < 0) {
		pr_err("UC PMU: Error setup hotplug, ret = %d\n", ret);
		return ret;
	}
	hisi_uc_pmu_online = ret;

	ret = platform_driver_register(&hisi_uc_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(hisi_uc_pmu_online);

	return ret;
}
module_init(hisi_uc_pmu_module_init);

static void __exit hisi_uc_pmu_module_exit(void)
{
	platform_driver_unregister(&hisi_uc_pmu_driver);
	cpuhp_remove_multi_state(hisi_uc_pmu_online);
}
module_exit(hisi_uc_pmu_module_exit);

MODULE_DESCRIPTION("HiSilicon SoC UC uncore PMU driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Junhao He <hejunhao3@huawei.com>");
