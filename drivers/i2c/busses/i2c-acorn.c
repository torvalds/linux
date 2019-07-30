// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ARM IOC/IOMD i2c driver.
 *
 *  Copyright (C) 2000 Russell King
 *
 *  On Acorn machines, the following i2c devices are on the bus:
 *	- PCF8583 real time clock & static RAM
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/hardware/ioc.h>

#define FORCE_ONES	0xdc
#define SCL		0x02
#define SDA		0x01

/*
 * We must preserve all non-i2c output bits in IOC_CONTROL.
 * Note also that we need to preserve the value of SCL and
 * SDA outputs as well (which may be different from the
 * values read back from IOC_CONTROL).
 */
static u_int force_ones;

static void ioc_setscl(void *data, int state)
{
	u_int ioc_control = ioc_readb(IOC_CONTROL) & ~(SCL | SDA);
	u_int ones = force_ones;

	if (state)
		ones |= SCL;
	else
		ones &= ~SCL;

	force_ones = ones;

 	ioc_writeb(ioc_control | ones, IOC_CONTROL);
}

static void ioc_setsda(void *data, int state)
{
	u_int ioc_control = ioc_readb(IOC_CONTROL) & ~(SCL | SDA);
	u_int ones = force_ones;

	if (state)
		ones |= SDA;
	else
		ones &= ~SDA;

	force_ones = ones;

 	ioc_writeb(ioc_control | ones, IOC_CONTROL);
}

static int ioc_getscl(void *data)
{
	return (ioc_readb(IOC_CONTROL) & SCL) != 0;
}

static int ioc_getsda(void *data)
{
	return (ioc_readb(IOC_CONTROL) & SDA) != 0;
}

static struct i2c_algo_bit_data ioc_data = {
	.setsda		= ioc_setsda,
	.setscl		= ioc_setscl,
	.getsda		= ioc_getsda,
	.getscl		= ioc_getscl,
	.udelay		= 80,
	.timeout	= HZ,
};

static struct i2c_adapter ioc_ops = {
	.nr			= 0,
	.name			= "ioc",
	.algo_data		= &ioc_data,
};

static int __init i2c_ioc_init(void)
{
	force_ones = FORCE_ONES | SCL | SDA;

	return i2c_bit_add_numbered_bus(&ioc_ops);
}

module_init(i2c_ioc_init);

MODULE_AUTHOR("Russell King <linux@armlinux.org.uk>");
MODULE_DESCRIPTION("ARM IOC/IOMD i2c driver");
MODULE_LICENSE("GPL v2");
