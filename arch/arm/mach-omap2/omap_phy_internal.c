// SPDX-License-Identifier: GPL-2.0-or-later
/*
  * This file configures the internal USB PHY in OMAP4430. Used
  * with TWL6030 transceiver and MUSB on OMAP4430.
  *
  * Copyright (C) 2010 Texas Instruments Incorporated - https://www.ti.com
  * Author: Hema HK <hemahk@ti.com>
  */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/usb.h>
#include <linux/usb/musb.h>

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
	writel_relaxed(PHY_PD, ctrl_base + CONTROL_DEV_CONF);

	iounmap(ctrl_base);

	return 0;
}
omap_early_initcall(omap4430_phy_power_down);
