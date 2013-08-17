/*
 * arch/arm/mach-exynos/board-smdk4x12-display.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include <linux/pwm_backlight.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/lcd.h>
#include <linux/clk.h>

#include <plat/backlight.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/gpio-cfg.h>
#include <plat/regs-serial.h>
#include <plat/fb.h>
#include <plat/fb-core.h>
#include <plat/regs-fb.h>
#include <plat/regs-fb-v4.h>
#include <plat/backlight.h>

#include <mach/map.h>
#include <mach/spi-clocks.h>

#include "common.h"
#include "board-smdk4x12.h"

static struct samsung_bl_gpio_info smdk4x12_bl_gpio_info = {
	.no = EXYNOS4_GPD0(1),
	.func = S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data smdk4x12_bl_data = {
	.pwm_id = 1,
	.pwm_period_ns  = 1000,
};

static int lcd_power_on(struct lcd_device *ld, int enable)
{
	return 1;
}

static int reset_lcd(struct lcd_device *ld)
{
	int err = 0;

	if (exynos4_smdk4x12_get_revision() == SMDK4X12_REV_0_1) {
		err = gpio_request_one(EXYNOS4_GPM3(6),
				       GPIOF_OUT_INIT_HIGH, "GPM3");
		if (err) {
			printk(KERN_ERR "failed to request GPM3 for " \
				"lcd reset control\n");
			return err;
		}
		gpio_set_value(EXYNOS4_GPM3(6), 0);
		mdelay(1);
		gpio_set_value(EXYNOS4_GPM3(6), 1);
		gpio_free(EXYNOS4_GPM3(6));
	} else {
		err = gpio_request_one(EXYNOS4_GPX1(5),
				GPIOF_OUT_INIT_HIGH, "GPX1");
		if (err) {
			printk(KERN_ERR "failed to request GPX1 for " \
				"lcd reset control\n");
			return err;
		}
		gpio_set_value(EXYNOS4_GPX1(5), 0);
		mdelay(1);
		gpio_set_value(EXYNOS4_GPX1(5), 1);
		gpio_free(EXYNOS4_GPX1(5));
	}

	return 1;
}

static struct lcd_platform_data lms501kf03_platform_data = {
	.reset			= reset_lcd,
	.power_on		= lcd_power_on,
	.lcd_enabled		= 0,
	.reset_delay		= 100,	/* 100ms */
};

#define		LCD_BUS_NUM	3
#define		DISPLAY_CS	EXYNOS4_GPB(5)
#define		DISPLAY_CLK	EXYNOS4_GPB(4)
#define		DISPLAY_SI	EXYNOS4_GPB(7)

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias		= "lms501kf03",
		.platform_data		= (void *)&lms501kf03_platform_data,
		.max_speed_hz		= 1200000,
		.bus_num		= LCD_BUS_NUM,
		.chip_select		= 0,
		.mode			= SPI_MODE_3,
		.controller_data	= (void *)DISPLAY_CS,
	}
};

static struct spi_gpio_platform_data lms501kf03_spi_gpio_data = {
	.sck	= DISPLAY_CLK,
	.mosi	= DISPLAY_SI,
	.miso	= -1,
	.num_chipselect = 1,
};

static struct platform_device s3c_device_spi_gpio = {
	.name	= "spi_gpio",
	.id	= LCD_BUS_NUM,
	.dev	= {
		.parent		= &s5p_device_fimd0.dev,
		.platform_data	= &lms501kf03_spi_gpio_data,
	},
};

static struct s3c_fb_pd_win smdk4x12_fb_win0 = {
	.win_mode = {
		.left_margin	= 8,		/* HBPD */
		.right_margin	= 8,		/* HFPD */
		.upper_margin	= 6,		/* VBPD */
		.lower_margin	= 6,		/* VFPD */
		.hsync_len	= 6,		/* HSPW */
		.vsync_len	= 4,		/* VSPW */
		.xres		= 480,
		.yres		= 800,
	},
	.virtual_x		= 480,
	.virtual_y		= 1600,
	.width			= 48,
	.height			= 80,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk4x12_fb_win1 = {
	.win_mode = {
		.left_margin	= 8,		/* HBPD */
		.right_margin	= 8,		/* HFPD */
		.upper_margin	= 6,		/* VBPD */
		.lower_margin	= 6,		/* VFPD */
		.hsync_len	= 6,		/* HSPW */
		.vsync_len	= 4,		/* VSPW */
		.xres		= 480,
		.yres		= 800,
	},
	.virtual_x		= 480,
	.virtual_y		= 1600,
	.width			= 48,
	.height			= 80,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk4x12_fb_win2 = {
	.win_mode = {
		.left_margin	= 8,		/* HBPD */
		.right_margin	= 8,		/* HFPD */
		.upper_margin	= 6,		/* VBPD */
		.lower_margin	= 6,		/* VFPD */
		.hsync_len	= 6,		/* HSPW */
		.vsync_len	= 4,		/* VSPW */
		.xres		= 480,
		.yres		= 800,
	},
	.virtual_x		= 480,
	.virtual_y		= 1600,
	.width			= 48,
	.height			= 80,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk4x12_fb_win3 = {
	.win_mode = {
		.left_margin	= 8,		/* HBPD */
		.right_margin	= 8,		/* HFPD */
		.upper_margin	= 6,		/* VBPD */
		.lower_margin	= 6,		/* VFPD */
		.hsync_len	= 6,		/* HSPW */
		.vsync_len	= 4,		/* VSPW */
		.xres		= 480,
		.yres		= 800,
	},
	.virtual_x		= 480,
	.virtual_y		= 1600,
	.width			= 48,
	.height			= 80,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk4x12_fb_win4 = {
	.win_mode = {
		.left_margin	= 8,		/* HBPD */
		.right_margin	= 8,		/* HFPD */
		.upper_margin	= 6,		/* VBPD */
		.lower_margin	= 6,		/* VFPD */
		.hsync_len	= 6,		/* HSPW */
		.vsync_len	= 4,		/* VSPW */
		.xres		= 480,
		.yres		= 800,
	},
	.virtual_x		= 480,
	.virtual_y		= 1600,
	.width			= 48,
	.height			= 80,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_platdata smdk4x12_lcd0_pdata __initdata = {
	.win[0]		= &smdk4x12_fb_win0,
	.win[1]		= &smdk4x12_fb_win1,
	.win[2]		= &smdk4x12_fb_win2,
	.win[3]		= &smdk4x12_fb_win3,
	.win[4]		= &smdk4x12_fb_win4,
	.default_win	= 0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= exynos4_fimd0_gpio_setup_24bpp,
	.ip_version	= FIMD_VERSION_4X,
};

static struct platform_device *smdk4x12_display_devices[] __initdata = {
	&s5p_device_fimd0,
	&s3c_device_spi_gpio,
};

void __init exynos4_smdk4x12_display_init(void)
{
	samsung_bl_set(&smdk4x12_bl_gpio_info, &smdk4x12_bl_data);

	dev_set_name(&s5p_device_fimd0.dev, "s3cfb.0");
	clk_add_alias("lcd", "exynos4-fb.0", "lcd", &s5p_device_fimd0.dev);
	clk_add_alias("sclk_fimd", "exynos4-fb.0", "sclk_fimd", \
			&s5p_device_fimd0.dev);
	s5p_fb_setname(0, "exynos4-fb");

	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));

	s5p_fimd0_set_platdata(&smdk4x12_lcd0_pdata);

	platform_add_devices(smdk4x12_display_devices, \
				ARRAY_SIZE(smdk4x12_display_devices));

	exynos4_fimd_setup_clock(&s5p_device_fimd0.dev, "sclk_fimd", \
				"mout_mpll_user", 800 * MHZ);
}
