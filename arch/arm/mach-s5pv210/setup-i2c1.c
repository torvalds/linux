/* linux/arch/arm/mach-s5pv210/setup-i2c1.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * I2C1 GPIO configuration.
 *
 * Based on plat-s3c64xx/setup-i2c1.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gpio.h>

struct platform_device; /* don't need the contents */

#include <linux/platform_data/i2c-s3c2410.h>
#include <plat/gpio-cfg.h>

void s3c_i2c1_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgall_range(S5PV210_GPD1(2), 2,
			      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
}
