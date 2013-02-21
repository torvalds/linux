/* ehci-msm.c - HSUSB Host Controller Driver Implementation
 *
 * Copyright (c) 2008-2011, Code Aurora Forum. All rights reserved.
 *
 * Partly derived from ehci-fsl.c and ehci-hcd.c
 * Copyright (c) 2000-2004 by David Brownell
 * Copyright (c) 2005 MontaVista Software
 *
 * All source code in this file is licensed under the following license except
 * where indicated.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can find it at http://www.fsf.org
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/device.h>

#include "ehci.h"


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
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
};

static int ehci_rk_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource *res;
	struct clk *clk1;
	struct clk *clk2;
	struct clk *clk3;
	struct device *dev = &pdev->dev;
	int ret;
	static u64 usb_dmamask = 0xffffffffUL;

	dev_dbg(&pdev->dev, "ehci_rk proble\n");
	
	dev->dma_mask = &usb_dmamask;

	hcd = usb_create_hcd(&rk_hc_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return  -ENOMEM;
	}
    clk1 = clk_get(NULL, "hclk_hsic");
    clk2 = clk_get(NULL, "hsicphy_480m");
    clk3 = clk_get(NULL, "hsicphy_12m");
    clk_enable(clk1);
    clk_enable(clk2);
    clk_enable(clk3);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get memory resource\n");
		ret = -ENODEV;
		goto put_hcd;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto put_hcd;
	}
	
	hcd->irq = platform_get_irq(pdev, 0);
	if (hcd->irq < 0) {
		dev_err(&pdev->dev, "Unable to get IRQ resource\n");
		ret = hcd->irq;
		goto put_hcd;
	}
	
    ehci = hcd_to_ehci(hcd);
    ehci->caps = hcd->regs;
    ehci->regs = hcd->regs + 0x10;
    printk("%s %p %p\n", __func__, ehci->caps, ehci->regs);
    
    dbg_hcs_params(ehci, "reset");
    dbg_hcc_params(ehci, "reset");

    ehci->hcs_params = readl(&ehci->caps->hcs_params);

	ret = usb_add_hcd(hcd, hcd->irq, IRQF_DISABLED | IRQF_SHARED);
    if (ret) {
        dev_err(&pdev->dev, "Failed to add USB HCD\n");
          goto unmap;
    }

	ehci_port_power(ehci, 1);
	writel_relaxed(1 ,hcd->regs +0xb0);
	writel_relaxed(0x1d4d ,hcd->regs +0x90);
	writel_relaxed(0x4 ,hcd->regs +0xa0);
	dsb();


	printk("%s ok\n", __func__);

	return 0;

unmap:
	iounmap(hcd->regs);
put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int __devexit ehci_rk_remove(struct platform_device *pdev)
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

static struct platform_driver ehci_rk_driver = {
	.probe	= ehci_rk_probe,
	.remove	= __devexit_p(ehci_rk_remove),
	.driver = {
		   .name = "rk_hsusb_host",
		   .pm = &ehci_rk_dev_pm_ops,
	},
};

