// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exynos generic interconnect provider driver
 *
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 *
 * Authors: Artur Świgoń <a.swigon@samsung.com>
 *          Sylwester Nawrocki <s.nawrocki@samsung.com>
 */
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>

#define EXYNOS_ICC_DEFAULT_BUS_CLK_RATIO	8

struct exynos_icc_priv {
	struct device *dev;

	/* One interconnect node per provider */
	struct icc_provider provider;
	struct icc_node *node;

	struct dev_pm_qos_request qos_req;
	u32 bus_clk_ratio;
};

static struct icc_node *exynos_icc_get_parent(struct device_node *np)
{
	struct of_phandle_args args;
	struct icc_node_data *icc_node_data;
	struct icc_node *icc_node;
	int num, ret;

	num = of_count_phandle_with_args(np, "interconnects",
					 "#interconnect-cells");
	if (num < 1)
		return NULL; /* parent nodes are optional */

	/* Get the interconnect target node */
	ret = of_parse_phandle_with_args(np, "interconnects",
					"#interconnect-cells", 0, &args);
	if (ret < 0)
		return ERR_PTR(ret);

	icc_node_data = of_icc_get_from_provider(&args);
	of_node_put(args.np);

	if (IS_ERR(icc_node_data))
		return ERR_CAST(icc_node_data);

	icc_node = icc_node_data->node;
	kfree(icc_node_data);

	return icc_node;
}

static int exynos_generic_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct exynos_icc_priv *src_priv = src->data, *dst_priv = dst->data;
	s32 src_freq = max(src->avg_bw, src->peak_bw) / src_priv->bus_clk_ratio;
	s32 dst_freq = max(dst->avg_bw, dst->peak_bw) / dst_priv->bus_clk_ratio;
	int ret;

	ret = dev_pm_qos_update_request(&src_priv->qos_req, src_freq);
	if (ret < 0) {
		dev_err(src_priv->dev, "failed to update PM QoS of %s (src)\n",
			src->name);
		return ret;
	}

	ret = dev_pm_qos_update_request(&dst_priv->qos_req, dst_freq);
	if (ret < 0) {
		dev_err(dst_priv->dev, "failed to update PM QoS of %s (dst)\n",
			dst->name);
		return ret;
	}

	return 0;
}

static struct icc_node *exynos_generic_icc_xlate(const struct of_phandle_args *spec,
						 void *data)
{
	struct exynos_icc_priv *priv = data;

	if (spec->np != priv->dev->parent->of_node)
		return ERR_PTR(-EINVAL);

	return priv->node;
}

static void exynos_generic_icc_remove(struct platform_device *pdev)
{
	struct exynos_icc_priv *priv = platform_get_drvdata(pdev);

	icc_provider_deregister(&priv->provider);
	icc_nodes_remove(&priv->provider);
}

static int exynos_generic_icc_probe(struct platform_device *pdev)
{
	struct device *bus_dev = pdev->dev.parent;
	struct exynos_icc_priv *priv;
	struct icc_provider *provider;
	struct icc_node *icc_node, *icc_parent_node;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	provider = &priv->provider;

	provider->set = exynos_generic_icc_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = exynos_generic_icc_xlate;
	provider->dev = bus_dev;
	provider->inter_set = true;
	provider->data = priv;

	icc_provider_init(provider);

	icc_node = icc_node_create(pdev->id);
	if (IS_ERR(icc_node))
		return PTR_ERR(icc_node);

	priv->node = icc_node;
	icc_node->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%pOFn",
					bus_dev->of_node);
	if (!icc_node->name) {
		icc_node_destroy(pdev->id);
		return -ENOMEM;
	}

	if (of_property_read_u32(bus_dev->of_node, "samsung,data-clock-ratio",
				 &priv->bus_clk_ratio))
		priv->bus_clk_ratio = EXYNOS_ICC_DEFAULT_BUS_CLK_RATIO;

	icc_node->data = priv;
	icc_node_add(icc_node, provider);

	/*
	 * Register a PM QoS request for the parent (devfreq) device.
	 */
	ret = dev_pm_qos_add_request(bus_dev, &priv->qos_req,
				     DEV_PM_QOS_MIN_FREQUENCY, 0);
	if (ret < 0)
		goto err_node_del;

	icc_parent_node = exynos_icc_get_parent(bus_dev->of_node);
	if (IS_ERR(icc_parent_node)) {
		ret = PTR_ERR(icc_parent_node);
		goto err_pmqos_del;
	}
	if (icc_parent_node) {
		ret = icc_link_create(icc_node, icc_parent_node->id);
		if (ret < 0)
			goto err_pmqos_del;
	}

	ret = icc_provider_register(provider);
	if (ret < 0)
		goto err_pmqos_del;

	return 0;

err_pmqos_del:
	dev_pm_qos_remove_request(&priv->qos_req);
err_node_del:
	icc_nodes_remove(provider);

	return ret;
}

static struct platform_driver exynos_generic_icc_driver = {
	.driver = {
		.name = "exynos-generic-icc",
		.sync_state = icc_sync_state,
	},
	.probe = exynos_generic_icc_probe,
	.remove = exynos_generic_icc_remove,
};
module_platform_driver(exynos_generic_icc_driver);

MODULE_DESCRIPTION("Exynos generic interconnect driver");
MODULE_AUTHOR("Artur Świgoń <a.swigon@samsung.com>");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:exynos-generic-icc");
