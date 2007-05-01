/* ------------------------------------------------------------------------ *
 * i2c-parport-light.c I2C bus over parallel port                           *
 * ------------------------------------------------------------------------ *
   Copyright (C) 2003-2007 Jean Delvare <khali@linux-fr.org>
   
   Based on older i2c-velleman.c driver
   Copyright (C) 1995-2000 Simon G. Vogl
   With some changes from:
   Frodo Looijaard <frodol@dds.nl>
   Kyösti Mälkki <kmalkki@cc.hut.fi>
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ------------------------------------------------------------------------ */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <asm/io.h>
#include "i2c-parport.h"

#define DEFAULT_BASE 0x378
#define DRVNAME "i2c-parport-light"

static struct platform_device *pdev;

static u16 base;
module_param(base, ushort, 0);
MODULE_PARM_DESC(base, "Base I/O address");

/* ----- Low-level parallel port access ----------------------------------- */

static inline void port_write(unsigned char p, unsigned char d)
{
	outb(d, base+p);
}

static inline unsigned char port_read(unsigned char p)
{
	return inb(base+p);
}

/* ----- Unified line operation functions --------------------------------- */

static inline void line_set(int state, const struct lineop *op)
{
	u8 oldval = port_read(op->port);

	/* Touch only the bit(s) needed */
	if ((op->inverted && !state) || (!op->inverted && state))
		port_write(op->port, oldval | op->val);
	else
		port_write(op->port, oldval & ~op->val);
}

static inline int line_get(const struct lineop *op)
{
	u8 oldval = port_read(op->port);

	return ((op->inverted && (oldval & op->val) != op->val)
	    || (!op->inverted && (oldval & op->val) == op->val));
}

/* ----- I2C algorithm call-back functions and structures ----------------- */

static void parport_setscl(void *data, int state)
{
	line_set(state, &adapter_parm[type].setscl);
}

static void parport_setsda(void *data, int state)
{
	line_set(state, &adapter_parm[type].setsda);
}

static int parport_getscl(void *data)
{
	return line_get(&adapter_parm[type].getscl);
}

static int parport_getsda(void *data)
{
	return line_get(&adapter_parm[type].getsda);
}

/* Encapsulate the functions above in the correct structure
   Note that getscl will be set to NULL by the attaching code for adapters
   that cannot read SCL back */
static struct i2c_algo_bit_data parport_algo_data = {
	.setsda		= parport_setsda,
	.setscl		= parport_setscl,
	.getsda		= parport_getsda,
	.getscl		= parport_getscl,
	.udelay		= 50,
	.timeout	= HZ,
}; 

/* ----- Driver registration ---------------------------------------------- */

static struct i2c_adapter parport_adapter = {
	.owner		= THIS_MODULE,
	.class		= I2C_CLASS_HWMON,
	.id		= I2C_HW_B_LP,
	.algo_data	= &parport_algo_data,
	.name		= "Parallel port adapter (light)",
};

static int __devinit i2c_parport_probe(struct platform_device *pdev)
{
	int err;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!request_region(res->start, res->end - res->start + 1, DRVNAME))
		return -EBUSY;

	/* Reset hardware to a sane state (SCL and SDA high) */
	parport_setsda(NULL, 1);
	parport_setscl(NULL, 1);
	/* Other init if needed (power on...) */
	if (adapter_parm[type].init.val)
		line_set(1, &adapter_parm[type].init);

	parport_adapter.dev.parent = &pdev->dev;
	err = i2c_bit_add_bus(&parport_adapter);
	if (err) {
		dev_err(&pdev->dev, "Unable to register with I2C\n");
		goto exit_region;
	}
	return 0;

exit_region:
	release_region(res->start, res->end - res->start + 1);
	return err;
}

static int __devexit i2c_parport_remove(struct platform_device *pdev)
{
	struct resource *res;

	i2c_del_adapter(&parport_adapter);

	/* Un-init if needed (power off...) */
	if (adapter_parm[type].init.val)
		line_set(0, &adapter_parm[type].init);

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	release_region(res->start, res->end - res->start + 1);
	return 0;
}

static struct platform_driver i2c_parport_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= i2c_parport_probe,
	.remove		= __devexit_p(i2c_parport_remove),
};

static int __init i2c_parport_device_add(u16 address)
{
	struct resource res = {
		.start	= address,
		.end	= address + 2,
		.name	= DRVNAME,
		.flags	= IORESOURCE_IO,
	};
	int err;

	pdev = platform_device_alloc(DRVNAME, -1);
	if (!pdev) {
		err = -ENOMEM;
		printk(KERN_ERR DRVNAME ": Device allocation failed\n");
		goto exit;
	}

	err = platform_device_add_resources(pdev, &res, 1);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device resource addition failed "
		       "(%d)\n", err);
		goto exit_device_put;
	}

	err = platform_device_add(pdev);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device addition failed (%d)\n",
		       err);
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(pdev);
exit:
	return err;
}

static int __init i2c_parport_init(void)
{
	int err;

	if (type < 0) {
		printk(KERN_ERR DRVNAME ": adapter type unspecified\n");
		return -ENODEV;
	}

	if (type >= ARRAY_SIZE(adapter_parm)) {
		printk(KERN_ERR DRVNAME ": invalid type (%d)\n", type);
		return -ENODEV;
	}

	if (base == 0) {
		pr_info(DRVNAME ": using default base 0x%x\n", DEFAULT_BASE);
		base = DEFAULT_BASE;
	}

        if (!adapter_parm[type].getscl.val)
		parport_algo_data.getscl = NULL;

	/* Sets global pdev as a side effect */
	err = i2c_parport_device_add(base);
	if (err)
		goto exit;

	err = platform_driver_register(&i2c_parport_driver);
	if (err)
		goto exit_device;

	return 0;

exit_device:
	platform_device_unregister(pdev);
exit:
	return err;
}

static void __exit i2c_parport_exit(void)
{
	platform_driver_unregister(&i2c_parport_driver);
	platform_device_unregister(pdev);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("I2C bus over parallel port (light)");
MODULE_LICENSE("GPL");

module_init(i2c_parport_init);
module_exit(i2c_parport_exit);
