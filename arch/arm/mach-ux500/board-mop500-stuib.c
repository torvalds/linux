/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL), version 2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input/bu21013.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>

#include "board-mop500.h"

/*
 * BU21013 ROHM touchscreen interface on the STUIBs
 */

#define TOUCH_GPIO_PIN  84

#define TOUCH_XMAX	384
#define TOUCH_YMAX	704

#define PRCMU_CLOCK_OCR		0x1CC
#define TSC_EXT_CLOCK_9_6MHZ	0x840000

static struct bu21013_platform_device tsc_plat_device = {
	.touch_pin = TOUCH_GPIO_PIN,
	.touch_x_max = TOUCH_XMAX,
	.touch_y_max = TOUCH_YMAX,
	.ext_clk = false,
	.x_flip = false,
	.y_flip = true,
};

static struct i2c_board_info __initdata u8500_i2c3_devices_stuib[] = {
	{
		I2C_BOARD_INFO("bu21013_tp", 0x5C),
		.platform_data = &tsc_plat_device,
	},
	{
		I2C_BOARD_INFO("bu21013_tp", 0x5D),
		.platform_data = &tsc_plat_device,
	},
};

void __init mop500_stuib_init(void)
{
	if (machine_is_hrefv60())
		tsc_plat_device.cs_pin = HREFV60_TOUCH_RST_GPIO;
	else
		tsc_plat_device.cs_pin = GPIO_BU21013_CS;

	mop500_uib_i2c_add(3, u8500_i2c3_devices_stuib,
			ARRAY_SIZE(u8500_i2c3_devices_stuib));
}
