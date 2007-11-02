/*
 * linux/arch/arm/mach-pxa/zylonite_pxa320.c
 *
 * PXA320 specific support code for the
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

#include <asm/arch/gpio.h>
#include <asm/arch/mfp-pxa320.h>
#include <asm/arch/zylonite.h>

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

static mfp_cfg_t mfp_cfg[] __initdata = {
	/* LCD */
	GPIO6_2_LCD_LDD_0,
	GPIO7_2_LCD_LDD_1,
	GPIO8_2_LCD_LDD_2,
	GPIO9_2_LCD_LDD_3,
	GPIO10_2_LCD_LDD_4,
	GPIO11_2_LCD_LDD_5,
	GPIO12_2_LCD_LDD_6,
	GPIO13_2_LCD_LDD_7,
	GPIO63_LCD_LDD_8,
	GPIO64_LCD_LDD_9,
	GPIO65_LCD_LDD_10,
	GPIO66_LCD_LDD_11,
	GPIO67_LCD_LDD_12,
	GPIO68_LCD_LDD_13,
	GPIO69_LCD_LDD_14,
	GPIO70_LCD_LDD_15,
	GPIO71_LCD_LDD_16,
	GPIO72_LCD_LDD_17,
	GPIO73_LCD_CS_N,
	GPIO74_LCD_VSYNC,
	GPIO14_2_LCD_FCLK,
	GPIO15_2_LCD_LCLK,
	GPIO16_2_LCD_PCLK,
	GPIO17_2_LCD_BIAS,

	/* FFUART */
	GPIO41_UART1_RXD,
	GPIO42_UART1_TXD,
	GPIO43_UART1_CTS,
	GPIO44_UART1_DCD,
	GPIO45_UART1_DSR,
	GPIO46_UART1_RI,
	GPIO47_UART1_DTR,
	GPIO48_UART1_RTS,

	/* AC97 */
	GPIO34_AC97_SYSCLK,
	GPIO35_AC97_SDATA_IN_0,
	GPIO37_AC97_SDATA_OUT,
	GPIO38_AC97_SYNC,
	GPIO39_AC97_BITCLK,
	GPIO40_AC97_nACRESET,

	/* I2C */
	GPIO32_I2C_SCL,
	GPIO33_I2C_SDA,

	/* Keypad */
	GPIO105_KP_DKIN_0,
	GPIO106_KP_DKIN_1,
	GPIO113_KP_MKIN_0,
	GPIO114_KP_MKIN_1,
	GPIO115_KP_MKIN_2,
	GPIO116_KP_MKIN_3,
	GPIO117_KP_MKIN_4,
	GPIO118_KP_MKIN_5,
	GPIO119_KP_MKIN_6,
	GPIO120_KP_MKIN_7,
	GPIO121_KP_MKOUT_0,
	GPIO122_KP_MKOUT_1,
	GPIO123_KP_MKOUT_2,
	GPIO124_KP_MKOUT_3,
	GPIO125_KP_MKOUT_4,
	GPIO126_KP_MKOUT_5,
	GPIO127_KP_MKOUT_6,
	GPIO5_2_KP_MKOUT_7,

	/* Ethernet */
	GPIO4_nCS3,
	GPIO90_GPIO,
};

#define NUM_LCD_DETECT_PINS	7

static int lcd_detect_pins[] __initdata = {
	MFP_PIN_GPIO72,   /* LCD_LDD_17 - ORIENT */
	MFP_PIN_GPIO71,   /* LCD_LDD_16 - LCDID[5] */
	MFP_PIN_GPIO17_2, /* LCD_BIAS   - LCDID[4] */
	MFP_PIN_GPIO15_2, /* LCD_LCLK   - LCDID[3] */
	MFP_PIN_GPIO14_2, /* LCD_FCLK   - LCDID[2] */
	MFP_PIN_GPIO73,   /* LCD_CS_N   - LCDID[1] */
	MFP_PIN_GPIO74,   /* LCD_VSYNC  - LCDID[0] */
	/*
	 * set the MFP_PIN_GPIO 14/15/17 to alternate function other than
	 * GPIO to avoid input level confliction with 14_2, 15_2, 17_2
	 */
	MFP_PIN_GPIO14,
	MFP_PIN_GPIO15,
	MFP_PIN_GPIO17,
};

static int lcd_detect_mfpr[] __initdata = {
	/* AF0, DS 1X, Pull Neither, Edge Clear */
	0x8440, 0x8440, 0x8440, 0x8440, 0x8440, 0x8440, 0x8440,
	0xc442, /* Backlight, Pull-Up, AF2 */
	0x8445, /* AF5 */
	0x8445, /* AF5 */
};

static void __init zylonite_detect_lcd_panel(void)
{
	unsigned long mfpr_save[ARRAY_SIZE(lcd_detect_pins)];
	int i, gpio, id = 0;

	/* save the original MFP settings of these pins and configure them
	 * as GPIO Input, DS01X, Pull Neither, Edge Clear
	 */
	for (i = 0; i < ARRAY_SIZE(lcd_detect_pins); i++) {
		mfpr_save[i] = pxa3xx_mfp_read(lcd_detect_pins[i]);
		pxa3xx_mfp_write(lcd_detect_pins[i], lcd_detect_mfpr[i]);
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
	for (i = 0; i < ARRAY_SIZE(lcd_detect_pins); i++)
		pxa3xx_mfp_write(lcd_detect_pins[i], mfpr_save[i]);
}

void __init zylonite_pxa320_init(void)
{
	if (cpu_is_pxa320()) {
		/* initialize MFP */
		pxa3xx_mfp_config(ARRAY_AND_SIZE(mfp_cfg));

		/* detect LCD panel */
		zylonite_detect_lcd_panel();

		/* GPIO pin assignment */
		gpio_backlight	= mfp_to_gpio(MFP_PIN_GPIO14);
		gpio_eth_irq	= mfp_to_gpio(MFP_PIN_GPIO9);
	}
}
