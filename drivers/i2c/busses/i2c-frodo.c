
/*
 * linux/drivers/i2c/i2c-frodo.c
 *
 * Author: Abraham van der Merwe <abraham@2d3d.co.za>
 *
 * An I2C adapter driver for the 2d3D, Inc. StrongARM SA-1110
 * Development board (Frodo).
 *
 * This source code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <asm/hardware.h>


static void frodo_setsda (void *data,int state)
{
	if (state)
		FRODO_CPLD_I2C |= FRODO_I2C_SDA_OUT;
	else
		FRODO_CPLD_I2C &= ~FRODO_I2C_SDA_OUT;
}

static void frodo_setscl (void *data,int state)
{
	if (state)
		FRODO_CPLD_I2C |= FRODO_I2C_SCL_OUT;
	else
		FRODO_CPLD_I2C &= ~FRODO_I2C_SCL_OUT;
}

static int frodo_getsda (void *data)
{
	return ((FRODO_CPLD_I2C & FRODO_I2C_SDA_IN) != 0);
}

static int frodo_getscl (void *data)
{
	return ((FRODO_CPLD_I2C & FRODO_I2C_SCL_IN) != 0);
}

static struct i2c_algo_bit_data bit_frodo_data = {
	.setsda		= frodo_setsda,
	.setscl		= frodo_setscl,
	.getsda		= frodo_getsda,
	.getscl		= frodo_getscl,
	.udelay		= 80,
	.mdelay		= 80,
	.timeout	= HZ
};

static struct i2c_adapter frodo_ops = {
	.owner			= THIS_MODULE,
	.id			= I2C_HW_B_FRODO,
	.algo_data		= &bit_frodo_data,
	.dev			= {
		.name		= "Frodo adapter driver",
	},
};

static int __init i2c_frodo_init (void)
{
	return i2c_bit_add_bus(&frodo_ops);
}

static void __exit i2c_frodo_exit (void)
{
	i2c_bit_del_bus(&frodo_ops);
}

MODULE_AUTHOR ("Abraham van der Merwe <abraham@2d3d.co.za>");
MODULE_DESCRIPTION ("I2C-Bus adapter routines for Frodo");
MODULE_LICENSE ("GPL");

module_init (i2c_frodo_init);
module_exit (i2c_frodo_exit);

