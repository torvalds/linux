/*
 * Copyright 2012 Nobuhiro Iwamatsu <iwamatsu@nigauri.org>
 *
 * arch/arm/mach-kirkwood/board-openblocks_a6.c
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

static struct mv643xx_eth_platform_data openblocks_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static unsigned int openblocks_a6_mpp_config[] __initdata = {
	MPP0_NF_IO2,
	MPP1_NF_IO3,
	MPP2_NF_IO4,
	MPP3_NF_IO5,
	MPP4_NF_IO6,
	MPP5_NF_IO7,
	MPP6_SYSRST_OUTn,
	MPP8_UART1_RTS,
	MPP9_UART1_CTS,
	MPP10_UART0_TXD,
	MPP11_UART0_RXD,
	MPP13_UART1_TXD,
	MPP14_UART1_RXD,
	MPP15_UART0_RTS,
	MPP16_UART0_CTS,
	MPP18_NF_IO0,
	MPP19_NF_IO1,
	MPP20_GPIO, /* DIP SW0 */
	MPP21_GPIO, /* DIP SW1 */
	MPP22_GPIO, /* DIP SW2 */
	MPP23_GPIO, /* DIP SW3 */
	MPP24_GPIO, /* GPIO 0 */
	MPP25_GPIO, /* GPIO 1 */
	MPP26_GPIO, /* GPIO 2 */
	MPP27_GPIO, /* GPIO 3 */
	MPP28_GPIO, /* GPIO 4 */
	MPP29_GPIO, /* GPIO 5 */
	MPP30_GPIO, /* GPIO 6 */
	MPP31_GPIO, /* GPIO 7 */
	MPP36_TW1_SDA,
	MPP37_TW1_SCK,
	MPP38_GPIO, /* INIT */
	MPP39_GPIO, /* USB OC */
	MPP41_GPIO, /* LED: Red */
	MPP42_GPIO, /* LED: Yellow */
	MPP43_GPIO, /* LED: Green */
	0,
};

void __init openblocks_a6_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_mpp_conf(openblocks_a6_mpp_config);
	kirkwood_ge00_init(&openblocks_ge00_data);
}
