// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP
 */

#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct imx_bus {
	struct devfreq_dev_profile profile;
	struct devfreq *devfreq;
	struct clk *clk;
	struct platform_device *icc_pdev;
};

static int imx_bus_target(struct device *dev,
		unsigned long *freq, u32 flags)
{
	struct dev_pm_opp *new_opp;
	int ret;

	new_opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(new_opp)) {
		ret = PTR_ERR(new_opp);
		dev_err(dev, "failed to get recommended opp: %d\n", ret);
		return ret;
	}
	dev_pm_opp_put(new_opp);

	return dev_pm_opp_set_rate(dev, *freq);
}

static int imx_bus_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct imx_bus *priv = dev_get_drvdata(dev);

	*freq = clk_get_rate(priv->clk);

	return 0;
}

static void imx_bus_exit(struct device *dev)
{
	struct imx_bus *priv = dev_get_drvdata(dev);

	dev_pm_opp_of_remove_table(dev);
	platform_device_unregister(priv->icc_pdev);
}

/* imx_bus_init_icc() - register matching icc provider if required */
static int imx_bus_init_icc(struct device *dev)
{
	struct imx_bus *priv = dev_get_drvdata(dev);
	const char *icc_driver_name;

	if (!of_get_property(dev->of_node, "#interconnect-cells", 0))
		return 0;
	if (!IS_ENABLED(CONFIG_INTERCONNECT_IMX)) {
		dev_warn(dev, "imx interconnect drivers disabled\n");
		return 0;
	}

	icc_driver_name = of_device_get_match_data(dev);
	if (!icc_driver_name) {
		dev_err(dev, "unknown interconnect driver\n");
		return 0;
	}

	priv->icc_pdev = platform_device_register_data(
			dev, icc_driver_name, -1, NULL, 0);
	if (IS_ERR(priv->icc_pdev)) {
		dev_err(dev, "failed to register icc provider %s: %ld\n",
				icc_driver_name, PTR_ERR(priv->icc_pdev));
		return PTR_ERR(priv->icc_pdev);
	}

	return 0;
}

static int imx_bus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imx_bus *priv;
	const char *gov = DEVFREQ_GOV_USERSPACE;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/*
	 * Fetch the clock to adjust but don't explicitly enable.
	 *
	 * For imx bus clock clk_set_rate is safe no matter if the clock is on
	 * or off and some peripheral side-buses might be off unless enabled by
	 * drivers for devices on those specific buses.
	 *
	 * Rate adjustment on a disabled bus clock just takes effect later.
	 */
	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		dev_err(dev, "failed to fetch clk: %d\n", ret);
		return ret;
	}
	platform_set_drvdata(pdev, priv);

	ret = dev_pm_opp_of_add_table(dev);
	if (ret < 0) {
		dev_err(dev, "failed to get OPP table\n");
		return ret;
	}

	priv->profile.target = imx_bus_target;
	priv->profile.exit = imx_bus_exit;
	priv->profile.get_cur_freq = imx_bus_get_cur_freq;
	priv->profile.initial_freq = clk_get_rate(priv->clk);

	priv->devfreq = devm_devfreq_add_device(dev, &priv->profile,
						gov, NULL);
	if (IS_ERR(priv->devfreq)) {
		ret = PTR_ERR(priv->devfreq);
		dev_err(dev, "failed to add devfreq device: %d\n", ret);
		goto err;
	}

	ret = imx_bus_init_icc(dev);
	if (ret)
		goto err;

	return 0;

err:
	dev_pm_opp_of_remove_table(dev);
	return ret;
}

static const struct of_device_id imx_bus_of_match[] = {
	{ .compatible = "fsl,imx8mq-noc", .data = "imx8mq-interconnect", },
	{ .compatible = "fsl,imx8mm-noc", .data = "imx8mm-interconnect", },
	{ .compatible = "fsl,imx8mn-noc", .data = "imx8mn-interconnect", },
	{ .compatible = "fsl,imx8m-noc", },
	{ .compatible = "fsl,imx8m-nic", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx_bus_of_match);

static struct platform_driver imx_bus_platdrv = {
	.probe		= imx_bus_probe,
	.driver = {
		.name	= "imx-bus-devfreq",
		.of_match_table = imx_bus_of_match,
	},
};
module_platform_driver(imx_bus_platdrv);

MODULE_DESCRIPTION("Generic i.MX bus frequency scaling driver");
MODULE_AUTHOR("Leonard Crestez <leonard.crestez@nxp.com>");
MODULE_LICENSE("GPL v2");
