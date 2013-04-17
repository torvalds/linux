/*
 * arch/arm/mach-kirkwood/guruplug-setup.c
 *
 * Marvell GuruPlug Reference Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include <linux/platform_data/mmc-mvsdio.h>
#include "common.h"
#include "mpp.h"

static struct mtd_partition guruplug_nand_parts[] = {
	{
		.name = "u-boot",
		.offset = 0,
		.size = SZ_1M
	}, {
		.name = "uImage",
		.offset = MTDPART_OFS_NXTBLK,
		.size = SZ_4M
	}, {
		.name = "root",
		.offset = MTDPART_OFS_NXTBLK,
		.size = MTDPART_SIZ_FULL
	},
};

static struct mv643xx_eth_platform_data guruplug_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mv643xx_eth_platform_data guruplug_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(1),
};

static struct mv_sata_platform_data guruplug_sata_data = {
	.n_ports	= 1,
};

static struct mvsdio_platform_data guruplug_mvsdio_data = {
	/* unfortunately the CD signal has not been connected */
	.gpio_card_detect = -1,
	.gpio_write_protect = -1,
};

static struct gpio_led guruplug_led_pins[] = {
	{
		.name			= "guruplug:red:health",
		.gpio			= 46,
		.active_low		= 1,
	},
	{
		.name			= "guruplug:green:health",
		.gpio			= 47,
		.active_low		= 1,
	},
	{
		.name			= "guruplug:red:wmode",
		.gpio			= 48,
		.active_low		= 1,
	},
	{
		.name			= "guruplug:green:wmode",
		.gpio			= 49,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data guruplug_led_data = {
	.leds		= guruplug_led_pins,
	.num_leds	= ARRAY_SIZE(guruplug_led_pins),
};

static struct platform_device guruplug_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &guruplug_led_data,
	}
};

static unsigned int guruplug_mpp_config[] __initdata = {
	MPP46_GPIO,	/* M_RLED */
	MPP47_GPIO,	/* M_GLED */
	MPP48_GPIO,	/* B_RLED */
	MPP49_GPIO,	/* B_GLED */
	0
};

static void __init guruplug_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();
	kirkwood_mpp_conf(guruplug_mpp_config);

	kirkwood_uart0_init();
	kirkwood_nand_init(ARRAY_AND_SIZE(guruplug_nand_parts), 25);

	kirkwood_ehci_init();
	kirkwood_ge00_init(&guruplug_ge00_data);
	kirkwood_ge01_init(&guruplug_ge01_data);
	kirkwood_sata_init(&guruplug_sata_data);
	kirkwood_sdio_init(&guruplug_mvsdio_data);

	platform_device_register(&guruplug_leds);
}

MACHINE_START(GURUPLUG, "Marvell GuruPlug Reference Board")
	/* Maintainer: Siddarth Gore <gores@marvell.com> */
	.atag_offset	= 0x100,
	.init_machine	= guruplug_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.init_time	= kirkwood_timer_init,
	.restart	= kirkwood_restart,
MACHINE_END
