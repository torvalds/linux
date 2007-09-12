/*
 * linux/arch/arm/mach-pxa/zylonite_pxa300.c
 *
 * PXA300/PXA310 specific support code for the
 * PXA3xx Development Platform (aka Zylonite)
 *
 * Copyright (C) 2007 Marvell Internation Ltd.
 * 2007-08-21: eric miao <eric.y.miao@gmail.com>
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

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

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
	GPIO112_UART2_RXD,
	GPIO113_UART2_TXD,
	GPIO114_UART2_CTS,

	/* STUART */
	GPIO109_UART3_TXD,
	GPIO110_UART3_RXD,

	/* AC97 */
	GPIO23_AC97_nACRESET,
	GPIO24_AC97_SYSCLK,
	GPIO29_AC97_BITCLK,
	GPIO25_AC97_SDATA_IN_0,
	GPIO27_AC97_SDATA_OUT,
	GPIO28_AC97_SYNC,

	/* Keypad */
	GPIO107_KP_DKIN_0,
	GPIO108_KP_DKIN_1,
	GPIO115_KP_MKIN_0,
	GPIO116_KP_MKIN_1,
	GPIO117_KP_MKIN_2,
	GPIO118_KP_MKIN_3,
	GPIO119_KP_MKIN_4,
	GPIO120_KP_MKIN_5,
	GPIO2_2_KP_MKIN_6,
	GPIO3_2_KP_MKIN_7,
	GPIO121_KP_MKOUT_0,
	GPIO122_KP_MKOUT_1,
	GPIO123_KP_MKOUT_2,
	GPIO124_KP_MKOUT_3,
	GPIO125_KP_MKOUT_4,
	GPIO4_2_KP_MKOUT_5,
	GPIO5_2_KP_MKOUT_6,
	GPIO6_2_KP_MKOUT_7,
};

static mfp_cfg_t pxa300_mfp_cfg[] __initdata = {
	/* FFUART */
	GPIO30_UART1_RXD,
	GPIO31_UART1_TXD,
	GPIO32_UART1_CTS,
	GPIO37_UART1_RTS,
	GPIO33_UART1_DCD,
	GPIO34_UART1_DSR,
	GPIO35_UART1_RI,
	GPIO36_UART1_DTR,

	/* Ethernet */
	GPIO2_nCS3,
	GPIO99_GPIO,
};

static mfp_cfg_t pxa310_mfp_cfg[] __initdata = {
	/* FFUART */
	GPIO99_UART1_RXD,
	GPIO100_UART1_TXD,
	GPIO101_UART1_CTS,
	GPIO106_UART1_RTS,

	/* Ethernet */
	GPIO2_nCS3,
	GPIO102_GPIO,
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
	}

	if (cpu_is_pxa300()) {
		pxa3xx_mfp_config(ARRAY_AND_SIZE(pxa300_mfp_cfg));
		gpio_eth_irq = mfp_to_gpio(MFP_PIN_GPIO99);
	}

	if (cpu_is_pxa310()) {
		pxa3xx_mfp_config(ARRAY_AND_SIZE(pxa310_mfp_cfg));
		gpio_eth_irq = mfp_to_gpio(MFP_PIN_GPIO102);
	}
}
