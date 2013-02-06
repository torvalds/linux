/*
 * linux/arch/arm/mach-exynos/setup-i2c7.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *
 * I2C7 GPIO configuration.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

struct platform_device; /* don't need the contents */

#include <linux/gpio.h>
#include <plat/iic.h>
#include <plat/gpio-cfg.h>
#include <plat/cpu.h>

void s3c_i2c7_cfg_gpio(struct platform_device *dev)
{
	if (soc_is_exynos5210() || soc_is_exynos5250())
		s3c_gpio_cfgall_range(EXYNOS5_GPB2(2), 2,
			S3C_GPIO_SFN(3), S3C_GPIO_PULL_UP);
	else
		s3c_gpio_cfgall_range(EXYNOS4_GPD0(2), 2,
			S3C_GPIO_SFN(3), S3C_GPIO_PULL_UP);
}
