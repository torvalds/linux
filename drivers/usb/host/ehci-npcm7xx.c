// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NPCM7xx driver for EHCI HCD
 *
 * Copyright (C) 2018 Nuvoton Technologies,
 * Avi Fishman <avi.fishman@nuvoton.com> <avifishman70@gmail.com>
 * Tomer Maimon <tomer.maimon@nuvoton.com> <tmaimon77@gmail.com>
 *
 * Based on various ehci-spear.c driver
 */


#include <linux/dma-mapping.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ehci.h"

#define DRIVER_DESC "EHCI npcm7xx driver"

static struct hc_driver __read_mostly ehci_npcm7xx_hc_driver;

static int __maybe_unused ehci_npcm7xx_drv_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	bool do_wakeup = device_may_wakeup(dev);

	return ehci_suspend(hcd, do_wakeup);
}

static int __maybe_unused ehci_npcm7xx_drv_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	ehci_resume(hcd, false);
	return 0;
}

static SIMPLE_DEV_PM_OPS(ehci_npcm7xx_pm_ops, ehci_npcm7xx_drv_suspend,
		ehci_npcm7xx_drv_resume);

static int npcm7xx_ehci_hcd_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	const struct hc_driver *driver = &ehci_npcm7xx_hc_driver;
	int irq;
	int retval;

	dev_dbg(&pdev->dev,	"initializing npcm7xx ehci USB Controller\n");

	if (usb_disabled())
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		retval = irq;
		goto fail;
	}

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	retval = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (retval)
		goto fail;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto fail;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		retval = PTR_ERR(hcd->regs);
		goto err_put_hcd;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	/* registers start at offset 0x0 */
	hcd_to_ehci(hcd)->caps = hcd->regs;

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval)
		goto err_put_hcd;

	device_wakeup_enable(hcd->self.controller);
	return retval;

err_put_hcd:
	usb_put_hcd(hcd);
fail:
	dev_err(&pdev->dev, "init fail, %d\n", retval);

	return retval;
}

static void npcm7xx_ehci_hcd_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);

	usb_put_hcd(hcd);
}

static const struct of_device_id npcm7xx_ehci_id_table[] = {
	{ .compatible = "nuvoton,npcm750-ehci" },
	{ },
};
MODULE_DEVICE_TABLE(of, npcm7xx_ehci_id_table);

static struct platform_driver npcm7xx_ehci_hcd_driver = {
	.probe		= npcm7xx_ehci_hcd_drv_probe,
	.remove_new	= npcm7xx_ehci_hcd_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name = "npcm7xx-ehci",
		.bus = &platform_bus_type,
		.pm = pm_ptr(&ehci_npcm7xx_pm_ops),
		.of_match_table = npcm7xx_ehci_id_table,
	}
};

static int __init ehci_npcm7xx_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	ehci_init_driver(&ehci_npcm7xx_hc_driver, NULL);
	return platform_driver_register(&npcm7xx_ehci_hcd_driver);
}
module_init(ehci_npcm7xx_init);

static void __exit ehci_npcm7xx_cleanup(void)
{
	platform_driver_unregister(&npcm7xx_ehci_hcd_driver);
}
module_exit(ehci_npcm7xx_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:npcm7xx-ehci");
MODULE_AUTHOR("Avi Fishman");
MODULE_LICENSE("GPL v2");
