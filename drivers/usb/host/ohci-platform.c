/*
 * Generic platform ohci driver
 *
 * Copyright 2007 Michael Buesch <m@bues.ch>
 * Copyright 2011-2012 Hauke Mehrtens <hauke@hauke-m.de>
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

#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/usb/ohci_pdriver.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ohci.h"

#define DRIVER_DESC "OHCI generic platform driver"

static const char hcd_name[] = "ohci-platform";

static int ohci_platform_reset(struct usb_hcd *hcd)
{
	struct platform_device *pdev = to_platform_device(hcd->self.controller);
	struct usb_ohci_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	if (pdata->big_endian_desc)
		ohci->flags |= OHCI_QUIRK_BE_DESC;
	if (pdata->big_endian_mmio)
		ohci->flags |= OHCI_QUIRK_BE_MMIO;
	if (pdata->no_big_frame_no)
		ohci->flags |= OHCI_QUIRK_FRAME_NO;
	if (pdata->num_ports)
		ohci->num_ports = pdata->num_ports;

	return ohci_setup(hcd);
}

static struct hc_driver __read_mostly ohci_platform_hc_driver;

static const struct ohci_driver_overrides platform_overrides __initconst = {
	.product_desc =	"Generic Platform OHCI controller",
	.reset =	ohci_platform_reset,
};

static int ohci_platform_probe(struct platform_device *dev)
{
	struct usb_hcd *hcd;
	struct resource *res_mem;
	struct usb_ohci_pdata *pdata = dev_get_platdata(&dev->dev);
	int irq;
	int err = -ENOMEM;

	if (!pdata) {
		WARN_ON(1);
		return -ENODEV;
	}

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

	if (pdata->power_on) {
		err = pdata->power_on(dev);
		if (err < 0)
			return err;
	}

	hcd = usb_create_hcd(&ohci_platform_hc_driver, &dev->dev,
			dev_name(&dev->dev));
	if (!hcd) {
		err = -ENOMEM;
		goto err_power;
	}

	hcd->rsrc_start = res_mem->start;
	hcd->rsrc_len = resource_size(res_mem);

	hcd->regs = devm_ioremap_resource(&dev->dev, res_mem);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		goto err_put_hcd;
	}
	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err)
		goto err_put_hcd;

	platform_set_drvdata(dev, hcd);

	return err;

err_put_hcd:
	usb_put_hcd(hcd);
err_power:
	if (pdata->power_off)
		pdata->power_off(dev);

	return err;
}

static int ohci_platform_remove(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct usb_ohci_pdata *pdata = dev_get_platdata(&dev->dev);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	if (pdata->power_off)
		pdata->power_off(dev);

	return 0;
}

#ifdef CONFIG_PM

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

#else /* !CONFIG_PM */
#define ohci_platform_suspend	NULL
#define ohci_platform_resume	NULL
#endif /* CONFIG_PM */

static const struct platform_device_id ohci_platform_table[] = {
	{ "ohci-platform", 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, ohci_platform_table);

static const struct dev_pm_ops ohci_platform_pm_ops = {
	.suspend	= ohci_platform_suspend,
	.resume		= ohci_platform_resume,
};

static struct platform_driver ohci_platform_driver = {
	.id_table	= ohci_platform_table,
	.probe		= ohci_platform_probe,
	.remove		= ohci_platform_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "ohci-platform",
		.pm	= &ohci_platform_pm_ops,
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
