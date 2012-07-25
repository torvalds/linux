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

#include <linux/clk.h>
#include <linux/davinci_emac.h>
#include <linux/platform_device.h>
#include <plat/irqs.h>
#include <mach/am35xx.h>

#include "control.h"

static struct mdio_platform_data am35xx_emac_mdio_pdata;

static struct resource am35xx_emac_mdio_resources[] = {
	DEFINE_RES_MEM(AM35XX_IPSS_EMAC_BASE + AM35XX_EMAC_MDIO_OFFSET, SZ_4K),
};

static struct platform_device am35xx_emac_mdio_device = {
	.name		= "davinci_mdio",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(am35xx_emac_mdio_resources),
	.resource	= am35xx_emac_mdio_resources,
	.dev.platform_data = &am35xx_emac_mdio_pdata,
};

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

static struct resource am35xx_emac_resources[] = {
	DEFINE_RES_MEM(AM35XX_IPSS_EMAC_BASE, 0x30000),
	DEFINE_RES_IRQ(INT_35XX_EMAC_C0_RXTHRESH_IRQ),
	DEFINE_RES_IRQ(INT_35XX_EMAC_C0_RX_PULSE_IRQ),
	DEFINE_RES_IRQ(INT_35XX_EMAC_C0_TX_PULSE_IRQ),
	DEFINE_RES_IRQ(INT_35XX_EMAC_C0_MISC_PULSE_IRQ),
};

static struct platform_device am35xx_emac_device = {
	.name		= "davinci_emac",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(am35xx_emac_resources),
	.resource	= am35xx_emac_resources,
	.dev		= {
		.platform_data	= &am35xx_emac_pdata,
	},
};

void __init am35xx_emac_init(unsigned long mdio_bus_freq, u8 rmii_en)
{
	u32 v;
	int err;

	am35xx_emac_pdata.rmii_en = rmii_en;
	am35xx_emac_mdio_pdata.bus_freq = mdio_bus_freq;
	err = platform_device_register(&am35xx_emac_device);
	if (err) {
		pr_err("AM35x: failed registering EMAC device: %d\n", err);
		return;
	}

	err = platform_device_register(&am35xx_emac_mdio_device);
	if (err) {
		pr_err("AM35x: failed registering EMAC MDIO device: %d\n", err);
		platform_device_unregister(&am35xx_emac_device);
		return;
	}

	v = omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET);
	v &= ~AM35XX_CPGMACSS_SW_RST;
	omap_ctrl_writel(v, AM35XX_CONTROL_IP_SW_RESET);
	omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET); /* OCP barrier */
}
