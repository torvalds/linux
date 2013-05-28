/*
 * linux/arch/arm/mach-s3c64xx/mach-smartq5.c
 *
 * Copyright (C) 2010 Maurus Cuelenaere
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <video/samsung_fimd.h>
#include <mach/map.h>
#include <mach/regs-gpio.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/fb.h>
#include <plat/gpio-cfg.h>
#include <plat/samsung-time.h>

#include "common.h"
#include "mach-smartq.h"

static struct gpio_led smartq5_leds[] = {
	{
		.name			= "smartq5:green",
		.active_low		= 1,
		.gpio			= S3C64XX_GPN(8),
	},
	{
		.name			= "smartq5:red",
		.active_low		= 1,
		.gpio			= S3C64XX_GPN(9),
	},
};

static struct gpio_led_platform_data smartq5_led_data = {
	.num_leds = ARRAY_SIZE(smartq5_leds),
	.leds = smartq5_leds,
};

static struct platform_device smartq5_leds_device = {
	.name			= "leds-gpio",
	.id			= -1,
	.dev.platform_data	= &smartq5_led_data,
};

/* Labels according to the SmartQ manual */
static struct gpio_keys_button smartq5_buttons[] = {
	{
		.gpio			= S3C64XX_GPL(14),
		.code			= KEY_POWER,
		.desc			= "Power",
		.active_low		= 1,
		.debounce_interval	= 5,
		.type                   = EV_KEY,
	},
	{
		.gpio			= S3C64XX_GPN(2),
		.code			= KEY_KPMINUS,
		.desc			= "Minus",
		.active_low		= 1,
		.debounce_interval	= 5,
		.type                   = EV_KEY,
	},
	{
		.gpio			= S3C64XX_GPN(12),
		.code			= KEY_KPPLUS,
		.desc			= "Plus",
		.active_low		= 1,
		.debounce_interval	= 5,
		.type                   = EV_KEY,
	},
	{
		.gpio			= S3C64XX_GPN(15),
		.code			= KEY_ENTER,
		.desc			= "Move",
		.active_low		= 1,
		.debounce_interval	= 5,
		.type                   = EV_KEY,
	},
};

static struct gpio_keys_platform_data smartq5_buttons_data  = {
	.buttons	= smartq5_buttons,
	.nbuttons	= ARRAY_SIZE(smartq5_buttons),
};

static struct platform_device smartq5_buttons_device  = {
	.name		= "gpio-keys",
	.id		= 0,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &smartq5_buttons_data,
	}
};

static struct s3c_fb_pd_win smartq5_fb_win0 = {
	.max_bpp	= 32,
	.default_bpp	= 16,
	.xres		= 800,
	.yres		= 480,
};

static struct fb_videomode smartq5_lcd_timing = {
	.left_margin	= 216,
	.right_margin	= 40,
	.upper_margin	= 35,
	.lower_margin	= 10,
	.hsync_len	= 1,
	.vsync_len	= 1,
	.xres		= 800,
	.yres		= 480,
	.refresh	= 80,
};

static struct s3c_fb_platdata smartq5_lcd_pdata __initdata = {
	.setup_gpio	= s3c64xx_fb_gpio_setup_24bpp,
	.vtiming	= &smartq5_lcd_timing,
	.win[0]		= &smartq5_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC |
			  VIDCON1_INV_VDEN,
};

static struct platform_device *smartq5_devices[] __initdata = {
	&smartq5_leds_device,
	&smartq5_buttons_device,
};

static void __init smartq5_machine_init(void)
{
	s3c_fb_set_platdata(&smartq5_lcd_pdata);

	smartq_machine_init();

	platform_add_devices(smartq5_devices, ARRAY_SIZE(smartq5_devices));
}

MACHINE_START(SMARTQ5, "SmartQ 5")
	/* Maintainer: Maurus Cuelenaere <mcuelenaere AT gmail DOT com> */
	.atag_offset	= 0x100,
	.init_irq	= s3c6410_init_irq,
	.map_io		= smartq_map_io,
	.init_machine	= smartq5_machine_init,
	.init_late	= s3c64xx_init_late,
	.init_time	= samsung_timer_init,
	.restart	= s3c64xx_restart,
MACHINE_END
