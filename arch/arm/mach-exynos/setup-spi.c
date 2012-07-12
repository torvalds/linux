/* linux/arch/arm/mach-exynos4/setup-spi.c
 *
 * Copyright (C) 2011 Samsung Electronics Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/gpio.h>
#include <linux/platform_device.h>

#include <plat/gpio-cfg.h>

#ifdef CONFIG_S3C64XX_DEV_SPI0
int s3c64xx_spi0_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgpin(EXYNOS4_GPB(0), S3C_GPIO_SFN(2));
	s3c_gpio_setpull(EXYNOS4_GPB(0), S3C_GPIO_PULL_UP);
	s3c_gpio_cfgall_range(EXYNOS4_GPB(2), 2,
			      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
	return 0;
}
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI1
int s3c64xx_spi1_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgpin(EXYNOS4_GPB(4), S3C_GPIO_SFN(2));
	s3c_gpio_setpull(EXYNOS4_GPB(4), S3C_GPIO_PULL_UP);
	s3c_gpio_cfgall_range(EXYNOS4_GPB(6), 2,
			      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
	return 0;
}
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI2
int s3c64xx_spi2_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgpin(EXYNOS4_GPC1(1), S3C_GPIO_SFN(5));
	s3c_gpio_setpull(EXYNOS4_GPC1(1), S3C_GPIO_PULL_UP);
	s3c_gpio_cfgall_range(EXYNOS4_GPC1(3), 2,
			      S3C_GPIO_SFN(5), S3C_GPIO_PULL_UP);
	return 0;
}
#endif
