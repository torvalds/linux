/*
 * Generic platform ohci driver
 *
 * Copyright 2007 Michael Buesch <m@bues.ch>
 * Copyright 2011-2012 Hauke Mehrtens <hauke@hauke-m.de>
 * Copyright 2014 Hans de Goede <hdegoede@redhat.com>
 *
 * Derived from the OCHI-SSB driver
 * Derived from the OHCI-PCI driver
 * Copyright 1999 Roman Weissgaerber
 * Copyright 2000-2002 David Brownell
 * Copyright 1999 Linus Torvalds
 * Copyright 1999 Gregory P. Smith
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/usb/ohci_pdriver.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ohci.h"

#define DRIVER_DESC "OHCI generic platform driver"
#define OHCI_MAX_CLKS 3
#define hcd_to_ohci_priv(h) ((struct ohci_platform_priv *)hcd_to_ohci(h)->priv)

struct ohci_platform_priv {
	struct clk *clks[OHCI_MAX_CLKS];
	struct reset_control *rst;
	struct phy *phy;
};

static const char hcd_name[] = "ohci-platform";

static int ohci_platform_power_on(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct ohci_platform_priv *priv = hcd_to_ohci_priv(hcd);
	int clk, ret;

	for (clk = 0; clk < OHCI_MAX_CLKS && priv->clks[clk]; clk++) {
		ret = clk_prepare_enable(priv->clks[clk]);
		if (ret)
			goto err_disable_clks;
	}

	if (priv->phy) {
		ret = phy_init(priv->phy);
		if (ret)
			goto err_disable_clks;

		ret = phy_power_on(priv->phy);
		if (ret)
			goto err_exit_phy;
	}

	return 0;

err_exit_phy:
	phy_exit(priv->phy);
err_disable_clks:
	while (--clk >= 0)
		clk_disable_unprepare(priv->clks[clk]);

	return ret;
}

static void ohci_platform_power_off(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct ohci_platform_priv *priv = hcd_to_ohci_priv(hcd);
	int clk;

	if (priv->phy) {
		phy_power_off(priv->phy);
		phy_exit(priv->phy);
	}

	for (clk = OHCI_MAX_CLKS - 1; clk >= 0; clk--)
		if (priv->clks[clk])
			clk_disable_unprepare(priv->clks[clk]);
}

static struct hc_driver __read_mostly ohci_platform_hc_driver;

static const struct ohci_driver_overrides platform_overrides __initconst = {
	.product_desc =		"Generic Platform OHCI controller",
	.extra_priv_size =	sizeof(struct ohci_platform_priv),
};

static struct usb_ohci_pdata ohci_platform_defaults = {
	.power_on =		ohci_platform_power_on,
	.power_suspend =	ohci_platform_power_off,
	.power_off =		ohci_platform_power_off,
};

static int ohci_platform_probe(struct platform_device *dev)
{
	struct usb_hcd *hcd;
	struct resource *res_mem;
	struct usb_ohci_pdata *pdata = dev_get_platdata(&dev->dev);
	struct ohci_platform_priv *priv;
	struct ohci_hcd *ohci;
	int err, irq, clk = 0;

	if (usb_disabled())
		return -ENODEV;

	/*
	 * Use reasonable defaults so platforms don't have to provide these
	 * with DT probing on ARM.
	 */
	if (!pdata)
		pdata = &ohci_platform_defaults;

	err = dma_coerce_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		dev_err(&dev->dev, "no irq provided");
		return irq;
	}

	hcd = usb_create_hcd(&ohci_platform_hc_driver, &dev->dev,
			dev_name(&dev->dev));
	if (!hcd)
		return -ENOMEM;

	platform_set_drvdata(dev, hcd);
	dev->dev.platform_data = pdata;
	priv = hcd_to_ohci_priv(hcd);
	ohci = hcd_to_ohci(hcd);

	if (pdata == &ohci_platform_defaults && dev->dev.of_node) {
		if (of_property_read_bool(dev->dev.of_node, "big-endian-regs"))
			ohci->flags |= OHCI_QUIRK_BE_MMIO;

		if (of_property_read_bool(dev->dev.of_node, "big-endian-desc"))
			ohci->flags |= OHCI_QUIRK_BE_DESC;

		if (of_property_read_bool(dev->dev.of_node, "big-endian"))
			ohci->flags |= OHCI_QUIRK_BE_MMIO | OHCI_QUIRK_BE_DESC;

		if (of_property_read_bool(dev->dev.of_node, "no-big-frame-no"))
			ohci->flags |= OHCI_QUIRK_FRAME_NO;

		of_property_read_u32(dev->dev.of_node, "num-ports",
				     &ohci->num_ports);

		priv->phy = devm_phy_get(&dev->dev, "usb");
		if (IS_ERR(priv->phy)) {
			err = PTR_ERR(priv->phy);
			if (err == -EPROBE_DEFER)
				goto err_put_hcd;
			priv->phy = NULL;
		}

		for (clk = 0; clk < OHCI_MAX_CLKS; clk++) {
			priv->clks[clk] = of_clk_get(dev->dev.of_node, clk);
			if (IS_ERR(priv->clks[clk])) {
				err = PTR_ERR(priv->clks[clk]);
				if (err == -EPROBE_DEFER)
					goto err_put_clks;
				priv->clks[clk] = NULL;
				break;
			}
		}

	}

	priv->rst = devm_reset_control_get_optional(&dev->dev, NULL);
	if (IS_ERR(priv->rst)) {
		err = PTR_ERR(priv->rst);
		if (err == -EPROBE_DEFER)
			goto err_put_clks;
		priv->rst = NULL;
	} else {
		err = reset_control_deassert(priv->rst);
		if (err)
			goto err_put_clks;
	}

	if (pdata->big_endian_desc)
		ohci->flags |= OHCI_QUIRK_BE_DESC;
	if (pdata->big_endian_mmio)
		ohci->flags |= OHCI_QUIRK_BE_MMIO;
	if (pdata->no_big_frame_no)
		ohci->flags |= OHCI_QUIRK_FRAME_NO;
	if (pdata->num_ports)
		ohci->num_ports = pdata->num_ports;

#ifndef CONFIG_USB_OHCI_BIG_ENDIAN_MMIO
	if (ohci->flags & OHCI_QUIRK_BE_MMIO) {
		dev_err(&dev->dev,
			"Error: CONFIG_USB_OHCI_BIG_ENDIAN_MMIO not set\n");
		err = -EINVAL;
		goto err_reset;
	}
#endif
#ifndef CONFIG_USB_OHCI_BIG_ENDIAN_DESC
	if (ohci->flags & OHCI_QUIRK_BE_DESC) {
		dev_err(&dev->dev,
			"Error: CONFIG_USB_OHCI_BIG_ENDIAN_DESC not set\n");
		err = -EINVAL;
		goto err_reset;
	}
#endif

	if (pdata->power_on) {
		err = pdata->power_on(dev);
		if (err < 0)
			goto err_reset;
	}

	res_mem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	hcd->regs = devm_ioremap_resource(&dev->dev, res_mem);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		goto err_power;
	}
	hcd->rsrc_start = res_mem->start;
	hcd->rsrc_len = resource_size(res_mem);

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err)
		goto err_power;

	device_wakeup_enable(hcd->self.controller);

	platform_set_drvdata(dev, hcd);

	return err;

err_power:
	if (pdata->power_off)
		pdata->power_off(dev);
err_reset:
	if (priv->rst)
		reset_control_assert(priv->rst);
err_put_clks:
	while (--clk >= 0)
		clk_put(priv->clks[clk]);
err_put_hcd:
	if (pdata == &ohci_platform_defaults)
		dev->dev.platform_data = NULL;

	usb_put_hcd(hcd);

	return err;
}

static int ohci_platform_remove(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct usb_ohci_pdata *pdata = dev_get_platdata(&dev->dev);
	struct ohci_platform_priv *priv = hcd_to_ohci_priv(hcd);
	int clk;

	usb_remove_hcd(hcd);

	if (pdata->power_off)
		pdata->power_off(dev);

	if (priv->rst)
		reset_control_assert(priv->rst);

	for (clk = 0; clk < OHCI_MAX_CLKS && priv->clks[clk]; clk++)
		clk_put(priv->clks[clk]);

	usb_put_hcd(hcd);

	if (pdata == &ohci_platform_defaults)
		dev->dev.platform_data = NULL;

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ohci_platform_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct usb_ohci_pdata *pdata = dev->platform_data;
	struct platform_device *pdev =
		container_of(dev, struct platform_device, dev);
	bool do_wakeup = device_may_wakeup(dev);
	int ret;

	ret = ohci_suspend(hcd, do_wakeup);
	if (ret)
		return ret;

	if (pdata->power_suspend)
		pdata->power_suspend(pdev);

	return ret;
}

static int ohci_platform_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct usb_ohci_pdata *pdata = dev_get_platdata(dev);
	struct platform_device *pdev =
		container_of(dev, struct platform_device, dev);

	if (pdata->power_on) {
		int err = pdata->power_on(pdev);
		if (err < 0)
			return err;
	}

	ohci_resume(hcd, false);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct of_device_id ohci_platform_ids[] = {
	{ .compatible = "generic-ohci", },
	{ }
};
MODULE_DEVICE_TABLE(of, ohci_platform_ids);

static const struct platform_device_id ohci_platform_table[] = {
	{ "ohci-platform", 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, ohci_platform_table);

static SIMPLE_DEV_PM_OPS(ohci_platform_pm_ops, ohci_platform_suspend,
	ohci_platform_resume);

static struct platform_driver ohci_platform_driver = {
	.id_table	= ohci_platform_table,
	.probe		= ohci_platform_probe,
	.remove		= ohci_platform_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= "ohci-platform",
		.pm	= &ohci_platform_pm_ops,
		.of_match_table = ohci_platform_ids,
	}
};

static int __init ohci_platform_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ohci_init_driver(&ohci_platform_hc_driver, &platform_overrides);
	return platform_driver_register(&ohci_platform_driver);
}
module_init(ohci_platform_init);

static void __exit ohci_platform_cleanup(void)
{
	platform_driver_unregister(&ohci_platform_driver);
}
module_exit(ohci_platform_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Hauke Mehrtens");
MODULE_AUTHOR("Alan Stern");
MODULE_LICENSE("GPL");
