/*
 * HP T5325 Board Setup
 *
 * Copyright (C) 2014
 *
 * Andrew Lunn <andrew@lunn.ch>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <sound/alc5623.h>
#include "board.h"

static struct platform_device hp_t5325_audio_device = {
	.name		= "t5325-audio",
	.id		= -1,
};

static struct alc5623_platform_data alc5621_data = {
	.add_ctrl = 0x3700,
	.jack_det_ctrl = 0x4810,
};

static struct i2c_board_info i2c_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("alc5621", 0x1a),
		.platform_data = &alc5621_data,
	},
};

void __init t5325_init(void)
{
	i2c_register_board_info(0, i2c_board_info, ARRAY_SIZE(i2c_board_info));
	platform_device_register(&hp_t5325_audio_device);
}
