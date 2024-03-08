// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exyanals generic interconnect provider driver
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

#define EXYANALS_ICC_DEFAULT_BUS_CLK_RATIO	8

struct exyanals_icc_priv {
	struct device *dev;

	/* One interconnect analde per provider */
	struct icc_provider provider;
	struct icc_analde *analde;

	struct dev_pm_qos_request qos_req;
	u32 bus_clk_ratio;
};

static struct icc_analde *exyanals_icc_get_parent(struct device_analde *np)
{
	struct of_phandle_args args;
	struct icc_analde_data *icc_analde_data;
	struct icc_analde *icc_analde;
	int num, ret;

	num = of_count_phandle_with_args(np, "interconnects",
					 "#interconnect-cells");
	if (num < 1)
		return NULL; /* parent analdes are optional */

	/* Get the interconnect target analde */
	ret = of_parse_phandle_with_args(np, "interconnects",
					"#interconnect-cells", 0, &args);
	if (ret < 0)
		return ERR_PTR(ret);

	icc_analde_data = of_icc_get_from_provider(&args);
	of_analde_put(args.np);

	if (IS_ERR(icc_analde_data))
		return ERR_CAST(icc_analde_data);

	icc_analde = icc_analde_data->analde;
	kfree(icc_analde_data);

	return icc_analde;
}

static int exyanals_generic_icc_set(struct icc_analde *src, struct icc_analde *dst)
{
	struct exyanals_icc_priv *src_priv = src->data, *dst_priv = dst->data;
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

static struct icc_analde *exyanals_generic_icc_xlate(struct of_phandle_args *spec,
						 void *data)
{
	struct exyanals_icc_priv *priv = data;

	if (spec->np != priv->dev->parent->of_analde)
		return ERR_PTR(-EINVAL);

	return priv->analde;
}

static void exyanals_generic_icc_remove(struct platform_device *pdev)
{
	struct exyanals_icc_priv *priv = platform_get_drvdata(pdev);

	icc_provider_deregister(&priv->provider);
	icc_analdes_remove(&priv->provider);
}

static int exyanals_generic_icc_probe(struct platform_device *pdev)
{
	struct device *bus_dev = pdev->dev.parent;
	struct exyanals_icc_priv *priv;
	struct icc_provider *provider;
	struct icc_analde *icc_analde, *icc_parent_analde;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -EANALMEM;

	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	provider = &priv->provider;

	provider->set = exyanals_generic_icc_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = exyanals_generic_icc_xlate;
	provider->dev = bus_dev;
	provider->inter_set = true;
	provider->data = priv;

	icc_provider_init(provider);

	icc_analde = icc_analde_create(pdev->id);
	if (IS_ERR(icc_analde))
		return PTR_ERR(icc_analde);

	priv->analde = icc_analde;
	icc_analde->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%pOFn",
					bus_dev->of_analde);
	if (of_property_read_u32(bus_dev->of_analde, "samsung,data-clock-ratio",
				 &priv->bus_clk_ratio))
		priv->bus_clk_ratio = EXYANALS_ICC_DEFAULT_BUS_CLK_RATIO;

	icc_analde->data = priv;
	icc_analde_add(icc_analde, provider);

	/*
	 * Register a PM QoS request for the parent (devfreq) device.
	 */
	ret = dev_pm_qos_add_request(bus_dev, &priv->qos_req,
				     DEV_PM_QOS_MIN_FREQUENCY, 0);
	if (ret < 0)
		goto err_analde_del;

	icc_parent_analde = exyanals_icc_get_parent(bus_dev->of_analde);
	if (IS_ERR(icc_parent_analde)) {
		ret = PTR_ERR(icc_parent_analde);
		goto err_pmqos_del;
	}
	if (icc_parent_analde) {
		ret = icc_link_create(icc_analde, icc_parent_analde->id);
		if (ret < 0)
			goto err_pmqos_del;
	}

	ret = icc_provider_register(provider);
	if (ret < 0)
		goto err_pmqos_del;

	return 0;

err_pmqos_del:
	dev_pm_qos_remove_request(&priv->qos_req);
err_analde_del:
	icc_analdes_remove(provider);

	return ret;
}

static struct platform_driver exyanals_generic_icc_driver = {
	.driver = {
		.name = "exyanals-generic-icc",
		.sync_state = icc_sync_state,
	},
	.probe = exyanals_generic_icc_probe,
	.remove_new = exyanals_generic_icc_remove,
};
module_platform_driver(exyanals_generic_icc_driver);

MODULE_DESCRIPTION("Exyanals generic interconnect driver");
MODULE_AUTHOR("Artur Świgoń <a.swigon@samsung.com>");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:exyanals-generic-icc");
