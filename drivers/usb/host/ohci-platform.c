// SPDX-License-Identifier: GPL-2.0
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
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/usb/ohci_pdriver.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/of.h>

#include "ohci.h"

#define DRIVER_DESC "OHCI generic platform driver"
#define OHCI_MAX_CLKS 4
#define hcd_to_ohci_priv(h) ((struct ohci_platform_priv *)hcd_to_ohci(h)->priv)

struct ohci_platform_priv {
	struct clk *clks[OHCI_MAX_CLKS];
	struct reset_control *resets;
};

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

	return 0;

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
	if (irq < 0)
		return irq;

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

		if (of_property_read_bool(dev->dev.of_node,
					  "remote-wakeup-connected"))
			ohci->hc_control = OHCI_CTRL_RWC;

		of_property_read_u32(dev->dev.of_node, "num-ports",
				     &ohci->num_ports);

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

		priv->resets = devm_reset_control_array_get_optional_shared(
								&dev->dev);
		if (IS_ERR(priv->resets)) {
			err = PTR_ERR(priv->resets);
			goto err_put_clks;
		}

		err = reset_control_deassert(priv->resets);
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

	pm_runtime_set_active(&dev->dev);
	pm_runtime_enable(&dev->dev);
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

	hcd->tpl_support = of_usb_host_tpl_support(dev->dev.of_node);

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
	pm_runtime_disable(&dev->dev);
	reset_control_assert(priv->resets);
err_put_clks:
	while (--clk >= 0)
		clk_put(priv->clks[clk]);

	if (pdata == &ohci_platform_defaults)
		dev->dev.platform_data = NULL;

	usb_put_hcd(hcd);

	return err;
}

static void ohci_platform_remove(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct usb_ohci_pdata *pdata = dev_get_platdata(&dev->dev);
	struct ohci_platform_priv *priv = hcd_to_ohci_priv(hcd);
	int clk;

	pm_runtime_get_sync(&dev->dev);
	usb_remove_hcd(hcd);

	if (pdata->power_off)
		pdata->power_off(dev);

	reset_control_assert(priv->resets);

	for (clk = 0; clk < OHCI_MAX_CLKS && priv->clks[clk]; clk++)
		clk_put(priv->clks[clk]);

	usb_put_hcd(hcd);

	pm_runtime_put_sync(&dev->dev);
	pm_runtime_disable(&dev->dev);

	if (pdata == &ohci_platform_defaults)
		dev->dev.platform_data = NULL;
}

#ifdef CONFIG_PM_SLEEP
static int ohci_platform_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct usb_ohci_pdata *pdata = dev->platform_data;
	struct platform_device *pdev = to_platform_device(dev);
	bool do_wakeup = device_may_wakeup(dev);
	int ret;

	ret = ohci_suspend(hcd, do_wakeup);
	if (ret)
		return ret;

	if (pdata->power_suspend)
		pdata->power_suspend(pdev);

	return ret;
}

static int ohci_platform_resume_common(struct device *dev, bool hibernated)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct usb_ohci_pdata *pdata = dev_get_platdata(dev);
	struct platform_device *pdev = to_platform_device(dev);

	if (pdata->power_on) {
		int err = pdata->power_on(pdev);
		if (err < 0)
			return err;
	}

	ohci_resume(hcd, hibernated);

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}

static int ohci_platform_resume(struct device *dev)
{
	return ohci_platform_resume_common(dev, false);
}

static int ohci_platform_restore(struct device *dev)
{
	return ohci_platform_resume_common(dev, true);
}
#endif /* CONFIG_PM_SLEEP */

static const struct of_device_id ohci_platform_ids[] = {
	{ .compatible = "generic-ohci", },
	{ .compatible = "cavium,octeon-6335-ohci", },
	{ .compatible = "ti,ohci-omap3", },
	{ }
};
MODULE_DEVICE_TABLE(of, ohci_platform_ids);

static const struct platform_device_id ohci_platform_table[] = {
	{ "ohci-platform", 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, ohci_platform_table);

#ifdef CONFIG_PM_SLEEP
static const struct dev_pm_ops ohci_platform_pm_ops = {
	.suspend = ohci_platform_suspend,
	.resume = ohci_platform_resume,
	.freeze = ohci_platform_suspend,
	.thaw = ohci_platform_resume,
	.poweroff = ohci_platform_suspend,
	.restore = ohci_platform_restore,
};
#endif

static struct platform_driver ohci_platform_driver = {
	.id_table	= ohci_platform_table,
	.probe		= ohci_platform_probe,
	.remove_new	= ohci_platform_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= "ohci-platform",
#ifdef CONFIG_PM_SLEEP
		.pm	= &ohci_platform_pm_ops,
#endif
		.of_match_table = ohci_platform_ids,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	}
};

static int __init ohci_platform_init(void)
{
	if (usb_disabled())
		return -ENODEV;

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
