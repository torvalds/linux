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

#define PPMU_ENABLE             BIT(0)
#define PPMU_DISABLE            0x0
#define PPMU_CYCLE_RESET        BIT(1)
#define PPMU_COUNTER_RESET      BIT(2)

#define PPMU_ENABLE_COUNT0      BIT(0)
#define PPMU_ENABLE_COUNT1      BIT(1)
#define PPMU_ENABLE_COUNT2      BIT(2)
#define PPMU_ENABLE_COUNT3      BIT(3)
#define PPMU_ENABLE_CYCLE       BIT(31)

#define PPMU_CNTENS		0x10
#define PPMU_CNTENC		0x20
#define PPMU_FLAG		0x50
#define PPMU_CCNT_OVERFLOW	BIT(31)
#define PPMU_CCNT		0x100

#define PPMU_PMCNT0		0x110
#define PPMU_PMCNT_OFFSET	0x10
#define PMCNT_OFFSET(x)		(PPMU_PMCNT0 + (PPMU_PMCNT_OFFSET * x))

#define PPMU_BEVT0SEL		0x1000
#define PPMU_BEVTSEL_OFFSET	0x100
#define PPMU_BEVTSEL(x)		(PPMU_BEVT0SEL + (x * PPMU_BEVTSEL_OFFSET))

#define RD_DATA_COUNT		0x5
#define WR_DATA_COUNT		0x6
#define RDWR_DATA_COUNT		0x7

enum ppmu_counter {
	PPMU_PMNCNT0 = 0,
	PPMU_PMNCNT1,
	PPMU_PMNCNT2,
	PPMU_PMNCNT3,

	PPMU_PMNCNT_MAX,
};

struct exynos_ppmu_data {
	struct devfreq_event_dev **edev;
	struct devfreq_event_desc *desc;
	unsigned int num_events;

	struct device *dev;
	struct clk *clk_ppmu;
	struct mutex lock;

	struct __exynos_ppmu {
		void __iomem *base;
		unsigned int event[PPMU_PMNCNT_MAX];
		unsigned int count[PPMU_PMNCNT_MAX];
		bool ccnt_overflow;
		bool count_overflow[PPMU_PMNCNT_MAX];
	} ppmu;
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

static int exynos_ppmu_reset(struct devfreq_event_dev *edev)
{
	struct exynos_ppmu_data *data = devfreq_event_get_drvdata(edev);

	__raw_writel(PPMU_CYCLE_RESET | PPMU_COUNTER_RESET, data->ppmu.base);
	__raw_writel(PPMU_ENABLE_CYCLE  |
		     PPMU_ENABLE_COUNT0 |
		     PPMU_ENABLE_COUNT1 |
		     PPMU_ENABLE_COUNT2 |
		     PPMU_ENABLE_COUNT3,
		     data->ppmu.base + PPMU_CNTENS);

	return 0;
}

static int exynos_ppmu_disable(struct devfreq_event_dev *edev)
{
	struct exynos_ppmu_data *data = devfreq_event_get_drvdata(edev);

	/* Reset the performance and cycle counters */
	exynos_ppmu_reset(edev);

	/* Disable event of PPMU */
	__raw_writel(PPMU_ENABLE_CYCLE  |
		     PPMU_ENABLE_COUNT0 |
		     PPMU_ENABLE_COUNT1 |
		     PPMU_ENABLE_COUNT2 |
		     PPMU_ENABLE_COUNT3,
		     data->ppmu.base + PPMU_CNTENC);

	/* Stop monitoring of PPMU IP */
	__raw_writel(PPMU_DISABLE, data->ppmu.base);

	return 0;
}

static int exynos_ppmu_set_event(struct devfreq_event_dev *edev)
{
	struct exynos_ppmu_data *data = devfreq_event_get_drvdata(edev);
	int id = exynos_ppmu_find_ppmu_id(edev);

	if (id < 0)
		return id;

	/* Reset the performance and cycle counters */
	exynos_ppmu_reset(edev);

	/* Setup count registers to monitor read/write transactions */
	__raw_writel(RDWR_DATA_COUNT, data->ppmu.base + PPMU_BEVTSEL(id));

	/* Start monitoring of PPMU IP */
	__raw_writel(PPMU_ENABLE, data->ppmu.base);

	return 0;
}

static int exynos_ppmu_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata)
{
	struct exynos_ppmu_data *data = devfreq_event_get_drvdata(edev);
	int id = exynos_ppmu_find_ppmu_id(edev);

	if (id < 0)
		return -EINVAL;

	/* Stop monitoring of PPMU IP */
	__raw_writel(PPMU_DISABLE, data->ppmu.base);

	/* Read total count cycle from of PPMU IP */
	edata->total_event = __raw_readl(data->ppmu.base + PPMU_CCNT);

	if (id == PPMU_PMNCNT3)
		edata->event =
			((__raw_readl(data->ppmu.base + PMCNT_OFFSET(id)) << 8)
			| __raw_readl(data->ppmu.base + PMCNT_OFFSET(id + 1)));
	else
		edata->event = __raw_readl(data->ppmu.base + PMCNT_OFFSET(id));

	dev_dbg(&edev->dev, "%s (event: %ld/%ld)\n", edev->desc->name,
					edata->event, edata->total_event);

	return 0;
}

static struct devfreq_event_ops exynos_ppmu_ops = {
	.disable = exynos_ppmu_disable,
	.set_event = exynos_ppmu_set_event,
	.get_event = exynos_ppmu_get_event,
};

static int of_get_devfreq_events(struct device_node *np,
				 struct exynos_ppmu_data *data)
{
	struct devfreq_event_desc *desc;
	struct device *dev = data->dev;
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
	data->num_events = count;

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
		desc[j].driver_data = data;

		of_property_read_string(node, "event-name", &desc[j].name);

		j++;

		of_node_put(node);
	}
	data->desc = desc;

	of_node_put(events_np);

	return 0;
}

static int exynos_ppmu_parse_dt(struct exynos_ppmu_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;
	int ret = 0;

	if (!np) {
		dev_err(dev, "failed to find devicetree node\n");
		return -EINVAL;
	}

	/* Maps the memory mapped IO to control PPMU register */
	data->ppmu.base = of_iomap(np, 0);
	if (IS_ERR_OR_NULL(data->ppmu.base)) {
		dev_err(dev, "failed to map memory region\n");
		return -ENOMEM;
	}

	data->clk_ppmu = devm_clk_get(dev, "ppmu");
	if (IS_ERR(data->clk_ppmu)) {
		data->clk_ppmu = NULL;
		dev_warn(dev, "cannot get PPMU clock\n");
	}

	ret = of_get_devfreq_events(np, data);
	if (ret < 0) {
		dev_err(dev, "failed to parse exynos ppmu dt node\n");
		goto err;
	}

	return 0;

err:
	iounmap(data->ppmu.base);

	return ret;
}

static int exynos_ppmu_probe(struct platform_device *pdev)
{
	struct exynos_ppmu_data *data;
	struct devfreq_event_dev **edev;
	struct devfreq_event_desc *desc;
	int i, ret = 0, size;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->lock);
	data->dev = &pdev->dev;

	/* Parse dt data to get resource */
	ret = exynos_ppmu_parse_dt(data);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to parse devicetree for resource\n");
		return ret;
	}
	desc = data->desc;

	size = sizeof(struct devfreq_event_dev *) * data->num_events;
	data->edev = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!data->edev) {
		dev_err(&pdev->dev,
			"failed to allocate memory devfreq-event devices\n");
		return -ENOMEM;
	}
	edev = data->edev;
	platform_set_drvdata(pdev, data);

	for (i = 0; i < data->num_events; i++) {
		edev[i] = devm_devfreq_event_add_edev(&pdev->dev, &desc[i]);
		if (IS_ERR(edev)) {
			ret = PTR_ERR(edev);
			dev_err(&pdev->dev,
				"failed to add devfreq-event device\n");
			goto err;
		}
	}

	clk_prepare_enable(data->clk_ppmu);

	return 0;
err:
	iounmap(data->ppmu.base);

	return ret;
}

static int exynos_ppmu_remove(struct platform_device *pdev)
{
	struct exynos_ppmu_data *data = platform_get_drvdata(pdev);

	clk_disable_unprepare(data->clk_ppmu);
	iounmap(data->ppmu.base);

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
