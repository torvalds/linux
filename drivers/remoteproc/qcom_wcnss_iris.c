// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Wireless Connectivity Subsystem Iris driver
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2014 Sony Mobile Communications AB
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "qcom_wcnss.h"

struct qcom_iris {
	struct device *dev;

	struct clk *xo_clk;

	struct regulator_bulk_data *vregs;
	size_t num_vregs;
};

struct iris_data {
	const struct wcnss_vreg_info *vregs;
	size_t num_vregs;

	bool use_48mhz_xo;
};

static const struct iris_data wcn3620_data = {
	.vregs = (struct wcnss_vreg_info[]) {
		{ "vddxo",  1800000, 1800000, 10000 },
		{ "vddrfa", 1300000, 1300000, 100000 },
		{ "vddpa",  3300000, 3300000, 515000 },
		{ "vdddig", 1800000, 1800000, 10000 },
	},
	.num_vregs = 4,
	.use_48mhz_xo = false,
};

static const struct iris_data wcn3660_data = {
	.vregs = (struct wcnss_vreg_info[]) {
		{ "vddxo",  1800000, 1800000, 10000 },
		{ "vddrfa", 1300000, 1300000, 100000 },
		{ "vddpa",  2900000, 3000000, 515000 },
		{ "vdddig", 1200000, 1225000, 10000 },
	},
	.num_vregs = 4,
	.use_48mhz_xo = true,
};

static const struct iris_data wcn3680_data = {
	.vregs = (struct wcnss_vreg_info[]) {
		{ "vddxo",  1800000, 1800000, 10000 },
		{ "vddrfa", 1300000, 1300000, 100000 },
		{ "vddpa",  3300000, 3300000, 515000 },
		{ "vdddig", 1800000, 1800000, 10000 },
	},
	.num_vregs = 4,
	.use_48mhz_xo = true,
};

int qcom_iris_enable(struct qcom_iris *iris)
{
	int ret;

	ret = regulator_bulk_enable(iris->num_vregs, iris->vregs);
	if (ret)
		return ret;

	ret = clk_prepare_enable(iris->xo_clk);
	if (ret) {
		dev_err(iris->dev, "failed to enable xo clk\n");
		goto disable_regulators;
	}

	return 0;

disable_regulators:
	regulator_bulk_disable(iris->num_vregs, iris->vregs);

	return ret;
}

void qcom_iris_disable(struct qcom_iris *iris)
{
	clk_disable_unprepare(iris->xo_clk);
	regulator_bulk_disable(iris->num_vregs, iris->vregs);
}

static int qcom_iris_probe(struct platform_device *pdev)
{
	const struct iris_data *data;
	struct qcom_wcnss *wcnss;
	struct qcom_iris *iris;
	int ret;
	int i;

	iris = devm_kzalloc(&pdev->dev, sizeof(struct qcom_iris), GFP_KERNEL);
	if (!iris)
		return -ENOMEM;

	data = of_device_get_match_data(&pdev->dev);
	wcnss = dev_get_drvdata(pdev->dev.parent);

	iris->xo_clk = devm_clk_get(&pdev->dev, "xo");
	if (IS_ERR(iris->xo_clk)) {
		if (PTR_ERR(iris->xo_clk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to acquire xo clk\n");
		return PTR_ERR(iris->xo_clk);
	}

	iris->num_vregs = data->num_vregs;
	iris->vregs = devm_kcalloc(&pdev->dev,
				   iris->num_vregs,
				   sizeof(struct regulator_bulk_data),
				   GFP_KERNEL);
	if (!iris->vregs)
		return -ENOMEM;

	for (i = 0; i < iris->num_vregs; i++)
		iris->vregs[i].supply = data->vregs[i].name;

	ret = devm_regulator_bulk_get(&pdev->dev, iris->num_vregs, iris->vregs);
	if (ret) {
		dev_err(&pdev->dev, "failed to get regulators\n");
		return ret;
	}

	for (i = 0; i < iris->num_vregs; i++) {
		if (data->vregs[i].max_voltage)
			regulator_set_voltage(iris->vregs[i].consumer,
					      data->vregs[i].min_voltage,
					      data->vregs[i].max_voltage);

		if (data->vregs[i].load_uA)
			regulator_set_load(iris->vregs[i].consumer,
					   data->vregs[i].load_uA);
	}

	qcom_wcnss_assign_iris(wcnss, iris, data->use_48mhz_xo);

	return 0;
}

static int qcom_iris_remove(struct platform_device *pdev)
{
	struct qcom_wcnss *wcnss = dev_get_drvdata(pdev->dev.parent);

	qcom_wcnss_assign_iris(wcnss, NULL, false);

	return 0;
}

static const struct of_device_id iris_of_match[] = {
	{ .compatible = "qcom,wcn3620", .data = &wcn3620_data },
	{ .compatible = "qcom,wcn3660", .data = &wcn3660_data },
	{ .compatible = "qcom,wcn3660b", .data = &wcn3680_data },
	{ .compatible = "qcom,wcn3680", .data = &wcn3680_data },
	{}
};
MODULE_DEVICE_TABLE(of, iris_of_match);

struct platform_driver qcom_iris_driver = {
	.probe = qcom_iris_probe,
	.remove = qcom_iris_remove,
	.driver = {
		.name = "qcom-iris",
		.of_match_table = iris_of_match,
	},
};
