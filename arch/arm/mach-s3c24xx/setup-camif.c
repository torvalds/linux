// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2012 Sylwester Nawrocki <sylvester.nawrocki@gmail.com>
//
// Helper functions for S3C24XX/S3C64XX SoC series CAMIF driver

#include <linux/gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/gpio-samsung.h>

/* Number of camera port pins, without FIELD */
#define S3C_CAMIF_NUM_GPIOS	13

/* Default camera port configuration helpers. */

static void camif_get_gpios(int *gpio_start, int *gpio_reset)
{
#ifdef CONFIG_ARCH_S3C24XX
	*gpio_start = S3C2410_GPJ(0);
	*gpio_reset = S3C2410_GPJ(12);
#else
	/* s3c64xx */
	*gpio_start = S3C64XX_GPF(0);
	*gpio_reset = S3C64XX_GPF(3);
#endif
}

int s3c_camif_gpio_get(void)
{
	int gpio_start, gpio_reset;
	int ret, i;

	camif_get_gpios(&gpio_start, &gpio_reset);

	for (i = 0; i < S3C_CAMIF_NUM_GPIOS; i++) {
		int gpio = gpio_start + i;

		if (gpio == gpio_reset)
			continue;

		ret = gpio_request(gpio, "camif");
		if (!ret)
			ret = s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		if (ret) {
			pr_err("failed to configure GPIO %d\n", gpio);
			for (--i; i >= 0; i--)
				gpio_free(gpio--);
			return ret;
		}
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	return 0;
}

void s3c_camif_gpio_put(void)
{
	int i, gpio_start, gpio_reset;

	camif_get_gpios(&gpio_start, &gpio_reset);

	for (i = 0; i < S3C_CAMIF_NUM_GPIOS; i++) {
		int gpio = gpio_start + i;
		if (gpio != gpio_reset)
			gpio_free(gpio);
	}
}
