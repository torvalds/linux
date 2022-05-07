// SPDX-License-Identifier: GPL-1.0+
/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2005 David Brownell
 * (C) Copyright 2002 Hewlett-Packard Company
 *
 * OMAP Bus Glue
 *
 * Modified for OMAP by Tony Lindgren <tony@atomide.com>
 * Based on the 2.4 OMAP OHCI driver originally done by MontaVista Software Inc.
 * and on ohci-sa1111.c by Christopher Hoover <ch@hpl.hp.com>
 *
 * This file is licenced under the GPL.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb/otg.h>
#include <linux/platform_device.h>
#include <linux/signal.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ohci.h"

#include <asm/io.h>
#include <asm/mach-types.h>

#include <mach/mux.h>

#include <mach/hardware.h>
#include <mach/usb.h>


/* OMAP-1510 OHCI has its own MMU for DMA */
#define OMAP1510_LB_MEMSIZE	32	/* Should be same as SDRAM size */
#define OMAP1510_LB_CLOCK_DIV	0xfffec10c
#define OMAP1510_LB_MMU_CTL	0xfffec208
#define OMAP1510_LB_MMU_LCK	0xfffec224
#define OMAP1510_LB_MMU_LD_TLB	0xfffec228
#define OMAP1510_LB_MMU_CAM_H	0xfffec22c
#define OMAP1510_LB_MMU_CAM_L	0xfffec230
#define OMAP1510_LB_MMU_RAM_H	0xfffec234
#define OMAP1510_LB_MMU_RAM_L	0xfffec238

#define DRIVER_DESC "OHCI OMAP driver"

struct ohci_omap_priv {
	struct clk *usb_host_ck;
	struct clk *usb_dc_ck;
	struct gpio_desc *power;
	struct gpio_desc *overcurrent;
};

static const char hcd_name[] = "ohci-omap";
static struct hc_driver __read_mostly ohci_omap_hc_driver;

#define hcd_to_ohci_omap_priv(h) \
	((struct ohci_omap_priv *)hcd_to_ohci(h)->priv)

static void omap_ohci_clock_power(struct ohci_omap_priv *priv, int on)
{
	if (on) {
		clk_enable(priv->usb_dc_ck);
		clk_enable(priv->usb_host_ck);
		/* guesstimate for T5 == 1x 32K clock + APLL lock time */
		udelay(100);
	} else {
		clk_disable(priv->usb_host_ck);
		clk_disable(priv->usb_dc_ck);
	}
}

/*
 * Board specific gang-switched transceiver power on/off.
 * NOTE:  OSK supplies power from DC, not battery.
 */
static int omap_ohci_transceiver_power(struct ohci_omap_priv *priv, int on)
{
	if (on) {
		if (machine_is_omap_innovator() && cpu_is_omap1510())
			__raw_writeb(__raw_readb(INNOVATOR_FPGA_CAM_USB_CONTROL)
				| ((1 << 5/*usb1*/) | (1 << 3/*usb2*/)),
			       INNOVATOR_FPGA_CAM_USB_CONTROL);
		else if (priv->power)
			gpiod_set_value_cansleep(priv->power, 0);
	} else {
		if (machine_is_omap_innovator() && cpu_is_omap1510())
			__raw_writeb(__raw_readb(INNOVATOR_FPGA_CAM_USB_CONTROL)
				& ~((1 << 5/*usb1*/) | (1 << 3/*usb2*/)),
			       INNOVATOR_FPGA_CAM_USB_CONTROL);
		else if (priv->power)
			gpiod_set_value_cansleep(priv->power, 1);
	}

	return 0;
}

#ifdef CONFIG_ARCH_OMAP15XX
/*
 * OMAP-1510 specific Local Bus clock on/off
 */
static int omap_1510_local_bus_power(int on)
{
	if (on) {
		omap_writel((1 << 1) | (1 << 0), OMAP1510_LB_MMU_CTL);
		udelay(200);
	} else {
		omap_writel(0, OMAP1510_LB_MMU_CTL);
	}

	return 0;
}

/*
 * OMAP-1510 specific Local Bus initialization
 * NOTE: This assumes 32MB memory size in OMAP1510LB_MEMSIZE.
 *       See also arch/mach-omap/memory.h for __virt_to_dma() and
 *       __dma_to_virt() which need to match with the physical
 *       Local Bus address below.
 */
static int omap_1510_local_bus_init(void)
{
	unsigned int tlb;
	unsigned long lbaddr, physaddr;

	omap_writel((omap_readl(OMAP1510_LB_CLOCK_DIV) & 0xfffffff8) | 0x4,
	       OMAP1510_LB_CLOCK_DIV);

	/* Configure the Local Bus MMU table */
	for (tlb = 0; tlb < OMAP1510_LB_MEMSIZE; tlb++) {
		lbaddr = tlb * 0x00100000 + OMAP1510_LB_OFFSET;
		physaddr = tlb * 0x00100000 + PHYS_OFFSET;
		omap_writel((lbaddr & 0x0fffffff) >> 22, OMAP1510_LB_MMU_CAM_H);
		omap_writel(((lbaddr & 0x003ffc00) >> 6) | 0xc,
		       OMAP1510_LB_MMU_CAM_L);
		omap_writel(physaddr >> 16, OMAP1510_LB_MMU_RAM_H);
		omap_writel((physaddr & 0x0000fc00) | 0x300, OMAP1510_LB_MMU_RAM_L);
		omap_writel(tlb << 4, OMAP1510_LB_MMU_LCK);
		omap_writel(0x1, OMAP1510_LB_MMU_LD_TLB);
	}

	/* Enable the walking table */
	omap_writel(omap_readl(OMAP1510_LB_MMU_CTL) | (1 << 3), OMAP1510_LB_MMU_CTL);
	udelay(200);

	return 0;
}
#else
#define omap_1510_local_bus_power(x)	{}
#define omap_1510_local_bus_init()	{}
#endif

#ifdef	CONFIG_USB_OTG

static void start_hnp(struct ohci_hcd *ohci)
{
	struct usb_hcd *hcd = ohci_to_hcd(ohci);
	const unsigned	port = hcd->self.otg_port - 1;
	unsigned long	flags;
	u32 l;

	otg_start_hnp(hcd->usb_phy->otg);

	local_irq_save(flags);
	hcd->usb_phy->otg->state = OTG_STATE_A_SUSPEND;
	writel (RH_PS_PSS, &ohci->regs->roothub.portstatus [port]);
	l = omap_readl(OTG_CTRL);
	l &= ~OTG_A_BUSREQ;
	omap_writel(l, OTG_CTRL);
	local_irq_restore(flags);
}

#endif

/*-------------------------------------------------------------------------*/

static int ohci_omap_reset(struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci(hcd);
	struct omap_usb_config	*config = dev_get_platdata(hcd->self.controller);
	struct ohci_omap_priv	*priv = hcd_to_ohci_omap_priv(hcd);
	int			need_transceiver = (config->otg != 0);
	int			ret;

	dev_dbg(hcd->self.controller, "starting USB Controller\n");

	if (config->otg) {
		hcd->self.otg_port = config->otg;
		/* default/minimum OTG power budget:  8 mA */
		hcd->power_budget = 8;
	}

	/* boards can use OTG transceivers in non-OTG modes */
	need_transceiver = need_transceiver
			|| machine_is_omap_h2() || machine_is_omap_h3();

	/* XXX OMAP16xx only */
	if (config->ocpi_enable)
		config->ocpi_enable();

#ifdef	CONFIG_USB_OTG
	if (need_transceiver) {
		hcd->usb_phy = usb_get_phy(USB_PHY_TYPE_USB2);
		if (!IS_ERR_OR_NULL(hcd->usb_phy)) {
			int	status = otg_set_host(hcd->usb_phy->otg,
						&ohci_to_hcd(ohci)->self);
			dev_dbg(hcd->self.controller, "init %s phy, status %d\n",
					hcd->usb_phy->label, status);
			if (status) {
				usb_put_phy(hcd->usb_phy);
				return status;
			}
		} else {
			return -EPROBE_DEFER;
		}
		hcd->skip_phy_initialization = 1;
		ohci->start_hnp = start_hnp;
	}
#endif

	omap_ohci_clock_power(priv, 1);

	if (cpu_is_omap15xx()) {
		omap_1510_local_bus_power(1);
		omap_1510_local_bus_init();
	}

	ret = ohci_setup(hcd);
	if (ret < 0)
		return ret;

	if (config->otg || config->rwc) {
		ohci->hc_control = OHCI_CTRL_RWC;
		writel(OHCI_CTRL_RWC, &ohci->regs->control);
	}

	/* board-specific power switching and overcurrent support */
	if (machine_is_omap_osk() || machine_is_omap_innovator()) {
		u32	rh = roothub_a (ohci);

		/* power switching (ganged by default) */
		rh &= ~RH_A_NPS;

		/* TPS2045 switch for internal transceiver (port 1) */
		if (machine_is_omap_osk()) {
			ohci_to_hcd(ohci)->power_budget = 250;

			rh &= ~RH_A_NOCP;

			/* gpio9 for overcurrent detction */
			omap_cfg_reg(W8_1610_GPIO9);

			/* for paranoia's sake:  disable USB.PUEN */
			omap_cfg_reg(W4_USB_HIGHZ);
		}
		ohci_writel(ohci, rh, &ohci->regs->roothub.a);
		ohci->flags &= ~OHCI_QUIRK_HUB_POWER;
	} else if (machine_is_nokia770()) {
		/* We require a self-powered hub, which should have
		 * plenty of power. */
		ohci_to_hcd(ohci)->power_budget = 0;
	}

	/* FIXME hub_wq hub requests should manage power switching */
	omap_ohci_transceiver_power(priv, 1);

	/* board init will have already handled HMC and mux setup.
	 * any external transceiver should already be initialized
	 * too, so all configured ports use the right signaling now.
	 */

	return 0;
}

/*-------------------------------------------------------------------------*/

/**
 * ohci_hcd_omap_probe - initialize OMAP-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
static int ohci_hcd_omap_probe(struct platform_device *pdev)
{
	int retval, irq;
	struct usb_hcd *hcd = 0;
	struct ohci_omap_priv *priv;

	if (pdev->num_resources != 2) {
		dev_err(&pdev->dev, "invalid num_resources: %i\n",
		       pdev->num_resources);
		return -ENODEV;
	}

	if (pdev->resource[0].flags != IORESOURCE_MEM
			|| pdev->resource[1].flags != IORESOURCE_IRQ) {
		dev_err(&pdev->dev, "invalid resource type\n");
		return -ENODEV;
	}

	hcd = usb_create_hcd(&ohci_omap_hc_driver, &pdev->dev,
			dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = pdev->resource[0].start;
	hcd->rsrc_len = pdev->resource[0].end - pdev->resource[0].start + 1;
	priv = hcd_to_ohci_omap_priv(hcd);

	/* Obtain two optional GPIO lines */
	priv->power = devm_gpiod_get_optional(&pdev->dev, "power", GPIOD_ASIS);
	if (IS_ERR(priv->power)) {
		retval = PTR_ERR(priv->power);
		goto err_put_hcd;
	}
	if (priv->power)
		gpiod_set_consumer_name(priv->power, "OHCI power");

	/*
	 * This "overcurrent" GPIO line isn't really used in the code,
	 * but has a designated hardware function.
	 * TODO: implement proper overcurrent handling.
	 */
	priv->overcurrent = devm_gpiod_get_optional(&pdev->dev, "overcurrent",
						    GPIOD_IN);
	if (IS_ERR(priv->overcurrent)) {
		retval = PTR_ERR(priv->overcurrent);
		goto err_put_hcd;
	}
	if (priv->overcurrent)
		gpiod_set_consumer_name(priv->overcurrent, "OHCI overcurrent");

	priv->usb_host_ck = clk_get(&pdev->dev, "usb_hhc_ck");
	if (IS_ERR(priv->usb_host_ck)) {
		retval = PTR_ERR(priv->usb_host_ck);
		goto err_put_hcd;
	}

	if (!cpu_is_omap15xx())
		priv->usb_dc_ck = clk_get(&pdev->dev, "usb_dc_ck");
	else
		priv->usb_dc_ck = clk_get(&pdev->dev, "lb_ck");

	if (IS_ERR(priv->usb_dc_ck)) {
		retval = PTR_ERR(priv->usb_dc_ck);
		goto err_put_host_ck;
	}

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		dev_dbg(&pdev->dev, "request_mem_region failed\n");
		retval = -EBUSY;
		goto err_put_dc_ck;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "can't ioremap OHCI HCD\n");
		retval = -ENOMEM;
		goto err2;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		retval = -ENXIO;
		goto err3;
	}
	retval = usb_add_hcd(hcd, irq, 0);
	if (retval)
		goto err3;

	device_wakeup_enable(hcd->self.controller);
	return 0;
err3:
	iounmap(hcd->regs);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err_put_dc_ck:
	clk_put(priv->usb_dc_ck);
err_put_host_ck:
	clk_put(priv->usb_host_ck);
err_put_hcd:
	usb_put_hcd(hcd);
	return retval;
}


/* may be called with controller, bus, and devices active */

/**
 * ohci_hcd_omap_remove - shutdown processing for OMAP-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of ohci_hcd_omap_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 */
static int ohci_hcd_omap_remove(struct platform_device *pdev)
{
	struct usb_hcd	*hcd = platform_get_drvdata(pdev);
	struct ohci_omap_priv *priv = hcd_to_ohci_omap_priv(hcd);

	dev_dbg(hcd->self.controller, "stopping USB Controller\n");
	usb_remove_hcd(hcd);
	omap_ohci_clock_power(priv, 0);
	if (!IS_ERR_OR_NULL(hcd->usb_phy)) {
		(void) otg_set_host(hcd->usb_phy->otg, 0);
		usb_put_phy(hcd->usb_phy);
	}
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	clk_put(priv->usb_dc_ck);
	clk_put(priv->usb_host_ck);
	usb_put_hcd(hcd);
	return 0;
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

static int ohci_omap_suspend(struct platform_device *pdev, pm_message_t message)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	struct ohci_omap_priv *priv = hcd_to_ohci_omap_priv(hcd);
	bool do_wakeup = device_may_wakeup(&pdev->dev);
	int ret;

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	ret = ohci_suspend(hcd, do_wakeup);
	if (ret)
		return ret;

	omap_ohci_clock_power(priv, 0);
	return ret;
}

static int ohci_omap_resume(struct platform_device *dev)
{
	struct usb_hcd	*hcd = platform_get_drvdata(dev);
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	struct ohci_omap_priv *priv = hcd_to_ohci_omap_priv(hcd);

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	omap_ohci_clock_power(priv, 1);
	ohci_resume(hcd, false);
	return 0;
}

#endif

/*-------------------------------------------------------------------------*/

/*
 * Driver definition to register with the OMAP bus
 */
static struct platform_driver ohci_hcd_omap_driver = {
	.probe		= ohci_hcd_omap_probe,
	.remove		= ohci_hcd_omap_remove,
	.shutdown	= usb_hcd_platform_shutdown,
#ifdef	CONFIG_PM
	.suspend	= ohci_omap_suspend,
	.resume		= ohci_omap_resume,
#endif
	.driver		= {
		.name	= "ohci",
	},
};

static const struct ohci_driver_overrides omap_overrides __initconst = {
	.product_desc	= "OMAP OHCI",
	.reset		= ohci_omap_reset,
	.extra_priv_size = sizeof(struct ohci_omap_priv),
};

static int __init ohci_omap_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ohci_init_driver(&ohci_omap_hc_driver, &omap_overrides);
	return platform_driver_register(&ohci_hcd_omap_driver);
}
module_init(ohci_omap_init);

static void __exit ohci_omap_cleanup(void)
{
	platform_driver_unregister(&ohci_hcd_omap_driver);
}
module_exit(ohci_omap_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:ohci");
MODULE_LICENSE("GPL");
