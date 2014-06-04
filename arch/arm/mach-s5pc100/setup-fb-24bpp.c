/*
 * linux/arch/arm/mach-s5pc100/setup-fb-24bpp.c
 *
 * Copyright 2009 Samsung Electronics
 *
 * Base S5PC100 setup information for 24bpp LCD framebuffer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fb.h>

#include <mach/map.h>
#include <mach/gpio-samsung.h>
#include <plat/fb.h>
#include <plat/gpio-cfg.h>

#define DISR_OFFSET	0x7008

static void s5pc100_fb_setgpios(unsigned int base, unsigned int nr)
{
	s3c_gpio_cfgrange_nopull(base, nr, S3C_GPIO_SFN(2));
}

void s5pc100_fb_gpio_setup_24bpp(void)
{
	s5pc100_fb_setgpios(S5PC100_GPF0(0), 8);
	s5pc100_fb_setgpios(S5PC100_GPF1(0), 8);
	s5pc100_fb_setgpios(S5PC100_GPF2(0), 8);
	s5pc100_fb_setgpios(S5PC100_GPF3(0), 4);
}
