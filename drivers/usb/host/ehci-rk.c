/*
 * EHCI-compliant USB host controller driver for Rockchip SoCs
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2009 - 2013 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
# include <linux/platform_device.h>
# include <linux/clk.h>
# include <linux/err.h>
# include <linux/device.h>
# include <linux/of.h>
# include <linux/of_platform.h>
# include "ehci.h"
#ifdef CONFIG_DWC_OTG_274
# include "../dwc_otg/usbdev_rk.h"
#endif
#ifdef CONFIG_DWC_OTG_310
# include "../dwc_otg_310/usbdev_rk.h"
#endif

static int rkehci_status = 1;
static void ehci_port_power (struct ehci_hcd *ehci, int is_on)
{

}

static struct hc_driver rk_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Rockchip On-Chip EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_USB2 | HCD_MEMORY,

	.reset			= ehci_init,
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
	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,

	/*
	 * scheduling support
	 */
	.get_frame_number	= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	/*
	 * PM support
	 */
#ifdef CONFIG_PM
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
#endif
};
static ssize_t ehci_power_show( struct device *_dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", rkehci_status);
}
static ssize_t ehci_power_store( struct device *_dev,
					struct device_attribute *attr,
					const char *buf, size_t count )
{
	return count;
}
static DEVICE_ATTR(ehci_power, S_IRUGO|S_IWUSR, ehci_power_show, ehci_power_store);
static ssize_t debug_show( struct device *_dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "EHCI Registers Dump\n");
}
static DEVICE_ATTR(debug_ehci, S_IRUGO, debug_show, NULL);
static int ehci_rk_probe(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "ehci_rk proble\n");
	return 0;
}
static int ehci_rk_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_put_hcd(hcd);

	return 0;
}
#ifdef CONFIG_PM
static int ehci_rk_pm_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	bool wakeup = device_may_wakeup(dev);

	dev_dbg(dev, "ehci-rk PM suspend\n");

	/*
	 * EHCI helper function has also the same check before manipulating
	 * port wakeup flags.  We do check here the same condition before
	 * calling the same helper function to avoid bringing hardware
	 * from Low power mode when there is no need for adjusting port
	 * wakeup flags.
	 */
	if (hcd->self.root_hub->do_remote_wakeup && !wakeup) {
		pm_runtime_resume(dev);
		ehci_prepare_ports_for_controller_suspend(hcd_to_ehci(hcd),
				wakeup);
	}

	return 0;
}

static int ehci_rk_pm_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	dev_dbg(dev, "ehci-rk PM resume\n");
	ehci_prepare_ports_for_controller_resume(hcd_to_ehci(hcd));

	return 0;
}
#else
#define ehci_rk_pm_suspend	NULL
#define ehci_rk_pm_resume	NULL
#endif

static const struct dev_pm_ops ehci_rk_dev_pm_ops = {
	.suspend         = ehci_rk_pm_suspend,
	.resume          = ehci_rk_pm_resume,
};

static struct of_device_id rk_ehci_of_match[] = {
	{ .compatible = "rockchip,rk_ehci_host", },
	{ },
};

MODULE_DEVICE_TABLE(of, rk_ehci_of_match);

static struct platform_driver ehci_rk_driver = {
	.probe	= ehci_rk_probe,
	.remove	= ehci_rk_remove,
	.driver = {
		   .name = "rk_ehci_host",
		   .of_match_table = of_match_ptr(rk_ehci_of_match),
#ifdef CONFIG_PM
		   .pm = &ehci_rk_dev_pm_ops,
#endif
	},
};
