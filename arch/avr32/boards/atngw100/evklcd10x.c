/*
 * Board-specific setup code for the ATEVKLCD10X addon board to the ATNGW100
 * Network Gateway
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/gpio.h>
#include <linux/fb.h>
#include <linux/platform_device.h>

#include <video/atmel_lcdc.h>

#include <asm/setup.h>

#include <mach/at32ap700x.h>
#include <mach/portmux.h>
#include <mach/board.h>

#include <sound/atmel-ac97c.h>

static struct ac97c_platform_data __initdata ac97c0_data = {
	.reset_pin = GPIO_PIN_PB(19),
};

#ifdef CONFIG_BOARD_ATNGW100_EVKLCD10X_VGA
static struct fb_videomode __initdata tcg057vglad_modes[] = {
	{
		.name		= "640x480 @ 50",
		.refresh	= 50,
		.xres		= 640,		.yres		= 480,
		.pixclock	= KHZ2PICOS(25180),

		.left_margin	= 64,		.right_margin	= 96,
		.upper_margin	= 34,		.lower_margin	= 11,
		.hsync_len	= 64,		.vsync_len	= 15,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs __initdata atevklcd10x_default_monspecs = {
	.manufacturer		= "KYO",
	.monitor		= "TCG057VGLAD",
	.modedb			= tcg057vglad_modes,
	.modedb_len		= ARRAY_SIZE(tcg057vglad_modes),
	.hfmin			= 19948,
	.hfmax			= 31478,
	.vfmin			= 50,
	.vfmax			= 67,
	.dclkmax		= 28330000,
};

static struct atmel_lcdfb_pdata __initdata atevklcd10x_lcdc_data = {
	.default_bpp		= 16,
	.default_dmacon		= ATMEL_LCDC_DMAEN | ATMEL_LCDC_DMA2DEN,
	.default_lcdcon2	= (ATMEL_LCDC_DISTYPE_TFT
				   | ATMEL_LCDC_CLKMOD_ALWAYSACTIVE
				   | ATMEL_LCDC_MEMOR_BIG),
	.default_monspecs	= &atevklcd10x_default_monspecs,
	.guard_time		= 2,
};
#elif CONFIG_BOARD_ATNGW100_EVKLCD10X_QVGA
static struct fb_videomode __initdata tcg057qvlad_modes[] = {
	{
		.name		= "320x240 @ 50",
		.refresh	= 50,
		.xres		= 320,		.yres		= 240,
		.pixclock	= KHZ2PICOS(6300),

		.left_margin	= 34,		.right_margin	= 46,
		.upper_margin	= 7,		.lower_margin	= 15,
		.hsync_len	= 64,		.vsync_len	= 12,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs __initdata atevklcd10x_default_monspecs = {
	.manufacturer		= "KYO",
	.monitor		= "TCG057QVLAD",
	.modedb			= tcg057qvlad_modes,
	.modedb_len		= ARRAY_SIZE(tcg057qvlad_modes),
	.hfmin			= 19948,
	.hfmax			= 31478,
	.vfmin			= 50,
	.vfmax			= 67,
	.dclkmax		= 7000000,
};

static struct atmel_lcdfb_pdata __initdata atevklcd10x_lcdc_data = {
	.default_bpp		= 16,
	.default_dmacon		= ATMEL_LCDC_DMAEN | ATMEL_LCDC_DMA2DEN,
	.default_lcdcon2	= (ATMEL_LCDC_DISTYPE_TFT
				   | ATMEL_LCDC_CLKMOD_ALWAYSACTIVE
				   | ATMEL_LCDC_MEMOR_BIG),
	.default_monspecs	= &atevklcd10x_default_monspecs,
	.guard_time		= 2,
};
#elif CONFIG_BOARD_ATNGW100_EVKLCD10X_POW_QVGA
static struct fb_videomode __initdata ph320240t_modes[] = {
	{
		.name		= "320x240 @ 60",
		.refresh	= 60,
		.xres		= 320,		.yres		= 240,
		.pixclock	= KHZ2PICOS(6300),

		.left_margin	= 38,		.right_margin	= 20,
		.upper_margin	= 15,		.lower_margin	= 5,
		.hsync_len	= 30,		.vsync_len	= 3,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs __initdata atevklcd10x_default_monspecs = {
	.manufacturer		= "POW",
	.monitor		= "PH320240T",
	.modedb			= ph320240t_modes,
	.modedb_len		= ARRAY_SIZE(ph320240t_modes),
	.hfmin			= 14400,
	.hfmax			= 21600,
	.vfmin			= 50,
	.vfmax			= 90,
	.dclkmax		= 6400000,
};

static struct atmel_lcdfb_pdata __initdata atevklcd10x_lcdc_data = {
	.default_bpp		= 16,
	.default_dmacon		= ATMEL_LCDC_DMAEN | ATMEL_LCDC_DMA2DEN,
	.default_lcdcon2	= (ATMEL_LCDC_DISTYPE_TFT
				   | ATMEL_LCDC_CLKMOD_ALWAYSACTIVE
				   | ATMEL_LCDC_MEMOR_BIG),
	.default_monspecs	= &atevklcd10x_default_monspecs,
	.guard_time		= 2,
};
#endif

static void atevklcd10x_lcdc_power_control(struct atmel_lcdfb_pdata *pdata, int on)
{
	gpio_set_value(GPIO_PIN_PB(15), on);
}

static int __init atevklcd10x_init(void)
{
	/* PB15 is connected to the enable line on the boost regulator
	 * controlling the backlight for the LCD panel.
	 */
	at32_select_gpio(GPIO_PIN_PB(15), AT32_GPIOF_OUTPUT);
	gpio_request(GPIO_PIN_PB(15), "backlight");
	gpio_direction_output(GPIO_PIN_PB(15), 0);

	atevklcd10x_lcdc_data.atmel_lcdfb_power_control =
		atevklcd10x_lcdc_power_control;

	at32_add_device_lcdc(0, &atevklcd10x_lcdc_data,
			fbmem_start, fbmem_size,
#ifdef CONFIG_BOARD_ATNGW100_MKII
			ATMEL_LCDC_PRI_18BIT | ATMEL_LCDC_PC_DVAL
#else
			ATMEL_LCDC_ALT_18BIT | ATMEL_LCDC_PE_DVAL
#endif
			);

	at32_add_device_ac97c(0, &ac97c0_data, AC97C_BOTH);

	return 0;
}
postcore_initcall(atevklcd10x_init);
