/*
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/usb/chipidea.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ulpi.h>

#include "ci.h"

struct ci_hdrc_usb2_priv {
	struct platform_device	*ci_pdev;
	struct clk		*clk;
};

static const struct ci_hdrc_platform_data ci_default_pdata = {
	.capoffset	= DEF_CAPOFFSET,
	.flags		= CI_HDRC_DISABLE_STREAMING,
};

static struct ci_hdrc_platform_data ci_zynq_pdata = {
	.capoffset	= DEF_CAPOFFSET,
};

static const struct of_device_id ci_hdrc_usb2_of_match[] = {
	{ .compatible = "chipidea,usb2"},
	{ .compatible = "xlnx,zynq-usb-2.20a", .data = &ci_zynq_pdata},
	{ }
};
MODULE_DEVICE_TABLE(of, ci_hdrc_usb2_of_match);

static int ci_hdrc_usb2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ci_hdrc_usb2_priv *priv;
	struct ci_hdrc_platform_data *ci_pdata = dev_get_platdata(dev);
	int ret;
	const struct of_device_id *match;

	if (!ci_pdata) {
		ci_pdata = devm_kmalloc(dev, sizeof(*ci_pdata), GFP_KERNEL);
		*ci_pdata = ci_default_pdata;	/* struct copy */
	}

	match = of_match_device(ci_hdrc_usb2_of_match, &pdev->dev);
	if (match && match->data) {
		/* struct copy */
		*ci_pdata = *(struct ci_hdrc_platform_data *)match->data;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (!IS_ERR(priv->clk)) {
		ret = clk_prepare_enable(priv->clk);
		if (ret) {
			dev_err(dev, "failed to enable the clock: %d\n", ret);
			return ret;
		}
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		goto clk_err;

	ci_pdata->name = dev_name(dev);

	priv->ci_pdev = ci_hdrc_add_device(dev, pdev->resource,
					   pdev->num_resources, ci_pdata);
	if (IS_ERR(priv->ci_pdev)) {
		ret = PTR_ERR(priv->ci_pdev);
		if (ret != -EPROBE_DEFER)
			dev_err(dev,
				"failed to register ci_hdrc platform device: %d\n",
				ret);
		goto clk_err;
	}

	platform_set_drvdata(pdev, priv);

	pm_runtime_no_callbacks(dev);
	pm_runtime_enable(dev);

	return 0;

clk_err:
	if (!IS_ERR(priv->clk))
		clk_disable_unprepare(priv->clk);
	return ret;
}

static int ci_hdrc_usb2_remove(struct platform_device *pdev)
{
	struct ci_hdrc_usb2_priv *priv = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	ci_hdrc_remove_device(priv->ci_pdev);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static struct platform_driver ci_hdrc_usb2_driver = {
	.probe	= ci_hdrc_usb2_probe,
	.remove	= ci_hdrc_usb2_remove,
	.driver	= {
		.name		= "chipidea-usb2",
		.of_match_table	= of_match_ptr(ci_hdrc_usb2_of_match),
	},
};
module_platform_driver(ci_hdrc_usb2_driver);

MODULE_DESCRIPTION("ChipIdea HDRC USB2 binding for ci13xxx");
MODULE_AUTHOR("Antoine Tenart <antoine.tenart@free-electrons.com>");
MODULE_LICENSE("GPL");
