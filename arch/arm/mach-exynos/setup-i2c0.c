/*
 * Copyright (c) 2009-2012 Samsung Electronics Co., Ltd.
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
#include <linux/platform_data/i2c-s3c2410.h>
#include <plat/gpio-cfg.h>
#include <plat/cpu.h>

void s3c_i2c0_cfg_gpio(struct platform_device *dev)
{
	if (soc_is_exynos5250())
		/* will be implemented with gpio function */
		return;

	s3c_gpio_cfgall_range(EXYNOS4_GPD1(0), 2,
			      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
}
