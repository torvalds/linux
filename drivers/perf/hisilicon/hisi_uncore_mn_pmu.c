// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SoC MN uncore Hardware event counters support
 *
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd.
 */
#include <linux/cpuhotplug.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>

#include "hisi_uncore_pmu.h"

/* Dynamic CPU hotplug state used by MN PMU */
static enum cpuhp_state hisi_mn_pmu_online;

/* MN register definition */
#define HISI_MN_DYNAMIC_CTRL_REG	0x400
#define   HISI_MN_DYNAMIC_CTRL_EN	BIT(0)
#define HISI_MN_PERF_CTRL_REG		0x408
#define   HISI_MN_PERF_CTRL_EN		BIT(6)
#define HISI_MN_INT_MASK_REG		0x800
#define HISI_MN_INT_STATUS_REG		0x808
#define HISI_MN_INT_CLEAR_REG		0x80C
#define HISI_MN_EVENT_CTRL_REG		0x1C00
#define HISI_MN_VERSION_REG		0x1C04
#define HISI_MN_EVTYPE0_REG		0x1d00
#define   HISI_MN_EVTYPE_MASK		GENMASK(7, 0)
#define HISI_MN_CNTR0_REG		0x1e00
#define HISI_MN_EVTYPE_REGn(evtype0, n)	((evtype0) + (n) * 4)
#define HISI_MN_CNTR_REGn(cntr0, n)	((cntr0) + (n) * 8)

#define HISI_MN_NR_COUNTERS		4
#define HISI_MN_TIMEOUT_US		500U

struct hisi_mn_pmu_regs {
	u32 version;
	u32 dyn_ctrl;
	u32 perf_ctrl;
	u32 int_mask;
	u32 int_clear;
	u32 int_status;
	u32 event_ctrl;
	u32 event_type0;
	u32 event_cntr0;
};

/*
 * Each event request takes a certain amount of time to complete. If
 * we counting the latency related event, we need to wait for the all
 * requests complete. Otherwise, the value of counter is slightly larger.
 */
static void hisi_mn_pmu_counter_flush(struct hisi_pmu *mn_pmu)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;
	int ret;
	u32 val;

	val = readl(mn_pmu->base + reg_info->dyn_ctrl);
	val |= HISI_MN_DYNAMIC_CTRL_EN;
	writel(val, mn_pmu->base + reg_info->dyn_ctrl);

	ret = readl_poll_timeout_atomic(mn_pmu->base + reg_info->dyn_ctrl,
					val, !(val & HISI_MN_DYNAMIC_CTRL_EN),
					1, HISI_MN_TIMEOUT_US);
	if (ret)
		dev_warn(mn_pmu->dev, "Counter flush timeout\n");
}

static u64 hisi_mn_pmu_read_counter(struct hisi_pmu *mn_pmu,
				    struct hw_perf_event *hwc)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;

	return readq(mn_pmu->base + HISI_MN_CNTR_REGn(reg_info->event_cntr0, hwc->idx));
}

static void hisi_mn_pmu_write_counter(struct hisi_pmu *mn_pmu,
				      struct hw_perf_event *hwc, u64 val)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;

	writeq(val, mn_pmu->base + HISI_MN_CNTR_REGn(reg_info->event_cntr0, hwc->idx));
}

static void hisi_mn_pmu_write_evtype(struct hisi_pmu *mn_pmu, int idx, u32 type)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;
	u32 val;

	/*
	 * Select the appropriate event select register.
	 * There are 2 32-bit event select registers for the
	 * 8 hardware counters, each event code is 8-bit wide.
	 */
	val = readl(mn_pmu->base + HISI_MN_EVTYPE_REGn(reg_info->event_type0, idx / 4));
	val &= ~(HISI_MN_EVTYPE_MASK << HISI_PMU_EVTYPE_SHIFT(idx));
	val |= (type << HISI_PMU_EVTYPE_SHIFT(idx));
	writel(val, mn_pmu->base + HISI_MN_EVTYPE_REGn(reg_info->event_type0, idx / 4));
}

static void hisi_mn_pmu_start_counters(struct hisi_pmu *mn_pmu)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;
	u32 val;

	val = readl(mn_pmu->base + reg_info->perf_ctrl);
	val |= HISI_MN_PERF_CTRL_EN;
	writel(val, mn_pmu->base + reg_info->perf_ctrl);
}

static void hisi_mn_pmu_stop_counters(struct hisi_pmu *mn_pmu)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;
	u32 val;

	val = readl(mn_pmu->base + reg_info->perf_ctrl);
	val &= ~HISI_MN_PERF_CTRL_EN;
	writel(val, mn_pmu->base + reg_info->perf_ctrl);

	hisi_mn_pmu_counter_flush(mn_pmu);
}

static void hisi_mn_pmu_enable_counter(struct hisi_pmu *mn_pmu,
				       struct hw_perf_event *hwc)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;
	u32 val;

	val = readl(mn_pmu->base + reg_info->event_ctrl);
	val |= BIT(hwc->idx);
	writel(val, mn_pmu->base + reg_info->event_ctrl);
}

static void hisi_mn_pmu_disable_counter(struct hisi_pmu *mn_pmu,
					struct hw_perf_event *hwc)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;
	u32 val;

	val = readl(mn_pmu->base + reg_info->event_ctrl);
	val &= ~BIT(hwc->idx);
	writel(val, mn_pmu->base + reg_info->event_ctrl);
}

static void hisi_mn_pmu_enable_counter_int(struct hisi_pmu *mn_pmu,
					   struct hw_perf_event *hwc)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;
	u32 val;

	val = readl(mn_pmu->base + reg_info->int_mask);
	val &= ~BIT(hwc->idx);
	writel(val, mn_pmu->base + reg_info->int_mask);
}

static void hisi_mn_pmu_disable_counter_int(struct hisi_pmu *mn_pmu,
					    struct hw_perf_event *hwc)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;
	u32 val;

	val = readl(mn_pmu->base + reg_info->int_mask);
	val |= BIT(hwc->idx);
	writel(val, mn_pmu->base + reg_info->int_mask);
}

static u32 hisi_mn_pmu_get_int_status(struct hisi_pmu *mn_pmu)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;

	return readl(mn_pmu->base + reg_info->int_status);
}

static void hisi_mn_pmu_clear_int_status(struct hisi_pmu *mn_pmu, int idx)
{
	struct hisi_mn_pmu_regs *reg_info = mn_pmu->dev_info->private;

	writel(BIT(idx), mn_pmu->base + reg_info->int_clear);
}

static struct attribute *hisi_mn_pmu_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	NULL
};

static const struct attribute_group hisi_mn_pmu_format_group = {
	.name = "format",
	.attrs = hisi_mn_pmu_format_attr,
};

static struct attribute *hisi_mn_pmu_events_attr[] = {
	HISI_PMU_EVENT_ATTR(req_eobarrier_num,		0x00),
	HISI_PMU_EVENT_ATTR(req_ecbarrier_num,		0x01),
	HISI_PMU_EVENT_ATTR(req_dvmop_num,		0x02),
	HISI_PMU_EVENT_ATTR(req_dvmsync_num,		0x03),
	HISI_PMU_EVENT_ATTR(req_retry_num,		0x04),
	HISI_PMU_EVENT_ATTR(req_writenosnp_num,		0x05),
	HISI_PMU_EVENT_ATTR(req_readnosnp_num,		0x06),
	HISI_PMU_EVENT_ATTR(snp_dvm_num,		0x07),
	HISI_PMU_EVENT_ATTR(snp_dvmsync_num,		0x08),
	HISI_PMU_EVENT_ATTR(l3t_req_dvm_num,		0x09),
	HISI_PMU_EVENT_ATTR(l3t_req_dvmsync_num,	0x0A),
	HISI_PMU_EVENT_ATTR(mn_req_dvm_num,		0x0B),
	HISI_PMU_EVENT_ATTR(mn_req_dvmsync_num,		0x0C),
	HISI_PMU_EVENT_ATTR(pa_req_dvm_num,		0x0D),
	HISI_PMU_EVENT_ATTR(pa_req_dvmsync_num,		0x0E),
	HISI_PMU_EVENT_ATTR(snp_dvm_latency,		0x80),
	HISI_PMU_EVENT_ATTR(snp_dvmsync_latency,	0x81),
	HISI_PMU_EVENT_ATTR(l3t_req_dvm_latency,	0x82),
	HISI_PMU_EVENT_ATTR(l3t_req_dvmsync_latency,	0x83),
	HISI_PMU_EVENT_ATTR(mn_req_dvm_latency,		0x84),
	HISI_PMU_EVENT_ATTR(mn_req_dvmsync_latency,	0x85),
	HISI_PMU_EVENT_ATTR(pa_req_dvm_latency,		0x86),
	HISI_PMU_EVENT_ATTR(pa_req_dvmsync_latency,	0x87),
	NULL
};

static const struct attribute_group hisi_mn_pmu_events_group = {
	.name = "events",
	.attrs = hisi_mn_pmu_events_attr,
};

static const struct attribute_group *hisi_mn_pmu_attr_groups[] = {
	&hisi_mn_pmu_format_group,
	&hisi_mn_pmu_events_group,
	&hisi_pmu_cpumask_attr_group,
	&hisi_pmu_identifier_group,
	NULL
};

static const struct hisi_uncore_ops hisi_uncore_mn_ops = {
	.write_evtype		= hisi_mn_pmu_write_evtype,
	.get_event_idx		= hisi_uncore_pmu_get_event_idx,
	.start_counters		= hisi_mn_pmu_start_counters,
	.stop_counters		= hisi_mn_pmu_stop_counters,
	.enable_counter		= hisi_mn_pmu_enable_counter,
	.disable_counter	= hisi_mn_pmu_disable_counter,
	.enable_counter_int	= hisi_mn_pmu_enable_counter_int,
	.disable_counter_int	= hisi_mn_pmu_disable_counter_int,
	.write_counter		= hisi_mn_pmu_write_counter,
	.read_counter		= hisi_mn_pmu_read_counter,
	.get_int_status		= hisi_mn_pmu_get_int_status,
	.clear_int_status	= hisi_mn_pmu_clear_int_status,
};

static int hisi_mn_pmu_dev_init(struct platform_device *pdev,
				struct hisi_pmu *mn_pmu)
{
	struct hisi_mn_pmu_regs *reg_info;
	int ret;

	hisi_uncore_pmu_init_topology(mn_pmu, &pdev->dev);

	if (mn_pmu->topo.scl_id < 0)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "Failed to read MN scl id\n");

	if (mn_pmu->topo.index_id < 0)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "Failed to read MN index id\n");

	mn_pmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mn_pmu->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(mn_pmu->base),
				     "Failed to ioremap resource\n");

	ret = hisi_uncore_pmu_init_irq(mn_pmu, pdev);
	if (ret)
		return ret;

	mn_pmu->dev_info = device_get_match_data(&pdev->dev);
	if (!mn_pmu->dev_info)
		return -ENODEV;

	mn_pmu->pmu_events.attr_groups = mn_pmu->dev_info->attr_groups;
	mn_pmu->counter_bits = mn_pmu->dev_info->counter_bits;
	mn_pmu->check_event = mn_pmu->dev_info->check_event;
	mn_pmu->num_counters = HISI_MN_NR_COUNTERS;
	mn_pmu->ops = &hisi_uncore_mn_ops;
	mn_pmu->dev = &pdev->dev;
	mn_pmu->on_cpu = -1;

	reg_info = mn_pmu->dev_info->private;
	mn_pmu->identifier = readl(mn_pmu->base + reg_info->version);

	return 0;
}

static void hisi_mn_pmu_remove_cpuhp(void *hotplug_node)
{
	cpuhp_state_remove_instance_nocalls(hisi_mn_pmu_online, hotplug_node);
}

static void hisi_mn_pmu_unregister(void *pmu)
{
	perf_pmu_unregister(pmu);
}

static int hisi_mn_pmu_probe(struct platform_device *pdev)
{
	struct hisi_pmu *mn_pmu;
	char *name;
	int ret;

	mn_pmu = devm_kzalloc(&pdev->dev, sizeof(*mn_pmu), GFP_KERNEL);
	if (!mn_pmu)
		return -ENOMEM;

	platform_set_drvdata(pdev, mn_pmu);

	ret = hisi_mn_pmu_dev_init(pdev, mn_pmu);
	if (ret)
		return ret;

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hisi_scl%d_mn%d",
				mn_pmu->topo.scl_id, mn_pmu->topo.index_id);
	if (!name)
		return -ENOMEM;

	ret = cpuhp_state_add_instance(hisi_mn_pmu_online, &mn_pmu->node);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to register cpu hotplug\n");

	ret = devm_add_action_or_reset(&pdev->dev, hisi_mn_pmu_remove_cpuhp, &mn_pmu->node);
	if (ret)
		return ret;

	hisi_pmu_init(mn_pmu, THIS_MODULE);

	ret = perf_pmu_register(&mn_pmu->pmu, name, -1);
	if (ret)
		return dev_err_probe(mn_pmu->dev, ret, "Failed to register MN PMU\n");

	return devm_add_action_or_reset(&pdev->dev, hisi_mn_pmu_unregister, &mn_pmu->pmu);
}

static struct hisi_mn_pmu_regs hisi_mn_v1_pmu_regs = {
	.version = HISI_MN_VERSION_REG,
	.dyn_ctrl = HISI_MN_DYNAMIC_CTRL_REG,
	.perf_ctrl = HISI_MN_PERF_CTRL_REG,
	.int_mask = HISI_MN_INT_MASK_REG,
	.int_clear = HISI_MN_INT_CLEAR_REG,
	.int_status = HISI_MN_INT_STATUS_REG,
	.event_ctrl = HISI_MN_EVENT_CTRL_REG,
	.event_type0 = HISI_MN_EVTYPE0_REG,
	.event_cntr0 = HISI_MN_CNTR0_REG,
};

static const struct hisi_pmu_dev_info hisi_mn_v1 = {
	.attr_groups = hisi_mn_pmu_attr_groups,
	.counter_bits = 48,
	.check_event = HISI_MN_EVTYPE_MASK,
	.private = &hisi_mn_v1_pmu_regs,
};

static const struct acpi_device_id hisi_mn_pmu_acpi_match[] = {
	{ "HISI0222", (kernel_ulong_t) &hisi_mn_v1 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, hisi_mn_pmu_acpi_match);

static struct platform_driver hisi_mn_pmu_driver = {
	.driver = {
		.name = "hisi_mn_pmu",
		.acpi_match_table = hisi_mn_pmu_acpi_match,
		/*
		 * We have not worked out a safe bind/unbind process,
		 * Forcefully unbinding during sampling will lead to a
		 * kernel panic, so this is not supported yet.
		 */
		.suppress_bind_attrs = true,
	},
	.probe = hisi_mn_pmu_probe,
};

static int __init hisi_mn_pmu_module_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, "perf/hisi/mn:online",
				      hisi_uncore_pmu_online_cpu,
				      hisi_uncore_pmu_offline_cpu);
	if (ret < 0) {
		pr_err("hisi_mn_pmu: Failed to setup MN PMU hotplug: %d\n", ret);
		return ret;
	}
	hisi_mn_pmu_online = ret;

	ret = platform_driver_register(&hisi_mn_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(hisi_mn_pmu_online);

	return ret;
}
module_init(hisi_mn_pmu_module_init);

static void __exit hisi_mn_pmu_module_exit(void)
{
	platform_driver_unregister(&hisi_mn_pmu_driver);
	cpuhp_remove_multi_state(hisi_mn_pmu_online);
}
module_exit(hisi_mn_pmu_module_exit);

MODULE_IMPORT_NS("HISI_PMU");
MODULE_DESCRIPTION("HiSilicon SoC MN uncore PMU driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Junhao He <hejunhao3@huawei.com>");
