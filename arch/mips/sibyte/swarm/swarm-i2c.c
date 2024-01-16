// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Broadcom BCM91250A (SWARM), etc. I2C platform setup.
 *
 *	Copyright (c) 2008  Maciej W. Rozycki
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>


static struct i2c_board_info swarm_i2c_info1[] __initdata = {
	{
		I2C_BOARD_INFO("m41t81", 0x68),
	},
};

static int __init swarm_i2c_init(void)
{
	int err;

	err = i2c_register_board_info(1, swarm_i2c_info1,
				      ARRAY_SIZE(swarm_i2c_info1));
	if (err < 0)
		printk(KERN_ERR
		       "swarm-i2c: cannot register board I2C devices\n");
	return err;
}

arch_initcall(swarm_i2c_init);
