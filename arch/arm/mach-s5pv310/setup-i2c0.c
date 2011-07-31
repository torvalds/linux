/*
 * linux/arch/arm/mach-s5pv310/setup-i2c0.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * I2C0 GPIO configuration.
 *
 * Based on plat-s3c64xx/setup-i2c0.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

struct platform_device; /* don't need the contents */

#include <linux/gpio.h>
#include <plat/iic.h>
#include <plat/gpio-cfg.h>

void s3c_i2c0_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgpin(S5PV310_GPD1(0), S3C_GPIO_SFN(2));
	s3c_gpio_setpull(S5PV310_GPD1(0), S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(S5PV310_GPD1(1), S3C_GPIO_SFN(2));
	s3c_gpio_setpull(S5PV310_GPD1(1), S3C_GPIO_PULL_UP);
}
