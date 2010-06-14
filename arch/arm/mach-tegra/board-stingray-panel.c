/*
 * arch/arm/mach-tegra/board-stingray-panel.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/leds-auo-panel-backlight.h>
#include <linux/resource.h>
#include <linux/leds-lp8550.h>
#include <linux/platform_device.h>
#include <asm/mach-types.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/tegra_fb.h>

#include "board-stingray.h"
#include "gpio-names.h"

#define STINGRAY_AUO_DISP_BL	TEGRA_GPIO_PD0

/* Framebuffer */
static struct resource fb_resource[] = {
	[0] = {
		.start  = INT_DISPLAY_GENERAL,
		.end    = INT_DISPLAY_GENERAL,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= 0x1c03a000,
		.end	= 0x1c03a000 + 0x500000 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_fb_lcd_data tegra_fb_lcd_platform_data = {
	.lcd_xres	= 1280,
	.lcd_yres	= 720,
	.fb_xres	= 1280,
	.fb_yres	= 720,
	.bits_per_pixel	= 16,
};

static struct platform_device tegra_fb_device = {
	.name = "tegrafb",
	.id	= 0,
	.resource = fb_resource,
	.num_resources = ARRAY_SIZE(fb_resource),
	.dev = {
		.platform_data = &tegra_fb_lcd_platform_data,
	},
};

static void stingray_backlight_enable(void)
{
	gpio_set_value(STINGRAY_AUO_DISP_BL, 1);
}

static void stingray_backlight_disable(void)
{
	gpio_set_value(STINGRAY_AUO_DISP_BL, 0);
}

struct auo_panel_bl_platform_data stingray_auo_backlight_data = {
	.bl_enable = stingray_backlight_enable,
	.bl_disable = stingray_backlight_disable,
	.pwm_enable = NULL,
	.pwm_disable = NULL,
};

static struct platform_device stingray_panel_bl_driver = {
	.name = LD_AUO_PANEL_BL_NAME,
	.id = -1,
	.dev = {
		.platform_data = &stingray_auo_backlight_data,
		},
};
struct lp8550_eeprom_data stingray_lp8550_eeprom_data[] = {
	/* Set the backlight current to 15mA each step is .12mA */
	{0x7f},
	/* Boost freq 625khz, PWM controled w/constant current,
	thermal deration disabled, no brightness slope */
	{0xa0},
	/* Adaptive mode for light loads, No advanced slope, 50% mode selected,
	Adaptive mode enabled, Boost is enabled, Boost Imax is 2.5A */
	{0x9f},
	/* UVLO is disabled, phase shift PWM enabled, PWM Freq 19232 */
	{0x3f},
	/* LED current resistor disabled, LED Fault = 3.3V */
	{0x08},
	/* Vsync is enabled, Dither disabled, Boost voltage 20V */
	{0x8a},
	/* PLL 13-bit counter */
	{0x64},
	/* 1-bit hysteresis w/11 bit resolution, PWM output freq is set with
	PWM_FREQ EEPROM bits */
	{0x29},
};

struct lp8550_platform_data stingray_lp8550_backlight_data = {
	.power_up_brightness = 0x80,
	.dev_ctrl_config = 0x05,
	.brightness_control = 0x80,
	.dev_id = 0xfc,
	.direct_ctrl = 0x01,
	.eeprom_table = stingray_lp8550_eeprom_data,
	.eeprom_tbl_sz = ARRAY_SIZE(stingray_lp8550_eeprom_data),
};

static struct i2c_board_info __initdata stingray_i2c_bus1_led_info[] = {
	 {
		I2C_BOARD_INFO(LD_LP8550_NAME, 0x2c),
		.platform_data = &stingray_lp8550_backlight_data,
	 },
};

int __init stingray_panel_init(void)
{
	if (stingray_revision() <= STINGRAY_REVISION_P1) {
		tegra_gpio_enable(STINGRAY_AUO_DISP_BL);
		gpio_request(STINGRAY_AUO_DISP_BL, "auo_disp_bl");
		gpio_direction_output(STINGRAY_AUO_DISP_BL, 1);
		platform_device_register(&stingray_panel_bl_driver);
	} else {
		i2c_register_board_info(0, stingray_i2c_bus1_led_info,
			ARRAY_SIZE(stingray_i2c_bus1_led_info));
	}

	return platform_device_register(&tegra_fb_device);
}

