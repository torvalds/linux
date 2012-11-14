/*
  * This file configures the internal USB PHY in OMAP4430. Used
  * with TWL6030 transceiver and MUSB on OMAP4430.
  *
  * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation; either version 2 of the License, or
  * (at your option) any later version.
  *
  * Author: Hema HK <hemahk@ti.com>
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program; if not, write to the Free Software
  * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
  *
  */

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/usb.h>

#include <plat/usb.h>

#include "soc.h"
#include "control.h"

#define CONTROL_DEV_CONF		0x300
#define PHY_PD				0x1

/**
 * omap4430_phy_power_down: disable MUSB PHY during early init
 *
 * OMAP4 MUSB PHY module is enabled by default on reset, but this will
 * prevent core retention if not disabled by SW. USB driver will
 * later on enable this, once and if the driver needs it.
 */
static int __init omap4430_phy_power_down(void)
{
	void __iomem *ctrl_base;

	if (!cpu_is_omap44xx())
		return 0;

	ctrl_base = ioremap(OMAP443X_SCM_BASE, SZ_1K);
	if (!ctrl_base) {
		pr_err("control module ioremap failed\n");
		return -ENOMEM;
	}

	/* Power down the phy */
	__raw_writel(PHY_PD, ctrl_base + CONTROL_DEV_CONF);

	iounmap(ctrl_base);

	return 0;
}
early_initcall(omap4430_phy_power_down);

void am35x_musb_reset(void)
{
	u32	regval;

	/* Reset the musb interface */
	regval = omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET);

	regval |= AM35XX_USBOTGSS_SW_RST;
	omap_ctrl_writel(regval, AM35XX_CONTROL_IP_SW_RESET);

	regval &= ~AM35XX_USBOTGSS_SW_RST;
	omap_ctrl_writel(regval, AM35XX_CONTROL_IP_SW_RESET);

	regval = omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET);
}

void am35x_musb_phy_power(u8 on)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100);
	u32 devconf2;

	if (on) {
		/*
		 * Start the on-chip PHY and its PLL.
		 */
		devconf2 = omap_ctrl_readl(AM35XX_CONTROL_DEVCONF2);

		devconf2 &= ~(CONF2_RESET | CONF2_PHYPWRDN | CONF2_OTGPWRDN);
		devconf2 |= CONF2_PHY_PLLON;

		omap_ctrl_writel(devconf2, AM35XX_CONTROL_DEVCONF2);

		pr_info(KERN_INFO "Waiting for PHY clock good...\n");
		while (!(omap_ctrl_readl(AM35XX_CONTROL_DEVCONF2)
				& CONF2_PHYCLKGD)) {
			cpu_relax();

			if (time_after(jiffies, timeout)) {
				pr_err(KERN_ERR "musb PHY clock good timed out\n");
				break;
			}
		}
	} else {
		/*
		 * Power down the on-chip PHY.
		 */
		devconf2 = omap_ctrl_readl(AM35XX_CONTROL_DEVCONF2);

		devconf2 &= ~CONF2_PHY_PLLON;
		devconf2 |=  CONF2_PHYPWRDN | CONF2_OTGPWRDN;
		omap_ctrl_writel(devconf2, AM35XX_CONTROL_DEVCONF2);
	}
}

void am35x_musb_clear_irq(void)
{
	u32 regval;

	regval = omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR);
	regval |= AM35XX_USBOTGSS_INT_CLR;
	omap_ctrl_writel(regval, AM35XX_CONTROL_LVL_INTR_CLEAR);
	regval = omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR);
}

void am35x_set_mode(u8 musb_mode)
{
	u32 devconf2 = omap_ctrl_readl(AM35XX_CONTROL_DEVCONF2);

	devconf2 &= ~CONF2_OTGMODE;
	switch (musb_mode) {
	case MUSB_HOST:		/* Force VBUS valid, ID = 0 */
		devconf2 |= CONF2_FORCE_HOST;
		break;
	case MUSB_PERIPHERAL:	/* Force VBUS valid, ID = 1 */
		devconf2 |= CONF2_FORCE_DEVICE;
		break;
	case MUSB_OTG:		/* Don't override the VBUS/ID comparators */
		devconf2 |= CONF2_NO_OVERRIDE;
		break;
	default:
		pr_info(KERN_INFO "Unsupported mode %u\n", musb_mode);
	}

	omap_ctrl_writel(devconf2, AM35XX_CONTROL_DEVCONF2);
}

void ti81xx_musb_phy_power(u8 on)
{
	void __iomem *scm_base = NULL;
	u32 usbphycfg;

	scm_base = ioremap(TI81XX_SCM_BASE, SZ_2K);
	if (!scm_base) {
		pr_err("system control module ioremap failed\n");
		return;
	}

	usbphycfg = __raw_readl(scm_base + USBCTRL0);

	if (on) {
		if (cpu_is_ti816x()) {
			usbphycfg |= TI816X_USBPHY0_NORMAL_MODE;
			usbphycfg &= ~TI816X_USBPHY_REFCLK_OSC;
		} else if (cpu_is_ti814x()) {
			usbphycfg &= ~(USBPHY_CM_PWRDN | USBPHY_OTG_PWRDN
				| USBPHY_DPINPUT | USBPHY_DMINPUT);
			usbphycfg |= (USBPHY_OTGVDET_EN | USBPHY_OTGSESSEND_EN
				| USBPHY_DPOPBUFCTL | USBPHY_DMOPBUFCTL);
		}
	} else {
		if (cpu_is_ti816x())
			usbphycfg &= ~TI816X_USBPHY0_NORMAL_MODE;
		else if (cpu_is_ti814x())
			usbphycfg |= USBPHY_CM_PWRDN | USBPHY_OTG_PWRDN;

	}
	__raw_writel(usbphycfg, scm_base + USBCTRL0);

	iounmap(scm_base);
}
