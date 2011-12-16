/*
 * arch/arm/mach-kirkwood/sheevaplug-setup.c
 *
 * Marvell SheevaPlug Reference Board Setup
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

static struct mtd_partition sheevaplug_nand_parts[] = {
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

static struct mv643xx_eth_platform_data sheevaplug_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mv_sata_platform_data sheeva_esata_sata_data = {
	.n_ports	= 2,
};

static struct mvsdio_platform_data sheevaplug_mvsdio_data = {
	/* unfortunately the CD signal has not been connected */
};

static struct mvsdio_platform_data sheeva_esata_mvsdio_data = {
	.gpio_write_protect = 44, /* MPP44 used as SD write protect */
	.gpio_card_detect = 47,	  /* MPP47 used as SD card detect */
};

static struct gpio_led sheevaplug_led_pins[] = {
	{
		.name			= "plug:red:misc",
		.default_trigger	= "none",
		.gpio			= 46,
		.active_low		= 1,
	},
	{
		.name			= "plug:green:health",
		.default_trigger	= "default-on",
		.gpio			= 49,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data sheevaplug_led_data = {
	.leds		= sheevaplug_led_pins,
	.num_leds	= ARRAY_SIZE(sheevaplug_led_pins),
};

static struct platform_device sheevaplug_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &sheevaplug_led_data,
	}
};

static unsigned int sheevaplug_mpp_config[] __initdata = {
	MPP29_GPIO,	/* USB Power Enable */
	MPP46_GPIO,	/* LED Red */
	MPP49_GPIO,	/* LED */
	0
};

static unsigned int sheeva_esata_mpp_config[] __initdata = {
	MPP29_GPIO,	/* USB Power Enable */
	MPP44_GPIO,	/* SD Write Protect */
	MPP47_GPIO,	/* SD Card Detect */
	MPP49_GPIO,	/* LED Green */
	0
};

static void __init sheevaplug_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();

	/* setup gpio pin select */
	if (machine_is_sheeva_esata())
		kirkwood_mpp_conf(sheeva_esata_mpp_config);
	else
		kirkwood_mpp_conf(sheevaplug_mpp_config);

	kirkwood_uart0_init();
	kirkwood_nand_init(ARRAY_AND_SIZE(sheevaplug_nand_parts), 25);

	if (gpio_request(29, "USB Power Enable") != 0 ||
	    gpio_direction_output(29, 1) != 0)
		printk(KERN_ERR "can't set up GPIO 29 (USB Power Enable)\n");
	kirkwood_ehci_init();

	kirkwood_ge00_init(&sheevaplug_ge00_data);

	/* honor lower power consumption for plugs with out eSATA */
	if (machine_is_sheeva_esata())
		kirkwood_sata_init(&sheeva_esata_sata_data);

	/* enable sd wp and sd cd on plugs with esata */
	if (machine_is_sheeva_esata())
		kirkwood_sdio_init(&sheeva_esata_mvsdio_data);
	else
		kirkwood_sdio_init(&sheevaplug_mvsdio_data);

	platform_device_register(&sheevaplug_leds);
}

#ifdef CONFIG_MACH_SHEEVAPLUG
MACHINE_START(SHEEVAPLUG, "Marvell SheevaPlug Reference Board")
	/* Maintainer: shadi Ammouri <shadi@marvell.com> */
	.atag_offset	= 0x100,
	.init_machine	= sheevaplug_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_ESATA_SHEEVAPLUG
MACHINE_START(ESATA_SHEEVAPLUG, "Marvell eSATA SheevaPlug Reference Board")
	.atag_offset	= 0x100,
	.init_machine	= sheevaplug_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
#endif
