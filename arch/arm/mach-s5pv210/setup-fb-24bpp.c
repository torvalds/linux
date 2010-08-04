/* linux/arch/arm/plat-s5pv210/setup-fb-24bpp.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Base s5pv210 setup information for 24bpp LCD framebuffer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fb.h>

#include <mach/regs-fb.h>
#include <mach/gpio.h>
#include <mach/map.h>
#include <plat/fb.h>
#include <mach/regs-clock.h>
#include <plat/gpio-cfg.h>

void s5pv210_fb_gpio_setup_24bpp(void)
{
	unsigned int gpio = 0;

	for (gpio = S5PV210_GPF0(0); gpio <= S5PV210_GPF0(7); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	for (gpio = S5PV210_GPF1(0); gpio <= S5PV210_GPF1(7); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	for (gpio = S5PV210_GPF2(0); gpio <= S5PV210_GPF2(7); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	for (gpio = S5PV210_GPF3(0); gpio <= S5PV210_GPF3(3); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	/* Set DISPLAY_CONTROL register for Display path selection.
	 *
	 * ouput   |   RGB   |   I80   |   ITU
	 * -----------------------------------
	 *  00     |   MIE   |  FIMD   |  FIMD
	 *  01     | MDNIE   | MDNIE   |  FIMD
	 *  10     |  FIMD   |  FIMD   |  FIMD
	 *  11     |  FIMD   |  FIMD   |  FIMD
	 */
	writel(0x2, S5P_MDNIE_SEL);
}
