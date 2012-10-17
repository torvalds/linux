/*
 * Copyright 2012 (C), Simon Guinot <simon.guinot@sequanux.org>
 *
 * arch/arm/mach-kirkwood/board-ns2.c
 *
 * LaCie Network Space v2 board (and parents) initialization for drivers
 * not converted to flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include "common.h"
#include "mpp.h"

static struct mv643xx_eth_platform_data ns2_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static unsigned int ns2_mpp_config[] __initdata = {
	MPP0_SPI_SCn,
	MPP1_SPI_MOSI,
	MPP2_SPI_SCK,
	MPP3_SPI_MISO,
	MPP4_NF_IO6,
	MPP5_NF_IO7,
	MPP6_SYSRST_OUTn,
	MPP7_GPO,		/* Fan speed (bit 1) */
	MPP8_TW0_SDA,
	MPP9_TW0_SCK,
	MPP10_UART0_TXD,
	MPP11_UART0_RXD,
	MPP12_GPO,		/* Red led */
	MPP14_GPIO,		/* USB fuse */
	MPP16_GPIO,		/* SATA 0 power */
	MPP17_GPIO,		/* SATA 1 power */
	MPP18_NF_IO0,
	MPP19_NF_IO1,
	MPP20_SATA1_ACTn,
	MPP21_SATA0_ACTn,
	MPP22_GPIO,		/* Fan speed (bit 0) */
	MPP23_GPIO,		/* Fan power */
	MPP24_GPIO,		/* USB mode select */
	MPP25_GPIO,		/* Fan rotation fail */
	MPP26_GPIO,		/* USB device vbus */
	MPP28_GPIO,		/* USB enable host vbus */
	MPP29_GPIO,		/* Blue led (slow register) */
	MPP30_GPIO,		/* Blue led (command register) */
	MPP31_GPIO,		/* Board power off */
	MPP32_GPIO,		/* Power button (0 = Released, 1 = Pushed) */
	MPP33_GPO,		/* Fan speed (bit 2) */
	0
};

#define NS2_GPIO_POWER_OFF	31

static void ns2_power_off(void)
{
	gpio_set_value(NS2_GPIO_POWER_OFF, 1);
}

void __init ns2_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_mpp_conf(ns2_mpp_config);

	kirkwood_ehci_init();
	if (of_machine_is_compatible("lacie,netspace_lite_v2"))
		ns2_ge00_data.phy_addr = MV643XX_ETH_PHY_ADDR(0);
	kirkwood_ge00_init(&ns2_ge00_data);

	if (gpio_request(NS2_GPIO_POWER_OFF, "power-off") == 0 &&
	    gpio_direction_output(NS2_GPIO_POWER_OFF, 0) == 0)
		pm_power_off = ns2_power_off;
	else
		pr_err("ns2: failed to configure power-off GPIO\n");
}
