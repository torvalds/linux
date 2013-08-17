/* xhci-exynos.c - Driver for USB HOST on Samsung EXYNOS platform device
 *
 * Bus Glue for SAMSUNG EXYNOS USB HOST xHCI Controller
 * xHCI host controller driver
 *
 * Copyright (C) 2008 Intel Corp.
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 *
 * Author: Yulgon Kim <yulgon.kim@samsung.com>
 *
 * Based on "xhci-pci.c" by Sarah Sharp
 * Modified for SAMSUNG EXYNOS XHCI by Yulgon Kim <yulgon.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/usb/otg.h>
#include <linux/usb/exynos_usb3_drd.h>
#include <linux/platform_data/dwc3-exynos.h>

#include "xhci.h"

struct exynos_xhci_hcd {
	struct device		*dev;
	struct dwc3_exynos_data	*pdata;
	struct usb_hcd		*hcd;
	struct exynos_drd_core	*core;
};

struct xhci_hcd *exynos_xhci_dbg;

static const char hcd_name[] = "xhci_hcd";

#ifdef CONFIG_PM
static int exynos_xhci_suspend(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct exynos_xhci_hcd	*exynos_xhci;
	struct usb_hcd		*hcd;
	struct xhci_hcd		*xhci;
	int			retval = 0;

#ifdef CONFIG_PM_RUNTIME
	dev_dbg(dev, "%s: usage_count = %d\n",
		      __func__, atomic_read(&dev->power.usage_count));
#endif
	exynos_xhci = dev_get_drvdata(dev);
	if (!exynos_xhci)
		return -EINVAL;

	hcd = exynos_xhci->hcd;
	if (!hcd)
		return -EINVAL;

	xhci = hcd_to_xhci(hcd);

	if (hcd->state != HC_STATE_SUSPENDED ||
			xhci->shared_hcd->state != HC_STATE_SUSPENDED)
		dev_err(dev, "%s: HC state is not suspended!\n", __func__);
#ifdef CONFIG_USB_SUSPEND
	if (pm_runtime_suspended(dev)) {
		dev_dbg(dev, "xhci is runtime suspended\n");
		return 0;
	}
#endif
	retval = xhci_suspend(xhci);
	if (retval < 0)
		dev_err(dev, "%s: cannot stop xHC\n", __func__);

	pm_runtime_put_sync(dev->parent);

	exynos_drd_put(pdev);

	return retval;
}

static int exynos_xhci_resume(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct exynos_xhci_hcd	*exynos_xhci;
	struct usb_hcd		*hcd;
	struct xhci_hcd		*xhci;
	int			retval = 0;

#ifdef CONFIG_PM_RUNTIME
	dev_dbg(dev, "%s: usage_count = %d\n",
		      __func__, atomic_read(&dev->power.usage_count));
#endif
	exynos_xhci = dev_get_drvdata(dev);
	if (!exynos_xhci)
		return -EINVAL;

	hcd = exynos_xhci->hcd;
	if (!hcd)
		return -EINVAL;

	if (exynos_drd_try_get(pdev)) {
		dev_err(dev, "%s: cannot get DRD\n", __func__);
		return -EAGAIN;
	}

	/* Wake up and initialize DRD core */
	pm_runtime_get_sync(dev->parent);
	if (exynos_xhci->core->ops->change_mode)
		exynos_xhci->core->ops->change_mode(exynos_xhci->core, true);
	if (exynos_xhci->core->ops->core_init)
		exynos_xhci->core->ops->core_init(exynos_xhci->core);

	xhci = hcd_to_xhci(hcd);
	retval = xhci_resume(xhci, 0);
	if (retval < 0)
		dev_err(dev, "%s: cannot start xHC\n", __func__);

	/*
	 * In xhci_resume(), config values(AHB bus and los_bias) are intialized.
	 * So after called xhci_resume(), set the config values again.
	 */
	if (exynos_xhci->core->ops->config)
		exynos_xhci->core->ops->config(exynos_xhci->core);

	/* Update runtime PM status and clear runtime_error */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return retval;
}
#else
#define exynos_xhci_suspend	NULL
#define exynos_xhci_resume	NULL
#endif

#ifdef CONFIG_USB_SUSPEND
static int exynos_xhci_runtime_suspend(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct exynos_xhci_hcd	*exynos_xhci;
	struct usb_hcd		*hcd;
	struct xhci_hcd		*xhci;
	int			retval = 0;

	dev_dbg(dev, "%s\n", __func__);

	exynos_xhci = dev_get_drvdata(dev);
	if (!exynos_xhci)
		return -EINVAL;

	hcd = exynos_xhci->hcd;
	if (!hcd)
		return -EINVAL;

	xhci = hcd_to_xhci(hcd);

	if (hcd->state != HC_STATE_SUSPENDED ||
			xhci->shared_hcd->state != HC_STATE_SUSPENDED) {
		dev_dbg(dev, "%s: HC state is not suspended!\n", __func__);
		return -EAGAIN;
	}

	retval = xhci_suspend(xhci);
	if (retval < 0)
		dev_err(dev, "%s: cannot stop xHC\n", __func__);

	pm_runtime_put_sync(exynos_xhci->dev->parent);

	exynos_drd_put(pdev);

	return retval;
}

static int exynos_xhci_runtime_resume(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct exynos_xhci_hcd	*exynos_xhci;
	struct usb_hcd		*hcd;
	struct xhci_hcd		*xhci;
	int			retval = 0;

	dev_dbg(dev, "%s\n", __func__);

	exynos_xhci = dev_get_drvdata(dev);
	if (!exynos_xhci)
		return -EINVAL;

	hcd = exynos_xhci->hcd;
	if (!hcd)
		return -EINVAL;

	if (dev->power.is_suspended) {
		dev_dbg(dev, "xhci is system suspended\n");
		return 0;
	}

	/* Userspace may try to access host when DRD is in B-Dev mode */
	if (exynos_drd_try_get(pdev)) {
		dev_dbg(dev, "%s: cannot get DRD\n", __func__);
		return -EAGAIN;
	}

	pm_runtime_get_sync(exynos_xhci->dev->parent);

	/*
	 * Parent device (DRD core) resumes before its child (xHCI).
	 * Since we "get" DRD when it's already active, we need to
	 * reconfigure PHY here, so PHY tuning took effect.
	 */
	if (exynos_xhci->core->ops->phy_set)
		exynos_xhci->core->ops->phy_set(exynos_xhci->core);

	if (exynos_xhci->core->ops->change_mode)
		exynos_xhci->core->ops->change_mode(exynos_xhci->core, true);

	if (exynos_xhci->core->ops->core_init)
		exynos_xhci->core->ops->core_init(exynos_xhci->core);

	xhci = hcd_to_xhci(hcd);
	retval = xhci_resume(xhci, 0);
	if (retval < 0)
		dev_err(dev, "%s: cannot start xHC\n", __func__);

	/*
	 * In xhci_resume(), config values(AHB bus and los_bias) are intialized.
	 * So after called xhci_resume(), set the config values again.
	 */
	if (exynos_xhci->core->ops->config)
		exynos_xhci->core->ops->config(exynos_xhci->core);

	return retval;
}
#else
#define exynos_xhci_runtime_suspend	NULL
#define exynos_xhci_runtime_resume	NULL
#endif

#ifdef CONFIG_PM
int exynos_xhci_bus_resume(struct usb_hcd *hcd)
{
	/* When suspend is failed, re-enable clocks & PHY */
	pm_runtime_resume(hcd->self.controller);

	return xhci_bus_resume(hcd);
}
#else
#define exynos_xhci_bus_resume NULL
#endif

static void exynos_xhci_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	struct exynos_xhci_hcd  *exynos_xhci = dev_get_drvdata(dev);
	struct exynos_drd_core  *core = exynos_xhci->core;

	/* Don't use MSI interrupt */
	xhci->quirks |= XHCI_BROKEN_MSI;

	/* Race Condition in PORTSC Write Followed by Read */
	if (core->release <= 0x210a)
		xhci->quirks |= XHCI_PORTSC_RACE_CONDITION;
}

/* called during probe() after chip reset completes */
static int exynos_xhci_setup(struct usb_hcd *hcd)
{
	struct device		*dev = hcd->self.controller;
	struct exynos_xhci_hcd	*exynos_xhci = dev_get_drvdata(dev);
	struct xhci_hcd		*xhci;
	int			retval;

	retval = xhci_gen_setup(hcd, exynos_xhci_quirks);
	if (retval)
		return retval;

	/*
	 * During xhci_gen_setup() GSBUSCFG0 DRD register resets (detected by
	 * experiment). We need to configure it again here.
	 */
	if (exynos_xhci->core->ops->config)
		exynos_xhci->core->ops->config(exynos_xhci->core);

	xhci = hcd_to_xhci(hcd);
	if (!usb_hcd_is_primary_hcd(hcd))
		return 0;

	xhci->sbrn = HCD_USB3;
	xhci_dbg(xhci, "Got SBRN %u\n", (unsigned int) xhci->sbrn);

	return retval;
}

static int exynos_xhci_hub_control(struct usb_hcd *hcd, u16 typeReq,
		u16 wValue, u16 wIndex, char *buf, u16 wLength)
{
	struct device		*dev = hcd->self.controller;
	struct exynos_xhci_hcd	*exynos_xhci = dev_get_drvdata(dev);
	struct exynos_drd_core  *core = exynos_xhci->core;
	int retval = 0;

	/*
	 * When the port power is switched on to a 2.0 port and the device
	 * is not connected, the PLS field in PORTSC does not get updated
	 * from 4'h4 to 4'h5. It affects the version 2.00a and earlier.
	 * The workaround is as follows;
	 * Before switching on the port power, set the GUSB2PHYCFG[6] to 0.
	 * After switching on the port power, set this bit to 1.
	 */
	if (typeReq == SetPortFeature && wValue == USB_PORT_FEAT_POWER)
		if (core->release <= 0x200a && core->ops->phy20_suspend)
			core->ops->phy20_suspend(core, 0);

	retval = xhci_hub_control(hcd, typeReq, wValue, wIndex, buf, wLength);

	if (typeReq == SetPortFeature && wValue == USB_PORT_FEAT_POWER)
		if (core->release <= 0x200a && core->ops->phy20_suspend)
			core->ops->phy20_suspend(core, 1);

	return retval;
}

static const struct hc_driver exynos_xhci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "EXYNOS xHCI Host Controller",
	.hcd_priv_size		= sizeof(struct xhci_hcd *),

	/*
	 * generic hardware linkage
	 */
	.irq			= xhci_irq,
	.flags			= HCD_MEMORY | HCD_USB3 | HCD_SHARED,

	/*
	 * basic lifecycle operations
	 */
	.reset			= exynos_xhci_setup,
	.start			= xhci_run,
	.stop			= xhci_stop,
	.shutdown		= xhci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= xhci_urb_enqueue,
	.urb_dequeue		= xhci_urb_dequeue,
	.alloc_dev		= xhci_alloc_dev,
	.free_dev		= xhci_free_dev,
	.alloc_streams		= xhci_alloc_streams,
	.free_streams		= xhci_free_streams,
	.add_endpoint		= xhci_add_endpoint,
	.drop_endpoint		= xhci_drop_endpoint,
	.endpoint_reset		= xhci_endpoint_reset,
	.check_bandwidth	= xhci_check_bandwidth,
	.reset_bandwidth	= xhci_reset_bandwidth,
	.address_device		= xhci_address_device,
	.update_hub_device	= xhci_update_hub_device,
	.reset_device		= xhci_discover_or_reset_device,

	/*
	 * scheduling support
	 */
	.get_frame_number	= xhci_get_frame,

	/* Root hub support */
	.hub_control		= exynos_xhci_hub_control,
	.hub_status_data	= xhci_hub_status_data,
	.bus_suspend		= xhci_bus_suspend,
	.bus_resume		= exynos_xhci_bus_resume,
};

static int __devinit exynos_xhci_probe(struct platform_device *pdev)
{
	struct dwc3_exynos_data	*pdata = pdev->dev.platform_data;
	struct device		*dev = &pdev->dev;
	const struct hc_driver	*driver = &exynos_xhci_hc_driver;
	struct exynos_xhci_hcd	*exynos_xhci;
	struct usb_hcd		*hcd;
	struct xhci_hcd		*xhci;
	struct resource		*res;
	int			irq;
	int			err;

	if (usb_disabled())
		return -ENODEV;

	if (!pdata) {
		dev_err(dev, "No platform data defined\n");
		return -ENODEV;
	}

	exynos_xhci = devm_kzalloc(dev, sizeof(struct exynos_xhci_hcd),
				   GFP_KERNEL);
	if (!exynos_xhci) {
		dev_err(dev, "Not enough memory\n");
		return -ENOMEM;
	}

	exynos_xhci->dev = dev;
	exynos_xhci->pdata = pdata;
	exynos_xhci->core = exynos_drd_bind(pdev);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Failed to get IRQ\n");
		return -ENXIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Failed to get I/O memory\n");
		return -ENXIO;
	}

	/* Create and add primary HCD */

	hcd = usb_create_hcd(driver, dev, dev_name(dev));
	if (!hcd) {
		dev_err(dev, "Failed to create primary HCD\n");
		return -ENOMEM;
	}

	exynos_xhci->hcd = hcd;
	/* Rewrite driver data with our structure */
	platform_set_drvdata(pdev, exynos_xhci);

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	if (!devm_request_mem_region(dev, res->start,
				     resource_size(res), dev_name(dev))) {
		dev_err(dev, "Failed to reserve registers\n");
		err = -ENOENT;
		goto put_hcd;
	}

	hcd->regs = devm_ioremap_nocache(dev, res->start, resource_size(res));
	if (!hcd->regs) {
		dev_err(dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto put_hcd;
	}
	hcd->regs -= EXYNOS_USB3_XHCI_REG_START;

	err = exynos_drd_try_get(pdev);
	if (err) {
		/* REVISIT: what shall we do if UDC is already running */
		dev_err(dev, "Failed to access DRD\n");
		goto put_hcd;
	}

	/* Wake up and initialize DRD core */
	pm_runtime_get_sync(dev->parent);
	if (exynos_xhci->core->ops->change_mode)
		exynos_xhci->core->ops->change_mode(exynos_xhci->core, true);
	if (exynos_xhci->core->ops->core_init)
		exynos_xhci->core->ops->core_init(exynos_xhci->core);

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(dev, "Failed to add primary HCD\n");
		goto put_hcd;
	}

	/* Create and add shared HCD */

	xhci = hcd_to_xhci(hcd);
	exynos_xhci_dbg = xhci;

	xhci->shared_hcd = usb_create_shared_hcd(driver, dev,
						 dev_name(dev), hcd);
	if (!xhci->shared_hcd) {
		dev_err(dev, "Failed to create shared HCD\n");
		err = -ENOMEM;
		goto remove_hcd;
	}

	xhci->shared_hcd->regs = hcd->regs;

	/*
	 * Set the xHCI pointer before exynos_xhci_setup()
	 * (aka hcd_driver.reset) is called by usb_add_hcd().
	 */
	*((struct xhci_hcd **) xhci->shared_hcd->hcd_priv) = xhci;

	err = usb_add_hcd(xhci->shared_hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(dev, "Failed to add shared HCD\n");
		goto put_usb3_hcd;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	if (exynos_xhci->core->otg) {
		err = otg_set_host(exynos_xhci->core->otg, &hcd->self);
		if (err) {
			dev_err(dev, "Unable to bind hcd to DRD switch\n");
			goto remove_usb3_hcd;
		}
	}

	return 0;

remove_usb3_hcd:
	pm_runtime_disable(dev);
	usb_remove_hcd(xhci->shared_hcd);
put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);
remove_hcd:
	usb_remove_hcd(hcd);
put_hcd:
	usb_put_hcd(hcd);

	return err;
}

static int __devexit exynos_xhci_remove(struct platform_device *pdev)
{
	struct exynos_xhci_hcd	*exynos_xhci = platform_get_drvdata(pdev);
	struct usb_hcd		*hcd = exynos_xhci->hcd;
	struct xhci_hcd		*xhci = hcd_to_xhci(hcd);

	if (exynos_xhci->core->otg)
		otg_set_host(exynos_xhci->core->otg, NULL);

	pm_runtime_disable(&pdev->dev);

	usb_remove_hcd(xhci->shared_hcd);
	usb_put_hcd(xhci->shared_hcd);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	kfree(xhci);

	return 0;
}

static void exynos_xhci_shutdown(struct platform_device *pdev)
{
	struct exynos_xhci_hcd	*s5p_xhci = platform_get_drvdata(pdev);
	struct usb_hcd		*hcd = s5p_xhci->hcd;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static const struct dev_pm_ops exynos_xhci_pm_ops = {
	.suspend		= exynos_xhci_suspend,
	.resume			= exynos_xhci_resume,
	.runtime_suspend	= exynos_xhci_runtime_suspend,
	.runtime_resume		= exynos_xhci_runtime_resume,
};

static struct platform_driver exynos_xhci_driver = {
	.probe		= exynos_xhci_probe,
	.remove		= __devexit_p(exynos_xhci_remove),
	.shutdown	= exynos_xhci_shutdown,
	.driver = {
		.name	= "exynos-xhci",
		.owner	= THIS_MODULE,
		.pm = &exynos_xhci_pm_ops,
	}
};

int xhci_register_exynos(void)
{
	return platform_driver_register(&exynos_xhci_driver);
}

void xhci_unregister_exynos(void)
{
	platform_driver_unregister(&exynos_xhci_driver);
}

MODULE_ALIAS("platform:exynos-xhci");
