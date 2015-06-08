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

# include <linux/platform_device.h>
# include <linux/clk.h>
# include <linux/err.h>
# include <linux/device.h>
# include <linux/of.h>
# include <linux/of_platform.h>
# include "ehci.h"
# include "../dwc_otg_310/usbdev_rk.h"

static int rkehci1_status = 1;
static struct ehci_hcd *g_ehci;
#define EHCI1_PRINT(x...)	printk(KERN_INFO "EHCI1: " x)

static void ehci1_rk_port_power(struct ehci_hcd *ehci, int is_on)
{
	unsigned port;

	if (!HCS_PPC(ehci->hcs_params))
		return;

	ehci_dbg(ehci, "...power%s ports...\n", is_on ? "up" : "down");
	for (port = HCS_N_PORTS(ehci->hcs_params); port > 0;)
		(void)ehci_hub_control(ehci_to_hcd(ehci),
				       is_on ? SetPortFeature :
				       ClearPortFeature, USB_PORT_FEAT_POWER,
				       port--, NULL, 0);
	/* Flush those writes */
	ehci_readl(ehci, &ehci->regs->command);
	msleep(20);
}

static struct hc_driver rk_ehci1_driver = {
	.description = hcd_name,
	.product_desc = "Rockchip On-Chip EHCI1 Host Controller",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_USB2 | HCD_MEMORY,

	.reset = ehci_init,
	.start = ehci_run,

	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,
	.endpoint_reset = ehci_endpoint_reset,
	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,
	.relinquish_port = ehci_relinquish_port,
	.port_handed_over = ehci_port_handed_over,

	/*
	 * PM support
	 */
#ifdef CONFIG_PM
	.bus_suspend = ehci_bus_suspend,
	.bus_resume = ehci_bus_resume,
#endif
};

static ssize_t ehci1_rk_power_show(struct device *_dev,
				      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", rkehci1_status);
}

static ssize_t ehci1_rk_power_store(struct device *_dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	uint32_t val = simple_strtoul(buf, NULL, 16);
	struct usb_hcd *hcd = dev_get_drvdata(_dev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct rkehci_platform_data *pldata = _dev->platform_data;

	printk("%s: %d setting to: %d\n", __func__, rkehci1_status, val);
	if (val == rkehci1_status)
		goto out;

	rkehci1_status = val;
	switch (val) {
	case 0:	/* power down */
		ehci1_rk_port_power(ehci, 0);
		writel_relaxed(0, hcd->regs + 0xb0);
		dsb();
		msleep(5);
		usb_remove_hcd(hcd);
		break;
	case 1:	/* power on */
		pldata->soft_reset(pldata, RST_POR);
		usb_add_hcd(hcd, hcd->irq, IRQF_DISABLED | IRQF_SHARED);

		ehci1_rk_port_power(ehci, 1);
		writel_relaxed(1, hcd->regs + 0xb0);
		writel_relaxed(0x1d4d, hcd->regs + 0x90);
		writel_relaxed(0x4, hcd->regs + 0xa0);
		dsb();
		break;
	default:
		break;
	}
out:
	return count;
}

static DEVICE_ATTR(ehci1_rk_power, S_IRUGO | S_IWUSR, ehci1_rk_power_show,
		   ehci1_rk_power_store);

static ssize_t ehci1_debug_show(struct device *_dev,
			       struct device_attribute *attr, char *buf)
{
	volatile uint32_t *addr;

	EHCI1_PRINT("******** EHCI Capability Registers **********\n");
	addr = &g_ehci->caps->hc_capbase;
	EHCI1_PRINT("HCIVERSION / CAPLENGTH  @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->caps->hcs_params;
	EHCI1_PRINT("HCSPARAMS               @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->caps->hcc_params;
	EHCI1_PRINT("HCCPARAMS               @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	EHCI1_PRINT("********* EHCI Operational Registers *********\n");
	addr = &g_ehci->regs->command;
	EHCI1_PRINT("USBCMD                  @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->status;
	EHCI1_PRINT("USBSTS                  @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->intr_enable;
	EHCI1_PRINT("USBINTR                 @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->frame_index;
	EHCI1_PRINT("FRINDEX                 @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->segment;
	EHCI1_PRINT("CTRLDSSEGMENT           @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->frame_list;
	EHCI1_PRINT("PERIODICLISTBASE        @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->async_next;
	EHCI1_PRINT("ASYNCLISTADDR           @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->configured_flag;
	EHCI1_PRINT("CONFIGFLAG              @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	addr = g_ehci->regs->port_status;
	EHCI1_PRINT("PORTSC                  @0x%08x:  0x%08x\n",
			(uint32_t) addr, readl_relaxed(addr));
	return sprintf(buf, "EHCI1 Registers Dump\n");
}

static DEVICE_ATTR(debug_ehci1, S_IRUGO, ehci1_debug_show, NULL);

static struct of_device_id rk_ehci1_of_match[] = {
	{
	 .compatible = "rockchip,rk3188_rk_ehci_host",
	 .data = &rkehci_pdata_rk3188,
	 },
	{
	 .compatible = "rockchip,rk3288_rk_ehci1_host",
	 .data = &rkehci1_pdata_rk3288,
	 },
	{},
};

MODULE_DEVICE_TABLE(of, rk_ehci1_of_match);

static int ehci1_rk_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct rkehci_platform_data *pldata;
	int ret;
	int retval = 0;
	static u64 usb_dmamask = 0xffffffffUL;
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *match =
	    of_match_device(of_match_ptr(rk_ehci1_of_match), &pdev->dev);

	dev_dbg(&pdev->dev, "ehci1_rk proble\n");

	if (match && match->data) {
		dev->platform_data  = (void *)match->data;
	} else {
		dev_err(dev, "ehci1_rk match failed\n");
		return -EINVAL;
	}

	pldata = dev->platform_data;
	pldata->dev = dev;

	if (!node) {
		dev_err(dev, "device node not found\n");
		return -EINVAL;
	}

	dev->dma_mask = &usb_dmamask;

	retval = device_create_file(dev, &dev_attr_ehci1_rk_power);
	retval = device_create_file(dev, &dev_attr_debug_ehci1);
	hcd = usb_create_hcd(&rk_ehci1_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}

	if (pldata->hw_init)
		pldata->hw_init();

	if (pldata->clock_init) {
		pldata->clock_init(pldata);
		pldata->clock_enable(pldata, 1);
	}

	if (pldata->soft_reset)
		pldata->soft_reset(pldata, RST_POR);;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get memory resource\n");
		ret = -ENODEV;
		goto put_hcd;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap_resource(dev, res);

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
		goto put_hcd;
	}

	g_ehci = ehci;
	ehci1_rk_port_power(ehci, 1);
	writel_relaxed(1, hcd->regs + 0xb0);
	writel_relaxed(0x1d4d, hcd->regs + 0x90);
	writel_relaxed(0x4, hcd->regs + 0xa0);
	dsb();

	printk("%s ok\n", __func__);

	return 0;

put_hcd:
	if (pldata->clock_enable)
		pldata->clock_enable(pldata, 0);
	usb_put_hcd(hcd);

	return ret;
}

static int ehci1_rk_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int ehci1_rk_pm_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	bool do_wakeup = device_may_wakeup(dev);
	int ret;

	dev_dbg(dev, "ehci1-rockchip PM suspend\n");
	ret = ehci_suspend(hcd, do_wakeup);

	return ret;
}

static int ehci1_rk_pm_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	dev_dbg(dev, "ehci1-rockchip PM resume\n");
	ehci_resume(hcd, false);

	return 0;
}
#else
#define ehci1_rk_pm_suspend	NULL
#define ehci1_rk_pm_resume	NULL
#endif

static const struct dev_pm_ops ehci1_rk_dev_pm_ops = {
	.suspend = ehci1_rk_pm_suspend,
	.resume = ehci1_rk_pm_resume,
};

static struct platform_driver ehci1_rk_driver = {
	.probe = ehci1_rk_probe,
	.remove = ehci1_rk_remove,
	.driver = {
		   .name = "rockchip_ehci1_host",
		   .of_match_table = of_match_ptr(rk_ehci1_of_match),
#ifdef CONFIG_PM
		   .pm = &ehci1_rk_dev_pm_ops,
#endif
		   },
};
