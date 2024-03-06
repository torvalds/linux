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
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "qcom_wcnss.h"

struct qcom_iris {
	struct device dev;

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
		dev_err(&iris->dev, "failed to enable xo clk\n");
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

static const struct of_device_id iris_of_match[] = {
	{ .compatible = "qcom,wcn3620", .data = &wcn3620_data },
	{ .compatible = "qcom,wcn3660", .data = &wcn3660_data },
	{ .compatible = "qcom,wcn3660b", .data = &wcn3680_data },
	{ .compatible = "qcom,wcn3680", .data = &wcn3680_data },
	{}
};

static void qcom_iris_release(struct device *dev)
{
	struct qcom_iris *iris = container_of(dev, struct qcom_iris, dev);

	of_node_put(iris->dev.of_node);
	kfree(iris);
}

struct qcom_iris *qcom_iris_probe(struct device *parent, bool *use_48mhz_xo)
{
	const struct of_device_id *match;
	const struct iris_data *data;
	struct device_node *of_node;
	struct qcom_iris *iris;
	int ret;
	int i;

	of_node = of_get_child_by_name(parent->of_node, "iris");
	if (!of_node) {
		dev_err(parent, "No child node \"iris\" found\n");
		return ERR_PTR(-EINVAL);
	}

	iris = kzalloc(sizeof(*iris), GFP_KERNEL);
	if (!iris) {
		of_node_put(of_node);
		return ERR_PTR(-ENOMEM);
	}

	device_initialize(&iris->dev);
	iris->dev.parent = parent;
	iris->dev.release = qcom_iris_release;
	iris->dev.of_node = of_node;

	dev_set_name(&iris->dev, "%s.iris", dev_name(parent));

	ret = device_add(&iris->dev);
	if (ret) {
		put_device(&iris->dev);
		return ERR_PTR(ret);
	}

	match = of_match_device(iris_of_match, &iris->dev);
	if (!match) {
		dev_err(&iris->dev, "no matching compatible for iris\n");
		ret = -EINVAL;
		goto err_device_del;
	}

	data = match->data;

	iris->xo_clk = devm_clk_get(&iris->dev, "xo");
	if (IS_ERR(iris->xo_clk)) {
		ret = PTR_ERR(iris->xo_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&iris->dev, "failed to acquire xo clk\n");
		goto err_device_del;
	}

	iris->num_vregs = data->num_vregs;
	iris->vregs = devm_kcalloc(&iris->dev,
				   iris->num_vregs,
				   sizeof(struct regulator_bulk_data),
				   GFP_KERNEL);
	if (!iris->vregs) {
		ret = -ENOMEM;
		goto err_device_del;
	}

	for (i = 0; i < iris->num_vregs; i++)
		iris->vregs[i].supply = data->vregs[i].name;

	ret = devm_regulator_bulk_get(&iris->dev, iris->num_vregs, iris->vregs);
	if (ret) {
		dev_err(&iris->dev, "failed to get regulators\n");
		goto err_device_del;
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

	*use_48mhz_xo = data->use_48mhz_xo;

	return iris;

err_device_del:
	device_del(&iris->dev);

	return ERR_PTR(ret);
}

void qcom_iris_remove(struct qcom_iris *iris)
{
	device_del(&iris->dev);
}
