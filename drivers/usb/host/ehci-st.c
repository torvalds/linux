// SPDX-License-Identifier: GPL-2.0
/*
 * ST EHCI driver
 *
 * Copyright (C) 2014 STMicroelectronics – All Rights Reserved
 *
 * Author: Peter Griffin <peter.griffin@linaro.org>
 *
 * Derived from ehci-platform.c
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ehci_pdriver.h>

#include "ehci.h"

#define USB_MAX_CLKS 3

struct st_ehci_platform_priv {
	struct clk *clks[USB_MAX_CLKS];
	struct clk *clk48;
	struct reset_control *rst;
	struct reset_control *pwr;
	struct phy *phy;
};

#define DRIVER_DESC "EHCI STMicroelectronics driver"

#define hcd_to_ehci_priv(h) \
	((struct st_ehci_platform_priv *)hcd_to_ehci(h)->priv)

static const char hcd_name[] = "ehci-st";

#define EHCI_CAPS_SIZE 0x10
#define AHB2STBUS_INSREG01 (EHCI_CAPS_SIZE + 0x84)

static int st_ehci_platform_reset(struct usb_hcd *hcd)
{
	struct platform_device *pdev = to_platform_device(hcd->self.controller);
	struct usb_ehci_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 threshold;

	/* Set EHCI packet buffer IN/OUT threshold to 128 bytes */
	threshold = 128 | (128 << 16);
	writel(threshold, hcd->regs + AHB2STBUS_INSREG01);

	ehci->caps = hcd->regs + pdata->caps_offset;
	return ehci_setup(hcd);
}

static int st_ehci_platform_power_on(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct st_ehci_platform_priv *priv = hcd_to_ehci_priv(hcd);
	int clk, ret;

	ret = reset_control_deassert(priv->pwr);
	if (ret)
		return ret;

	ret = reset_control_deassert(priv->rst);
	if (ret)
		goto err_assert_power;

	/* some SoCs don't have a dedicated 48Mhz clock, but those that do
	   need the rate to be explicitly set */
	if (priv->clk48) {
		ret = clk_set_rate(priv->clk48, 48000000);
		if (ret)
			goto err_assert_reset;
	}

	for (clk = 0; clk < USB_MAX_CLKS && priv->clks[clk]; clk++) {
		ret = clk_prepare_enable(priv->clks[clk]);
		if (ret)
			goto err_disable_clks;
	}

	ret = phy_init(priv->phy);
	if (ret)
		goto err_disable_clks;

	ret = phy_power_on(priv->phy);
	if (ret)
		goto err_exit_phy;

	return 0;

err_exit_phy:
	phy_exit(priv->phy);
err_disable_clks:
	while (--clk >= 0)
		clk_disable_unprepare(priv->clks[clk]);
err_assert_reset:
	reset_control_assert(priv->rst);
err_assert_power:
	reset_control_assert(priv->pwr);

	return ret;
}

static void st_ehci_platform_power_off(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct st_ehci_platform_priv *priv = hcd_to_ehci_priv(hcd);
	int clk;

	reset_control_assert(priv->pwr);

	reset_control_assert(priv->rst);

	phy_power_off(priv->phy);

	phy_exit(priv->phy);

	for (clk = USB_MAX_CLKS - 1; clk >= 0; clk--)
		if (priv->clks[clk])
			clk_disable_unprepare(priv->clks[clk]);

}

static struct hc_driver __read_mostly ehci_platform_hc_driver;

static const struct ehci_driver_overrides platform_overrides __initconst = {
	.reset =		st_ehci_platform_reset,
	.extra_priv_size =	sizeof(struct st_ehci_platform_priv),
};

static struct usb_ehci_pdata ehci_platform_defaults = {
	.power_on =		st_ehci_platform_power_on,
	.power_suspend =	st_ehci_platform_power_off,
	.power_off =		st_ehci_platform_power_off,
};

static int st_ehci_platform_probe(struct platform_device *dev)
{
	struct usb_hcd *hcd;
	struct resource *res_mem;
	struct usb_ehci_pdata *pdata = &ehci_platform_defaults;
	struct st_ehci_platform_priv *priv;
	struct ehci_hcd *ehci;
	int err, irq, clk = 0;

	if (usb_disabled())
		return -ENODEV;

	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		dev_err(&dev->dev, "no irq provided");
		return irq;
	}
	res_mem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res_mem) {
		dev_err(&dev->dev, "no memory resource provided");
		return -ENXIO;
	}

	hcd = usb_create_hcd(&ehci_platform_hc_driver, &dev->dev,
			     dev_name(&dev->dev));
	if (!hcd)
		return -ENOMEM;

	platform_set_drvdata(dev, hcd);
	dev->dev.platform_data = pdata;
	priv = hcd_to_ehci_priv(hcd);
	ehci = hcd_to_ehci(hcd);

	priv->phy = devm_phy_get(&dev->dev, "usb");
	if (IS_ERR(priv->phy)) {
		err = PTR_ERR(priv->phy);
		goto err_put_hcd;
	}

	for (clk = 0; clk < USB_MAX_CLKS; clk++) {
		priv->clks[clk] = of_clk_get(dev->dev.of_node, clk);
		if (IS_ERR(priv->clks[clk])) {
			err = PTR_ERR(priv->clks[clk]);
			if (err == -EPROBE_DEFER)
				goto err_put_clks;
			priv->clks[clk] = NULL;
			break;
		}
	}

	/* some SoCs don't have a dedicated 48Mhz clock, but those that
	   do need the rate to be explicitly set */
	priv->clk48 = devm_clk_get(&dev->dev, "clk48");
	if (IS_ERR(priv->clk48)) {
		dev_info(&dev->dev, "48MHz clk not found\n");
		priv->clk48 = NULL;
	}

	priv->pwr =
		devm_reset_control_get_optional_shared(&dev->dev, "power");
	if (IS_ERR(priv->pwr)) {
		err = PTR_ERR(priv->pwr);
		if (err == -EPROBE_DEFER)
			goto err_put_clks;
		priv->pwr = NULL;
	}

	priv->rst =
		devm_reset_control_get_optional_shared(&dev->dev, "softreset");
	if (IS_ERR(priv->rst)) {
		err = PTR_ERR(priv->rst);
		if (err == -EPROBE_DEFER)
			goto err_put_clks;
		priv->rst = NULL;
	}

	if (pdata->power_on) {
		err = pdata->power_on(dev);
		if (err < 0)
			goto err_put_clks;
	}

	hcd->rsrc_start = res_mem->start;
	hcd->rsrc_len = resource_size(res_mem);

	hcd->regs = devm_ioremap_resource(&dev->dev, res_mem);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		goto err_put_clks;
	}

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err)
		goto err_put_clks;

	device_wakeup_enable(hcd->self.controller);
	platform_set_drvdata(dev, hcd);

	return err;

err_put_clks:
	while (--clk >= 0)
		clk_put(priv->clks[clk]);
err_put_hcd:
	if (pdata == &ehci_platform_defaults)
		dev->dev.platform_data = NULL;

	usb_put_hcd(hcd);

	return err;
}

static int st_ehci_platform_remove(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct usb_ehci_pdata *pdata = dev_get_platdata(&dev->dev);
	struct st_ehci_platform_priv *priv = hcd_to_ehci_priv(hcd);
	int clk;

	usb_remove_hcd(hcd);

	if (pdata->power_off)
		pdata->power_off(dev);

	for (clk = 0; clk < USB_MAX_CLKS && priv->clks[clk]; clk++)
		clk_put(priv->clks[clk]);

	usb_put_hcd(hcd);

	if (pdata == &ehci_platform_defaults)
		dev->dev.platform_data = NULL;

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int st_ehci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct usb_ehci_pdata *pdata = dev_get_platdata(dev);
	struct platform_device *pdev = to_platform_device(dev);
	bool do_wakeup = device_may_wakeup(dev);
	int ret;

	ret = ehci_suspend(hcd, do_wakeup);
	if (ret)
		return ret;

	if (pdata->power_suspend)
		pdata->power_suspend(pdev);

	pinctrl_pm_select_sleep_state(dev);

	return ret;
}

static int st_ehci_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct usb_ehci_pdata *pdata = dev_get_platdata(dev);
	struct platform_device *pdev = to_platform_device(dev);
	int err;

	pinctrl_pm_select_default_state(dev);

	if (pdata->power_on) {
		err = pdata->power_on(pdev);
		if (err < 0)
			return err;
	}

	ehci_resume(hcd, false);
	return 0;
}

static SIMPLE_DEV_PM_OPS(st_ehci_pm_ops, st_ehci_suspend, st_ehci_resume);

#endif /* CONFIG_PM_SLEEP */

static const struct of_device_id st_ehci_ids[] = {
	{ .compatible = "st,st-ehci-300x", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, st_ehci_ids);

static struct platform_driver ehci_platform_driver = {
	.probe		= st_ehci_platform_probe,
	.remove		= st_ehci_platform_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= "st-ehci",
#ifdef CONFIG_PM_SLEEP
		.pm	= &st_ehci_pm_ops,
#endif
		.of_match_table = st_ehci_ids,
	}
};

static int __init ehci_platform_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ehci_init_driver(&ehci_platform_hc_driver, &platform_overrides);
	return platform_driver_register(&ehci_platform_driver);
}
module_init(ehci_platform_init);

static void __exit ehci_platform_cleanup(void)
{
	platform_driver_unregister(&ehci_platform_driver);
}
module_exit(ehci_platform_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Peter Griffin <peter.griffin@linaro.org>");
MODULE_LICENSE("GPL");
