// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SoC L3C uncore Hardware event counters support
 *
 * Copyright (C) 2017 Hisilicon Limited
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
#include <linux/platform_device.h>
#include <linux/smp.h>

#include "hisi_uncore_pmu.h"

/* L3C register definition */
#define L3C_PERF_CTRL		0x0408
#define L3C_INT_MASK		0x0800
#define L3C_INT_STATUS		0x0808
#define L3C_INT_CLEAR		0x080c
#define L3C_EVENT_CTRL	        0x1c00
#define L3C_EVENT_TYPE0		0x1d00
/*
 * Each counter is 48-bits and [48:63] are reserved
 * which are Read-As-Zero and Writes-Ignored.
 */
#define L3C_CNTR0_LOWER		0x1e00

/* L3C has 8-counters */
#define L3C_NR_COUNTERS		0x8

#define L3C_PERF_CTRL_EN	0x20000
#define L3C_EVTYPE_NONE		0xff

/*
 * Select the counter register offset using the counter index
 */
static u32 hisi_l3c_pmu_get_counter_offset(int cntr_idx)
{
	return (L3C_CNTR0_LOWER + (cntr_idx * 8));
}

static u64 hisi_l3c_pmu_read_counter(struct hisi_pmu *l3c_pmu,
				     struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;

	if (!hisi_uncore_pmu_counter_valid(l3c_pmu, idx)) {
		dev_err(l3c_pmu->dev, "Unsupported event index:%d!\n", idx);
		return 0;
	}

	/* Read 64-bits and the upper 16 bits are RAZ */
	return readq(l3c_pmu->base + hisi_l3c_pmu_get_counter_offset(idx));
}

static void hisi_l3c_pmu_write_counter(struct hisi_pmu *l3c_pmu,
				       struct hw_perf_event *hwc, u64 val)
{
	u32 idx = hwc->idx;

	if (!hisi_uncore_pmu_counter_valid(l3c_pmu, idx)) {
		dev_err(l3c_pmu->dev, "Unsupported event index:%d!\n", idx);
		return;
	}

	/* Write 64-bits and the upper 16 bits are WI */
	writeq(val, l3c_pmu->base + hisi_l3c_pmu_get_counter_offset(idx));
}

static void hisi_l3c_pmu_write_evtype(struct hisi_pmu *l3c_pmu, int idx,
				      u32 type)
{
	u32 reg, reg_idx, shift, val;

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
	val = readl(l3c_pmu->base + reg);
	val &= ~(L3C_EVTYPE_NONE << shift);
	val |= (type << shift);
	writel(val, l3c_pmu->base + reg);
}

static void hisi_l3c_pmu_start_counters(struct hisi_pmu *l3c_pmu)
{
	u32 val;

	/*
	 * Set perf_enable bit in L3C_PERF_CTRL register to start counting
	 * for all enabled counters.
	 */
	val = readl(l3c_pmu->base + L3C_PERF_CTRL);
	val |= L3C_PERF_CTRL_EN;
	writel(val, l3c_pmu->base + L3C_PERF_CTRL);
}

static void hisi_l3c_pmu_stop_counters(struct hisi_pmu *l3c_pmu)
{
	u32 val;

	/*
	 * Clear perf_enable bit in L3C_PERF_CTRL register to stop counting
	 * for all enabled counters.
	 */
	val = readl(l3c_pmu->base + L3C_PERF_CTRL);
	val &= ~(L3C_PERF_CTRL_EN);
	writel(val, l3c_pmu->base + L3C_PERF_CTRL);
}

static void hisi_l3c_pmu_enable_counter(struct hisi_pmu *l3c_pmu,
					struct hw_perf_event *hwc)
{
	u32 val;

	/* Enable counter index in L3C_EVENT_CTRL register */
	val = readl(l3c_pmu->base + L3C_EVENT_CTRL);
	val |= (1 << hwc->idx);
	writel(val, l3c_pmu->base + L3C_EVENT_CTRL);
}

static void hisi_l3c_pmu_disable_counter(struct hisi_pmu *l3c_pmu,
					 struct hw_perf_event *hwc)
{
	u32 val;

	/* Clear counter index in L3C_EVENT_CTRL register */
	val = readl(l3c_pmu->base + L3C_EVENT_CTRL);
	val &= ~(1 << hwc->idx);
	writel(val, l3c_pmu->base + L3C_EVENT_CTRL);
}

static void hisi_l3c_pmu_enable_counter_int(struct hisi_pmu *l3c_pmu,
					    struct hw_perf_event *hwc)
{
	u32 val;

	val = readl(l3c_pmu->base + L3C_INT_MASK);
	/* Write 0 to enable interrupt */
	val &= ~(1 << hwc->idx);
	writel(val, l3c_pmu->base + L3C_INT_MASK);
}

static void hisi_l3c_pmu_disable_counter_int(struct hisi_pmu *l3c_pmu,
					     struct hw_perf_event *hwc)
{
	u32 val;

	val = readl(l3c_pmu->base + L3C_INT_MASK);
	/* Write 1 to mask interrupt */
	val |= (1 << hwc->idx);
	writel(val, l3c_pmu->base + L3C_INT_MASK);
}

static irqreturn_t hisi_l3c_pmu_isr(int irq, void *dev_id)
{
	struct hisi_pmu *l3c_pmu = dev_id;
	struct perf_event *event;
	unsigned long overflown;
	int idx;

	/* Read L3C_INT_STATUS register */
	overflown = readl(l3c_pmu->base + L3C_INT_STATUS);
	if (!overflown)
		return IRQ_NONE;

	/*
	 * Find the counter index which overflowed if the bit was set
	 * and handle it.
	 */
	for_each_set_bit(idx, &overflown, L3C_NR_COUNTERS) {
		/* Write 1 to clear the IRQ status flag */
		writel((1 << idx), l3c_pmu->base + L3C_INT_CLEAR);

		/* Get the corresponding event struct */
		event = l3c_pmu->pmu_events.hw_events[idx];
		if (!event)
			continue;

		hisi_uncore_pmu_event_update(event);
		hisi_uncore_pmu_set_event_period(event);
	}

	return IRQ_HANDLED;
}

static int hisi_l3c_pmu_init_irq(struct hisi_pmu *l3c_pmu,
				 struct platform_device *pdev)
{
	int irq, ret;

	/* Read and init IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, hisi_l3c_pmu_isr,
			       IRQF_NOBALANCING | IRQF_NO_THREAD,
			       dev_name(&pdev->dev), l3c_pmu);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Fail to request IRQ:%d ret:%d\n", irq, ret);
		return ret;
	}

	l3c_pmu->irq = irq;

	return 0;
}

static const struct acpi_device_id hisi_l3c_pmu_acpi_match[] = {
	{ "HISI0213", },
	{},
};
MODULE_DEVICE_TABLE(acpi, hisi_l3c_pmu_acpi_match);

static int hisi_l3c_pmu_init_data(struct platform_device *pdev,
				  struct hisi_pmu *l3c_pmu)
{
	unsigned long long id;
	struct resource *res;
	acpi_status status;

	status = acpi_evaluate_integer(ACPI_HANDLE(&pdev->dev),
				       "_UID", NULL, &id);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	l3c_pmu->index_id = id;

	/*
	 * Use the SCCL_ID and CCL_ID to identify the L3C PMU, while
	 * SCCL_ID is in MPIDR[aff2] and CCL_ID is in MPIDR[aff1].
	 */
	if (device_property_read_u32(&pdev->dev, "hisilicon,scl-id",
				     &l3c_pmu->sccl_id)) {
		dev_err(&pdev->dev, "Can not read l3c sccl-id!\n");
		return -EINVAL;
	}

	if (device_property_read_u32(&pdev->dev, "hisilicon,ccl-id",
				     &l3c_pmu->ccl_id)) {
		dev_err(&pdev->dev, "Can not read l3c ccl-id!\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	l3c_pmu->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(l3c_pmu->base)) {
		dev_err(&pdev->dev, "ioremap failed for l3c_pmu resource\n");
		return PTR_ERR(l3c_pmu->base);
	}

	return 0;
}

static struct attribute *hisi_l3c_pmu_format_attr[] = {
	HISI_PMU_FORMAT_ATTR(event, "config:0-7"),
	NULL,
};

static const struct attribute_group hisi_l3c_pmu_format_group = {
	.name = "format",
	.attrs = hisi_l3c_pmu_format_attr,
};

static struct attribute *hisi_l3c_pmu_events_attr[] = {
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

static const struct attribute_group hisi_l3c_pmu_events_group = {
	.name = "events",
	.attrs = hisi_l3c_pmu_events_attr,
};

static DEVICE_ATTR(cpumask, 0444, hisi_cpumask_sysfs_show, NULL);

static struct attribute *hisi_l3c_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group hisi_l3c_pmu_cpumask_attr_group = {
	.attrs = hisi_l3c_pmu_cpumask_attrs,
};

static const struct attribute_group *hisi_l3c_pmu_attr_groups[] = {
	&hisi_l3c_pmu_format_group,
	&hisi_l3c_pmu_events_group,
	&hisi_l3c_pmu_cpumask_attr_group,
	NULL,
};

static const struct hisi_uncore_ops hisi_uncore_l3c_ops = {
	.write_evtype		= hisi_l3c_pmu_write_evtype,
	.get_event_idx		= hisi_uncore_pmu_get_event_idx,
	.start_counters		= hisi_l3c_pmu_start_counters,
	.stop_counters		= hisi_l3c_pmu_stop_counters,
	.enable_counter		= hisi_l3c_pmu_enable_counter,
	.disable_counter	= hisi_l3c_pmu_disable_counter,
	.enable_counter_int	= hisi_l3c_pmu_enable_counter_int,
	.disable_counter_int	= hisi_l3c_pmu_disable_counter_int,
	.write_counter		= hisi_l3c_pmu_write_counter,
	.read_counter		= hisi_l3c_pmu_read_counter,
};

static int hisi_l3c_pmu_dev_probe(struct platform_device *pdev,
				  struct hisi_pmu *l3c_pmu)
{
	int ret;

	ret = hisi_l3c_pmu_init_data(pdev, l3c_pmu);
	if (ret)
		return ret;

	ret = hisi_l3c_pmu_init_irq(l3c_pmu, pdev);
	if (ret)
		return ret;

	l3c_pmu->num_counters = L3C_NR_COUNTERS;
	l3c_pmu->counter_bits = 48;
	l3c_pmu->ops = &hisi_uncore_l3c_ops;
	l3c_pmu->dev = &pdev->dev;
	l3c_pmu->on_cpu = -1;
	l3c_pmu->check_event = 0x59;

	return 0;
}

static int hisi_l3c_pmu_probe(struct platform_device *pdev)
{
	struct hisi_pmu *l3c_pmu;
	char *name;
	int ret;

	l3c_pmu = devm_kzalloc(&pdev->dev, sizeof(*l3c_pmu), GFP_KERNEL);
	if (!l3c_pmu)
		return -ENOMEM;

	platform_set_drvdata(pdev, l3c_pmu);

	ret = hisi_l3c_pmu_dev_probe(pdev, l3c_pmu);
	if (ret)
		return ret;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_HISI_L3_ONLINE,
				       &l3c_pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug\n", ret);
		return ret;
	}

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hisi_sccl%u_l3c%u",
			      l3c_pmu->sccl_id, l3c_pmu->index_id);
	l3c_pmu->pmu = (struct pmu) {
		.name		= name,
		.task_ctx_nr	= perf_invalid_context,
		.event_init	= hisi_uncore_pmu_event_init,
		.pmu_enable	= hisi_uncore_pmu_enable,
		.pmu_disable	= hisi_uncore_pmu_disable,
		.add		= hisi_uncore_pmu_add,
		.del		= hisi_uncore_pmu_del,
		.start		= hisi_uncore_pmu_start,
		.stop		= hisi_uncore_pmu_stop,
		.read		= hisi_uncore_pmu_read,
		.attr_groups	= hisi_l3c_pmu_attr_groups,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
	};

	ret = perf_pmu_register(&l3c_pmu->pmu, name, -1);
	if (ret) {
		dev_err(l3c_pmu->dev, "L3C PMU register failed!\n");
		cpuhp_state_remove_instance(CPUHP_AP_PERF_ARM_HISI_L3_ONLINE,
					    &l3c_pmu->node);
	}

	return ret;
}

static int hisi_l3c_pmu_remove(struct platform_device *pdev)
{
	struct hisi_pmu *l3c_pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&l3c_pmu->pmu);
	cpuhp_state_remove_instance(CPUHP_AP_PERF_ARM_HISI_L3_ONLINE,
				    &l3c_pmu->node);

	return 0;
}

static struct platform_driver hisi_l3c_pmu_driver = {
	.driver = {
		.name = "hisi_l3c_pmu",
		.acpi_match_table = ACPI_PTR(hisi_l3c_pmu_acpi_match),
	},
	.probe = hisi_l3c_pmu_probe,
	.remove = hisi_l3c_pmu_remove,
};

static int __init hisi_l3c_pmu_module_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_HISI_L3_ONLINE,
				      "AP_PERF_ARM_HISI_L3_ONLINE",
				      hisi_uncore_pmu_online_cpu,
				      hisi_uncore_pmu_offline_cpu);
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

MODULE_DESCRIPTION("HiSilicon SoC L3C uncore PMU driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Anurup M <anurup.m@huawei.com>");
MODULE_AUTHOR("Shaokun Zhang <zhangshaokun@hisilicon.com>");
