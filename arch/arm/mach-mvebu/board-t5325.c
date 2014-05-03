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
#include <linux/init.h>
#include <linux/platform_device.h>
#include "board.h"

static struct platform_device hp_t5325_audio_device = {
	.name		= "t5325-audio",
	.id		= -1,
};

void __init t5325_init(void)
{
	platform_device_register(&hp_t5325_audio_device);
}
