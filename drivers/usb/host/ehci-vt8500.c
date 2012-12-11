/*
 * drivers/usb/host/ehci-vt8500.c
 *
 * Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * Based on ehci-au1xxx.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/of.h>
#include <linux/platform_device.h>

static const struct hc_driver vt8500_ehci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "VT8500 EHCI",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset			= ehci_setup,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number	= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

static u64 vt8500_ehci_dma_mask = DMA_BIT_MASK(32);

static int vt8500_ehci_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource *res;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &vt8500_ehci_dma_mask;

	if (pdev->resource[1].flags != IORESOURCE_IRQ) {
		pr_debug("resource[1] is not IORESOURCE_IRQ");
		return -ENOMEM;
	}
	hcd = usb_create_hcd(&vt8500_ehci_hc_driver, &pdev->dev, "VT8500");
	if (!hcd)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	hcd->regs = devm_request_and_ioremap(&pdev->dev, res);
	if (!hcd->regs) {
		pr_debug("ioremap failed");
		ret = -ENOMEM;
		goto err1;
	}

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;

	ret = usb_add_hcd(hcd, pdev->resource[1].start,
			  IRQF_SHARED);
	if (ret == 0) {
		platform_set_drvdata(pdev, hcd);
		return ret;
	}

err1:
	usb_put_hcd(hcd);
	return ret;
}

static int vt8500_ehci_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id vt8500_ehci_ids[] = {
	{ .compatible = "via,vt8500-ehci", },
	{ .compatible = "wm,prizm-ehci", },
	{}
};

static struct platform_driver vt8500_ehci_driver = {
	.probe		= vt8500_ehci_drv_probe,
	.remove		= vt8500_ehci_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver = {
		.name	= "vt8500-ehci",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(vt8500_ehci_ids),
	}
};

MODULE_ALIAS("platform:vt8500-ehci");
MODULE_DEVICE_TABLE(of, vt8500_ehci_ids);
