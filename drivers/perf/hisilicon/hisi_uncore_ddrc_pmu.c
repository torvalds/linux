// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SoC DDRC uncore Hardware event counters support
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

/* DDRC register definition in v1 */
#define DDRC_PERF_CTRL		0x010
#define DDRC_FLUX_WR		0x380
#define DDRC_FLUX_RD		0x384
#define DDRC_FLUX_WCMD          0x388
#define DDRC_FLUX_RCMD          0x38c
#define DDRC_PRE_CMD            0x3c0
#define DDRC_ACT_CMD            0x3c4
#define DDRC_RNK_CHG            0x3cc
#define DDRC_RW_CHG             0x3d0
#define DDRC_EVENT_CTRL         0x6C0
#define DDRC_INT_MASK		0x6c8
#define DDRC_INT_STATUS		0x6cc
#define DDRC_INT_CLEAR		0x6d0
#define DDRC_VERSION		0x710

/* DDRC register definition in v2 */
#define DDRC_V2_INT_MASK	0x528
#define DDRC_V2_INT_STATUS	0x52c
#define DDRC_V2_INT_CLEAR	0x530
#define DDRC_V2_EVENT_CNT	0xe00
#define DDRC_V2_EVENT_CTRL	0xe70
#define DDRC_V2_EVENT_TYPE	0xe74
#define DDRC_V2_PERF_CTRL	0xeA0

/* DDRC interrupt registers definition in v3 */
#define DDRC_V3_INT_MASK	0x534
#define DDRC_V3_INT_STATUS	0x538
#define DDRC_V3_INT_CLEAR	0x53C

/* DDRC has 8-counters */
#define DDRC_NR_COUNTERS	0x8
#define DDRC_V1_PERF_CTRL_EN	0x2
#define DDRC_V2_PERF_CTRL_EN	0x1
#define DDRC_V1_NR_EVENTS	0x7
#define DDRC_V2_NR_EVENTS	0xFF

#define DDRC_EVENT_CNTn(base, n)	((base) + (n) * 8)
#define DDRC_EVENT_TYPEn(base, n)	((base) + (n) * 4)
#define DDRC_UNIMPLEMENTED_REG		GENMASK(31, 0)

/*
 * For PMU v1, there are eight-events and every event has been mapped
 * to fixed-purpose counters which register offset is not consistent.
 * Therefore there is no write event type and we assume that event
 * code (0 to 7) is equal to counter index in PMU driver.
 */
#define GET_DDRC_EVENTID(hwc)	(hwc->config_base & 0x7)

static const u32 ddrc_reg_off[] = {
	DDRC_FLUX_WR, DDRC_FLUX_RD, DDRC_FLUX_WCMD, DDRC_FLUX_RCMD,
	DDRC_PRE_CMD, DDRC_ACT_CMD, DDRC_RNK_CHG, DDRC_RW_CHG
};

struct hisi_ddrc_pmu_regs {
	u32 event_cnt;
	u32 event_ctrl;
	u32 event_type;
	u32 perf_ctrl;
	u32 perf_ctrl_en;
	u32 int_mask;
	u32 int_clear;
	u32 int_status;
};

static u64 hisi_ddrc_pmu_read_counter(struct hisi_pmu *ddrc_pmu,
				      struct hw_perf_event *hwc)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;

	if (regs->event_cnt == DDRC_UNIMPLEMENTED_REG)
		return readl(ddrc_pmu->base + ddrc_reg_off[hwc->idx]);

	return readq(ddrc_pmu->base + DDRC_EVENT_CNTn(regs->event_cnt, hwc->idx));
}

static void hisi_ddrc_pmu_write_counter(struct hisi_pmu *ddrc_pmu,
					struct hw_perf_event *hwc, u64 val)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;

	if (regs->event_cnt == DDRC_UNIMPLEMENTED_REG)
		writel((u32)val, ddrc_pmu->base + ddrc_reg_off[hwc->idx]);
	else
		writeq(val, ddrc_pmu->base + DDRC_EVENT_CNTn(regs->event_cnt, hwc->idx));
}

/*
 * For DDRC PMU v1, event has been mapped to fixed-purpose counter by hardware,
 * so there is no need to write event type, while it is programmable counter in
 * PMU v2.
 */
static void hisi_ddrc_pmu_write_evtype(struct hisi_pmu *ddrc_pmu, int idx,
				       u32 type)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;

	if (regs->event_type == DDRC_UNIMPLEMENTED_REG)
		return;

	writel(type, ddrc_pmu->base + DDRC_EVENT_TYPEn(regs->event_type, idx));
}

static int hisi_ddrc_pmu_v1_get_event_idx(struct perf_event *event)
{
	struct hisi_pmu *ddrc_pmu = to_hisi_pmu(event->pmu);
	unsigned long *used_mask = ddrc_pmu->pmu_events.used_mask;
	struct hw_perf_event *hwc = &event->hw;
	/* For DDRC PMU, we use event code as counter index */
	int idx = GET_DDRC_EVENTID(hwc);

	if (test_bit(idx, used_mask))
		return -EAGAIN;

	set_bit(idx, used_mask);

	return idx;
}

static int hisi_ddrc_pmu_get_event_idx(struct perf_event *event)
{
	struct hisi_pmu *ddrc_pmu = to_hisi_pmu(event->pmu);
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;

	if (regs->event_type == DDRC_UNIMPLEMENTED_REG)
		return hisi_ddrc_pmu_v1_get_event_idx(event);

	return hisi_uncore_pmu_get_event_idx(event);
}

static void hisi_ddrc_pmu_start_counters(struct hisi_pmu *ddrc_pmu)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;
	u32 val;

	val = readl(ddrc_pmu->base + regs->perf_ctrl);
	val |= regs->perf_ctrl_en;
	writel(val, ddrc_pmu->base + regs->perf_ctrl);
}

static void hisi_ddrc_pmu_stop_counters(struct hisi_pmu *ddrc_pmu)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;
	u32 val;

	val = readl(ddrc_pmu->base + regs->perf_ctrl);
	val &= ~regs->perf_ctrl_en;
	writel(val, ddrc_pmu->base + regs->perf_ctrl);
}

static void hisi_ddrc_pmu_enable_counter(struct hisi_pmu *ddrc_pmu,
					    struct hw_perf_event *hwc)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;
	u32 val;

	val = readl(ddrc_pmu->base + regs->event_ctrl);
	val |= BIT_ULL(hwc->idx);
	writel(val, ddrc_pmu->base + regs->event_ctrl);
}

static void hisi_ddrc_pmu_disable_counter(struct hisi_pmu *ddrc_pmu,
					     struct hw_perf_event *hwc)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;
	u32 val;

	val = readl(ddrc_pmu->base + regs->event_ctrl);
	val &= ~BIT_ULL(hwc->idx);
	writel(val, ddrc_pmu->base + regs->event_ctrl);
}

static void hisi_ddrc_pmu_enable_counter_int(struct hisi_pmu *ddrc_pmu,
					     struct hw_perf_event *hwc)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;
	u32 val;

	val = readl(ddrc_pmu->base + regs->int_mask);
	val &= ~BIT_ULL(hwc->idx);
	writel(val, ddrc_pmu->base + regs->int_mask);
}

static void hisi_ddrc_pmu_disable_counter_int(struct hisi_pmu *ddrc_pmu,
					      struct hw_perf_event *hwc)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;
	u32 val;

	val = readl(ddrc_pmu->base + regs->int_mask);
	val |= BIT_ULL(hwc->idx);
	writel(val, ddrc_pmu->base + regs->int_mask);
}

static u32 hisi_ddrc_pmu_get_int_status(struct hisi_pmu *ddrc_pmu)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;

	return readl(ddrc_pmu->base + regs->int_status);
}

static void hisi_ddrc_pmu_clear_int_status(struct hisi_pmu *ddrc_pmu,
					   int idx)
{
	struct hisi_ddrc_pmu_regs *regs = ddrc_pmu->dev_info->private;

	writel(1 << idx, ddrc_pmu->base + regs->int_clear);
}

static int hisi_ddrc_pmu_init_data(struct platform_device *pdev,
				   struct hisi_pmu *ddrc_pmu)
{
	hisi_uncore_pmu_init_topology(ddrc_pmu, &pdev->dev);

	/*
	 * Use the SCCL_ID and DDRC channel ID to identify the
	 * DDRC PMU, while SCCL_ID is in MPIDR[aff2].
	 */
	if (device_property_read_u32(&pdev->dev, "hisilicon,ch-id",
				     &ddrc_pmu->topo.index_id)) {
		dev_err(&pdev->dev, "Can not read ddrc channel-id!\n");
		return -EINVAL;
	}

	if (ddrc_pmu->topo.sccl_id < 0) {
		dev_err(&pdev->dev, "Can not read ddrc sccl-id!\n");
		return -EINVAL;
	}

	ddrc_pmu->dev_info = device_get_match_data(&pdev->dev);
	if (!ddrc_pmu->dev_info)
		return -ENODEV;

	ddrc_pmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ddrc_pmu->base)) {
		dev_err(&pdev->dev, "ioremap failed for ddrc_pmu resource\n");
		return PTR_ERR(ddrc_pmu->base);
	}

	ddrc_pmu->identifier = readl(ddrc_pmu->base + DDRC_VERSION);
	if (ddrc_pmu->identifier >= HISI_PMU_V2) {
		if (ddrc_pmu->topo.sub_id < 0) {
			dev_err(&pdev->dev, "Can not read sub-id!\n");
			return -EINVAL;
		}
	}

	return 0;
}

static struct attribute *hisi_ddrc_pmu_v1_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-4"),
	NULL,
};

static const struct attribute_group hisi_ddrc_pmu_v1_format_group = {
	.name = "format",
	.attrs = hisi_ddrc_pmu_v1_format_attr,
};

static struct attribute *hisi_ddrc_pmu_v2_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	NULL
};

static const struct attribute_group hisi_ddrc_pmu_v2_format_group = {
	.name = "format",
	.attrs = hisi_ddrc_pmu_v2_format_attr,
};

static struct attribute *hisi_ddrc_pmu_v1_events_attr[] = {
	HISI_PMU_EVENT_ATTR(flux_wr,		0x00),
	HISI_PMU_EVENT_ATTR(flux_rd,		0x01),
	HISI_PMU_EVENT_ATTR(flux_wcmd,		0x02),
	HISI_PMU_EVENT_ATTR(flux_rcmd,		0x03),
	HISI_PMU_EVENT_ATTR(pre_cmd,		0x04),
	HISI_PMU_EVENT_ATTR(act_cmd,		0x05),
	HISI_PMU_EVENT_ATTR(rnk_chg,		0x06),
	HISI_PMU_EVENT_ATTR(rw_chg,		0x07),
	NULL,
};

static const struct attribute_group hisi_ddrc_pmu_v1_events_group = {
	.name = "events",
	.attrs = hisi_ddrc_pmu_v1_events_attr,
};

static struct attribute *hisi_ddrc_pmu_v2_events_attr[] = {
	HISI_PMU_EVENT_ATTR(cycles,		0x00),
	HISI_PMU_EVENT_ATTR(flux_wr,		0x83),
	HISI_PMU_EVENT_ATTR(flux_rd,		0x84),
	NULL
};

static const struct attribute_group hisi_ddrc_pmu_v2_events_group = {
	.name = "events",
	.attrs = hisi_ddrc_pmu_v2_events_attr,
};

static const struct attribute_group *hisi_ddrc_pmu_v1_attr_groups[] = {
	&hisi_ddrc_pmu_v1_format_group,
	&hisi_ddrc_pmu_v1_events_group,
	&hisi_pmu_cpumask_attr_group,
	&hisi_pmu_identifier_group,
	NULL,
};

static const struct attribute_group *hisi_ddrc_pmu_v2_attr_groups[] = {
	&hisi_ddrc_pmu_v2_format_group,
	&hisi_ddrc_pmu_v2_events_group,
	&hisi_pmu_cpumask_attr_group,
	&hisi_pmu_identifier_group,
	NULL
};

static const struct hisi_uncore_ops hisi_uncore_ddrc_ops = {
	.write_evtype           = hisi_ddrc_pmu_write_evtype,
	.get_event_idx		= hisi_ddrc_pmu_get_event_idx,
	.start_counters		= hisi_ddrc_pmu_start_counters,
	.stop_counters		= hisi_ddrc_pmu_stop_counters,
	.enable_counter		= hisi_ddrc_pmu_enable_counter,
	.disable_counter	= hisi_ddrc_pmu_disable_counter,
	.enable_counter_int	= hisi_ddrc_pmu_enable_counter_int,
	.disable_counter_int	= hisi_ddrc_pmu_disable_counter_int,
	.write_counter		= hisi_ddrc_pmu_write_counter,
	.read_counter		= hisi_ddrc_pmu_read_counter,
	.get_int_status		= hisi_ddrc_pmu_get_int_status,
	.clear_int_status	= hisi_ddrc_pmu_clear_int_status,
};

static int hisi_ddrc_pmu_dev_probe(struct platform_device *pdev,
				   struct hisi_pmu *ddrc_pmu)
{
	int ret;

	ret = hisi_ddrc_pmu_init_data(pdev, ddrc_pmu);
	if (ret)
		return ret;

	ret = hisi_uncore_pmu_init_irq(ddrc_pmu, pdev);
	if (ret)
		return ret;

	ddrc_pmu->pmu_events.attr_groups = ddrc_pmu->dev_info->attr_groups;
	ddrc_pmu->counter_bits = ddrc_pmu->dev_info->counter_bits;
	ddrc_pmu->check_event = ddrc_pmu->dev_info->check_event;
	ddrc_pmu->ops = &hisi_uncore_ddrc_ops;
	ddrc_pmu->num_counters = DDRC_NR_COUNTERS;
	ddrc_pmu->dev = &pdev->dev;
	ddrc_pmu->on_cpu = -1;

	return 0;
}

static int hisi_ddrc_pmu_probe(struct platform_device *pdev)
{
	struct hisi_pmu *ddrc_pmu;
	char *name;
	int ret;

	ddrc_pmu = devm_kzalloc(&pdev->dev, sizeof(*ddrc_pmu), GFP_KERNEL);
	if (!ddrc_pmu)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddrc_pmu);

	ret = hisi_ddrc_pmu_dev_probe(pdev, ddrc_pmu);
	if (ret)
		return ret;

	if (ddrc_pmu->identifier >= HISI_PMU_V2)
		name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
				      "hisi_sccl%d_ddrc%d_%d",
				      ddrc_pmu->topo.sccl_id, ddrc_pmu->topo.index_id,
				      ddrc_pmu->topo.sub_id);
	else
		name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
				      "hisi_sccl%d_ddrc%d", ddrc_pmu->topo.sccl_id,
				      ddrc_pmu->topo.index_id);

	if (!name)
		return -ENOMEM;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_HISI_DDRC_ONLINE,
				       &ddrc_pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug;\n", ret);
		return ret;
	}

	hisi_pmu_init(ddrc_pmu, THIS_MODULE);

	ret = perf_pmu_register(&ddrc_pmu->pmu, name, -1);
	if (ret) {
		dev_err(ddrc_pmu->dev, "DDRC PMU register failed!\n");
		cpuhp_state_remove_instance_nocalls(
			CPUHP_AP_PERF_ARM_HISI_DDRC_ONLINE, &ddrc_pmu->node);
	}

	return ret;
}

static void hisi_ddrc_pmu_remove(struct platform_device *pdev)
{
	struct hisi_pmu *ddrc_pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&ddrc_pmu->pmu);
	cpuhp_state_remove_instance_nocalls(CPUHP_AP_PERF_ARM_HISI_DDRC_ONLINE,
					    &ddrc_pmu->node);
}

static struct hisi_ddrc_pmu_regs hisi_ddrc_v1_pmu_regs = {
	.event_cnt = DDRC_UNIMPLEMENTED_REG,
	.event_ctrl = DDRC_EVENT_CTRL,
	.event_type = DDRC_UNIMPLEMENTED_REG,
	.perf_ctrl = DDRC_PERF_CTRL,
	.perf_ctrl_en = DDRC_V1_PERF_CTRL_EN,
	.int_mask = DDRC_INT_MASK,
	.int_clear = DDRC_INT_CLEAR,
	.int_status = DDRC_INT_STATUS,
};

static const struct hisi_pmu_dev_info hisi_ddrc_v1 = {
	.counter_bits = 32,
	.check_event = DDRC_V1_NR_EVENTS,
	.attr_groups = hisi_ddrc_pmu_v1_attr_groups,
	.private = &hisi_ddrc_v1_pmu_regs,
};

static struct hisi_ddrc_pmu_regs hisi_ddrc_v2_pmu_regs = {
	.event_cnt = DDRC_V2_EVENT_CNT,
	.event_ctrl = DDRC_V2_EVENT_CTRL,
	.event_type = DDRC_V2_EVENT_TYPE,
	.perf_ctrl = DDRC_V2_PERF_CTRL,
	.perf_ctrl_en = DDRC_V2_PERF_CTRL_EN,
	.int_mask = DDRC_V2_INT_MASK,
	.int_clear = DDRC_V2_INT_CLEAR,
	.int_status = DDRC_V2_INT_STATUS,
};

static const struct hisi_pmu_dev_info hisi_ddrc_v2 = {
	.counter_bits = 48,
	.check_event = DDRC_V2_NR_EVENTS,
	.attr_groups = hisi_ddrc_pmu_v2_attr_groups,
	.private = &hisi_ddrc_v2_pmu_regs,
};

static struct hisi_ddrc_pmu_regs hisi_ddrc_v3_pmu_regs = {
	.event_cnt = DDRC_V2_EVENT_CNT,
	.event_ctrl = DDRC_V2_EVENT_CTRL,
	.event_type = DDRC_V2_EVENT_TYPE,
	.perf_ctrl = DDRC_V2_PERF_CTRL,
	.perf_ctrl_en = DDRC_V2_PERF_CTRL_EN,
	.int_mask = DDRC_V3_INT_MASK,
	.int_clear = DDRC_V3_INT_CLEAR,
	.int_status = DDRC_V3_INT_STATUS,
};

static const struct hisi_pmu_dev_info hisi_ddrc_v3 = {
	.counter_bits = 48,
	.check_event = DDRC_V2_NR_EVENTS,
	.attr_groups = hisi_ddrc_pmu_v2_attr_groups,
	.private = &hisi_ddrc_v3_pmu_regs,
};

static const struct acpi_device_id hisi_ddrc_pmu_acpi_match[] = {
	{ "HISI0233", (kernel_ulong_t)&hisi_ddrc_v1 },
	{ "HISI0234", (kernel_ulong_t)&hisi_ddrc_v2 },
	{ "HISI0235", (kernel_ulong_t)&hisi_ddrc_v3 },
	{}
};
MODULE_DEVICE_TABLE(acpi, hisi_ddrc_pmu_acpi_match);

static struct platform_driver hisi_ddrc_pmu_driver = {
	.driver = {
		.name = "hisi_ddrc_pmu",
		.acpi_match_table = ACPI_PTR(hisi_ddrc_pmu_acpi_match),
		.suppress_bind_attrs = true,
	},
	.probe = hisi_ddrc_pmu_probe,
	.remove = hisi_ddrc_pmu_remove,
};

static int __init hisi_ddrc_pmu_module_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_HISI_DDRC_ONLINE,
				      "AP_PERF_ARM_HISI_DDRC_ONLINE",
				      hisi_uncore_pmu_online_cpu,
				      hisi_uncore_pmu_offline_cpu);
	if (ret) {
		pr_err("DDRC PMU: setup hotplug, ret = %d\n", ret);
		return ret;
	}

	ret = platform_driver_register(&hisi_ddrc_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_DDRC_ONLINE);

	return ret;
}
module_init(hisi_ddrc_pmu_module_init);

static void __exit hisi_ddrc_pmu_module_exit(void)
{
	platform_driver_unregister(&hisi_ddrc_pmu_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_DDRC_ONLINE);
}
module_exit(hisi_ddrc_pmu_module_exit);

MODULE_IMPORT_NS("HISI_PMU");
MODULE_DESCRIPTION("HiSilicon SoC DDRC uncore PMU driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shaokun Zhang <zhangshaokun@hisilicon.com>");
MODULE_AUTHOR("Anurup M <anurup.m@huawei.com>");
