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
# include "../dwc_otg_310/usbdev_rk.h"

static int rkehci_status = 1;
static struct ehci_hcd *g_ehci;
struct rk_ehci_hcd {
	struct ehci_hcd *ehci;
	uint8_t host_enabled;
	uint8_t host_setenable;
	struct rkehci_platform_data *pldata;
	struct timer_list connect_detect_timer;
	struct delayed_work host_enable_work;
};
#define EHCI_PRINT(x...)   printk(KERN_INFO "EHCI: " x)

static struct rkehci_pdata_id rkehci_pdata[] = {
	{
	 .name = "rk3188-reserved",
	 .pdata = NULL,
	 },
	{
	 .name = "rk3288-ehci",
	 .pdata = &rkehci_pdata_rk3288,
	 },
	{},
};

static void ehci_port_power(struct ehci_hcd *ehci, int is_on)
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

static void rk_ehci_hcd_enable(struct work_struct *work)
{
	struct rk_ehci_hcd *rk_ehci;
	struct usb_hcd *hcd;
	struct rkehci_platform_data *pldata;
	struct ehci_hcd *ehci;

	rk_ehci = container_of(work, struct rk_ehci_hcd, host_enable_work.work);
	pldata = rk_ehci->pldata;
	ehci = rk_ehci->ehci;
	hcd = ehci_to_hcd(ehci);

	if (rk_ehci->host_enabled == rk_ehci->host_setenable) {
		printk("%s, enable flag %d\n", __func__,
		       rk_ehci->host_setenable);
		goto out;
	}

	if (rk_ehci->host_setenable == 2) {
		/* enable -> disable */
		if (pldata->get_status(USB_STATUS_DPDM)) {
			/* usb device connected */
			rk_ehci->host_setenable = 1;
			goto out;
		}

		printk("%s, disable host controller\n", __func__);
		ehci_port_power(ehci, 0);
		usb_remove_hcd(hcd);

		/* reset cru and reinitialize EHCI controller */
		pldata->soft_reset(pldata, RST_RECNT);
		usb_add_hcd(hcd, hcd->irq, IRQF_DISABLED | IRQF_SHARED);
		if (pldata->phy_suspend)
			pldata->phy_suspend(pldata, USB_PHY_SUSPEND);
		/* do not disable EHCI clk, otherwise RK3288
		 * host1(DWC_OTG) can't work normally.
		 */
		/* pldata->clock_enable(pldata, 0); */
	} else if (rk_ehci->host_setenable == 1) {
		/* pldata->clock_enable(pldata, 1); */
		if (pldata->phy_suspend)
			pldata->phy_suspend(pldata, USB_PHY_ENABLED);
		mdelay(5);
		ehci_port_power(ehci, 1);
		printk("%s, enable host controller\n", __func__);
	}
	rk_ehci->host_enabled = rk_ehci->host_setenable;

out:
	return;
}

static void rk_ehci_hcd_connect_detect(unsigned long pdata)
{
	struct rk_ehci_hcd *rk_ehci = (struct rk_ehci_hcd *)pdata;
	struct ehci_hcd *ehci = rk_ehci->ehci;
	struct rkehci_platform_data *pldata;
	uint32_t status;
	unsigned long flags;

	local_irq_save(flags);

	pldata = rk_ehci->pldata;

	if (pldata->get_status(USB_STATUS_DPDM)) {
		/* usb device connected */
		rk_ehci->host_setenable = 1;
	} else {
		/* no device, suspend host */
		status = readl(&ehci->regs->port_status[0]);
		if (!(status & PORT_CONNECT)) {
			rk_ehci->host_setenable = 2;
		}
	}

	if ((rk_ehci->host_enabled)
	    && (rk_ehci->host_setenable != rk_ehci->host_enabled)) {
		schedule_delayed_work(&rk_ehci->host_enable_work, 1);
	}

	mod_timer(&rk_ehci->connect_detect_timer, jiffies + (HZ << 1));

	local_irq_restore(flags);
	return;
}

static struct hc_driver rk_ehci_hc_driver = {
	.description = hcd_name,
	.product_desc = "Rockchip On-Chip EHCI Host Controller",
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

static ssize_t ehci_power_show(struct device *_dev,
			       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", rkehci_status);
}

static ssize_t ehci_power_store(struct device *_dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	uint32_t val = simple_strtoul(buf, NULL, 16);
	struct usb_hcd *hcd = dev_get_drvdata(_dev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct rkehci_platform_data *pldata = _dev->platform_data;

	printk("%s: %d setting to: %d\n", __func__, rkehci_status, val);
	if (val == rkehci_status)
		goto out;

	rkehci_status = val;
	switch (val) {
	case 0:	/* power down */
		ehci_port_power(ehci, 0);
		msleep(5);
		usb_remove_hcd(hcd);
		break;
	case 1:	/*  power on */
		pldata->soft_reset(pldata, RST_POR);
		usb_add_hcd(hcd, hcd->irq, IRQF_DISABLED | IRQF_SHARED);
		ehci_port_power(ehci, 1);
		break;
	default:
		break;
	}
out:
	return count;
}

static DEVICE_ATTR(ehci_power, S_IRUGO | S_IWUSR, ehci_power_show,
		   ehci_power_store);
static ssize_t debug_show(struct device *_dev, struct device_attribute *attr,
			  char *buf)
{
	volatile uint32_t *addr;

	EHCI_PRINT("******** EHCI Capability Registers **********\n");
	addr = &g_ehci->caps->hc_capbase;
	EHCI_PRINT("HCIVERSION / CAPLENGTH  @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->caps->hcs_params;
	EHCI_PRINT("HCSPARAMS               @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->caps->hcc_params;
	EHCI_PRINT("HCCPARAMS               @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	EHCI_PRINT("********* EHCI Operational Registers *********\n");
	addr = &g_ehci->regs->command;
	EHCI_PRINT("USBCMD                  @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->status;
	EHCI_PRINT("USBSTS                  @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->intr_enable;
	EHCI_PRINT("USBINTR                 @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->frame_index;
	EHCI_PRINT("FRINDEX                 @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->segment;
	EHCI_PRINT("CTRLDSSEGMENT           @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->frame_list;
	EHCI_PRINT("PERIODICLISTBASE        @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->async_next;
	EHCI_PRINT("ASYNCLISTADDR           @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	addr = &g_ehci->regs->configured_flag;
	EHCI_PRINT("CONFIGFLAG              @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	addr = g_ehci->regs->port_status;
	EHCI_PRINT("PORTSC                  @0x%08x:  0x%08x\n",
		   (uint32_t) addr, readl_relaxed(addr));
	return sprintf(buf, "EHCI Registers Dump\n");
}

static DEVICE_ATTR(debug_ehci, S_IRUGO, debug_show, NULL);

static struct of_device_id rk_ehci_of_match[] = {
	{
	 .compatible = "rockchip,rk3288_rk_ehci_host",
	 .data = &rkehci_pdata[RK3288_USB_CTLR],
	 },
	{},
};

MODULE_DEVICE_TABLE(of, rk_ehci_of_match);

static int ehci_rk_probe(struct platform_device *pdev)
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
	struct rkehci_pdata_id *p;
	struct rk_ehci_hcd *rk_ehci;
	const struct of_device_id *match =
	    of_match_device(of_match_ptr(rk_ehci_of_match), &pdev->dev);

	dev_dbg(&pdev->dev, "ehci_rk proble\n");

	if (match) {
		p = (struct rkehci_pdata_id *)match->data;
	} else {
		dev_err(dev, "ehci_rk match failed\n");
		return -EINVAL;
	}

	dev->platform_data = p->pdata;
	pldata = dev->platform_data;
	pldata->dev = dev;

	if (!node) {
		dev_err(dev, "device node not found\n");
		return -EINVAL;
	}

	dev->dma_mask = &usb_dmamask;

	retval = device_create_file(dev, &dev_attr_ehci_power);
	retval = device_create_file(dev, &dev_attr_debug_ehci);
	hcd =
	    usb_create_hcd(&rk_ehci_hc_driver, &pdev->dev,
			   dev_name(&pdev->dev));
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

	if (pldata->phy_suspend)
		pldata->phy_suspend(pldata, USB_PHY_ENABLED);

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

	rk_ehci = devm_kzalloc(&pdev->dev, sizeof(struct rk_ehci_hcd),
			       GFP_KERNEL);
	if (!rk_ehci) {
		ret = -ENOMEM;
		goto put_hcd;
	}

	rk_ehci->ehci = ehci;
	rk_ehci->pldata = pldata;
	rk_ehci->host_enabled = 2;
	rk_ehci->host_setenable = 2;
	rk_ehci->connect_detect_timer.function = rk_ehci_hcd_connect_detect;
	rk_ehci->connect_detect_timer.data = (unsigned long)(rk_ehci);
	init_timer(&rk_ehci->connect_detect_timer);
	mod_timer(&rk_ehci->connect_detect_timer, jiffies + (HZ << 1));
	INIT_DELAYED_WORK(&rk_ehci->host_enable_work, rk_ehci_hcd_enable);

	ehci_port_power(ehci, 0);

	if (pldata->phy_suspend) {
		if (pldata->phy_status == USB_PHY_ENABLED) {
			pldata->phy_suspend(pldata, USB_PHY_SUSPEND);
			/* do not disable EHCI clk, otherwise RK3288
			 * host1(DWC_OTG) can't work normally.
			 * udelay(3);
			 * pldata->clock_enable(pldata, 0);
			 */
		}
	}

	printk("%s ok\n", __func__);

	return 0;

put_hcd:
	if (pldata->clock_enable)
		pldata->clock_enable(pldata, 0);
	usb_put_hcd(hcd);

	return ret;
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
	bool do_wakeup = device_may_wakeup(dev);
	int ret;

	dev_dbg(dev, "ehci-rockchip PM suspend\n");

	ret = ehci_suspend(hcd, do_wakeup);

	return ret;
}

static int ehci_rk_pm_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	dev_dbg(dev, "ehci-rockchip PM resume\n");
	ehci_resume(hcd, false);

	return 0;
}
#else
#define ehci_rk_pm_suspend	NULL
#define ehci_rk_pm_resume	NULL
#endif

static const struct dev_pm_ops ehci_rk_dev_pm_ops = {
	.suspend = ehci_rk_pm_suspend,
	.resume = ehci_rk_pm_resume,
};

static struct platform_driver ehci_rk_driver = {
	.probe = ehci_rk_probe,
	.remove = ehci_rk_remove,
	.driver = {
		   .name = "rockchip_ehci_host",
		   .of_match_table = of_match_ptr(rk_ehci_of_match),
#ifdef CONFIG_PM
		   .pm = &ehci_rk_dev_pm_ops,
#endif
		   },
};
