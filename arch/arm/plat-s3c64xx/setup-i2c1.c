/* linux/arch/arm/plat-s3c64xx/setup-i2c1.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Base S3C64XX I2C bus 1 gpio configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>

struct platform_device; /* don't need the contents */

#include <mach/gpio.h>
#include <plat/iic.h>
#include <plat/gpio-bank-b.h>
#include <plat/gpio-cfg.h>

void s3c_i2c1_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgpin(S3C64XX_GPB(2), S3C64XX_GPB2_I2C_SCL1);
	s3c_gpio_cfgpin(S3C64XX_GPB(3), S3C64XX_GPB3_I2C_SDA1);
	s3c_gpio_setpull(S3C64XX_GPB(2), S3C_GPIO_PULL_UP);
	s3c_gpio_setpull(S3C64XX_GPB(3), S3C_GPIO_PULL_UP);
}
