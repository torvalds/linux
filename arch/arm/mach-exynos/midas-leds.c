/*
 *
 * Copyright (C) 2011 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/leds-lp5521.h>
#include "midas.h"

static struct lp5521_led_config lp5521_led_config[] = {
	{
		.name = "led_r",
		.chan_nr	= 0,
		.led_current	= 20,
		.max_current	= 20,
	},
	{
		.name = "led_g",
		.chan_nr	= 1,
		.led_current	= 20,
		.max_current	= 20,
	},
	{
		.name = "led_b",
		.chan_nr	= 2,
		.led_current	= 20,
		.max_current	= 20,
	},
};

/* mode 1 (charging) : RGB(255,0,0) always on */
static u8 mode_charging[] = { 0x40, 0xFF, };

/* mode 2 (charging error) : RGB(255,0,0) 500ms off, 500ms on */
static u8 mode_charging_err[] = {
		0x40, 0x00, 0x60, 0x00, 0x40, 0xFF,
		0x60, 0x00,
		};

/* mode 3 (missed noti) : RGB(0,0,255) 5000ms off, 500ms on */
static u8 mode_missed_noti[] = {
		0x40, 0x00, 0x60, 0x00, 0xA4, 0xA1,
		0x40, 0xFF, 0x60, 0x00,
		};

/* mode 4 (low batt) : RGB(255,0,0) 5000ms off, 500ms on */
static u8 mode_low_batt[] = {
		0x40, 0x00, 0x60, 0x00, 0xA4, 0xA1,
		0x40, 0xFF, 0x60, 0x00,
		};

/* mode 5 (full charged) : RGB(0,255,0) always on */
static u8 mode_full_chg[] = { 0x40, 0xFF, };

/* mode 6 (power on) : RGB(0,0,0) -> RGB(0,140,255)
		ramp up 672ms, wait 328ms, ramp down 672ms, wait 328ms */
static u8 mode_poweron_green[] = {
		0x40, 0x00, 0x0A, 0x45, 0x0A, 0x45,
		0x40, 0x8C, 0x55, 0x00, 0x0A, 0xC5,
		0xE0, 0x08, 0x0A, 0xC5, 0x40, 0x00,
		0x55, 0x00, 0xE0, 0x08,
		};

static u8 mode_poweron_blue[] = {
		0x40, 0x00, 0x05, 0x7E, 0x05, 0x7E,
		0x40, 0xFF, 0x55, 0x00, 0x05, 0xFE,
		0xE1, 0x00, 0x05, 0xFE, 0x40, 0x00,
		0x55, 0x00, 0xE1, 0x00,
		};

static struct lp5521_led_pattern board_led_patterns[] = {
	{
		.r = mode_charging,
		.size_r = ARRAY_SIZE(mode_charging),
	},
	{
		.r = mode_charging_err,
		.size_r = ARRAY_SIZE(mode_charging_err),
	},
	{
		.b = mode_missed_noti,
		.size_b = ARRAY_SIZE(mode_missed_noti),
	},
	{
		.r = mode_low_batt,
		.size_r = ARRAY_SIZE(mode_low_batt),
	},
	{
		.g = mode_full_chg,
		.size_g = ARRAY_SIZE(mode_full_chg),
	},
	{
		.g = mode_poweron_green,
		.b = mode_poweron_blue,
		.size_g = ARRAY_SIZE(mode_poweron_green),
		.size_b = ARRAY_SIZE(mode_poweron_blue),
	},
};

#define LP5521_CONFIGS	(LP5521_PWM_HF | LP5521_PWRSAVE_EN | \
			LP5521_CP_MODE_BYPASS | \
			LP5521_CLOCK_INT)

static struct lp5521_platform_data lp5521_pdata = {
	.led_config = lp5521_led_config,
	.num_channels = ARRAY_SIZE(lp5521_led_config),
	.clock_mode = LP5521_CLOCK_INT,
	.update_config = LP5521_CONFIGS,
	.patterns = board_led_patterns,
	.num_patterns = ARRAY_SIZE(board_led_patterns),
};

static struct i2c_board_info i2c_devs21_emul[] __initdata = {
	{
		I2C_BOARD_INFO("lp5521", 0x32),
		.platform_data = &lp5521_pdata,
	},
};

int __init plat_leds_init(void)
{
	i2c_add_devices(21, i2c_devs21_emul,
				ARRAY_SIZE(i2c_devs21_emul));
	return 0;
}

module_init(plat_leds_init);


