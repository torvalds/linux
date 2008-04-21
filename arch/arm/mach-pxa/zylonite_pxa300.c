/*
 * linux/arch/arm/mach-pxa/zylonite_pxa300.c
 *
 * PXA300/PXA310 specific support code for the
 * PXA3xx Development Platform (aka Zylonite)
 *
 * Copyright (C) 2007 Marvell Internation Ltd.
 * 2007-08-21: eric miao <eric.miao@marvell.com>
 *             initial version
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/gpio.h>
#include <asm/arch/mfp-pxa300.h>
#include <asm/arch/zylonite.h>

#include "generic.h"

/* PXA300/PXA310 common configurations */
static mfp_cfg_t common_mfp_cfg[] __initdata = {
	/* LCD */
	GPIO54_LCD_LDD_0,
	GPIO55_LCD_LDD_1,
	GPIO56_LCD_LDD_2,
	GPIO57_LCD_LDD_3,
	GPIO58_LCD_LDD_4,
	GPIO59_LCD_LDD_5,
	GPIO60_LCD_LDD_6,
	GPIO61_LCD_LDD_7,
	GPIO62_LCD_LDD_8,
	GPIO63_LCD_LDD_9,
	GPIO64_LCD_LDD_10,
	GPIO65_LCD_LDD_11,
	GPIO66_LCD_LDD_12,
	GPIO67_LCD_LDD_13,
	GPIO68_LCD_LDD_14,
	GPIO69_LCD_LDD_15,
	GPIO70_LCD_LDD_16,
	GPIO71_LCD_LDD_17,
	GPIO72_LCD_FCLK,
	GPIO73_LCD_LCLK,
	GPIO74_LCD_PCLK,
	GPIO75_LCD_BIAS,
	GPIO76_LCD_VSYNC,
	GPIO127_LCD_CS_N,

	/* BTUART */
	GPIO111_UART2_RTS,
	GPIO112_UART2_RXD | MFP_LPM_EDGE_FALL,
	GPIO113_UART2_TXD,
	GPIO114_UART2_CTS | MFP_LPM_EDGE_BOTH,

	/* STUART */
	GPIO109_UART3_TXD,
	GPIO110_UART3_RXD | MFP_LPM_EDGE_FALL,

	/* AC97 */
	GPIO23_AC97_nACRESET,
	GPIO24_AC97_SYSCLK,
	GPIO29_AC97_BITCLK,
	GPIO25_AC97_SDATA_IN_0,
	GPIO27_AC97_SDATA_OUT,
	GPIO28_AC97_SYNC,

	/* WM9713 IRQ */
	GPIO26_GPIO,

	/* Keypad */
	GPIO107_KP_DKIN_0 | MFP_LPM_EDGE_BOTH,
	GPIO108_KP_DKIN_1 | MFP_LPM_EDGE_BOTH,
	GPIO115_KP_MKIN_0 | MFP_LPM_EDGE_BOTH,
	GPIO116_KP_MKIN_1 | MFP_LPM_EDGE_BOTH,
	GPIO117_KP_MKIN_2 | MFP_LPM_EDGE_BOTH,
	GPIO118_KP_MKIN_3 | MFP_LPM_EDGE_BOTH,
	GPIO119_KP_MKIN_4 | MFP_LPM_EDGE_BOTH,
	GPIO120_KP_MKIN_5 | MFP_LPM_EDGE_BOTH,
	GPIO2_2_KP_MKIN_6 | MFP_LPM_EDGE_BOTH,
	GPIO3_2_KP_MKIN_7 | MFP_LPM_EDGE_BOTH,
	GPIO121_KP_MKOUT_0,
	GPIO122_KP_MKOUT_1,
	GPIO123_KP_MKOUT_2,
	GPIO124_KP_MKOUT_3,
	GPIO125_KP_MKOUT_4,
	GPIO4_2_KP_MKOUT_5,
	GPIO5_2_KP_MKOUT_6,
	GPIO6_2_KP_MKOUT_7,

	/* MMC1 */
	GPIO3_MMC1_DAT0,
	GPIO4_MMC1_DAT1 | MFP_LPM_EDGE_BOTH,
	GPIO5_MMC1_DAT2,
	GPIO6_MMC1_DAT3,
	GPIO7_MMC1_CLK,
	GPIO8_MMC1_CMD,	/* CMD0 for slot 0 */
	GPIO15_GPIO,	/* CMD1 default as GPIO for slot 0 */

	/* MMC2 */
	GPIO9_MMC2_DAT0,
	GPIO10_MMC2_DAT1 | MFP_LPM_EDGE_BOTH,
	GPIO11_MMC2_DAT2,
	GPIO12_MMC2_DAT3,
	GPIO13_MMC2_CLK,
	GPIO14_MMC2_CMD,
};

static mfp_cfg_t pxa300_mfp_cfg[] __initdata = {
	/* FFUART */
	GPIO30_UART1_RXD | MFP_LPM_EDGE_FALL,
	GPIO31_UART1_TXD,
	GPIO32_UART1_CTS,
	GPIO37_UART1_RTS,
	GPIO33_UART1_DCD,
	GPIO34_UART1_DSR | MFP_LPM_EDGE_FALL,
	GPIO35_UART1_RI,
	GPIO36_UART1_DTR,

	/* Ethernet */
	GPIO2_nCS3,
	GPIO99_GPIO,
};

static mfp_cfg_t pxa310_mfp_cfg[] __initdata = {
	/* FFUART */
	GPIO99_UART1_RXD | MFP_LPM_EDGE_FALL,
	GPIO100_UART1_TXD,
	GPIO101_UART1_CTS,
	GPIO106_UART1_RTS,

	/* Ethernet */
	GPIO2_nCS3,
	GPIO102_GPIO,

	/* MMC3 */
	GPIO7_2_MMC3_DAT0,
	GPIO8_2_MMC3_DAT1 | MFP_LPM_EDGE_BOTH,
	GPIO9_2_MMC3_DAT2,
	GPIO10_2_MMC3_DAT3,
	GPIO103_MMC3_CLK,
	GPIO105_MMC3_CMD,
};

#define NUM_LCD_DETECT_PINS	7

static int lcd_detect_pins[] __initdata = {
	MFP_PIN_GPIO71,	/* LCD_LDD_17 - ORIENT */
	MFP_PIN_GPIO70, /* LCD_LDD_16 - LCDID[5] */
	MFP_PIN_GPIO75, /* LCD_BIAS   - LCDID[4] */
	MFP_PIN_GPIO73, /* LCD_LCLK   - LCDID[3] */
	MFP_PIN_GPIO72, /* LCD_FCLK   - LCDID[2] */
	MFP_PIN_GPIO127,/* LCD_CS_N   - LCDID[1] */
	MFP_PIN_GPIO76, /* LCD_VSYNC  - LCDID[0] */
};

static void __init zylonite_detect_lcd_panel(void)
{
	unsigned long mfpr_save[NUM_LCD_DETECT_PINS];
	int i, gpio, id = 0;

	/* save the original MFP settings of these pins and configure
	 * them as GPIO Input, DS01X, Pull Neither, Edge Clear
	 */
	for (i = 0; i < NUM_LCD_DETECT_PINS; i++) {
		mfpr_save[i] = pxa3xx_mfp_read(lcd_detect_pins[i]);
		pxa3xx_mfp_write(lcd_detect_pins[i], 0x8440);
	}

	for (i = 0; i < NUM_LCD_DETECT_PINS; i++) {
		id = id << 1;
		gpio = mfp_to_gpio(lcd_detect_pins[i]);
		gpio_direction_input(gpio);

		if (gpio_get_value(gpio))
			id = id | 0x1;
	}

	/* lcd id, flush out bit 1 */
	lcd_id = id & 0x3d;

	/* lcd orientation, portrait or landscape */
	lcd_orientation = (id >> 6) & 0x1;

	/* restore the original MFP settings */
	for (i = 0; i < NUM_LCD_DETECT_PINS; i++)
		pxa3xx_mfp_write(lcd_detect_pins[i], mfpr_save[i]);
}

void __init zylonite_pxa300_init(void)
{
	if (cpu_is_pxa300() || cpu_is_pxa310()) {
		/* initialize MFP */
		pxa3xx_mfp_config(ARRAY_AND_SIZE(common_mfp_cfg));

		/* detect LCD panel */
		zylonite_detect_lcd_panel();

		/* GPIO pin assignment */
		gpio_backlight = mfp_to_gpio(MFP_PIN_GPIO20);

		/* MMC card detect & write protect for controller 0 */
		zylonite_mmc_slot[0].gpio_cd  = EXT_GPIO(0);
		zylonite_mmc_slot[0].gpio_wp  = EXT_GPIO(2);

		/* WM9713 IRQ */
		wm9713_irq = mfp_to_gpio(MFP_PIN_GPIO26);
	}

	if (cpu_is_pxa300()) {
		pxa3xx_mfp_config(ARRAY_AND_SIZE(pxa300_mfp_cfg));
		gpio_eth_irq = mfp_to_gpio(MFP_PIN_GPIO99);
	}

	if (cpu_is_pxa310()) {
		pxa3xx_mfp_config(ARRAY_AND_SIZE(pxa310_mfp_cfg));
		gpio_eth_irq = mfp_to_gpio(MFP_PIN_GPIO102);

		/* MMC card detect & write protect for controller 2 */
		zylonite_mmc_slot[2].gpio_cd = EXT_GPIO(30);
		zylonite_mmc_slot[2].gpio_wp = EXT_GPIO(31);
	}
}
