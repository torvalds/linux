/* ohci-s5p.c - Driver for USB HOST on Samsung S5P platform device
 *
 * Bus Glue for SAMSUNG S5P USB HOST OHCI Controller
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * Based on "ohci-au1xxx.c" by Matt Porter <mporter@kernel.crashing.org>
 * Modified for SAMSUNG s5p OHCI by Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include <linux/clk.h>

#include <plat/ehci.h>
#include <plat/usb-phy.h>

#include <mach/board_rev.h>

struct s5p_ohci_hcd {
	struct device *dev;
	struct usb_hcd *hcd;
	struct clk *clk;
	int power_on;
};

#ifdef CONFIG_USB_EXYNOS_SWITCH
int s5p_ohci_port_power_off(struct platform_device *pdev)
{
	struct s5p_ohci_hcd *s5p_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ohci->hcd;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	(void)ohci_readl(ohci, &ohci->regs->intrdisable);

	ohci_writel (ohci, RH_HS_LPS, &ohci->regs->roothub.status);

	return 0;
}
EXPORT_SYMBOL_GPL(s5p_ohci_port_power_off);

int s5p_ohci_port_power_on(struct platform_device *pdev)
{
	struct s5p_ohci_hcd *s5p_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ohci->hcd;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	ohci_writel (ohci, RH_HS_LPSC, &ohci->regs->roothub.status);

	return 0;
}
EXPORT_SYMBOL_GPL(s5p_ohci_port_power_on);
#endif

#ifdef CONFIG_PM
static int ohci_hcd_s5p_drv_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ohci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ohci_hcd *s5p_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ohci->hcd;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	unsigned long flags;
	int rc = 0;

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */
	spin_lock_irqsave(&ohci->lock, flags);
	if (hcd->state != HC_STATE_SUSPENDED && hcd->state != HC_STATE_HALT) {
		spin_unlock_irqrestore(&ohci->lock, flags);
		return -EINVAL;
	}

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	spin_unlock_irqrestore(&ohci->lock, flags);

	if (pdata && pdata->phy_exit)
		pdata->phy_exit(pdev, S5P_USB_PHY_HOST);

	clk_disable(s5p_ohci->clk);

	return rc;
}

static int ohci_hcd_s5p_drv_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ohci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ohci_hcd *s5p_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ohci->hcd;
	int rc = 0;

	clk_enable(s5p_ohci->clk);
	pm_runtime_resume(&pdev->dev);

	if (pdata->phy_init)
		pdata->phy_init(pdev, S5P_USB_PHY_HOST);

	/* Mark hardware accessible again as we are out of D3 state by now */
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	ohci_finish_controller_resume(hcd);

	return rc;
}

#else
#define ohci_hcd_s5p_drv_suspend	NULL
#define ohci_hcd_s5p_drv_resume		NULL
#endif

#ifdef CONFIG_USB_SUSPEND
static int ohci_hcd_s5p_drv_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ohci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ohci_hcd *s5p_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ohci->hcd;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	unsigned long flags;
	int rc = 0;

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */
	spin_lock_irqsave(&ohci->lock, flags);
	if (hcd->state != HC_STATE_SUSPENDED && hcd->state != HC_STATE_HALT) {
		spin_unlock_irqrestore(&ohci->lock, flags);
		err("Not ready %s", hcd->self.bus_name);
		return rc;
	}

	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	(void)ohci_readl(ohci, &ohci->regs->intrdisable);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	spin_unlock_irqrestore(&ohci->lock, flags);

#ifdef CONFIG_USB_EXYNOS_SWITCH
	if (samsung_board_rev_is_0_0())
		ohci_writel (ohci, RH_HS_LPS, &ohci->regs->roothub.status);
#endif
	if (pdata->phy_suspend)
		pdata->phy_suspend(pdev, S5P_USB_PHY_HOST);

	return rc;
}

static int ohci_hcd_s5p_drv_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ohci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ohci_hcd *s5p_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ohci->hcd;
#ifdef CONFIG_USB_EXYNOS_SWITCH
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
#endif
	if (dev->power.is_suspended)
		return 0;

	if (pdata->phy_resume)
		pdata->phy_resume(pdev, S5P_USB_PHY_HOST);
	/* Mark hardware accessible again as we are out of D3 state by now */
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

#ifdef CONFIG_USB_EXYNOS_SWITCH
	if (samsung_board_rev_is_0_0())
		ohci_writel (ohci, RH_HS_LPSC, &ohci->regs->roothub.status);
#endif

	ohci_finish_controller_resume(hcd);
	return 0;
}
#else
#define ohci_hcd_s5p_drv_runtime_suspend	NULL
#define ohci_hcd_s5p_drv_runtime_resume		NULL
#endif

static int ohci_s5p_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	ohci_dbg(ohci, "ohci_s5p_start, ohci:%p", ohci);

	ret = ohci_init(ohci);
	if (ret < 0)
		return ret;

	ret = ohci_run(ohci);
	if (ret < 0) {
		err("can't start %s", hcd->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}

	return 0;
}

static const struct hc_driver ohci_s5p_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "s5p OHCI",
	.hcd_priv_size		= sizeof(struct ohci_hcd),

	.irq			= ohci_irq,
	.flags			= HCD_MEMORY|HCD_USB11,

	.start			= ohci_s5p_start,
	.stop			= ohci_stop,
	.shutdown		= ohci_shutdown,

	.get_frame_number	= ohci_get_frame,

	.urb_enqueue		= ohci_urb_enqueue,
	.urb_dequeue		= ohci_urb_dequeue,
	.endpoint_disable	= ohci_endpoint_disable,

	.hub_status_data	= ohci_hub_status_data,
	.hub_control		= ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend		= ohci_bus_suspend,
	.bus_resume		= ohci_bus_resume,
#endif
	.start_port_reset	= ohci_start_port_reset,
};

static ssize_t show_ohci_power(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ohci_hcd *s5p_ohci = platform_get_drvdata(pdev);

	return sprintf(buf, "EHCI Power %s\n", (s5p_ohci->power_on) ? "on" : "off");
}

static ssize_t store_ohci_power(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ohci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ohci_hcd *s5p_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ohci->hcd;
	int power_on;
	int irq;
	int retval;

	if (sscanf(buf, "%d", &power_on) != 1)
		return -EINVAL;

	device_lock(dev);
	if (!power_on && s5p_ohci->power_on) {
		printk(KERN_DEBUG "%s: EHCI turns off\n", __func__);
		pm_runtime_forbid(dev);
		s5p_ohci->power_on = 0;
		usb_remove_hcd(hcd);

		if (pdata && pdata->phy_exit)
			pdata->phy_exit(pdev, S5P_USB_PHY_HOST);
	} else if (power_on) {
		printk(KERN_DEBUG "%s: EHCI turns on\n", __func__);
		if (s5p_ohci->power_on) {
			usb_remove_hcd(hcd);
		}

		if (pdata->phy_init)
			pdata->phy_init(pdev, S5P_USB_PHY_HOST);

		irq = platform_get_irq(pdev, 0);
		retval = usb_add_hcd(hcd, irq,
				IRQF_DISABLED | IRQF_SHARED);
		if (retval < 0) {
			dev_err(dev, "Power On Fail\n");
			goto exit;
		}

		s5p_ohci->power_on = 1;
		pm_runtime_allow(dev);
	}
exit:
	device_unlock(dev);
	return count;
}
static DEVICE_ATTR(ohci_power, 0664, show_ohci_power, store_ohci_power);

static inline int create_ohci_sys_file(struct ohci_hcd *ohci)
{
	return device_create_file(ohci_to_hcd(ohci)->self.controller,
			&dev_attr_ohci_power);
}

static inline void remove_ohci_sys_file(struct ohci_hcd *ohci)
{
	device_remove_file(ohci_to_hcd(ohci)->self.controller,
			&dev_attr_ohci_power);
}
static int __devinit ohci_hcd_s5p_drv_probe(struct platform_device *pdev)
{
	struct s5p_ohci_platdata *pdata;
	struct s5p_ohci_hcd *s5p_ohci;
	struct usb_hcd  *hcd = NULL;
	struct ohci_hcd *ohci;
	struct resource *res;
	int irq;
	int err;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data defined\n");
		return -EINVAL;
	}

	s5p_ohci = kzalloc(sizeof(struct s5p_ohci_hcd), GFP_KERNEL);
	if (!s5p_ohci)
		return -ENOMEM;

	s5p_ohci->dev = &pdev->dev;

	hcd = usb_create_hcd(&ohci_s5p_hc_driver, &pdev->dev,
					dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		err = -ENOMEM;
		goto fail_hcd;
	}

	s5p_ohci->hcd = hcd;
	s5p_ohci->clk = clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(s5p_ohci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(s5p_ohci->clk);
		goto fail_clk;
	}

	err = clk_enable(s5p_ohci->clk);
	if (err)
		goto fail_clken;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = ioremap(res->start, resource_size(res));
	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto fail_io;
	}

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail;
	}

	if (pdata->phy_init)
		pdata->phy_init(pdev, S5P_USB_PHY_HOST);

	ohci = hcd_to_ohci(hcd);
	ohci_hcd_init(ohci);
#ifdef CONFIG_USB_EXYNOS_SWITCH
	if (samsung_board_rev_is_0_0())
		ohci->flags |= OHCI_QUIRK_SUPERIO;
#endif

	err = usb_add_hcd(hcd, irq,
				IRQF_DISABLED | IRQF_SHARED);

	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail;
	}

	platform_set_drvdata(pdev, s5p_ohci);

	create_ohci_sys_file(ohci);
	s5p_ohci->power_on = 1;

#ifdef CONFIG_USB_EXYNOS_SWITCH
	if (samsung_board_rev_is_0_0()) {
		ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
		(void)ohci_readl(ohci, &ohci->regs->intrdisable);

		ohci_writel (ohci, RH_HS_LPS, &ohci->regs->roothub.status);
	}
#endif
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	return 0;

fail:
	iounmap(hcd->regs);
fail_io:
	clk_disable(s5p_ohci->clk);
fail_clken:
	clk_put(s5p_ohci->clk);
fail_clk:
	usb_put_hcd(hcd);
fail_hcd:
	kfree(s5p_ohci);
	return err;
}

static int __devexit ohci_hcd_s5p_drv_remove(struct platform_device *pdev)
{
	struct s5p_ohci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ohci_hcd *s5p_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ohci->hcd;

	if (pdata && pdata->phy_resume)
		pdata->phy_resume(pdev, S5P_USB_PHY_HOST);

	usb_remove_hcd(hcd);

	s5p_ohci->power_on = 0;
	remove_ohci_sys_file(hcd_to_ohci(hcd));

	if (pdata && pdata->phy_exit)
		pdata->phy_exit(pdev, S5P_USB_PHY_HOST);

	iounmap(hcd->regs);

	clk_disable(s5p_ohci->clk);
	clk_put(s5p_ohci->clk);

	usb_put_hcd(hcd);
	kfree(s5p_ohci);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static void ohci_hcd_s5p_drv_shutdown(struct platform_device *pdev)
{
	struct s5p_ohci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ohci_hcd *s5p_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ohci->hcd;

	if (!s5p_ohci->power_on)
		return;

	if (pdata && pdata->phy_resume)
		pdata->phy_resume(pdev, S5P_USB_PHY_HOST);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static const struct dev_pm_ops ohci_s5p_pm_ops = {
	.suspend		= ohci_hcd_s5p_drv_suspend,
	.resume			= ohci_hcd_s5p_drv_resume,
	.runtime_suspend	= ohci_hcd_s5p_drv_runtime_suspend,
	.runtime_resume		= ohci_hcd_s5p_drv_runtime_resume,
};

static struct platform_driver ohci_hcd_s5p_driver = {
	.probe		= ohci_hcd_s5p_drv_probe,
	.remove		= __devexit_p(ohci_hcd_s5p_drv_remove),
	.shutdown	= ohci_hcd_s5p_drv_shutdown,
	.driver = {
		.name	= "s5p-ohci",
		.owner	= THIS_MODULE,
		.pm	= &ohci_s5p_pm_ops,
	}
};

MODULE_ALIAS("platform:s5p-ohci");
