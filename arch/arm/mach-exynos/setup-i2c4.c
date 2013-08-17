/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *
 * I2C4 GPIO configuration.
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

void s3c_i2c4_cfg_gpio(struct platform_device *dev)
{
	if (soc_is_exynos4210())
		s3c_gpio_cfgall_range(EXYNOS4_GPB(2), 2,
				      S3C_GPIO_SFN(3), S3C_GPIO_PULL_UP);

	else if (soc_is_exynos4212() || soc_is_exynos4412())
		s3c_gpio_cfgall_range(EXYNOS4_GPB(0), 2,
				      S3C_GPIO_SFN(3), S3C_GPIO_PULL_UP);

	else if (soc_is_exynos5250())
		s3c_gpio_cfgall_range(EXYNOS5_GPA2(0), 2,
				      S3C_GPIO_SFN(3), S3C_GPIO_PULL_UP);

	else
		pr_err("failed to configure gpio for i2c4\n");
}
