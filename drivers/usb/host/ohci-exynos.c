/*
 * SAMSUNG EXYNOS USB HOST OHCI Controller
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <mach/ohci.h>
#include <plat/usb-phy.h>

struct exynos_ohci_hcd {
	struct device *dev;
	struct usb_hcd *hcd;
	struct clk *clk;
	int power_on;
};

static int ohci_exynos_init(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	ohci_dbg(ohci, "ohci_exynos_init, ohci:%p", ohci);

	ret = ohci_init(ohci);
	if (ret < 0)
		return ret;

	return 0;
}

static void ohci_exynos_stop(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	ohci_dump(ohci, 1);

	if (quirk_nec(ohci))
		flush_work_sync(&ohci->nec_work);

	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	ohci_usb_reset(ohci);

	free_irq(hcd->irq, hcd);
	hcd->irq = 0;

	if (quirk_zfmicro(ohci))
		del_timer(&ohci->unlink_watchdog);
	if (quirk_amdiso(ohci))
		usb_amd_dev_put();

	remove_debug_files(ohci);
	ohci_mem_cleanup(ohci);
	if (ohci->hcca) {
		dma_free_coherent(hcd->self.controller,
				sizeof *ohci->hcca,
				ohci->hcca, ohci->hcca_dma);
		ohci->hcca = NULL;
		ohci->hcca_dma = 0;
	}
}

static int ohci_exynos_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	ohci_dbg(ohci, "ohci_exynos_start, ohci:%p", ohci);

	ret = ohci_run(ohci);
	if (ret < 0) {
		err("can't start %s", hcd->self.bus_name);
		ohci_exynos_stop(hcd);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_PM
static int exynos_ohci_suspend(struct device *dev)
{
	struct exynos_ohci_hcd *exynos_ohci = dev_get_drvdata(dev);
	struct usb_hcd *hcd = exynos_ohci->hcd;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos4_ohci_platdata *pdata = pdev->dev.platform_data;
	unsigned long flags;

	/*
	 * Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 */
	spin_lock_irqsave(&ohci->lock, flags);
	if (ohci->rh_state != OHCI_RH_SUSPENDED &&
			ohci->rh_state != OHCI_RH_HALTED) {
		spin_unlock_irqrestore(&ohci->lock, flags);
		return -EINVAL;
	}

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	spin_unlock_irqrestore(&ohci->lock, flags);

	if (pdata && pdata->phy_exit)
		pdata->phy_exit(pdev, S5P_USB_PHY_HOST);

	return 0;
}

static int exynos_ohci_resume(struct device *dev)
{
	struct exynos_ohci_hcd *exynos_ohci = dev_get_drvdata(dev);
	struct usb_hcd *hcd = exynos_ohci->hcd;
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos4_ohci_platdata *pdata = pdev->dev.platform_data;

	clk_enable(exynos_ohci->clk);
	if (pdata && pdata->phy_init)
		pdata->phy_init(pdev, S5P_USB_PHY_HOST);

	/* Mark hardware accessible again as we are out of D3 state by now */
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	ohci_finish_controller_resume(hcd);

	/*Update runtime PM status and clear runtime_error */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	/* Prevent device from runtime suspend during resume time */
	pm_runtime_get_sync(dev);

	return 0;
}

static int exynos_ohci_bus_suspend(struct usb_hcd *hcd)
{
	int ret;
	ret = ohci_bus_suspend(hcd);

	/* Decrease pm_count that was increased at s5p_ehci_resume func. */
	pm_runtime_put_noidle(hcd->self.controller);

	return ret;
}

static int exynos_ohci_bus_resume(struct usb_hcd *hcd)
{
	/* When suspend is failed, re-enable clocks & PHY */
	pm_runtime_resume(hcd->self.controller);

	return ohci_bus_resume(hcd);
}
#else
#define exynos_ohci_suspend	NULL
#define exynos_ohci_resume		NULL
#define exynos_ohci_bus_resume			NULL
#endif

static const struct hc_driver exynos_ohci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "EXYNOS OHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ohci_hcd),

	.irq			= ohci_irq,
	.flags			= HCD_MEMORY|HCD_USB11,

	.reset			= ohci_exynos_init,
	.start			= ohci_exynos_start,
	.stop			= ohci_exynos_stop,
	.shutdown		= ohci_shutdown,

	.get_frame_number	= ohci_get_frame,

	.urb_enqueue		= ohci_urb_enqueue,
	.urb_dequeue		= ohci_urb_dequeue,
	.endpoint_disable	= ohci_endpoint_disable,

	.hub_status_data	= ohci_hub_status_data,
	.hub_control		= ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend		= exynos_ohci_bus_suspend,
	.bus_resume		= exynos_ohci_bus_resume,
#endif
	.start_port_reset	= ohci_start_port_reset,
};

static ssize_t show_ohci_power(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos_ohci_hcd *exynos_ohci = platform_get_drvdata(pdev);

	return snprintf(buf, PAGE_SIZE, "EHCI Power %s\n",
			(exynos_ohci->power_on) ? "on" : "off");
}

static ssize_t store_ohci_power(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos4_ohci_platdata *pdata = pdev->dev.platform_data;
	struct exynos_ohci_hcd *exynos_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = exynos_ohci->hcd;
	int power_on;
	int irq;
	int retval;

	if (sscanf(buf, "%d", &power_on) != 1)
		return -EINVAL;

	device_lock(dev);
	if (!power_on && exynos_ohci->power_on) {
		printk(KERN_DEBUG "%s: EHCI turns off\n", __func__);
		pm_runtime_forbid(dev);
		exynos_ohci->power_on = 0;
		usb_remove_hcd(hcd);

		if (pdata && pdata->phy_exit)
			pdata->phy_exit(pdev, S5P_USB_PHY_HOST);
	} else if (power_on) {
		printk(KERN_DEBUG "%s: EHCI turns on\n", __func__);
		if (exynos_ohci->power_on) {
			pm_runtime_forbid(dev);
			usb_remove_hcd(hcd);
		} else {
			if (pdata && pdata->phy_init)
				pdata->phy_init(pdev, S5P_USB_PHY_HOST);
		}

		irq = platform_get_irq(pdev, 0);
		retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
		if (retval < 0) {
			dev_err(dev, "Power On Fail\n");
			goto exit;
		}

		/*
		 * OHCI root hubs are expected to handle remote wakeup.
		 * So, wakeup flag init defaults for root hubs.
		 */
		device_wakeup_enable(&hcd->self.root_hub->dev);

		exynos_ohci->power_on = 1;
		pm_runtime_allow(dev);
	}

exit:
	device_unlock(dev);
	return count;
}
static DEVICE_ATTR(ohci_power, S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP,
	show_ohci_power, store_ohci_power);

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

static int __devinit exynos_ohci_probe(struct platform_device *pdev)
{
	struct exynos4_ohci_platdata *pdata;
	struct exynos_ohci_hcd *exynos_ohci;
	struct usb_hcd *hcd;
	struct ohci_hcd *ohci;
	struct resource *res;
	int irq;
	int err;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data defined\n");
		return -EINVAL;
	}

	exynos_ohci = kzalloc(sizeof(struct exynos_ohci_hcd), GFP_KERNEL);
	if (!exynos_ohci)
		return -ENOMEM;

	exynos_ohci->dev = &pdev->dev;

	hcd = usb_create_hcd(&exynos_ohci_hc_driver, &pdev->dev,
					dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		err = -ENOMEM;
		goto fail_hcd;
	}

	exynos_ohci->hcd = hcd;
	exynos_ohci->clk = clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(exynos_ohci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exynos_ohci->clk);
		goto fail_clk;
	}

	err = clk_enable(exynos_ohci->clk);
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

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);

	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail;
	}

	/*
	 * OHCI root hubs are expected to handle remote wakeup.
	 * So, wakeup flag init defaults for root hubs.
	 */
	device_wakeup_enable(&hcd->self.root_hub->dev);

	platform_set_drvdata(pdev, exynos_ohci);

	create_ohci_sys_file(ohci);
	exynos_ohci->power_on = 1;

#if (defined(CONFIG_USB_SUSPEND)&&!defined(CONFIG_MACH_ODROIDXU))
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get(hcd->self.controller);
#endif
	return 0;

fail:
	iounmap(hcd->regs);
fail_io:
	clk_disable(exynos_ohci->clk);
fail_clken:
	clk_put(exynos_ohci->clk);
fail_clk:
	usb_put_hcd(hcd);
fail_hcd:
	kfree(exynos_ohci);
	return err;
}

static int __devexit exynos_ohci_remove(struct platform_device *pdev)
{
	struct exynos4_ohci_platdata *pdata = pdev->dev.platform_data;
	struct exynos_ohci_hcd *exynos_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = exynos_ohci->hcd;

	if (pdata && pdata->phy_resume)
		pdata->phy_resume(pdev, S5P_USB_PHY_HOST);

#if (defined(CONFIG_USB_SUSPEND)&&!defined(CONFIG_MACH_ODROIDXU))
	pm_runtime_put(hcd->self.controller);
	pm_runtime_disable(&pdev->dev);
#endif
	usb_remove_hcd(hcd);

	exynos_ohci->power_on = 0;
	remove_ohci_sys_file(hcd_to_ohci(hcd));

	if (pdata && pdata->phy_exit)
		pdata->phy_exit(pdev, S5P_USB_PHY_HOST);

	iounmap(hcd->regs);

	clk_disable(exynos_ohci->clk);
	clk_put(exynos_ohci->clk);

	usb_put_hcd(hcd);
	kfree(exynos_ohci);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static void exynos_ohci_shutdown(struct platform_device *pdev)
{
	struct exynos4_ohci_platdata *pdata = pdev->dev.platform_data;
	struct exynos_ohci_hcd *exynos_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = exynos_ohci->hcd;

	if (!exynos_ohci->power_on)
		return;

	if (!hcd->rh_registered)
		return;

	if (pdata && pdata->phy_resume)
		pdata->phy_resume(pdev, S5P_USB_PHY_HOST);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

#if (defined(CONFIG_USB_SUSPEND)&&!defined(CONFIG_MACH_ODROIDXU))
static int exynos_ohci_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos4_ohci_platdata *pdata = pdev->dev.platform_data;
	struct exynos_ohci_hcd *exynos_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = exynos_ohci->hcd;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	unsigned long flags;

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */
	spin_lock_irqsave(&ohci->lock, flags);
	if (ohci->rh_state != OHCI_RH_SUSPENDED && ohci->rh_state != OHCI_RH_HALTED) {
		spin_unlock_irqrestore(&ohci->lock, flags);
		err("Not ready %s", hcd->self.bus_name);
		return 0;
	}

	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	(void)ohci_readl(ohci, &ohci->regs->intrdisable);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	spin_unlock_irqrestore(&ohci->lock, flags);

	if (pdata->phy_suspend)
		pdata->phy_suspend(pdev, S5P_USB_PHY_HOST);

	return 0;
}

static int exynos_ohci_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos4_ohci_platdata *pdata = pdev->dev.platform_data;
	struct exynos_ohci_hcd *exynos_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = exynos_ohci->hcd;

	if (dev->power.is_suspended)
		return 0;

	if (pdata->phy_resume)
		pdata->phy_resume(pdev, S5P_USB_PHY_HOST);
	/* Mark hardware accessible again as we are out of D3 state by now */
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	ohci_finish_controller_resume(hcd);

	return 0;
}
#else
#define exynos_ohci_runtime_suspend	NULL
#define exynos_ohci_runtime_resume	NULL
#endif

static const struct dev_pm_ops exynos_ohci_pm_ops = {
	.suspend	= exynos_ohci_suspend,
	.resume		= exynos_ohci_resume,
	.runtime_suspend	= exynos_ohci_runtime_suspend,
	.runtime_resume		= exynos_ohci_runtime_resume,
};

static struct platform_driver exynos_ohci_driver = {
	.probe		= exynos_ohci_probe,
	.remove		= __devexit_p(exynos_ohci_remove),
	.shutdown	= exynos_ohci_shutdown,
	.driver = {
		.name	= "exynos-ohci",
		.owner	= THIS_MODULE,
		.pm	= &exynos_ohci_pm_ops,
	}
};

MODULE_ALIAS("platform:exynos-ohci");
MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
