/*
 * linux/arch/arm/plat-s5pc100/setup-fb-24bpp.c
 *
 * Copyright 2009 Samsung Electronics
 *
 * Base S5PC1XX setup information for 24bpp LCD framebuffer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/gpio.h>

#include <mach/regs-fb.h>
#include <mach/map.h>
#include <plat/fb.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-s5pc1xx.h>

#define DISR_OFFSET	0x7008

void s5pc100_fb_gpio_setup_24bpp(void)
{
	unsigned int gpio = 0;

	for (gpio = S5PC100_GPF0(0); gpio <= S5PC100_GPF0(7); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	for (gpio = S5PC100_GPF1(0); gpio <= S5PC100_GPF1(7); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	for (gpio = S5PC100_GPF2(0); gpio <= S5PC100_GPF2(7); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	for (gpio = S5PC100_GPF3(0); gpio <= S5PC100_GPF3(3); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}
}
