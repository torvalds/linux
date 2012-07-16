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

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb/ulpi.h>
#include <plat/usb.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/gpio.h>
#include <linux/clk.h>

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

/* Errata i693 */
static struct clk	*utmi_p1_fck;
static struct clk	*utmi_p2_fck;
static struct clk	*xclk60mhsp1_ck;
static struct clk	*xclk60mhsp2_ck;
static struct clk	*usbhost_p1_fck;
static struct clk	*usbhost_p2_fck;
static struct clk	*init_60m_fclk;

/*-------------------------------------------------------------------------*/

static const struct hc_driver ehci_omap_hc_driver;


static inline void ehci_write(void __iomem *base, u32 reg, u32 val)
{
	__raw_writel(val, base + reg);
}

static inline u32 ehci_read(void __iomem *base, u32 reg)
{
	return __raw_readl(base + reg);
}

/* Erratum i693 workaround sequence */
static void omap_ehci_erratum_i693(struct ehci_hcd *ehci)
{
	int ret = 0;

	/* Switch to the internal 60 MHz clock */
	ret = clk_set_parent(utmi_p1_fck, init_60m_fclk);
	if (ret != 0)
		ehci_err(ehci, "init_60m_fclk set parent"
			"failed error:%d\n", ret);

	ret = clk_set_parent(utmi_p2_fck, init_60m_fclk);
	if (ret != 0)
		ehci_err(ehci, "init_60m_fclk set parent"
			"failed error:%d\n", ret);

	clk_enable(usbhost_p1_fck);
	clk_enable(usbhost_p2_fck);

	/* Wait 1ms and switch back to the external clock */
	mdelay(1);
	ret = clk_set_parent(utmi_p1_fck, xclk60mhsp1_ck);
	if (ret != 0)
		ehci_err(ehci, "xclk60mhsp1_ck set parent"
			"failed error:%d\n", ret);

	ret = clk_set_parent(utmi_p2_fck, xclk60mhsp2_ck);
	if (ret != 0)
		ehci_err(ehci, "xclk60mhsp2_ck set parent"
			"failed error:%d\n", ret);

	clk_disable(usbhost_p1_fck);
	clk_disable(usbhost_p2_fck);
}

static void omap_ehci_soft_phy_reset(struct platform_device *pdev, u8 port)
{
	struct usb_hcd	*hcd = dev_get_drvdata(&pdev->dev);
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
			dev_dbg(&pdev->dev, "phy reset operation timed out\n");
			break;
		}
	}
}

static int omap_ehci_hub_control(
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
)
{
	struct ehci_hcd	*ehci = hcd_to_ehci(hcd);
	u32 __iomem *status_reg = &ehci->regs->port_status[
				(wIndex & 0xff) - 1];
	u32		temp;
	unsigned long	flags;
	int		retval = 0;

	spin_lock_irqsave(&ehci->lock, flags);

	if (typeReq == SetPortFeature && wValue == USB_PORT_FEAT_SUSPEND) {
		temp = ehci_readl(ehci, status_reg);
		if ((temp & PORT_PE) == 0 || (temp & PORT_RESET) != 0) {
			retval = -EPIPE;
			goto done;
		}

		temp &= ~PORT_WKCONN_E;
		temp |= PORT_WKDISC_E | PORT_WKOC_E;
		ehci_writel(ehci, temp | PORT_SUSPEND, status_reg);

		omap_ehci_erratum_i693(ehci);

		set_bit((wIndex & 0xff) - 1, &ehci->suspended_ports);
		goto done;
	}

	spin_unlock_irqrestore(&ehci->lock, flags);

	/* Handle the hub control events here */
	return ehci_hub_control(hcd, typeReq, wValue, wIndex, buf, wLength);
done:
	spin_unlock_irqrestore(&ehci->lock, flags);
	return retval;
}

static void disable_put_regulator(
		struct ehci_hcd_omap_platform_data *pdata)
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
	struct ehci_hcd_omap_platform_data	*pdata = dev->platform_data;
	struct resource				*res;
	struct usb_hcd				*hcd;
	void __iomem				*regs;
	struct ehci_hcd				*omap_ehci;
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
	if (!res) {
		dev_err(dev, "UHH EHCI get resource failed\n");
		return -ENODEV;
	}

	regs = ioremap(res->start, resource_size(res));
	if (!regs) {
		dev_err(dev, "UHH EHCI ioremap failed\n");
		return -ENOMEM;
	}

	hcd = usb_create_hcd(&ehci_omap_hc_driver, dev,
			dev_name(dev));
	if (!hcd) {
		dev_err(dev, "failed to create hcd with err %d\n", ret);
		ret = -ENOMEM;
		goto err_io;
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

	/* Hold PHYs in reset while initializing EHCI controller */
	if (pdata->phy_reset) {
		if (gpio_is_valid(pdata->reset_gpio_port[0]))
			gpio_set_value_cansleep(pdata->reset_gpio_port[0], 0);

		if (gpio_is_valid(pdata->reset_gpio_port[1]))
			gpio_set_value_cansleep(pdata->reset_gpio_port[1], 0);

		/* Hold the PHY in RESET for enough time till DIR is high */
		udelay(10);
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

	/* Soft reset the PHY using PHY reset command over ULPI */
	if (pdata->port_mode[0] == OMAP_EHCI_PORT_MODE_PHY)
		omap_ehci_soft_phy_reset(pdev, 0);
	if (pdata->port_mode[1] == OMAP_EHCI_PORT_MODE_PHY)
		omap_ehci_soft_phy_reset(pdev, 1);

	omap_ehci = hcd_to_ehci(hcd);
	omap_ehci->sbrn = 0x20;

	/* we know this is the memory we want, no need to ioremap again */
	omap_ehci->caps = hcd->regs;
	omap_ehci->regs = hcd->regs
		+ HC_LENGTH(ehci, readl(&omap_ehci->caps->hc_capbase));

	dbg_hcs_params(omap_ehci, "reset");
	dbg_hcc_params(omap_ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	omap_ehci->hcs_params = readl(&omap_ehci->caps->hcs_params);

	ehci_reset(omap_ehci);
	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret) {
		dev_err(dev, "failed to add hcd with err %d\n", ret);
		goto err_add_hcd;
	}

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

	/* root ports should always stay powered */
	ehci_port_power(omap_ehci, 1);

	/* get clocks */
	utmi_p1_fck = clk_get(dev, "utmi_p1_gfclk");
	if (IS_ERR(utmi_p1_fck)) {
		ret = PTR_ERR(utmi_p1_fck);
		dev_err(dev, "utmi_p1_gfclk failed error:%d\n",	ret);
		goto err_add_hcd;
	}

	xclk60mhsp1_ck = clk_get(dev, "xclk60mhsp1_ck");
	if (IS_ERR(xclk60mhsp1_ck)) {
		ret = PTR_ERR(xclk60mhsp1_ck);
		dev_err(dev, "xclk60mhsp1_ck failed error:%d\n", ret);
		goto err_utmi_p1_fck;
	}

	utmi_p2_fck = clk_get(dev, "utmi_p2_gfclk");
	if (IS_ERR(utmi_p2_fck)) {
		ret = PTR_ERR(utmi_p2_fck);
		dev_err(dev, "utmi_p2_gfclk failed error:%d\n", ret);
		goto err_xclk60mhsp1_ck;
	}

	xclk60mhsp2_ck = clk_get(dev, "xclk60mhsp2_ck");
	if (IS_ERR(xclk60mhsp2_ck)) {
		ret = PTR_ERR(xclk60mhsp2_ck);
		dev_err(dev, "xclk60mhsp2_ck failed error:%d\n", ret);
		goto err_utmi_p2_fck;
	}

	usbhost_p1_fck = clk_get(dev, "usb_host_hs_utmi_p1_clk");
	if (IS_ERR(usbhost_p1_fck)) {
		ret = PTR_ERR(usbhost_p1_fck);
		dev_err(dev, "usbhost_p1_fck failed error:%d\n", ret);
		goto err_xclk60mhsp2_ck;
	}

	usbhost_p2_fck = clk_get(dev, "usb_host_hs_utmi_p2_clk");
	if (IS_ERR(usbhost_p2_fck)) {
		ret = PTR_ERR(usbhost_p2_fck);
		dev_err(dev, "usbhost_p2_fck failed error:%d\n", ret);
		goto err_usbhost_p1_fck;
	}

	init_60m_fclk = clk_get(dev, "init_60m_fclk");
	if (IS_ERR(init_60m_fclk)) {
		ret = PTR_ERR(init_60m_fclk);
		dev_err(dev, "init_60m_fclk failed error:%d\n", ret);
		goto err_usbhost_p2_fck;
	}

	return 0;

err_usbhost_p2_fck:
	clk_put(usbhost_p2_fck);

err_usbhost_p1_fck:
	clk_put(usbhost_p1_fck);

err_xclk60mhsp2_ck:
	clk_put(xclk60mhsp2_ck);

err_utmi_p2_fck:
	clk_put(utmi_p2_fck);

err_xclk60mhsp1_ck:
	clk_put(xclk60mhsp1_ck);

err_utmi_p1_fck:
	clk_put(utmi_p1_fck);

err_add_hcd:
	disable_put_regulator(pdata);
	pm_runtime_put_sync(dev);

err_io:
	iounmap(regs);
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
	struct ehci_hcd_omap_platform_data *pdata	= dev->platform_data;

	usb_remove_hcd(hcd);
	disable_put_regulator(dev->platform_data);
	iounmap(hcd->regs);
	usb_put_hcd(hcd);

	clk_put(utmi_p1_fck);
	clk_put(utmi_p2_fck);
	clk_put(xclk60mhsp1_ck);
	clk_put(xclk60mhsp2_ck);
	clk_put(usbhost_p1_fck);
	clk_put(usbhost_p2_fck);
	clk_put(init_60m_fclk);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	if (pdata->phy_reset) {
		if (gpio_is_valid(pdata->reset_gpio_port[0]))
			gpio_free(pdata->reset_gpio_port[0]);

		if (gpio_is_valid(pdata->reset_gpio_port[1]))
			gpio_free(pdata->reset_gpio_port[1]);
	}
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
		.name		= "ehci-omap",
	}
};

/*-------------------------------------------------------------------------*/

static const struct hc_driver ehci_omap_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "OMAP-EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
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

	/*
	 * scheduling support
	 */
	.get_frame_number	= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= omap_ehci_hub_control,
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
};

MODULE_ALIAS("platform:omap-ehci");
MODULE_AUTHOR("Texas Instruments, Inc.");
MODULE_AUTHOR("Felipe Balbi <felipe.balbi@nokia.com>");

