/* linux/arch/arm/mach-s5pc100/setup-i2c1.c
 *
 * Copyright 2009 Samsung Electronics Co.
 *	Byungho Min <bhmin@samsung.com>
 *
 * Base S5PC100 I2C bus 1 gpio configuration
 *
 * Based on plat-s3c64xx/setup-i2c1.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>

struct platform_device; /* don't need the contents */

#include <linux/gpio.h>
#include <plat/iic.h>
#include <plat/gpio-cfg.h>

void s3c_i2c1_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgall_range(S5PC100_GPD(5), 2,
			      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
}
