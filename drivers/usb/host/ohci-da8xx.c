/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * TI DA8xx (OMAP-L1x) Bus Glue
 *
 * Derived from: ohci-omap.c and ohci-s3c2410.c
 * Copyright (C) 2008-2009 MontaVista Software, Inc. <source@mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_data/usb-davinci.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <asm/unaligned.h>

#include "ohci.h"

#define DRIVER_DESC "DA8XX"
#define DRV_NAME "ohci"

static struct hc_driver __read_mostly ohci_da8xx_hc_driver;

static int (*orig_ohci_hub_control)(struct usb_hcd  *hcd, u16 typeReq,
			u16 wValue, u16 wIndex, char *buf, u16 wLength);
static int (*orig_ohci_hub_status_data)(struct usb_hcd *hcd, char *buf);

static struct clk *usb11_clk;
static struct phy *usb11_phy;

/* Over-current indicator change bitmask */
static volatile u16 ocic_mask;

static int ohci_da8xx_enable(void)
{
	int ret;

	ret = clk_prepare_enable(usb11_clk);
	if (ret)
		return ret;

	ret = phy_init(usb11_phy);
	if (ret)
		goto err_phy_init;

	ret = phy_power_on(usb11_phy);
	if (ret)
		goto err_phy_power_on;

	return 0;

err_phy_power_on:
	phy_exit(usb11_phy);
err_phy_init:
	clk_disable_unprepare(usb11_clk);

	return ret;
}

static void ohci_da8xx_disable(void)
{
	phy_power_off(usb11_phy);
	phy_exit(usb11_phy);
	clk_disable_unprepare(usb11_clk);
}

/*
 * Handle the port over-current indicator change.
 */
static void ohci_da8xx_ocic_handler(struct da8xx_ohci_root_hub *hub,
				    unsigned port)
{
	ocic_mask |= 1 << port;

	/* Once over-current is detected, the port needs to be powered down */
	if (hub->get_oci(port) > 0)
		hub->set_power(port, 0);
}

static int ohci_da8xx_reset(struct usb_hcd *hcd)
{
	struct device *dev		= hcd->self.controller;
	struct da8xx_ohci_root_hub *hub	= dev_get_platdata(dev);
	struct ohci_hcd	*ohci		= hcd_to_ohci(hcd);
	int result;
	u32 rh_a;

	dev_dbg(dev, "starting USB controller\n");

	result = ohci_da8xx_enable();
	if (result < 0)
		return result;

	/*
	 * DA8xx only have 1 port connected to the pins but the HC root hub
	 * register A reports 2 ports, thus we'll have to override it...
	 */
	ohci->num_ports = 1;

	result = ohci_setup(hcd);
	if (result < 0) {
		ohci_da8xx_disable();
		return result;
	}

	/*
	 * Since we're providing a board-specific root hub port power control
	 * and over-current reporting, we have to override the HC root hub A
	 * register's default value, so that ohci_hub_control() could return
	 * the correct hub descriptor...
	 */
	rh_a = ohci_readl(ohci, &ohci->regs->roothub.a);
	if (hub->set_power) {
		rh_a &= ~RH_A_NPS;
		rh_a |=  RH_A_PSM;
	}
	if (hub->get_oci) {
		rh_a &= ~RH_A_NOCP;
		rh_a |=  RH_A_OCPM;
	}
	rh_a &= ~RH_A_POTPGT;
	rh_a |= hub->potpgt << 24;
	ohci_writel(ohci, rh_a, &ohci->regs->roothub.a);

	return result;
}

/*
 * Update the status data from the hub with the over-current indicator change.
 */
static int ohci_da8xx_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	int length		= orig_ohci_hub_status_data(hcd, buf);

	/* See if we have OCIC bit set on port 1 */
	if (ocic_mask & (1 << 1)) {
		dev_dbg(hcd->self.controller, "over-current indicator change "
			"on port 1\n");

		if (!length)
			length = 1;

		buf[0] |= 1 << 1;
	}
	return length;
}

/*
 * Look at the control requests to the root hub and see if we need to override.
 */
static int ohci_da8xx_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
				  u16 wIndex, char *buf, u16 wLength)
{
	struct device *dev		= hcd->self.controller;
	struct da8xx_ohci_root_hub *hub	= dev_get_platdata(dev);
	int temp;

	switch (typeReq) {
	case GetPortStatus:
		/* Check the port number */
		if (wIndex != 1)
			break;

		dev_dbg(dev, "GetPortStatus(%u)\n", wIndex);

		temp = roothub_portstatus(hcd_to_ohci(hcd), wIndex - 1);

		/* The port power status (PPS) bit defaults to 1 */
		if (hub->get_power && hub->get_power(wIndex) == 0)
			temp &= ~RH_PS_PPS;

		/* The port over-current indicator (POCI) bit is always 0 */
		if (hub->get_oci && hub->get_oci(wIndex) > 0)
			temp |=  RH_PS_POCI;

		/* The over-current indicator change (OCIC) bit is 0 too */
		if (ocic_mask & (1 << wIndex))
			temp |=  RH_PS_OCIC;

		put_unaligned(cpu_to_le32(temp), (__le32 *)buf);
		return 0;
	case SetPortFeature:
		temp = 1;
		goto check_port;
	case ClearPortFeature:
		temp = 0;

check_port:
		/* Check the port number */
		if (wIndex != 1)
			break;

		switch (wValue) {
		case USB_PORT_FEAT_POWER:
			dev_dbg(dev, "%sPortFeature(%u): %s\n",
				temp ? "Set" : "Clear", wIndex, "POWER");

			if (!hub->set_power)
				return -EPIPE;

			return hub->set_power(wIndex, temp) ? -EPIPE : 0;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			dev_dbg(dev, "%sPortFeature(%u): %s\n",
				temp ? "Set" : "Clear", wIndex,
				"C_OVER_CURRENT");

			if (temp)
				ocic_mask |= 1 << wIndex;
			else
				ocic_mask &= ~(1 << wIndex);
			return 0;
		}
	}

	return orig_ohci_hub_control(hcd, typeReq, wValue,
			wIndex, buf, wLength);
}

/*-------------------------------------------------------------------------*/

static int ohci_da8xx_probe(struct platform_device *pdev)
{
	struct da8xx_ohci_root_hub *hub	= dev_get_platdata(&pdev->dev);
	struct usb_hcd	*hcd;
	struct resource *mem;
	int error, irq;

	if (hub == NULL)
		return -ENODEV;

	usb11_clk = devm_clk_get(&pdev->dev, "usb11");
	if (IS_ERR(usb11_clk)) {
		if (PTR_ERR(usb11_clk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get clock.\n");
		return PTR_ERR(usb11_clk);
	}

	usb11_phy = devm_phy_get(&pdev->dev, "usb-phy");
	if (IS_ERR(usb11_phy)) {
		if (PTR_ERR(usb11_phy) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get phy.\n");
		return PTR_ERR(usb11_phy);
	}

	hcd = usb_create_hcd(&ohci_da8xx_hc_driver, &pdev->dev,
				dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(hcd->regs)) {
		error = PTR_ERR(hcd->regs);
		dev_err(&pdev->dev, "failed to map ohci.\n");
		goto err;
	}
	hcd->rsrc_start = mem->start;
	hcd->rsrc_len = resource_size(mem);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		error = -ENODEV;
		goto err;
	}

	error = usb_add_hcd(hcd, irq, 0);
	if (error)
		goto err;

	device_wakeup_enable(hcd->self.controller);

	if (hub->ocic_notify) {
		error = hub->ocic_notify(ohci_da8xx_ocic_handler);
		if (!error)
			return 0;
	}

	usb_remove_hcd(hcd);
err:
	usb_put_hcd(hcd);
	return error;
}

static int ohci_da8xx_remove(struct platform_device *pdev)
{
	struct usb_hcd	*hcd = platform_get_drvdata(pdev);
	struct da8xx_ohci_root_hub *hub	= dev_get_platdata(&pdev->dev);

	hub->ocic_notify(NULL);
	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int ohci_da8xx_suspend(struct platform_device *pdev,
				pm_message_t message)
{
	struct usb_hcd	*hcd	= platform_get_drvdata(pdev);
	struct ohci_hcd	*ohci	= hcd_to_ohci(hcd);
	bool		do_wakeup	= device_may_wakeup(&pdev->dev);
	int		ret;


	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	ret = ohci_suspend(hcd, do_wakeup);
	if (ret)
		return ret;

	ohci_da8xx_disable();
	hcd->state = HC_STATE_SUSPENDED;

	return ret;
}

static int ohci_da8xx_resume(struct platform_device *dev)
{
	struct usb_hcd	*hcd	= platform_get_drvdata(dev);
	struct ohci_hcd	*ohci	= hcd_to_ohci(hcd);
	int ret;

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	ret = ohci_da8xx_enable();
	if (ret)
		return ret;

	dev->dev.power.power_state = PMSG_ON;
	usb_hcd_resume_root_hub(hcd);

	return 0;
}
#endif

static const struct ohci_driver_overrides da8xx_overrides __initconst = {
	.reset		= ohci_da8xx_reset,
};

/*
 * Driver definition to register with platform structure.
 */
static struct platform_driver ohci_hcd_da8xx_driver = {
	.probe		= ohci_da8xx_probe,
	.remove		= ohci_da8xx_remove,
	.shutdown 	= usb_hcd_platform_shutdown,
#ifdef	CONFIG_PM
	.suspend	= ohci_da8xx_suspend,
	.resume		= ohci_da8xx_resume,
#endif
	.driver		= {
		.name	= DRV_NAME,
	},
};

static int __init ohci_da8xx_init(void)
{

	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", DRV_NAME);
	ohci_init_driver(&ohci_da8xx_hc_driver, &da8xx_overrides);

	/*
	 * The Davinci da8xx HW has some unusual quirks, which require
	 * da8xx-specific workarounds. We override certain hc_driver
	 * functions here to achieve that. We explicitly do not enhance
	 * ohci_driver_overrides to allow this more easily, since this
	 * is an unusual case, and we don't want to encourage others to
	 * override these functions by making it too easy.
	 */

	orig_ohci_hub_control = ohci_da8xx_hc_driver.hub_control;
	orig_ohci_hub_status_data = ohci_da8xx_hc_driver.hub_status_data;

	ohci_da8xx_hc_driver.hub_status_data     = ohci_da8xx_hub_status_data;
	ohci_da8xx_hc_driver.hub_control         = ohci_da8xx_hub_control;

	return platform_driver_register(&ohci_hcd_da8xx_driver);
}
module_init(ohci_da8xx_init);

static void __exit ohci_da8xx_exit(void)
{
	platform_driver_unregister(&ohci_hcd_da8xx_driver);
}
module_exit(ohci_da8xx_exit);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
