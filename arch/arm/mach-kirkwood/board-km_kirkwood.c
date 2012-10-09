/*
 * Copyright 2012 2012 KEYMILE AG, CH-3097 Bern
 * Valentin Longchamp <valentin.longchamp@keymile.com>
 *
 * arch/arm/mach-kirkwood/board-km_kirkwood.c
 *
 * Keymile km_kirkwood Reference Desing Init for drivers not converted to
 * flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mv643xx_eth.h>
#include <linux/clk.h>
#include <linux/clk-private.h>
#include "common.h"
#include "mpp.h"

static struct mv643xx_eth_platform_data km_kirkwood_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static unsigned int km_kirkwood_mpp_config[] __initdata = {
	MPP8_GPIO,	/* I2C SDA */
	MPP9_GPIO,	/* I2C SCL */
	0
};

void __init km_kirkwood_init(void)
{
	struct clk *sata_clk;
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_mpp_conf(km_kirkwood_mpp_config);

	/*
	 * Our variant of kirkwood (integrated in the Bobcat) hangs on accessing
	 * SATA bits (14-15) of the Clock Gating Control Register. Since these
	 * devices are also not present in this variant, their clocks get
	 * disabled because unused when clk_disable_unused() gets called.
	 * That's why we change the flags to these clocks to CLK_IGNORE_UNUSED
	 */
	sata_clk = clk_get_sys("sata_mv.0", "0");
	if (!IS_ERR(sata_clk))
		sata_clk->flags |= CLK_IGNORE_UNUSED;
	sata_clk = clk_get_sys("sata_mv.0", "1");
	if (!IS_ERR(sata_clk))
		sata_clk->flags |= CLK_IGNORE_UNUSED;

	kirkwood_ehci_init();
	kirkwood_ge00_init(&km_kirkwood_ge00_data);
}
