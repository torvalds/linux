/* linux/arch/arm/mach-s5p64x0/setup-fb-24bpp.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * Base S5P64X0 GPIO setup information for LCD framebuffer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/fb.h>

#include <plat/cpu.h>
#include <plat/fb.h>
#include <plat/gpio-cfg.h>
#include <mach/gpio-samsung.h>

void s5p64x0_fb_gpio_setup_24bpp(void)
{
	if (soc_is_s5p6440()) {
		s3c_gpio_cfgrange_nopull(S5P6440_GPI(0), 16, S3C_GPIO_SFN(2));
		s3c_gpio_cfgrange_nopull(S5P6440_GPJ(0), 12, S3C_GPIO_SFN(2));
	} else if (soc_is_s5p6450()) {
		s3c_gpio_cfgrange_nopull(S5P6450_GPI(0), 16, S3C_GPIO_SFN(2));
		s3c_gpio_cfgrange_nopull(S5P6450_GPJ(0), 12, S3C_GPIO_SFN(2));
	}
}
