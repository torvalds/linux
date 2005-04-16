/*
 * Copyright (C) 2004 Steven J. Hill
 * Copyright (C) 2001,2002,2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/i2c-algo-sibyte.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_smbus.h>

static struct i2c_algo_sibyte_data sibyte_board_data[2] = {
	{ NULL, 0, (void *) (KSEG1+A_SMB_BASE(0)) },
	{ NULL, 1, (void *) (KSEG1+A_SMB_BASE(1)) }
};

static struct i2c_adapter sibyte_board_adapter[2] = {
	{
		.owner		= THIS_MODULE,
		.id		= I2C_HW_SIBYTE,
		.class		= I2C_CLASS_HWMON,
		.algo		= NULL,
		.algo_data	= &sibyte_board_data[0],
		.name		= "SiByte SMBus 0",
	},
	{
		.owner		= THIS_MODULE,
		.id		= I2C_HW_SIBYTE,
		.class		= I2C_CLASS_HWMON,
		.algo		= NULL,
		.algo_data	= &sibyte_board_data[1],
		.name		= "SiByte SMBus 1",
	},
};

static int __init i2c_sibyte_init(void)
{
	printk("i2c-swarm.o: i2c SMBus adapter module for SiByte board\n");
	if (i2c_sibyte_add_bus(&sibyte_board_adapter[0], K_SMB_FREQ_100KHZ) < 0)
		return -ENODEV;
	if (i2c_sibyte_add_bus(&sibyte_board_adapter[1], K_SMB_FREQ_400KHZ) < 0)
		return -ENODEV;
	return 0;
}

static void __exit i2c_sibyte_exit(void)
{
	i2c_sibyte_del_bus(&sibyte_board_adapter[0]);
	i2c_sibyte_del_bus(&sibyte_board_adapter[1]);
}

module_init(i2c_sibyte_init);
module_exit(i2c_sibyte_exit);

MODULE_AUTHOR("Kip Walker <kwalker@broadcom.com>, Steven J. Hill <sjhill@realitydiluted.com>");
MODULE_DESCRIPTION("SMBus adapter routines for SiByte boards");
MODULE_LICENSE("GPL");
