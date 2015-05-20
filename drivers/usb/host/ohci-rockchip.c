/*
 * ROCKCHIP USB HOST OHCI Controller
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include "../dwc_otg_310/usbdev_rk.h"

static int ohci_rk_init(struct usb_hcd *hcd)
{
	dev_dbg(hcd->self.controller, "starting OHCI controller\n");

	return ohci_init(hcd_to_ohci(hcd));
}

static int ohci_rk_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	/*
	 * RemoteWakeupConnected has to be set explicitly before
	 * calling ohci_run. The reset value of RWC is 0.
	 */
	ohci->hc_control = OHCI_CTRL_RWC;
	writel(OHCI_CTRL_RWC, &ohci->regs->control);

	ret = ohci_run(ohci);

	if (ret < 0) {
		dev_err(hcd->self.controller, "can't start\n");
		ohci_stop(hcd);
	}

	return ret;
}

static const struct hc_driver ohci_rk_hc_driver = {
	.description = hcd_name,
	.product_desc = "RK OHCI Host Controller",
	.hcd_priv_size = sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ohci_irq,
	.flags = HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.reset = ohci_rk_init,
	.start = ohci_rk_start,
	.stop = ohci_stop,
	.shutdown = ohci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ohci_urb_enqueue,
	.urb_dequeue = ohci_urb_dequeue,
	.endpoint_disable = ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number = ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ohci_hub_status_data,
	.hub_control = ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend = ohci_bus_suspend,
	.bus_resume = ohci_bus_resume,
#endif
	.start_port_reset = ohci_start_port_reset,
};

static struct of_device_id rk_ohci_of_match[] = {
#ifdef CONFIG_ARM
	{
	 .compatible = "rockchip,rk3126_ohci",
	 .data = &usb20ohci_pdata_rk3126,
	 },
#endif
#ifdef CONFIG_ARM64
	{
	 .compatible = "rockchip,rk3368_ohci",
	 .data = &usb20ohci_pdata_rk3368,
	 },
#endif
	{},
};

MODULE_DEVICE_TABLE(of, rk_ohci_of_match);

/* ohci_hcd_rk_probe - initialize RK-based HCDs
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
static int ohci_hcd_rk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd = NULL;
	void __iomem *regs = NULL;
	struct resource *res;
	int ret = -ENODEV;
	int irq;
	struct dwc_otg_platform_data *pldata;
	const struct of_device_id *match;

	dev_dbg(&pdev->dev, "ohci_hcd_rk_probe\n");

	if (usb_disabled())
		return -ENODEV;

	match = of_match_device(of_match_ptr(rk_ohci_of_match), &pdev->dev);
	if (match && match->data) {
		pldata = (struct dwc_otg_platform_data *)match->data;
	} else {
		dev_err(dev, "ohci_rk match failed\n");
		return -EINVAL;
	}

	pldata->dev = dev;
	dev->platform_data = pldata;

	if (pldata->hw_init)
		pldata->hw_init();

	if (pldata->clock_init) {
		pldata->clock_init(pldata);
		pldata->clock_enable(pldata, 1);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "OHCI irq failed\n");
		ret = irq;
		goto clk_disable;
	}

	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));


	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "UHH OHCI get resource failed\n");
		ret = -ENOMEM;
		goto clk_disable;
	}

	regs = devm_ioremap_resource(dev, res);
	if (!regs) {
		dev_err(dev, "UHH OHCI ioremap failed\n");
		ret = -ENOMEM;
		goto clk_disable;
	}

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;
	if (!dev->coherent_dma_mask)
		dev->coherent_dma_mask = DMA_BIT_MASK(32);

	hcd = usb_create_hcd(&ohci_rk_hc_driver, dev, dev_name(dev));
	if (!hcd) {
		dev_err(dev, "usb_create_hcd failed\n");
		ret = -ENOMEM;
		goto clk_disable;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = regs;

	ohci_hcd_init(hcd_to_ohci(hcd));

	ret = usb_add_hcd(hcd, irq, 0);
	if (ret) {
		dev_dbg(dev, "failed to add hcd with err %d\n", ret);
		goto err_add_hcd;
	}

	return 0;

err_add_hcd:
	usb_put_hcd(hcd);

clk_disable:
	if (pldata->clock_enable)
		pldata->clock_enable(pldata, 0);

	return ret;
}

static int ohci_hcd_rk_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
	return 0;
}

static void ohci_hcd_rk_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = dev_get_drvdata(&pdev->dev);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static struct platform_driver ohci_hcd_rk_driver = {
	.probe = ohci_hcd_rk_probe,
	.remove = ohci_hcd_rk_remove,
	.shutdown = ohci_hcd_rk_shutdown,
	.driver = {
		   .name = "ohci-rockchip",
		   .of_match_table = of_match_ptr(rk_ohci_of_match),
		   },
};

MODULE_ALIAS("platform:rockchip-ohci");
