/*
 * arch/arm/mach-ep93xx/simone.c
 * Simplemachines Sim.One support.
 *
 * Copyright (C) 2010 Ryan Mallon <ryan@bluewatersys.com>
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
#include <linux/mtd/physmap.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>

#include <mach/hardware.h>
#include <mach/fb.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

static struct physmap_flash_data simone_flash_data = {
	.width		= 2,
};

static struct resource simone_flash_resource = {
	.start		= EP93XX_CS6_PHYS_BASE,
	.end		= EP93XX_CS6_PHYS_BASE + SZ_8M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device simone_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.num_resources	= 1,
	.resource	= &simone_flash_resource,
	.dev = {
		.platform_data	= &simone_flash_data,
	},
};

static struct ep93xx_eth_data simone_eth_data = {
	.phy_id		= 1,
};

static struct ep93xxfb_mach_info simone_fb_info = {
	.num_modes	= EP93XXFB_USE_MODEDB,
	.bpp		= 16,
	.flags		= EP93XXFB_USE_SDCSN0 | EP93XXFB_PCLK_FALLING,
};

static struct i2c_gpio_platform_data simone_i2c_gpio_data = {
	.sda_pin		= EP93XX_GPIO_LINE_EEDAT,
	.sda_is_open_drain	= 0,
	.scl_pin		= EP93XX_GPIO_LINE_EECLK,
	.scl_is_open_drain	= 0,
	.udelay			= 0,
	.timeout		= 0,
};

static struct i2c_board_info __initdata simone_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("ds1337", 0x68),
	},
};

static void __init simone_init_machine(void)
{
	ep93xx_init_devices();

	platform_device_register(&simone_flash);
	ep93xx_register_eth(&simone_eth_data, 1);
	ep93xx_register_fb(&simone_fb_info);
	ep93xx_register_i2c(&simone_i2c_gpio_data, simone_i2c_board_info,
			    ARRAY_SIZE(simone_i2c_board_info));
}

MACHINE_START(SIM_ONE, "Simplemachines Sim.One Board")
/* Maintainer: Ryan Mallon <ryan@bluewatersys.com> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE0_PHYS_BASE + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= simone_init_machine,
MACHINE_END
