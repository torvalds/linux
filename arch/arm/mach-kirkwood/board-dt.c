/*
 * Copyright 2012 (C), Jason Cooper <jason@lakedaemon.net>
 *
 * arch/arm/mach-kirkwood/board-dt.c
 *
 * Marvell DreamPlug Reference Board Setup
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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/spi/orion_spi.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include <plat/mvsdio.h>
#include "common.h"
#include "mpp.h"

static struct of_device_id kirkwood_dt_match_table[] __initdata = {
	{ .compatible = "simple-bus", },
	{ }
};

struct mtd_partition dreamplug_partitions[] = {
	{
		.name	= "u-boot",
		.size	= SZ_512K,
		.offset = 0,
	},
	{
		.name	= "u-boot env",
		.size	= SZ_64K,
		.offset = SZ_512K + SZ_512K,
	},
	{
		.name	= "dtb",
		.size	= SZ_64K,
		.offset = SZ_512K + SZ_512K + SZ_512K,
	},
};

static const struct flash_platform_data dreamplug_spi_slave_data = {
	.type		= "mx25l1606e",
	.name		= "spi_flash",
	.parts		= dreamplug_partitions,
	.nr_parts	= ARRAY_SIZE(dreamplug_partitions),
};

static struct spi_board_info __initdata dreamplug_spi_slave_info[] = {
	{
		.modalias	= "m25p80",
		.platform_data	= &dreamplug_spi_slave_data,
		.irq		= -1,
		.max_speed_hz	= 50000000,
		.bus_num	= 0,
		.chip_select	= 0,
	},
};

static struct mv643xx_eth_platform_data dreamplug_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mv643xx_eth_platform_data dreamplug_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(1),
};

static struct mv_sata_platform_data dreamplug_sata_data = {
	.n_ports	= 1,
};

static struct mvsdio_platform_data dreamplug_mvsdio_data = {
	/* unfortunately the CD signal has not been connected */
};

static struct gpio_led dreamplug_led_pins[] = {
	{
		.name			= "dreamplug:blue:bluetooth",
		.gpio			= 47,
		.active_low		= 1,
	},
	{
		.name			= "dreamplug:green:wifi",
		.gpio			= 48,
		.active_low		= 1,
	},
	{
		.name			= "dreamplug:green:wifi_ap",
		.gpio			= 49,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data dreamplug_led_data = {
	.leds		= dreamplug_led_pins,
	.num_leds	= ARRAY_SIZE(dreamplug_led_pins),
};

static struct platform_device dreamplug_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &dreamplug_led_data,
	}
};

static unsigned int dreamplug_mpp_config[] __initdata = {
	MPP0_SPI_SCn,
	MPP1_SPI_MOSI,
	MPP2_SPI_SCK,
	MPP3_SPI_MISO,
	MPP47_GPIO,	/* Bluetooth LED */
	MPP48_GPIO,	/* Wifi LED */
	MPP49_GPIO,	/* Wifi AP LED */
	0
};

static void __init dreamplug_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_mpp_conf(dreamplug_mpp_config);

	spi_register_board_info(dreamplug_spi_slave_info,
				ARRAY_SIZE(dreamplug_spi_slave_info));
	kirkwood_spi_init();

	kirkwood_ehci_init();
	kirkwood_ge00_init(&dreamplug_ge00_data);
	kirkwood_ge01_init(&dreamplug_ge01_data);
	kirkwood_sata_init(&dreamplug_sata_data);
	kirkwood_sdio_init(&dreamplug_mvsdio_data);

	platform_device_register(&dreamplug_leds);
}

static void __init kirkwood_dt_init(void)
{
	kirkwood_init();

	if (of_machine_is_compatible("globalscale,dreamplug"))
		dreamplug_init();

	of_platform_populate(NULL, kirkwood_dt_match_table, NULL, NULL);
}

static const char *kirkwood_dt_board_compat[] = {
	"globalscale,dreamplug",
	NULL
};

DT_MACHINE_START(KIRKWOOD_DT, "Marvell Kirkwood (Flattened Device Tree)")
	/* Maintainer: Jason Cooper <jason@lakedaemon.net> */
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
	.init_machine	= kirkwood_dt_init,
	.restart	= kirkwood_restart,
	.dt_compat	= kirkwood_dt_board_compat,
MACHINE_END
