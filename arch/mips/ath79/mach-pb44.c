/*
 *  Atheros PB44 reference board support
 *
 *  Copyright (C) 2009-2010 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/i2c/pcf857x.h>

#include "machtypes.h"

#define PB44_GPIO_I2C_SCL	0
#define PB44_GPIO_I2C_SDA	1

#define PB44_GPIO_EXP_BASE	16

static struct i2c_gpio_platform_data pb44_i2c_gpio_data = {
	.sda_pin        = PB44_GPIO_I2C_SDA,
	.scl_pin        = PB44_GPIO_I2C_SCL,
};

static struct platform_device pb44_i2c_gpio_device = {
	.name		= "i2c-gpio",
	.id		= 0,
	.dev = {
		.platform_data	= &pb44_i2c_gpio_data,
	}
};

static struct pcf857x_platform_data pb44_pcf857x_data = {
	.gpio_base	= PB44_GPIO_EXP_BASE,
};

static struct i2c_board_info pb44_i2c_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("pcf8575", 0x20),
		.platform_data  = &pb44_pcf857x_data,
	},
};

static void __init pb44_init(void)
{
	i2c_register_board_info(0, pb44_i2c_board_info,
				ARRAY_SIZE(pb44_i2c_board_info));
	platform_device_register(&pb44_i2c_gpio_device);
}

MIPS_MACHINE(ATH79_MACH_PB44, "PB44", "Atheros PB44 reference board",
	     pb44_init);
