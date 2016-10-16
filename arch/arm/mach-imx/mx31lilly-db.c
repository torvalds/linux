/*
 *  LILLY-1131 development board support
 *
 *    Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 *  based on code for other MX31 boards,
 *
 *    Copyright 2005-2007 Freescale Semiconductor
 *    Copyright (c) 2009 Alberto Panizzo <maramaopercheseimorto@gmail.com>
 *    Copyright (C) 2009 Valentin Longchamp, EPFL Mobots group
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

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "board-mx31lilly.h"
#include "common.h"
#include "devices-imx31.h"
#include "hardware.h"
#include "iomux-mx3.h"

/*
 * This file contains board-specific initialization routines for the
 * LILLY-1131 development board. If you design an own baseboard for the
 * module, use this file as base for support code.
 */

static unsigned int lilly_db_board_pins[] __initdata = {
	MX31_PIN_SD1_DATA3__SD1_DATA3,
	MX31_PIN_SD1_DATA2__SD1_DATA2,
	MX31_PIN_SD1_DATA1__SD1_DATA1,
	MX31_PIN_SD1_DATA0__SD1_DATA0,
	MX31_PIN_SD1_CLK__SD1_CLK,
	MX31_PIN_SD1_CMD__SD1_CMD,
	MX31_PIN_LD0__LD0,
	MX31_PIN_LD1__LD1,
	MX31_PIN_LD2__LD2,
	MX31_PIN_LD3__LD3,
	MX31_PIN_LD4__LD4,
	MX31_PIN_LD5__LD5,
	MX31_PIN_LD6__LD6,
	MX31_PIN_LD7__LD7,
	MX31_PIN_LD8__LD8,
	MX31_PIN_LD9__LD9,
	MX31_PIN_LD10__LD10,
	MX31_PIN_LD11__LD11,
	MX31_PIN_LD12__LD12,
	MX31_PIN_LD13__LD13,
	MX31_PIN_LD14__LD14,
	MX31_PIN_LD15__LD15,
	MX31_PIN_LD16__LD16,
	MX31_PIN_LD17__LD17,
	MX31_PIN_VSYNC3__VSYNC3,
	MX31_PIN_HSYNC__HSYNC,
	MX31_PIN_FPSHIFT__FPSHIFT,
	MX31_PIN_DRDY0__DRDY0,
	MX31_PIN_CONTRAST__CONTRAST,
};

/* MMC support */

static int mxc_mmc1_get_ro(struct device *dev)
{
	return gpio_get_value(IOMUX_TO_GPIO(MX31_PIN_LCS0));
}

static int gpio_det, gpio_wp;

#define MMC_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_HYS_CMOS | \
			PAD_CTL_ODE_CMOS | PAD_CTL_100K_PU)

static int mxc_mmc1_init(struct device *dev,
			 irq_handler_t detect_irq, void *data)
{
	int ret;

	gpio_det = IOMUX_TO_GPIO(MX31_PIN_GPIO1_1);
	gpio_wp = IOMUX_TO_GPIO(MX31_PIN_LCS0);

	mxc_iomux_set_pad(MX31_PIN_SD1_DATA0, MMC_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SD1_DATA1, MMC_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SD1_DATA2, MMC_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SD1_DATA3, MMC_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SD1_CLK, MMC_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SD1_CMD, MMC_PAD_CFG);

	ret = gpio_request(gpio_det, "MMC detect");
	if (ret)
		return ret;

	ret = gpio_request(gpio_wp, "MMC w/p");
	if (ret)
		goto exit_free_det;

	gpio_direction_input(gpio_det);
	gpio_direction_input(gpio_wp);

	ret = request_irq(gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO1_1)),
			  detect_irq, IRQF_TRIGGER_FALLING,
			  "MMC detect", data);
	if (ret)
		goto exit_free_wp;

	return 0;

exit_free_wp:
	gpio_free(gpio_wp);

exit_free_det:
	gpio_free(gpio_det);

	return ret;
}

static void mxc_mmc1_exit(struct device *dev, void *data)
{
	gpio_free(gpio_det);
	gpio_free(gpio_wp);
	free_irq(gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO1_1)), data);
}

static const struct imxmmc_platform_data mmc_pdata __initconst = {
	.get_ro	= mxc_mmc1_get_ro,
	.init	= mxc_mmc1_init,
	.exit	= mxc_mmc1_exit,
};

/* Framebuffer support */
static const struct fb_videomode fb_modedb = {
	/* 640x480 TFT panel (IPS-056T) */
	.name		= "CRT-VGA",
	.refresh	= 64,
	.xres		= 640,
	.yres		= 480,
	.pixclock	= 30000,
	.left_margin	= 200,
	.right_margin	= 2,
	.upper_margin	= 2,
	.lower_margin	= 2,
	.hsync_len	= 3,
	.vsync_len	= 1,
	.sync		= FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_ACT_HIGH,
	.vmode		= FB_VMODE_NONINTERLACED,
	.flag		= 0,
};

static struct mx3fb_platform_data fb_pdata __initdata = {
	.name		= "CRT-VGA",
	.mode		= &fb_modedb,
	.num_modes	= 1,
};

#define LCD_VCC_EN_GPIO	 (7)

static void __init mx31lilly_init_fb(void)
{
	if (gpio_request(LCD_VCC_EN_GPIO, "LCD enable") != 0) {
		printk(KERN_WARNING "unable to request LCD_VCC_EN pin.\n");
		return;
	}

	imx31_add_ipu_core();
	imx31_add_mx3_sdc_fb(&fb_pdata);
	gpio_direction_output(LCD_VCC_EN_GPIO, 1);
}

void __init mx31lilly_db_init(void)
{
	mxc_iomux_setup_multiple_pins(lilly_db_board_pins,
					ARRAY_SIZE(lilly_db_board_pins),
					"development board pins");
	imx31_add_mxc_mmc(0, &mmc_pdata);
	mx31lilly_init_fb();
}
