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
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>

/* interface and function clocks */
static struct clk *iclk, *fclk;
static int clocked;

/*-------------------------------------------------------------------------*/

static void atmel_start_clock(void)
{
	clk_enable(iclk);
	clk_enable(fclk);
	clocked = 1;
}

static void atmel_stop_clock(void)
{
	clk_disable(fclk);
	clk_disable(iclk);
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

static int ehci_atmel_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	/* registers start at offset 0x0 */
	ehci->caps = hcd->regs;

	return ehci_setup(hcd);
}

static const struct hc_driver ehci_atmel_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Atmel EHCI UHP HS",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/* generic hardware linkage */
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	/* basic lifecycle operations */
	.reset			= ehci_atmel_setup,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	/* managing i/o requests and associated device resources */
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,

	/* scheduling support */
	.get_frame_number	= ehci_get_frame,

	/* root hub support */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

static u64 at91_ehci_dma_mask = DMA_BIT_MASK(32);

static int ehci_atmel_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	const struct hc_driver *driver = &ehci_atmel_hc_driver;
	struct resource *res;
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
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &at91_ehci_dma_mask;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto fail_create_hcd;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no register addr. Check %s setup!\n",
			dev_name(&pdev->dev));
		retval = -ENODEV;
		goto fail_request_resource;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		retval = PTR_ERR(hcd->regs);
		goto fail_request_resource;
	}

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

	atmel_start_ehci(pdev);

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval)
		goto fail_add_hcd;

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

	ehci_shutdown(hcd);
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
