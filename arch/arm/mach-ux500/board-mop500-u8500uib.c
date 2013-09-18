/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Board data for the U8500 UIB, also known as the New UIB
 * License terms: GNU General Public License (GPL), version 2
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>

#include "irqs.h"

#include "board-mop500.h"

static struct i2c_board_info __initdata mop500_i2c3_devices_u8500[] = {
	{
		I2C_BOARD_INFO("synaptics_rmi4_i2c", 0x4B),
		.irq = NOMADIK_GPIO_TO_IRQ(84),
	},
};

void __init mop500_u8500uib_init(void)
{
	mop500_uib_i2c_add(3, mop500_i2c3_devices_u8500,
			ARRAY_SIZE(mop500_i2c3_devices_u8500));
}
