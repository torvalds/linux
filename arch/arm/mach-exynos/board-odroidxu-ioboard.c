/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/export.h>

#include <plat/devs.h>
#include <plat/gpio-cfg.h>

#include <mach/irqs.h>
#include <mach/hs-iic.h>
#include <plat/iic.h>
#include <mach/regs-gpio.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <plat/s3c64xx-spi.h>
#include <mach/spi-clocks.h>

#include "board-odroidxu.h"

static struct   platform_device   odroidxu_ioboard_keyled = {
    .name = "ioboard-keyled",
    .id = -1,
};

#define     GPIO_I2C_BUS_NUM    10
#define		GPIO_I2C2_SDA	    EXYNOS5410_GPX3(1)
#define		GPIO_I2C2_SCL	    EXYNOS5410_GPX1(7)

static struct 	i2c_gpio_platform_data 	i2c_gpio_platdata = {
	.sda_pin = GPIO_I2C2_SDA,   // gpio number
	.scl_pin = GPIO_I2C2_SCL,
	.udelay  = 10,              // 50KHz
	.sda_is_open_drain  = 0,
	.scl_is_open_drain  = 0,
	.scl_is_output_only = 0
};

static struct 	platform_device 	odroidxu_ioboard_i2c = {
	.name 	= "i2c-gpio",
	.id  	= GPIO_I2C_BUS_NUM,    // adepter number
	.dev.platform_data = &i2c_gpio_platdata,
};

static struct i2c_board_info i2c_gpio_devs[] __initdata = {
	{
		I2C_BOARD_INFO("ioboard-bh1780", (0x52 >> 1)),
	},
	{
		I2C_BOARD_INFO("ioboard-bmp180", (0xEE >> 1)),
	},
};

static struct   platform_device     odroidxu_ioboard_adc = {
    .name   = "ioboard-adc",
    .id     = -1,
};

static struct s3c64xx_spi_csinfo spi1_csi[] = {
	[0] = {
		.line       = EXYNOS5410_GPA2(5),
		.set_level  = gpio_set_value,
	},
};
static struct spi_board_info spi1_board_info[] __initdata = {
	{
		.modalias			= "ioboard-spi",
		.platform_data		= NULL,
		.max_speed_hz		= 20 * 1000 * 1000,     // 20 Mhz
		.bus_num			= 1,
		.chip_select		= 0,
		.mode				= SPI_MODE_0,
		.controller_data    = &spi1_csi[0],
	}
};

static struct platform_device odroidxu_ioboard_spi = {
	.name			= "ioboard-spi",
	.id 			= -1,
};

static struct platform_device *odroidxu_ioboard_devices[] __initdata = {
    &odroidxu_ioboard_i2c,
	&odroidxu_ioboard_keyled,
	&odroidxu_ioboard_adc,
	&s3c64xx_device_spi1,
	&odroidxu_ioboard_spi,
};

void __init exynos5_odroidxu_ioboard_init(void)
{
	i2c_register_board_info(GPIO_I2C_BUS_NUM, i2c_gpio_devs,
			ARRAY_SIZE(i2c_gpio_devs));

	if (!exynos_spi_cfg_cs(spi1_csi[0].line, 1))    {
		s3c64xx_spi1_set_platdata(&s3c64xx_spi1_pdata, EXYNOS_SPI_SRCCLK_SCLK, ARRAY_SIZE(spi1_csi));
		if (spi_register_board_info(spi1_board_info, ARRAY_SIZE(spi1_board_info)))
			printk("\n%s: spi_register_board_info returned error!\n\n", __func__);
	}
	else    printk("\n%s:exynos_spi_cfg_cs returned error\n\n", __func__);

	platform_add_devices(odroidxu_ioboard_devices, ARRAY_SIZE(odroidxu_ioboard_devices));
}
