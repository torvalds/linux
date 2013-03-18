/*
 * Copyright (C) 2011 Ilya Yanok, Emcraft Systems
 *
 * Based on mach-omap2/board-am3517evm.c
 * Copyright (C) 2009 Texas Instruments Incorporated
 * Author: Ranjith Lohithakshan <ranjithl@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/err.h>
#include <linux/davinci_emac.h>
#include <asm/system.h>
#include "omap_device.h"
#include "am35xx.h"
#include "control.h"
#include "am35xx-emac.h"

static void am35xx_enable_emac_int(void)
{
	u32 v;

	v = omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR);
	v |= (AM35XX_CPGMAC_C0_RX_PULSE_CLR | AM35XX_CPGMAC_C0_TX_PULSE_CLR |
	      AM35XX_CPGMAC_C0_MISC_PULSE_CLR | AM35XX_CPGMAC_C0_RX_THRESH_CLR);
	omap_ctrl_writel(v, AM35XX_CONTROL_LVL_INTR_CLEAR);
	omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR); /* OCP barrier */
}

static void am35xx_disable_emac_int(void)
{
	u32 v;

	v = omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR);
	v |= (AM35XX_CPGMAC_C0_RX_PULSE_CLR | AM35XX_CPGMAC_C0_TX_PULSE_CLR);
	omap_ctrl_writel(v, AM35XX_CONTROL_LVL_INTR_CLEAR);
	omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR); /* OCP barrier */
}

static struct emac_platform_data am35xx_emac_pdata = {
	.ctrl_reg_offset	= AM35XX_EMAC_CNTRL_OFFSET,
	.ctrl_mod_reg_offset	= AM35XX_EMAC_CNTRL_MOD_OFFSET,
	.ctrl_ram_offset	= AM35XX_EMAC_CNTRL_RAM_OFFSET,
	.ctrl_ram_size		= AM35XX_EMAC_CNTRL_RAM_SIZE,
	.hw_ram_addr		= AM35XX_EMAC_HW_RAM_ADDR,
	.version		= EMAC_VERSION_2,
	.interrupt_enable	= am35xx_enable_emac_int,
	.interrupt_disable	= am35xx_disable_emac_int,
};

static struct mdio_platform_data am35xx_mdio_pdata;

static int __init omap_davinci_emac_dev_init(struct omap_hwmod *oh,
		void *pdata, int pdata_len)
{
	struct platform_device *pdev;

	pdev = omap_device_build(oh->class->name, 0, oh, pdata, pdata_len);
	if (IS_ERR(pdev)) {
		WARN(1, "Can't build omap_device for %s:%s.\n",
		     oh->class->name, oh->name);
		return PTR_ERR(pdev);
	}

	return 0;
}

void __init am35xx_emac_init(unsigned long mdio_bus_freq, u8 rmii_en)
{
	struct omap_hwmod *oh;
	u32 v;
	int ret;

	oh = omap_hwmod_lookup("davinci_mdio");
	if (!oh) {
		pr_err("Could not find davinci_mdio hwmod\n");
		return;
	}

	am35xx_mdio_pdata.bus_freq = mdio_bus_freq;

	ret = omap_davinci_emac_dev_init(oh, &am35xx_mdio_pdata,
					 sizeof(am35xx_mdio_pdata));
	if (ret) {
		pr_err("Could not build davinci_mdio hwmod device\n");
		return;
	}

	oh = omap_hwmod_lookup("davinci_emac");
	if (!oh) {
		pr_err("Could not find davinci_emac hwmod\n");
		return;
	}

	am35xx_emac_pdata.rmii_en = rmii_en;

	ret = omap_davinci_emac_dev_init(oh, &am35xx_emac_pdata,
					 sizeof(am35xx_emac_pdata));
	if (ret) {
		pr_err("Could not build davinci_emac hwmod device\n");
		return;
	}

	v = omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET);
	v &= ~AM35XX_CPGMACSS_SW_RST;
	omap_ctrl_writel(v, AM35XX_CONTROL_IP_SW_RESET);
	omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET); /* OCP barrier */
}
