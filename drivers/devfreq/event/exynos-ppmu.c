/*
 * exynos_ppmu.c - EXYNOS PPMU (Platform Performance Monitoring Unit) support
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author : Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This driver is based on drivers/devfreq/exynos/exynos_ppmu.c
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/devfreq-event.h>

#include "exynos-ppmu.h"

struct exynos_ppmu_data {
	void __iomem *base;
	struct clk *clk;
};

struct exynos_ppmu {
	struct devfreq_event_dev **edev;
	struct devfreq_event_desc *desc;
	unsigned int num_events;

	struct device *dev;
	struct mutex lock;

	struct exynos_ppmu_data ppmu;
};

#define PPMU_EVENT(name)			\
	{ "ppmu-event0-"#name, PPMU_PMNCNT0 },	\
	{ "ppmu-event1-"#name, PPMU_PMNCNT1 },	\
	{ "ppmu-event2-"#name, PPMU_PMNCNT2 },	\
	{ "ppmu-event3-"#name, PPMU_PMNCNT3 }

struct __exynos_ppmu_events {
	char *name;
	int id;
} ppmu_events[] = {
	/* For Exynos3250, Exynos4 and Exynos5260 */
	PPMU_EVENT(g3d),
	PPMU_EVENT(fsys),

	/* For Exynos4 SoCs and Exynos3250 */
	PPMU_EVENT(dmc0),
	PPMU_EVENT(dmc1),
	PPMU_EVENT(cpu),
	PPMU_EVENT(rightbus),
	PPMU_EVENT(leftbus),
	PPMU_EVENT(lcd0),
	PPMU_EVENT(camif),

	/* Only for Exynos3250 and Exynos5260 */
	PPMU_EVENT(mfc),

	/* Only for Exynos4 SoCs */
	PPMU_EVENT(mfc-left),
	PPMU_EVENT(mfc-right),

	/* Only for Exynos5260 SoCs */
	PPMU_EVENT(drex0-s0),
	PPMU_EVENT(drex0-s1),
	PPMU_EVENT(drex1-s0),
	PPMU_EVENT(drex1-s1),
	PPMU_EVENT(eagle),
	PPMU_EVENT(kfc),
	PPMU_EVENT(isp),
	PPMU_EVENT(fimc),
	PPMU_EVENT(gscl),
	PPMU_EVENT(mscl),
	PPMU_EVENT(fimd0x),
	PPMU_EVENT(fimd1x),
	{ /* sentinel */ },
};

static int exynos_ppmu_find_ppmu_id(struct devfreq_event_dev *edev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ppmu_events); i++)
		if (!strcmp(edev->desc->name, ppmu_events[i].name))
			return ppmu_events[i].id;

	return -EINVAL;
}

static int exynos_ppmu_disable(struct devfreq_event_dev *edev)
{
	struct exynos_ppmu *info = devfreq_event_get_drvdata(edev);
	u32 pmnc;

	/* Disable all counters */
	__raw_writel(PPMU_CCNT_MASK |
		     PPMU_PMCNT0_MASK |
		     PPMU_PMCNT1_MASK |
		     PPMU_PMCNT2_MASK |
		     PPMU_PMCNT3_MASK,
		     info->ppmu.base + PPMU_CNTENC);

	/* Disable PPMU */
	pmnc = __raw_readl(info->ppmu.base + PPMU_PMNC);
	pmnc &= ~PPMU_PMNC_ENABLE_MASK;
	__raw_writel(pmnc, info->ppmu.base + PPMU_PMNC);

	return 0;
}

static int exynos_ppmu_set_event(struct devfreq_event_dev *edev)
{
	struct exynos_ppmu *info = devfreq_event_get_drvdata(edev);
	int id = exynos_ppmu_find_ppmu_id(edev);
	u32 pmnc, cntens;

	if (id < 0)
		return id;

	/* Enable specific counter */
	cntens = __raw_readl(info->ppmu.base + PPMU_CNTENS);
	cntens |= (PPMU_CCNT_MASK | (PPMU_ENABLE << id));
	__raw_writel(cntens, info->ppmu.base + PPMU_CNTENS);

	/* Set the event of Read/Write data count  */
	__raw_writel(PPMU_RO_DATA_CNT | PPMU_WO_DATA_CNT,
			info->ppmu.base + PPMU_BEVTxSEL(id));

	/* Reset cycle counter/performance counter and enable PPMU */
	pmnc = __raw_readl(info->ppmu.base + PPMU_PMNC);
	pmnc &= ~(PPMU_PMNC_ENABLE_MASK
			| PPMU_PMNC_COUNTER_RESET_MASK
			| PPMU_PMNC_CC_RESET_MASK);
	pmnc |= (PPMU_ENABLE << PPMU_PMNC_ENABLE_SHIFT);
	pmnc |= (PPMU_ENABLE << PPMU_PMNC_COUNTER_RESET_SHIFT);
	pmnc |= (PPMU_ENABLE << PPMU_PMNC_CC_RESET_SHIFT);
	__raw_writel(pmnc, info->ppmu.base + PPMU_PMNC);

	return 0;
}

static int exynos_ppmu_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata)
{
	struct exynos_ppmu *info = devfreq_event_get_drvdata(edev);
	int id = exynos_ppmu_find_ppmu_id(edev);
	u32 pmnc, cntenc;

	if (id < 0)
		return -EINVAL;

	/* Disable PPMU */
	pmnc = __raw_readl(info->ppmu.base + PPMU_PMNC);
	pmnc &= ~PPMU_PMNC_ENABLE_MASK;
	__raw_writel(pmnc, info->ppmu.base + PPMU_PMNC);

	/* Read cycle count */
	edata->total_count = __raw_readl(info->ppmu.base + PPMU_CCNT);

	/* Read performance count */
	switch (id) {
	case PPMU_PMNCNT0:
	case PPMU_PMNCNT1:
	case PPMU_PMNCNT2:
		edata->load_count
			= __raw_readl(info->ppmu.base + PPMU_PMNCT(id));
		break;
	case PPMU_PMNCNT3:
		edata->load_count =
			((__raw_readl(info->ppmu.base + PPMU_PMCNT3_HIGH) << 8)
			| __raw_readl(info->ppmu.base + PPMU_PMCNT3_LOW));
		break;
	default:
		return -EINVAL;
	}

	/* Disable specific counter */
	cntenc = __raw_readl(info->ppmu.base + PPMU_CNTENC);
	cntenc |= (PPMU_CCNT_MASK | (PPMU_ENABLE << id));
	__raw_writel(cntenc, info->ppmu.base + PPMU_CNTENC);

	dev_dbg(&edev->dev, "%s (event: %ld/%ld)\n", edev->desc->name,
					edata->load_count, edata->total_count);

	return 0;
}

static struct devfreq_event_ops exynos_ppmu_ops = {
	.disable = exynos_ppmu_disable,
	.set_event = exynos_ppmu_set_event,
	.get_event = exynos_ppmu_get_event,
};

static int of_get_devfreq_events(struct device_node *np,
				 struct exynos_ppmu *info)
{
	struct devfreq_event_desc *desc;
	struct device *dev = info->dev;
	struct device_node *events_np, *node;
	int i, j, count;

	events_np = of_get_child_by_name(np, "events");
	if (!events_np) {
		dev_err(dev,
			"failed to get child node of devfreq-event devices\n");
		return -EINVAL;
	}

	count = of_get_child_count(events_np);
	desc = devm_kzalloc(dev, sizeof(*desc) * count, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	info->num_events = count;

	j = 0;
	for_each_child_of_node(events_np, node) {
		for (i = 0; i < ARRAY_SIZE(ppmu_events); i++) {
			if (!ppmu_events[i].name)
				continue;

			if (!of_node_cmp(node->name, ppmu_events[i].name))
				break;
		}

		if (i == ARRAY_SIZE(ppmu_events)) {
			dev_warn(dev,
				"don't know how to configure events : %s\n",
				node->name);
			continue;
		}

		desc[j].ops = &exynos_ppmu_ops;
		desc[j].driver_data = info;

		of_property_read_string(node, "event-name", &desc[j].name);

		j++;

		of_node_put(node);
	}
	info->desc = desc;

	of_node_put(events_np);

	return 0;
}

static int exynos_ppmu_parse_dt(struct exynos_ppmu *info)
{
	struct device *dev = info->dev;
	struct device_node *np = dev->of_node;
	int ret = 0;

	if (!np) {
		dev_err(dev, "failed to find devicetree node\n");
		return -EINVAL;
	}

	/* Maps the memory mapped IO to control PPMU register */
	info->ppmu.base = of_iomap(np, 0);
	if (IS_ERR_OR_NULL(info->ppmu.base)) {
		dev_err(dev, "failed to map memory region\n");
		return -ENOMEM;
	}

	info->ppmu.clk = devm_clk_get(dev, "ppmu");
	if (IS_ERR(info->ppmu.clk)) {
		info->ppmu.clk = NULL;
		dev_warn(dev, "cannot get PPMU clock\n");
	}

	ret = of_get_devfreq_events(np, info);
	if (ret < 0) {
		dev_err(dev, "failed to parse exynos ppmu dt node\n");
		goto err;
	}

	return 0;

err:
	iounmap(info->ppmu.base);

	return ret;
}

static int exynos_ppmu_probe(struct platform_device *pdev)
{
	struct exynos_ppmu *info;
	struct devfreq_event_dev **edev;
	struct devfreq_event_desc *desc;
	int i, ret = 0, size;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;

	/* Parse dt data to get resource */
	ret = exynos_ppmu_parse_dt(info);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to parse devicetree for resource\n");
		return ret;
	}
	desc = info->desc;

	size = sizeof(struct devfreq_event_dev *) * info->num_events;
	info->edev = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!info->edev) {
		dev_err(&pdev->dev,
			"failed to allocate memory devfreq-event devices\n");
		return -ENOMEM;
	}
	edev = info->edev;
	platform_set_drvdata(pdev, info);

	for (i = 0; i < info->num_events; i++) {
		edev[i] = devm_devfreq_event_add_edev(&pdev->dev, &desc[i]);
		if (IS_ERR(edev[i])) {
			ret = PTR_ERR(edev[i]);
			dev_err(&pdev->dev,
				"failed to add devfreq-event device\n");
			goto err;
		}
	}

	clk_prepare_enable(info->ppmu.clk);

	return 0;
err:
	iounmap(info->ppmu.base);

	return ret;
}

static int exynos_ppmu_remove(struct platform_device *pdev)
{
	struct exynos_ppmu *info = platform_get_drvdata(pdev);

	clk_disable_unprepare(info->ppmu.clk);
	iounmap(info->ppmu.base);

	return 0;
}

static struct of_device_id exynos_ppmu_id_match[] = {
	{ .compatible = "samsung,exynos-ppmu", },
	{ /* sentinel */ },
};

static struct platform_driver exynos_ppmu_driver = {
	.probe	= exynos_ppmu_probe,
	.remove	= exynos_ppmu_remove,
	.driver = {
		.name	= "exynos-ppmu",
		.of_match_table = exynos_ppmu_id_match,
	},
};
module_platform_driver(exynos_ppmu_driver);

MODULE_DESCRIPTION("Exynos PPMU(Platform Performance Monitoring Unit) driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL");
