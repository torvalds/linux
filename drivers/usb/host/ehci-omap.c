/*
 * ehci-omap.c - driver for USBHOST on OMAP3/4 processors
 *
 * Bus Glue for the EHCI controllers in OMAP3/4
 * Tested on several OMAP3 boards, and OMAP4 Pandaboard
 *
 * Copyright (C) 2007-2011 Texas Instruments, Inc.
 *	Author: Vikram Pandita <vikram.pandita@ti.com>
 *	Author: Anand Gadiyar <gadiyar@ti.com>
 *	Author: Keshava Munegowda <keshava_mgowda@ti.com>
 *
 * Copyright (C) 2009 Nokia Corporation
 *	Contact: Felipe Balbi <felipe.balbi@nokia.com>
 *
 * Based on "ehci-fsl.c" and "ehci-au1xxx.c" ehci glue layers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * TODO (last updated Feb 27, 2010):
 *	- add kernel-doc
 *	- enable AUTOIDLE
 *	- add suspend/resume
 *	- add HSIC and TLL support
 *	- convert to use hwmod and runtime PM
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb/ulpi.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ehci.h"

#include <linux/platform_data/usb-omap.h>

/* EHCI Register Set */
#define EHCI_INSNREG04					(0xA0)
#define EHCI_INSNREG04_DISABLE_UNSUSPEND		(1 << 5)
#define	EHCI_INSNREG05_ULPI				(0xA4)
#define	EHCI_INSNREG05_ULPI_CONTROL_SHIFT		31
#define	EHCI_INSNREG05_ULPI_PORTSEL_SHIFT		24
#define	EHCI_INSNREG05_ULPI_OPSEL_SHIFT			22
#define	EHCI_INSNREG05_ULPI_REGADD_SHIFT		16
#define	EHCI_INSNREG05_ULPI_EXTREGADD_SHIFT		8
#define	EHCI_INSNREG05_ULPI_WRDATA_SHIFT		0

#define DRIVER_DESC "OMAP-EHCI Host Controller driver"

static const char hcd_name[] = "ehci-omap";

/*-------------------------------------------------------------------------*/


static inline void ehci_write(void __iomem *base, u32 reg, u32 val)
{
	__raw_writel(val, base + reg);
}

static inline u32 ehci_read(void __iomem *base, u32 reg)
{
	return __raw_readl(base + reg);
}


static void omap_ehci_soft_phy_reset(struct usb_hcd *hcd, u8 port)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	unsigned reg = 0;

	reg = ULPI_FUNC_CTRL_RESET
		/* FUNCTION_CTRL_SET register */
		| (ULPI_SET(ULPI_FUNC_CTRL) << EHCI_INSNREG05_ULPI_REGADD_SHIFT)
		/* Write */
		| (2 << EHCI_INSNREG05_ULPI_OPSEL_SHIFT)
		/* PORTn */
		| ((port + 1) << EHCI_INSNREG05_ULPI_PORTSEL_SHIFT)
		/* start ULPI access*/
		| (1 << EHCI_INSNREG05_ULPI_CONTROL_SHIFT);

	ehci_write(hcd->regs, EHCI_INSNREG05_ULPI, reg);

	/* Wait for ULPI access completion */
	while ((ehci_read(hcd->regs, EHCI_INSNREG05_ULPI)
			& (1 << EHCI_INSNREG05_ULPI_CONTROL_SHIFT))) {
		cpu_relax();

		if (time_after(jiffies, timeout)) {
			dev_dbg(hcd->self.controller,
					"phy reset operation timed out\n");
			break;
		}
	}
}

static int omap_ehci_init(struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	int			rc;
	struct usbhs_omap_platform_data	*pdata;

	pdata = hcd->self.controller->platform_data;

	/* Hold PHYs in reset while initializing EHCI controller */
	if (pdata->phy_reset) {
		if (gpio_is_valid(pdata->reset_gpio_port[0]))
			gpio_set_value_cansleep(pdata->reset_gpio_port[0], 0);

		if (gpio_is_valid(pdata->reset_gpio_port[1]))
			gpio_set_value_cansleep(pdata->reset_gpio_port[1], 0);

		/* Hold the PHY in RESET for enough time till DIR is high */
		udelay(10);
	}

	/* Soft reset the PHY using PHY reset command over ULPI */
	if (pdata->port_mode[0] == OMAP_EHCI_PORT_MODE_PHY)
		omap_ehci_soft_phy_reset(hcd, 0);
	if (pdata->port_mode[1] == OMAP_EHCI_PORT_MODE_PHY)
		omap_ehci_soft_phy_reset(hcd, 1);

	/* we know this is the memory we want, no need to ioremap again */
	ehci->caps = hcd->regs;

	rc = ehci_setup(hcd);

	if (pdata->phy_reset) {
		/* Hold the PHY in RESET for enough time till
		 * PHY is settled and ready
		 */
		udelay(10);

		if (gpio_is_valid(pdata->reset_gpio_port[0]))
			gpio_set_value_cansleep(pdata->reset_gpio_port[0], 1);

		if (gpio_is_valid(pdata->reset_gpio_port[1]))
			gpio_set_value_cansleep(pdata->reset_gpio_port[1], 1);
	}

	return rc;
}

static void disable_put_regulator(
		struct usbhs_omap_platform_data *pdata)
{
	int i;

	for (i = 0 ; i < OMAP3_HS_USB_PORTS ; i++) {
		if (pdata->regulator[i]) {
			regulator_disable(pdata->regulator[i]);
			regulator_put(pdata->regulator[i]);
		}
	}
}

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

static struct hc_driver __read_mostly ehci_omap_hc_driver;

static const struct ehci_driver_overrides ehci_omap_overrides __initdata = {
	.reset =		omap_ehci_init,
};

/**
 * ehci_hcd_omap_probe - initialize TI-based HCDs
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
static int ehci_hcd_omap_probe(struct platform_device *pdev)
{
	struct device				*dev = &pdev->dev;
	struct usbhs_omap_platform_data		*pdata = dev->platform_data;
	struct resource				*res;
	struct usb_hcd				*hcd;
	void __iomem				*regs;
	int					ret = -ENODEV;
	int					irq;
	int					i;
	char					supply[7];

	if (usb_disabled())
		return -ENODEV;

	if (!dev->parent) {
		dev_err(dev, "Missing parent device\n");
		return -ENODEV;
	}

	irq = platform_get_irq_byname(pdev, "ehci-irq");
	if (irq < 0) {
		dev_err(dev, "EHCI irq failed\n");
		return -ENODEV;
	}

	res =  platform_get_resource_byname(pdev,
				IORESOURCE_MEM, "ehci");
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	hcd = usb_create_hcd(&ehci_omap_hc_driver, dev,
			dev_name(dev));
	if (!hcd) {
		dev_err(dev, "Failed to create HCD\n");
		return -ENOMEM;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = regs;

	/* get ehci regulator and enable */
	for (i = 0 ; i < OMAP3_HS_USB_PORTS ; i++) {
		if (pdata->port_mode[i] != OMAP_EHCI_PORT_MODE_PHY) {
			pdata->regulator[i] = NULL;
			continue;
		}
		snprintf(supply, sizeof(supply), "hsusb%d", i);
		pdata->regulator[i] = regulator_get(dev, supply);
		if (IS_ERR(pdata->regulator[i])) {
			pdata->regulator[i] = NULL;
			dev_dbg(dev,
			"failed to get ehci port%d regulator\n", i);
		} else {
			regulator_enable(pdata->regulator[i]);
		}
	}

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	/*
	 * An undocumented "feature" in the OMAP3 EHCI controller,
	 * causes suspended ports to be taken out of suspend when
	 * the USBCMD.Run/Stop bit is cleared (for example when
	 * we do ehci_bus_suspend).
	 * This breaks suspend-resume if the root-hub is allowed
	 * to suspend. Writing 1 to this undocumented register bit
	 * disables this feature and restores normal behavior.
	 */
	ehci_write(regs, EHCI_INSNREG04,
				EHCI_INSNREG04_DISABLE_UNSUSPEND);

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret) {
		dev_err(dev, "failed to add hcd with err %d\n", ret);
		goto err_pm_runtime;
	}


	return 0;

err_pm_runtime:
	disable_put_regulator(pdata);
	pm_runtime_put_sync(dev);
	usb_put_hcd(hcd);

	return ret;
}


/**
 * ehci_hcd_omap_remove - shutdown processing for EHCI HCDs
 * @pdev: USB Host Controller being removed
 *
 * Reverses the effect of usb_ehci_hcd_omap_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 */
static int ehci_hcd_omap_remove(struct platform_device *pdev)
{
	struct device *dev				= &pdev->dev;
	struct usb_hcd *hcd				= dev_get_drvdata(dev);

	usb_remove_hcd(hcd);
	disable_put_regulator(dev->platform_data);
	usb_put_hcd(hcd);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return 0;
}

static void ehci_hcd_omap_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = dev_get_drvdata(&pdev->dev);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static struct platform_driver ehci_hcd_omap_driver = {
	.probe			= ehci_hcd_omap_probe,
	.remove			= ehci_hcd_omap_remove,
	.shutdown		= ehci_hcd_omap_shutdown,
	/*.suspend		= ehci_hcd_omap_suspend, */
	/*.resume		= ehci_hcd_omap_resume, */
	.driver = {
		.name		= hcd_name,
	}
};

/*-------------------------------------------------------------------------*/

static int __init ehci_omap_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ehci_init_driver(&ehci_omap_hc_driver, &ehci_omap_overrides);
	return platform_driver_register(&ehci_hcd_omap_driver);
}
module_init(ehci_omap_init);

static void __exit ehci_omap_cleanup(void)
{
	platform_driver_unregister(&ehci_hcd_omap_driver);
}
module_exit(ehci_omap_cleanup);

MODULE_ALIAS("platform:ehci-omap");
MODULE_AUTHOR("Texas Instruments, Inc.");
MODULE_AUTHOR("Felipe Balbi <felipe.balbi@nokia.com>");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
