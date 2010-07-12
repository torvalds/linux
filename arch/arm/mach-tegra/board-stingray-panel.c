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
#include <linux/nvhost.h>
#include <asm/mach-types.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/dc.h>
#include <mach/fb.h>

#include "board-stingray.h"
#include "gpio-names.h"

#define STINGRAY_AUO_DISP_BL	TEGRA_GPIO_PD0

/* Display Controller */
static struct resource stingray_disp1_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_GENERAL,
		.end	= INT_DISPLAY_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.start	= 0x1c038000,
		.end	= 0x1c038000 + 0x500000 - 1,
		.flags	= IORESOURCE_MEM,
	},

};

static struct tegra_dc_mode stingray_panel_modes_p0[] = {
	{
		.pclk = 62200000,
		.h_ref_to_sync = 11,
		.v_ref_to_sync = 1,
		.h_sync_width = 42,
		.v_sync_width = 6,
		.h_back_porch = 43,
		.v_back_porch = 5,
		.h_active = 1280,
		.v_active = 720,
		.h_front_porch = 43,
		.v_front_porch = 5,
	},
};

static struct tegra_dc_mode stingray_panel_modes[] = {
	{
		.pclk = 62200000,
		.h_ref_to_sync = 11,
		.v_ref_to_sync = 1,
		.h_sync_width = 42,
		.v_sync_width = 6,
		.h_back_porch = 43,
		.v_back_porch = 5,
		.h_active = 1280,
		.v_active = 800,
		.h_front_porch = 43,
		.v_front_porch = 5,
	},
};

static struct tegra_fb_data stingray_fb_data_p0 = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 720,
	.bits_per_pixel	= 32,
};

static struct tegra_fb_data stingray_fb_data = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 800,
	.bits_per_pixel	= 32,
};

static struct tegra_dc_out stingray_disp1_out = {
	.type = TEGRA_DC_OUT_RGB,

	.align = TEGRA_DC_ALIGN_MSB,
	.order = TEGRA_DC_ORDER_RED_BLUE,

	.modes = stingray_panel_modes,
	.n_modes = ARRAY_SIZE(stingray_panel_modes),
};

static struct tegra_dc_platform_data stingray_disp1_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &stingray_disp1_out,
	.fb		= &stingray_fb_data,
};

static struct nvhost_device stingray_disp1_device = {
	.name		= "tegradc",
	.id		= 0,
	.resource	= stingray_disp1_resources,
	.num_resources	= ARRAY_SIZE(stingray_disp1_resources),
	.dev = {
		.platform_data = &stingray_disp1_pdata,
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
	if (stingray_revision() < STINGRAY_REVISION_P1) {
		tegra_gpio_enable(STINGRAY_AUO_DISP_BL);
		gpio_request(STINGRAY_AUO_DISP_BL, "auo_disp_bl");
		gpio_direction_output(STINGRAY_AUO_DISP_BL, 1);
		platform_device_register(&stingray_panel_bl_driver);
		stingray_disp1_pdata.fb = &stingray_fb_data_p0;
		stingray_disp1_out.modes = stingray_panel_modes_p0;
	} else {
		i2c_register_board_info(0, stingray_i2c_bus1_led_info,
			ARRAY_SIZE(stingray_i2c_bus1_led_info));
	}

	return  nvhost_device_register(&stingray_disp1_device);
}

