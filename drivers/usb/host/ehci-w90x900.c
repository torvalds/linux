/*
 * linux/driver/usb/host/ehci-w90x900.c
 *
 * Copyright (c) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ehci.h"

/* enable phy0 and phy1 for w90p910 */
#define	ENPHY		(0x01<<8)
#define PHY0_CTR	(0xA4)
#define PHY1_CTR	(0xA8)

#define DRIVER_DESC "EHCI w90x900 driver"

static const char hcd_name[] = "ehci-w90x900 ";

static struct hc_driver __read_mostly ehci_w90x900_hc_driver;

static int usb_w90x900_probe(const struct hc_driver *driver,
		      struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource *res;
	int retval = 0, irq;
	unsigned long val;


	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		retval = -ENXIO;
		goto err1;
	}

	hcd = usb_create_hcd(driver, &pdev->dev, "w90x900 EHCI");
	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		retval = -EBUSY;
		goto err2;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (hcd->regs == NULL) {
		retval = -EFAULT;
		goto err3;
	}

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs +
		HC_LENGTH(ehci, ehci_readl(ehci, &ehci->caps->hc_capbase));

	/* enable PHY 0,1,the regs only apply to w90p910
	*  0xA4,0xA8 were offsets of PHY0 and PHY1 controller of
	*  w90p910 IC relative to ehci->regs.
	*/
	val = __raw_readl(ehci->regs+PHY0_CTR);
	val |= ENPHY;
	__raw_writel(val, ehci->regs+PHY0_CTR);

	val = __raw_readl(ehci->regs+PHY1_CTR);
	val |= ENPHY;
	__raw_writel(val, ehci->regs+PHY1_CTR);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		goto err4;

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval != 0)
		goto err4;

	return retval;
err4:
	iounmap(hcd->regs);
err3:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err2:
	usb_put_hcd(hcd);
err1:
	return retval;
}

static void usb_w90x900_remove(struct usb_hcd *hcd,
			struct platform_device *pdev)
{
	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
}

static int ehci_w90x900_probe(struct platform_device *pdev)
{
	if (usb_disabled())
		return -ENODEV;

	return usb_w90x900_probe(&ehci_w90x900_hc_driver, pdev);
}

static int ehci_w90x900_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_w90x900_remove(hcd, pdev);

	return 0;
}

static struct platform_driver ehci_hcd_w90x900_driver = {
	.probe  = ehci_w90x900_probe,
	.remove = ehci_w90x900_remove,
	.driver = {
		.name = "w90x900-ehci",
		.owner = THIS_MODULE,
	},
};

static int __init ehci_w90X900_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ehci_init_driver(&ehci_w90x900_hc_driver, NULL);
	return platform_driver_register(&ehci_hcd_w90x900_driver);
}
module_init(ehci_w90X900_init);

static void __exit ehci_w90X900_cleanup(void)
{
	platform_driver_unregister(&ehci_hcd_w90x900_driver);
}
module_exit(ehci_w90X900_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Wan ZongShun <mcuos.com@gmail.com>");
MODULE_ALIAS("platform:w90p910-ehci");
MODULE_LICENSE("GPL v2");
