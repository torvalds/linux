/*
 * arch/arm/mach-ep93xx/simone.c
 * Simplemachines Sim.One support.
 *
 * Copyright (C) 2010 Ryan Mallon
 *
 * Based on the 2.6.24.7 support:
 *   Copyright (C) 2009 Simplemachines
 *   MMC support by Peter Ivanov <ivanovp@gmail.com>, 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/mmc/host.h>
#include <linux/spi/spi.h>
#include <linux/spi/mmc_spi.h>
#include <linux/platform_data/video-ep93xx.h>
#include <linux/platform_data/spi-ep93xx.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>

#include <mach/hardware.h>
#include <mach/gpio-ep93xx.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "soc.h"

static struct ep93xx_eth_data __initdata simone_eth_data = {
	.phy_id		= 1,
};

static struct ep93xxfb_mach_info __initdata simone_fb_info = {
	.flags		= EP93XXFB_USE_SDCSN0 | EP93XXFB_PCLK_FALLING,
};

static struct mmc_spi_platform_data simone_mmc_spi_data = {
	.detect_delay	= 500,
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
};

static struct gpiod_lookup_table simone_mmc_spi_gpio_table = {
	.dev_id = "mmc_spi.0", /* "mmc_spi" @ CS0 */
	.table = {
		/* Card detect */
		GPIO_LOOKUP_IDX("A", 0, NULL, 0, GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct spi_board_info simone_spi_devices[] __initdata = {
	{
		.modalias		= "mmc_spi",
		.platform_data		= &simone_mmc_spi_data,
		/*
		 * We use 10 MHz even though the maximum is 3.7 MHz. The driver
		 * will limit it automatically to max. frequency.
		 */
		.max_speed_hz		= 10 * 1000 * 1000,
		.bus_num		= 0,
		.chip_select		= 0,
		.mode			= SPI_MODE_3,
	},
};

/*
 * Up to v1.3, the Sim.One used SFRMOUT as SD card chip select, but this goes
 * low between multi-message command blocks. From v1.4, it uses a GPIO instead.
 * v1.3 parts will still work, since the signal on SFRMOUT is automatic.
 */
static struct gpiod_lookup_table simone_spi_cs_gpio_table = {
	.dev_id = "ep93xx-spi.0",
	.table = {
		GPIO_LOOKUP("A", 1, "cs", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct ep93xx_spi_info simone_spi_info __initdata = {
	.use_dma = 1,
};

static struct i2c_board_info __initdata simone_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("ds1337", 0x68),
	},
};

static struct platform_device simone_audio_device = {
	.name		= "simone-audio",
	.id		= -1,
};

static void __init simone_register_audio(void)
{
	ep93xx_register_ac97();
	platform_device_register(&simone_audio_device);
}

static void __init simone_init_machine(void)
{
	ep93xx_init_devices();
	ep93xx_register_flash(2, EP93XX_CS6_PHYS_BASE, SZ_8M);
	ep93xx_register_eth(&simone_eth_data, 1);
	ep93xx_register_fb(&simone_fb_info);
	ep93xx_register_i2c(simone_i2c_board_info,
			    ARRAY_SIZE(simone_i2c_board_info));
	gpiod_add_lookup_table(&simone_mmc_spi_gpio_table);
	gpiod_add_lookup_table(&simone_spi_cs_gpio_table);
	ep93xx_register_spi(&simone_spi_info, simone_spi_devices,
			    ARRAY_SIZE(simone_spi_devices));
	simone_register_audio();
}

MACHINE_START(SIM_ONE, "Simplemachines Sim.One Board")
	/* Maintainer: Ryan Mallon */
	.atag_offset	= 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= simone_init_machine,
	.init_late	= ep93xx_init_late,
	.restart	= ep93xx_restart,
MACHINE_END
