/*
 * arch/arm/mach-kirkwood/board-mv88f6281gtw_ge.c
 *
 * Marvell 88F6281 GTW GE Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/timer.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <linux/gpio.h>
#include <net/dsa.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/kirkwood.h>
#include "common.h"

static struct mv643xx_eth_platform_data mv88f6281gtw_ge_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_NONE,
	.speed		= SPEED_1000,
	.duplex		= DUPLEX_FULL,
};

static struct dsa_chip_data mv88f6281gtw_ge_switch_chip_data = {
	.port_names[0]	= "lan1",
	.port_names[1]	= "lan2",
	.port_names[2]	= "lan3",
	.port_names[3]	= "lan4",
	.port_names[4]	= "wan",
	.port_names[5]	= "cpu",
};

static struct dsa_platform_data mv88f6281gtw_ge_switch_plat_data = {
	.nr_chips	= 1,
	.chip		= &mv88f6281gtw_ge_switch_chip_data,
};

void __init mv88f6281gtw_ge_init(void)
{
	kirkwood_ge00_init(&mv88f6281gtw_ge_ge00_data);
	kirkwood_ge00_switch_init(&mv88f6281gtw_ge_switch_plat_data, NO_IRQ);
}
