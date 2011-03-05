/*
 * Copyright 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/common.h>
#include <mach/iomux-mx23.h>

#include "devices-mx23.h"

#define MX23EVK_LCD_ENABLE	MXS_GPIO_NR(1, 18)
#define MX23EVK_BL_ENABLE	MXS_GPIO_NR(1, 28)

static const iomux_cfg_t mx23evk_pads[] __initconst = {
	/* duart */
	MX23_PAD_PWM0__DUART_RX | MXS_PAD_CTRL,
	MX23_PAD_PWM1__DUART_TX | MXS_PAD_CTRL,

	/* auart */
	MX23_PAD_AUART1_RX__AUART1_RX | MXS_PAD_CTRL,
	MX23_PAD_AUART1_TX__AUART1_TX | MXS_PAD_CTRL,
	MX23_PAD_AUART1_CTS__AUART1_CTS | MXS_PAD_CTRL,
	MX23_PAD_AUART1_RTS__AUART1_RTS | MXS_PAD_CTRL,

	/* mxsfb (lcdif) */
	MX23_PAD_LCD_D00__LCD_D00 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D01__LCD_D01 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D02__LCD_D02 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D03__LCD_D03 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D04__LCD_D04 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D05__LCD_D05 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D06__LCD_D06 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D07__LCD_D07 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D08__LCD_D08 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D09__LCD_D09 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D10__LCD_D10 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D11__LCD_D11 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D12__LCD_D12 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D13__LCD_D13 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D14__LCD_D14 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D15__LCD_D15 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D16__LCD_D16 | MXS_PAD_CTRL,
	MX23_PAD_LCD_D17__LCD_D17 | MXS_PAD_CTRL,
	MX23_PAD_GPMI_D08__LCD_D18 | MXS_PAD_CTRL,
	MX23_PAD_GPMI_D09__LCD_D19 | MXS_PAD_CTRL,
	MX23_PAD_GPMI_D10__LCD_D20 | MXS_PAD_CTRL,
	MX23_PAD_GPMI_D11__LCD_D21 | MXS_PAD_CTRL,
	MX23_PAD_GPMI_D12__LCD_D22 | MXS_PAD_CTRL,
	MX23_PAD_GPMI_D13__LCD_D23 | MXS_PAD_CTRL,
	MX23_PAD_LCD_VSYNC__LCD_VSYNC | MXS_PAD_CTRL,
	MX23_PAD_LCD_HSYNC__LCD_HSYNC | MXS_PAD_CTRL,
	MX23_PAD_LCD_DOTCK__LCD_DOTCK | MXS_PAD_CTRL,
	MX23_PAD_LCD_ENABLE__LCD_ENABLE | MXS_PAD_CTRL,
	/* LCD panel enable */
	MX23_PAD_LCD_RESET__GPIO_1_18 | MXS_PAD_CTRL,
	/* backlight control */
	MX23_PAD_PWM2__GPIO_1_28 | MXS_PAD_CTRL,
};

/* mxsfb (lcdif) */
static struct fb_videomode mx23evk_video_modes[] = {
	{
		.name		= "Samsung-LMS430HF02",
		.refresh	= 60,
		.xres		= 480,
		.yres		= 272,
		.pixclock	= 108096, /* picosecond (9.2 MHz) */
		.left_margin	= 15,
		.right_margin	= 8,
		.upper_margin	= 12,
		.lower_margin	= 4,
		.hsync_len	= 1,
		.vsync_len	= 1,
		.sync		= FB_SYNC_DATA_ENABLE_HIGH_ACT |
				  FB_SYNC_DOTCLK_FAILING_ACT,
	},
};

static const struct mxsfb_platform_data mx23evk_mxsfb_pdata __initconst = {
	.mode_list	= mx23evk_video_modes,
	.mode_count	= ARRAY_SIZE(mx23evk_video_modes),
	.default_bpp	= 32,
	.ld_intf_width	= STMLCDIF_24BIT,
};

static void __init mx23evk_init(void)
{
	int ret;

	mxs_iomux_setup_multiple_pads(mx23evk_pads, ARRAY_SIZE(mx23evk_pads));

	mx23_add_duart();
	mx23_add_auart0();

	ret = gpio_request_one(MX23EVK_LCD_ENABLE, GPIOF_DIR_OUT, "lcd-enable");
	if (ret)
		pr_warn("failed to request gpio lcd-enable: %d\n", ret);
	else
		gpio_set_value(MX23EVK_LCD_ENABLE, 1);

	ret = gpio_request_one(MX23EVK_BL_ENABLE, GPIOF_DIR_OUT, "bl-enable");
	if (ret)
		pr_warn("failed to request gpio bl-enable: %d\n", ret);
	else
		gpio_set_value(MX23EVK_BL_ENABLE, 1);

	mx23_add_mxsfb(&mx23evk_mxsfb_pdata);
}

static void __init mx23evk_timer_init(void)
{
	mx23_clocks_init();
}

static struct sys_timer mx23evk_timer = {
	.init	= mx23evk_timer_init,
};

MACHINE_START(MX23EVK, "Freescale MX23 EVK")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.map_io		= mx23_map_io,
	.init_irq	= mx23_init_irq,
	.init_machine	= mx23evk_init,
	.timer		= &mx23evk_timer,
MACHINE_END
