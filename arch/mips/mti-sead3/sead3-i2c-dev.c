/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/i2c.h>

static struct i2c_board_info __initdata sead3_i2c_devices[] = {
	{
		I2C_BOARD_INFO("adt7476", 0x2c),
		.irq = 0,
	},
	{
		I2C_BOARD_INFO("m41t80", 0x68),
		.irq = 0,
	},
};

static int __init sead3_i2c_init(void)
{
	int err;

	err = i2c_register_board_info(0, sead3_i2c_devices,
					ARRAY_SIZE(sead3_i2c_devices));
	if (err < 0)
		pr_err("sead3-i2c-dev: cannot register board I2C devices\n");
	return err;
}

arch_initcall(sead3_i2c_init);
