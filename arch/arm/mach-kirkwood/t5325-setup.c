/*
 *
 * HP t5325 Thin Client setup
 *
 * Copyright (C) 2010  Martin Michlmayr <tbm@cyrius.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/spi/orion_spi.h>
#include <linux/i2c.h>
#include <linux/mv643xx_eth.h>
#include <linux/ata_platform.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <sound/alc5623.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include "common.h"
#include "mpp.h"

struct mtd_partition hp_t5325_partitions[] = {
	{
		.name		= "u-boot env",
		.size		= SZ_64K,
		.offset		= SZ_512K + SZ_256K,
	},
	{
		.name		= "permanent u-boot env",
		.size		= SZ_64K,
		.offset		= MTDPART_OFS_APPEND,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "HP env",
		.size		= SZ_64K,
		.offset		= MTDPART_OFS_APPEND,
	},
	{
		.name		= "u-boot",
		.size		= SZ_512K,
		.offset		= 0,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "SSD firmware",
		.size		= SZ_256K,
		.offset		= SZ_512K,
	},
};

const struct flash_platform_data hp_t5325_flash = {
	.type		= "mx25l8005",
	.name		= "spi_flash",
	.parts		= hp_t5325_partitions,
	.nr_parts	= ARRAY_SIZE(hp_t5325_partitions),
};

struct spi_board_info __initdata hp_t5325_spi_slave_info[] = {
	{
		.modalias	= "m25p80",
		.platform_data	= &hp_t5325_flash,
		.irq		= -1,
	},
};

static struct mv643xx_eth_platform_data hp_t5325_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv_sata_platform_data hp_t5325_sata_data = {
	.n_ports	= 2,
};

static struct gpio_keys_button hp_t5325_buttons[] = {
	{
		.code		= KEY_POWER,
		.gpio		= 45,
		.desc		= "Power",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data hp_t5325_button_data = {
	.buttons	= hp_t5325_buttons,
	.nbuttons	= ARRAY_SIZE(hp_t5325_buttons),
};

static struct platform_device hp_t5325_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &hp_t5325_button_data,
	}
};

static unsigned int hp_t5325_mpp_config[] __initdata = {
	MPP0_NF_IO2,
	MPP1_SPI_MOSI,
	MPP2_SPI_SCK,
	MPP3_SPI_MISO,
	MPP4_NF_IO6,
	MPP5_NF_IO7,
	MPP6_SYSRST_OUTn,
	MPP7_SPI_SCn,
	MPP8_TW0_SDA,
	MPP9_TW0_SCK,
	MPP10_UART0_TXD,
	MPP11_UART0_RXD,
	MPP12_SD_CLK,
	MPP13_GPIO,
	MPP14_GPIO,
	MPP15_GPIO,
	MPP16_GPIO,
	MPP17_GPIO,
	MPP18_NF_IO0,
	MPP19_NF_IO1,
	MPP20_GPIO,
	MPP21_GPIO,
	MPP22_GPIO,
	MPP23_GPIO,
	MPP32_GPIO,
	MPP33_GE1_TXCTL,
	MPP39_AU_I2SBCLK,
	MPP40_AU_I2SDO,
	MPP43_AU_I2SDI,
	MPP41_AU_I2SLRCLK,
	MPP42_AU_I2SMCLK,
	MPP45_GPIO,		/* Power button */
	MPP48_GPIO,		/* Board power off */
	0
};

static struct alc5623_platform_data alc5621_data = {
	.add_ctrl = 0x3700,
	.jack_det_ctrl = 0x4810,
};

static struct i2c_board_info i2c_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("alc5621", 0x1a),
		.platform_data = &alc5621_data,
	},
};

#define HP_T5325_GPIO_POWER_OFF		48

static void hp_t5325_power_off(void)
{
	gpio_set_value(HP_T5325_GPIO_POWER_OFF, 1);
}

static void __init hp_t5325_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();
	kirkwood_mpp_conf(hp_t5325_mpp_config);

	kirkwood_uart0_init();
	spi_register_board_info(hp_t5325_spi_slave_info,
				ARRAY_SIZE(hp_t5325_spi_slave_info));
	kirkwood_spi_init();
	kirkwood_i2c_init();
	kirkwood_ge00_init(&hp_t5325_ge00_data);
	kirkwood_sata_init(&hp_t5325_sata_data);
	kirkwood_ehci_init();
	platform_device_register(&hp_t5325_button_device);

	i2c_register_board_info(0, i2c_board_info, ARRAY_SIZE(i2c_board_info));
	kirkwood_audio_init();

	if (gpio_request(HP_T5325_GPIO_POWER_OFF, "power-off") == 0 &&
	    gpio_direction_output(HP_T5325_GPIO_POWER_OFF, 0) == 0)
		pm_power_off = hp_t5325_power_off;
	else
		pr_err("t5325: failed to configure power-off GPIO\n");
}

static int __init hp_t5325_pci_init(void)
{
	if (machine_is_t5325())
		kirkwood_pcie_init(KW_PCIE0);

	return 0;
}
subsys_initcall(hp_t5325_pci_init);

MACHINE_START(T5325, "HP t5325 Thin Client")
	/* Maintainer: Martin Michlmayr <tbm@cyrius.com> */
	.atag_offset	= 0x100,
	.init_machine	= hp_t5325_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
	.restart	= kirkwood_restart,
MACHINE_END
