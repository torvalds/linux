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
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include <plat/mvsdio.h>
#include <plat/orion_nand.h>
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

static struct resource sheevaplug_nand_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= KIRKWOOD_NAND_MEM_PHYS_BASE,
	.end		= KIRKWOOD_NAND_MEM_PHYS_BASE +
			  KIRKWOOD_NAND_MEM_SIZE - 1,
};

static struct orion_nand_data sheevaplug_nand_data = {
	.parts		= sheevaplug_nand_parts,
	.nr_parts	= ARRAY_SIZE(sheevaplug_nand_parts),
	.cle		= 0,
	.ale		= 1,
	.width		= 8,
	.chip_delay	= 25,
};

static struct platform_device sheevaplug_nand_flash = {
	.name		= "orion_nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &sheevaplug_nand_data,
	},
	.resource	= &sheevaplug_nand_resource,
	.num_resources	= 1,
};

static struct mv643xx_eth_platform_data sheevaplug_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mvsdio_platform_data sheevaplug_mvsdio_data = {
	// unfortunately the CD signal has not been connected */
};

static struct gpio_led sheevaplug_led_pins[] = {
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
	MPP49_GPIO,	/* LED */
	0
};

static void __init sheevaplug_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();
	kirkwood_mpp_conf(sheevaplug_mpp_config);

	kirkwood_uart0_init();

	if (gpio_request(29, "USB Power Enable") != 0 ||
	    gpio_direction_output(29, 1) != 0)
		printk(KERN_ERR "can't set up GPIO 29 (USB Power Enable)\n");
	kirkwood_ehci_init();

	kirkwood_ge00_init(&sheevaplug_ge00_data);
	kirkwood_sdio_init(&sheevaplug_mvsdio_data);

	platform_device_register(&sheevaplug_nand_flash);
	platform_device_register(&sheevaplug_leds);
}

MACHINE_START(SHEEVAPLUG, "Marvell SheevaPlug Reference Board")
	/* Maintainer: shadi Ammouri <shadi@marvell.com> */
	.phys_io	= KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst	= ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.init_machine	= sheevaplug_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
