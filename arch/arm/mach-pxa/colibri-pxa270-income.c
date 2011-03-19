/*
 * linux/arch/arm/mach-pxa/income.c
 *
 * Support for Income s.r.o. SH-Dmaster PXA270 SBC
 *
 * Copyright (C) 2010
 * Marek Vasut <marek.vasut@gmail.com>
 * Pavel Revak <palo@bielyvlk.sk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/pwm_backlight.h>
#include <linux/sysdev.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include <mach/hardware.h>
#include <mach/mmc.h>
#include <mach/ohci.h>
#include <mach/pxa27x.h>
#include <mach/pxa27x-udc.h>
#include <mach/pxafb.h>

#include <plat/i2c.h>

#include "devices.h"
#include "generic.h"

#define GPIO114_INCOME_ETH_IRQ  (114)
#define GPIO0_INCOME_SD_DETECT  (0)
#define GPIO0_INCOME_SD_RO      (1)
#define GPIO54_INCOME_LED_A     (54)
#define GPIO55_INCOME_LED_B     (55)
#define GPIO113_INCOME_TS_IRQ   (113)

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static struct pxamci_platform_data income_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.gpio_power		= -1,
	.gpio_card_detect	= GPIO0_INCOME_SD_DETECT,
	.gpio_card_ro		= GPIO0_INCOME_SD_RO,
	.detect_delay_ms	= 200,
};

static void __init income_mmc_init(void)
{
	pxa_set_mci_info(&income_mci_platform_data);
}
#else
static inline void income_mmc_init(void) {}
#endif

/******************************************************************************
 * USB Host
 ******************************************************************************/
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static struct pxaohci_platform_data income_ohci_info = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 | POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

static void __init income_uhc_init(void)
{
	pxa_set_ohci_info(&income_ohci_info);
}
#else
static inline void income_uhc_init(void) {}
#endif

/******************************************************************************
 * LED
 ******************************************************************************/
#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
struct gpio_led income_gpio_leds[] = {
	{
		.name			= "income:green:leda",
		.default_trigger	= "none",
		.gpio			= GPIO54_INCOME_LED_A,
		.active_low		= 1,
	},
	{
		.name			= "income:green:ledb",
		.default_trigger	= "none",
		.gpio			= GPIO55_INCOME_LED_B,
		.active_low		= 1,
	}
};

static struct gpio_led_platform_data income_gpio_led_info = {
	.leds		= income_gpio_leds,
	.num_leds	= ARRAY_SIZE(income_gpio_leds),
};

static struct platform_device income_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &income_gpio_led_info,
	}
};

static void __init income_led_init(void)
{
	platform_device_register(&income_leds);
}
#else
static inline void income_led_init(void) {}
#endif

/******************************************************************************
 * I2C
 ******************************************************************************/
#if defined(CONFIG_I2C_PXA) || defined(CONFIG_I2C_PXA_MODULE)
static struct i2c_board_info __initdata income_i2c_devs[] = {
	{
		I2C_BOARD_INFO("ds1340", 0x68),
	}, {
		I2C_BOARD_INFO("lm75", 0x4f),
	},
};

static void __init income_i2c_init(void)
{
	pxa_set_i2c_info(NULL);
	pxa27x_set_i2c_power_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(income_i2c_devs));
}
#else
static inline void income_i2c_init(void) {}
#endif

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct pxafb_mode_info income_lcd_modes[] = {
{
	.pixclock	= 144700,
	.xres		= 320,
	.yres		= 240,
	.bpp		= 32,
	.depth		= 18,

	.left_margin	= 10,
	.right_margin	= 10,
	.upper_margin	= 7,
	.lower_margin	= 8,

	.hsync_len	= 20,
	.vsync_len	= 2,

	.sync		= FB_SYNC_VERT_HIGH_ACT,
},
};

static struct pxafb_mach_info income_lcd_screen = {
	.modes		= income_lcd_modes,
	.num_modes	= ARRAY_SIZE(income_lcd_modes),
	.lcd_conn	= LCD_COLOR_TFT_18BPP | LCD_PCLK_EDGE_FALL,
};

static void __init income_lcd_init(void)
{
	set_pxa_fb_info(&income_lcd_screen);
}
#else
static inline void income_lcd_init(void) {}
#endif

/******************************************************************************
 * Backlight
 ******************************************************************************/
#if defined(CONFIG_BACKLIGHT_PWM) || defined(CONFIG_BACKLIGHT_PWM__MODULE)
static struct platform_pwm_backlight_data income_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= 0x3ff,
	.dft_brightness	= 0x1ff,
	.pwm_period_ns	= 1000000,
};

static struct platform_device income_backlight = {
	.name	= "pwm-backlight",
	.dev	= {
		.parent		= &pxa27x_device_pwm0.dev,
		.platform_data	= &income_backlight_data,
	},
};

static void __init income_pwm_init(void)
{
	platform_device_register(&income_backlight);
}
#else
static inline void income_pwm_init(void) {}
#endif

void __init colibri_pxa270_income_boardinit(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	income_mmc_init();
	income_uhc_init();
	income_led_init();
	income_i2c_init();
	income_lcd_init();
	income_pwm_init();
}

