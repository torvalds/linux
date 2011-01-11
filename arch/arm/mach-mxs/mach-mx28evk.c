/*
 * Copyright 2010 Freescale Semiconductor, Inc. All Rights Reserved.
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
 */

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/clk.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/common.h>
#include <mach/iomux-mx28.h>

#include "devices-mx28.h"
#include "gpio.h"

#define MX28EVK_FEC_PHY_POWER	MXS_GPIO_NR(2, 15)
#define MX28EVK_FEC_PHY_RESET	MXS_GPIO_NR(4, 13)

static const iomux_cfg_t mx28evk_pads[] __initconst = {
	/* duart */
	MX28_PAD_PWM0__DUART_RX |
		(MXS_PAD_4MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	MX28_PAD_PWM1__DUART_TX |
		(MXS_PAD_4MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),

	/* fec0 */
	MX28_PAD_ENET0_MDC__ENET0_MDC |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_MDIO__ENET0_MDIO |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_RX_EN__ENET0_RX_EN |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_RXD0__ENET0_RXD0 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_RXD1__ENET0_RXD1 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_TX_EN__ENET0_TX_EN |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_TXD0__ENET0_TXD0 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_TXD1__ENET0_TXD1 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET_CLK__CLKCTRL_ENET |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	/* fec1 */
	MX28_PAD_ENET0_CRS__ENET1_RX_EN |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_RXD2__ENET1_RXD0 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_RXD3__ENET1_RXD1 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_COL__ENET1_TX_EN |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_TXD2__ENET1_TXD0 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_ENET0_TXD3__ENET1_TXD1 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	/* phy power line */
	MX28_PAD_SSP1_DATA3__GPIO_2_15 |
		(MXS_PAD_4MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	/* phy reset line */
	MX28_PAD_ENET0_RX_CLK__GPIO_4_13 |
		(MXS_PAD_4MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
};

/* fec */
static void __init mx28evk_fec_reset(void)
{
	int ret;
	struct clk *clk;

	/* Enable fec phy clock */
	clk = clk_get_sys("pll2", NULL);
	if (!IS_ERR(clk))
		clk_enable(clk);

	/* Power up fec phy */
	ret = gpio_request(MX28EVK_FEC_PHY_POWER, "fec-phy-power");
	if (ret) {
		pr_err("Failed to request gpio fec-phy-%s: %d\n", "power", ret);
		return;
	}

	ret = gpio_direction_output(MX28EVK_FEC_PHY_POWER, 0);
	if (ret) {
		pr_err("Failed to drive gpio fec-phy-%s: %d\n", "power", ret);
		return;
	}

	/* Reset fec phy */
	ret = gpio_request(MX28EVK_FEC_PHY_RESET, "fec-phy-reset");
	if (ret) {
		pr_err("Failed to request gpio fec-phy-%s: %d\n", "reset", ret);
		return;
	}

	gpio_direction_output(MX28EVK_FEC_PHY_RESET, 0);
	if (ret) {
		pr_err("Failed to drive gpio fec-phy-%s: %d\n", "reset", ret);
		return;
	}

	mdelay(1);
	gpio_set_value(MX28EVK_FEC_PHY_RESET, 1);
}

static struct fec_platform_data mx28_fec_pdata[] = {
	{
		/* fec0 */
		.phy = PHY_INTERFACE_MODE_RMII,
	}, {
		/* fec1 */
		.phy = PHY_INTERFACE_MODE_RMII,
	},
};

static void __init mx28evk_init(void)
{
	mxs_iomux_setup_multiple_pads(mx28evk_pads, ARRAY_SIZE(mx28evk_pads));

	mx28_add_duart();

	mx28evk_fec_reset();
	mx28_add_fec(0, &mx28_fec_pdata[0]);
	mx28_add_fec(1, &mx28_fec_pdata[1]);
}

static void __init mx28evk_timer_init(void)
{
	mx28_clocks_init();
}

static struct sys_timer mx28evk_timer = {
	.init	= mx28evk_timer_init,
};

MACHINE_START(MX28EVK, "Freescale MX28 EVK")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.map_io		= mx28_map_io,
	.init_irq	= mx28_init_irq,
	.init_machine	= mx28evk_init,
	.timer		= &mx28evk_timer,
MACHINE_END
