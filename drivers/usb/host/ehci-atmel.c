/*
 * Driver for EHCI UHP on Atmel chips
 *
 *  Copyright (C) 2009 Atmel Corporation,
 *                     Nicolas Ferre <nicolas.ferre@atmel.com>
 *
 *  Based on various ehci-*.c drivers
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ehci.h"

#define DRIVER_DESC "EHCI Atmel driver"

static const char hcd_name[] = "ehci-atmel";
static struct hc_driver __read_mostly ehci_atmel_hc_driver;

/* interface and function clocks */
static struct clk *iclk, *fclk, *uclk;
static int clocked;

/*-------------------------------------------------------------------------*/

static void atmel_start_clock(void)
{
	if (IS_ENABLED(CONFIG_COMMON_CLK)) {
		clk_set_rate(uclk, 48000000);
		clk_prepare_enable(uclk);
	}
	clk_prepare_enable(iclk);
	clk_prepare_enable(fclk);
	clocked = 1;
}

static void atmel_stop_clock(void)
{
	clk_disable_unprepare(fclk);
	clk_disable_unprepare(iclk);
	if (IS_ENABLED(CONFIG_COMMON_CLK))
		clk_disable_unprepare(uclk);
	clocked = 0;
}

static void atmel_start_ehci(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "start\n");
	atmel_start_clock();
}

static void atmel_stop_ehci(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "stop\n");
	atmel_stop_clock();
}

/*-------------------------------------------------------------------------*/

static int ehci_atmel_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	const struct hc_driver *driver = &ehci_atmel_hc_driver;
	struct resource *res;
	struct ehci_hcd *ehci;
	int irq;
	int retval;

	if (usb_disabled())
		return -ENODEV;

	pr_debug("Initializing Atmel-SoC USB Host Controller\n");

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev,
			"Found HC with no IRQ. Check %s setup!\n",
			dev_name(&pdev->dev));
		retval = -ENODEV;
		goto fail_create_hcd;
	}

	/* Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	retval = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (retval)
		goto fail_create_hcd;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto fail_create_hcd;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		retval = PTR_ERR(hcd->regs);
		goto fail_request_resource;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	iclk = devm_clk_get(&pdev->dev, "ehci_clk");
	if (IS_ERR(iclk)) {
		dev_err(&pdev->dev, "Error getting interface clock\n");
		retval = -ENOENT;
		goto fail_request_resource;
	}
	fclk = devm_clk_get(&pdev->dev, "uhpck");
	if (IS_ERR(fclk)) {
		dev_err(&pdev->dev, "Error getting function clock\n");
		retval = -ENOENT;
		goto fail_request_resource;
	}
	if (IS_ENABLED(CONFIG_COMMON_CLK)) {
		uclk = devm_clk_get(&pdev->dev, "usb_clk");
		if (IS_ERR(uclk)) {
			dev_err(&pdev->dev, "failed to get uclk\n");
			retval = PTR_ERR(uclk);
			goto fail_request_resource;
		}
	}

	ehci = hcd_to_ehci(hcd);
	/* registers start at offset 0x0 */
	ehci->caps = hcd->regs;

	atmel_start_ehci(pdev);

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval)
		goto fail_add_hcd;
	device_wakeup_enable(hcd->self.controller);

	return retval;

fail_add_hcd:
	atmel_stop_ehci(pdev);
fail_request_resource:
	usb_put_hcd(hcd);
fail_create_hcd:
	dev_err(&pdev->dev, "init %s fail, %d\n",
		dev_name(&pdev->dev), retval);

	return retval;
}

static int ehci_atmel_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	atmel_stop_ehci(pdev);
	fclk = iclk = NULL;

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id atmel_ehci_dt_ids[] = {
	{ .compatible = "atmel,at91sam9g45-ehci" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, atmel_ehci_dt_ids);
#endif

static struct platform_driver ehci_atmel_driver = {
	.probe		= ehci_atmel_drv_probe,
	.remove		= ehci_atmel_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= "atmel-ehci",
		.of_match_table	= of_match_ptr(atmel_ehci_dt_ids),
	},
};

static int __init ehci_atmel_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);
	ehci_init_driver(&ehci_atmel_hc_driver, NULL);
	return platform_driver_register(&ehci_atmel_driver);
}
module_init(ehci_atmel_init);

static void __exit ehci_atmel_cleanup(void)
{
	platform_driver_unregister(&ehci_atmel_driver);
}
module_exit(ehci_atmel_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:atmel-ehci");
MODULE_AUTHOR("Nicolas Ferre");
MODULE_LICENSE("GPL");
