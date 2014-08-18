/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Marek Vasut <marex@denx.de>
 * on behalf of DENX Software Engineering GmbH
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/usb/chipidea.h>
#include <linux/clk.h>

#include "ci.h"
#include "ci_hdrc_imx.h"

#define CI_HDRC_IMX_IMX28_WRITE_FIX BIT(0)

struct ci_hdrc_imx_platform_flag {
	unsigned int flags;
};

static const struct ci_hdrc_imx_platform_flag imx27_usb_data = {
};

static const struct ci_hdrc_imx_platform_flag imx28_usb_data = {
	.flags = CI_HDRC_IMX_IMX28_WRITE_FIX,
};

static const struct of_device_id ci_hdrc_imx_dt_ids[] = {
	{ .compatible = "fsl,imx28-usb", .data = &imx28_usb_data},
	{ .compatible = "fsl,imx27-usb", .data = &imx27_usb_data},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ci_hdrc_imx_dt_ids);

struct ci_hdrc_imx_data {
	struct usb_phy *phy;
	struct platform_device *ci_pdev;
	struct clk *clk;
	struct imx_usbmisc_data *usbmisc_data;
};

/* Common functions shared by usbmisc drivers */

static struct imx_usbmisc_data *usbmisc_get_init_data(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct of_phandle_args args;
	struct imx_usbmisc_data *data;
	int ret;

	/*
	 * In case the fsl,usbmisc property is not present this device doesn't
	 * need usbmisc. Return NULL (which is no error here)
	 */
	if (!of_get_property(np, "fsl,usbmisc", NULL))
		return NULL;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	ret = of_parse_phandle_with_args(np, "fsl,usbmisc", "#index-cells",
					0, &args);
	if (ret) {
		dev_err(dev, "Failed to parse property fsl,usbmisc, errno %d\n",
			ret);
		return ERR_PTR(ret);
	}

	data->index = args.args[0];
	of_node_put(args.np);

	if (of_find_property(np, "disable-over-current", NULL))
		data->disable_oc = 1;

	if (of_find_property(np, "external-vbus-divider", NULL))
		data->evdo = 1;

	return data;
}

/* End of common functions shared by usbmisc drivers*/

static int ci_hdrc_imx_probe(struct platform_device *pdev)
{
	struct ci_hdrc_imx_data *data;
	struct ci_hdrc_platform_data pdata = {
		.name		= dev_name(&pdev->dev),
		.capoffset	= DEF_CAPOFFSET,
		.flags		= CI_HDRC_REQUIRE_TRANSCEIVER |
				  CI_HDRC_DISABLE_STREAMING,
	};
	int ret;
	const struct of_device_id *of_id =
			of_match_device(ci_hdrc_imx_dt_ids, &pdev->dev);
	const struct ci_hdrc_imx_platform_flag *imx_platform_flag = of_id->data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate ci_hdrc-imx data!\n");
		return -ENOMEM;
	}

	data->usbmisc_data = usbmisc_get_init_data(&pdev->dev);
	if (IS_ERR(data->usbmisc_data))
		return PTR_ERR(data->usbmisc_data);

	data->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(data->clk)) {
		dev_err(&pdev->dev,
			"Failed to get clock, err=%ld\n", PTR_ERR(data->clk));
		return PTR_ERR(data->clk);
	}

	ret = clk_prepare_enable(data->clk);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to prepare or enable clock, err=%d\n", ret);
		return ret;
	}

	data->phy = devm_usb_get_phy_by_phandle(&pdev->dev, "fsl,usbphy", 0);
	if (IS_ERR(data->phy)) {
		ret = PTR_ERR(data->phy);
		/* Return -EINVAL if no usbphy is available */
		if (ret == -ENODEV)
			ret = -EINVAL;
		goto err_clk;
	}

	pdata.phy = data->phy;

	if (imx_platform_flag->flags & CI_HDRC_IMX_IMX28_WRITE_FIX)
		pdata.flags |= CI_HDRC_IMX28_WRITE_FIX;

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto err_clk;

	if (data->usbmisc_data) {
		ret = imx_usbmisc_init(data->usbmisc_data);
		if (ret) {
			dev_err(&pdev->dev, "usbmisc init failed, ret=%d\n",
					ret);
			goto err_clk;
		}
	}

	data->ci_pdev = ci_hdrc_add_device(&pdev->dev,
				pdev->resource, pdev->num_resources,
				&pdata);
	if (IS_ERR(data->ci_pdev)) {
		ret = PTR_ERR(data->ci_pdev);
		dev_err(&pdev->dev,
			"Can't register ci_hdrc platform device, err=%d\n",
			ret);
		goto err_clk;
	}

	if (data->usbmisc_data) {
		ret = imx_usbmisc_init_post(data->usbmisc_data);
		if (ret) {
			dev_err(&pdev->dev, "usbmisc post failed, ret=%d\n",
					ret);
			goto disable_device;
		}
	}

	platform_set_drvdata(pdev, data);

	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

disable_device:
	ci_hdrc_remove_device(data->ci_pdev);
err_clk:
	clk_disable_unprepare(data->clk);
	return ret;
}

static int ci_hdrc_imx_remove(struct platform_device *pdev)
{
	struct ci_hdrc_imx_data *data = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	ci_hdrc_remove_device(data->ci_pdev);
	clk_disable_unprepare(data->clk);

	return 0;
}

static struct platform_driver ci_hdrc_imx_driver = {
	.probe = ci_hdrc_imx_probe,
	.remove = ci_hdrc_imx_remove,
	.driver = {
		.name = "imx_usb",
		.owner = THIS_MODULE,
		.of_match_table = ci_hdrc_imx_dt_ids,
	 },
};

module_platform_driver(ci_hdrc_imx_driver);

MODULE_ALIAS("platform:imx-usb");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CI HDRC i.MX USB binding");
MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_AUTHOR("Richard Zhao <richard.zhao@freescale.com>");
