/*
 * arch/arm/mach-kirkwood/dockstar-setup.c
 *
 * Seagate FreeAgent DockStar Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/mtd/partitions.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include <plat/mvsdio.h>
#include "common.h"
#include "mpp.h"

static struct mtd_partition dockstar_nand_parts[] = {
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

static struct mv643xx_eth_platform_data dockstar_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct gpio_led dockstar_led_pins[] = {
	{
		.name			= "dockstar:green:health",
		.default_trigger	= "default-on",
		.gpio			= 46,
		.active_low		= 1,
	},
	{
		.name			= "dockstar:orange:misc",
		.default_trigger	= "none",
		.gpio			= 47,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data dockstar_led_data = {
	.leds		= dockstar_led_pins,
	.num_leds	= ARRAY_SIZE(dockstar_led_pins),
};

static struct platform_device dockstar_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &dockstar_led_data,
	}
};

static unsigned int dockstar_mpp_config[] __initdata = {
	MPP29_GPIO,	/* USB Power Enable */
	MPP46_GPIO,	/* LED green */
	MPP47_GPIO,	/* LED orange */
	0
};

static void __init dockstar_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();

	/* setup gpio pin select */
	kirkwood_mpp_conf(dockstar_mpp_config);

	kirkwood_uart0_init();
	kirkwood_nand_init(ARRAY_AND_SIZE(dockstar_nand_parts), 25);

	if (gpio_request(29, "USB Power Enable") != 0 ||
	    gpio_direction_output(29, 1) != 0)
		printk(KERN_ERR "can't set up GPIO 29 (USB Power Enable)\n");
	kirkwood_ehci_init();

	kirkwood_ge00_init(&dockstar_ge00_data);

	platform_device_register(&dockstar_leds);
}

MACHINE_START(DOCKSTAR, "Seagate FreeAgent DockStar")
	.atag_offset	= 0x100,
	.init_machine	= dockstar_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
