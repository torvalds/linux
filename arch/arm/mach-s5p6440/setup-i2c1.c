/* linux/arch/arm/mach-s5p6440/setup-i2c1.c
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * I2C1 GPIO configuration.
 *
 * Based on plat-s3c64xx/setup-i2c0.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gpio.h>

struct platform_device; /* don't need the contents */

#include <plat/gpio-cfg.h>
#include <plat/iic.h>

void s3c_i2c1_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgpin(S5P6440_GPR(9), S3C_GPIO_SFN(6));
	s3c_gpio_setpull(S5P6440_GPR(9), S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(S5P6440_GPR(10), S3C_GPIO_SFN(6));
	s3c_gpio_setpull(S5P6440_GPR(10), S3C_GPIO_PULL_UP);
}
