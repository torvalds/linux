/* linux/arch/arm/mach-exynos4/setup-sdhci-gpio.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * EXYNOS4 - Helper functions for setting up SDHCI device(s) GPIO (HSMMC)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>

#include <plat/gpio-cfg.h>

void exynos4_setup_dwmci_cfg_gpio(struct platform_device *dev, int width)
{
	unsigned int gpio;

	for (gpio = EXYNOS4_GPK0(0); gpio < EXYNOS4_GPK0(2); gpio++) {
		gpio_request(gpio, "GPK0");
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV2);
		gpio_free(gpio);
	}

	switch (width) {
	case MMC_BUS_WIDTH_8:
		for (gpio = EXYNOS4_GPK1(3); gpio <= EXYNOS4_GPK1(6); gpio++) {
			gpio_request(gpio, "GPK1");
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(4));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV2);
			gpio_free(gpio);
		}
	case MMC_BUS_WIDTH_4:
		for (gpio = EXYNOS4_GPK0(3); gpio <= EXYNOS4_GPK0(6); gpio++) {
			gpio_request(gpio, "GPK0");
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV2);
			gpio_free(gpio);
		}
		break;
	case MMC_BUS_WIDTH_1:
		gpio = EXYNOS4_GPK0(3);
		gpio_request(gpio, "GPK0");
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV2);
		gpio_free(gpio);
	default:
		break;
	}
}
