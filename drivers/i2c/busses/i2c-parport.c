/* ------------------------------------------------------------------------ *
 * i2c-parport.c I2C bus over parallel port                                 *
 * ------------------------------------------------------------------------ *
   Copyright (C) 2003-2004 Jean Delvare <khali@linux-fr.org>
   
   Based on older i2c-philips-par.c driver
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
#include <linux/parport.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include "i2c-parport.h"

/* ----- Device list ------------------------------------------------------ */

struct i2c_par {
	struct pardevice *pdev;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo_data;
	struct i2c_par *next;
};

static struct i2c_par *adapter_list;

/* ----- Low-level parallel port access ----------------------------------- */

static void port_write_data(struct parport *p, unsigned char d)
{
	parport_write_data(p, d);
}

static void port_write_control(struct parport *p, unsigned char d)
{
	parport_write_control(p, d);
}

static unsigned char port_read_data(struct parport *p)
{
	return parport_read_data(p);
}

static unsigned char port_read_status(struct parport *p)
{
	return parport_read_status(p);
}

static unsigned char port_read_control(struct parport *p)
{
	return parport_read_control(p);
}

static void (*port_write[])(struct parport *, unsigned char) = {
	port_write_data,
	NULL,
	port_write_control,
};

static unsigned char (*port_read[])(struct parport *) = {
	port_read_data,
	port_read_status,
	port_read_control,
};

/* ----- Unified line operation functions --------------------------------- */

static inline void line_set(struct parport *data, int state,
	const struct lineop *op)
{
	u8 oldval = port_read[op->port](data);

	/* Touch only the bit(s) needed */
	if ((op->inverted && !state) || (!op->inverted && state))
		port_write[op->port](data, oldval | op->val);
	else
		port_write[op->port](data, oldval & ~op->val);
}

static inline int line_get(struct parport *data,
	const struct lineop *op)
{
	u8 oldval = port_read[op->port](data);

	return ((op->inverted && (oldval & op->val) != op->val)
	    || (!op->inverted && (oldval & op->val) == op->val));
}

/* ----- I2C algorithm call-back functions and structures ----------------- */

static void parport_setscl(void *data, int state)
{
	line_set((struct parport *) data, state, &adapter_parm[type].setscl);
}

static void parport_setsda(void *data, int state)
{
	line_set((struct parport *) data, state, &adapter_parm[type].setsda);
}

static int parport_getscl(void *data)
{
	return line_get((struct parport *) data, &adapter_parm[type].getscl);
}

static int parport_getsda(void *data)
{
	return line_get((struct parport *) data, &adapter_parm[type].getsda);
}

/* Encapsulate the functions above in the correct structure.
   Note that this is only a template, from which the real structures are
   copied. The attaching code will set getscl to NULL for adapters that
   cannot read SCL back, and will also make the data field point to
   the parallel port structure. */
static struct i2c_algo_bit_data parport_algo_data = {
	.setsda		= parport_setsda,
	.setscl		= parport_setscl,
	.getsda		= parport_getsda,
	.getscl		= parport_getscl,
	.udelay		= 60,
	.mdelay		= 60,
	.timeout	= HZ,
}; 

/* ----- I2c and parallel port call-back functions and structures --------- */

static struct i2c_adapter parport_adapter = {
	.owner		= THIS_MODULE,
	.class		= I2C_CLASS_HWMON,
	.id		= I2C_HW_B_LP,
	.name		= "Parallel port adapter",
};

static void i2c_parport_attach (struct parport *port)
{
	struct i2c_par *adapter;
	
	adapter = kmalloc(sizeof(struct i2c_par), GFP_KERNEL);
	if (adapter == NULL) {
		printk(KERN_ERR "i2c-parport: Failed to kmalloc\n");
		return;
	}
	memset(adapter, 0x00, sizeof(struct i2c_par));

	pr_debug("i2c-parport: attaching to %s\n", port->name);
	adapter->pdev = parport_register_device(port, "i2c-parport",
		NULL, NULL, NULL, PARPORT_FLAG_EXCL, NULL);
	if (!adapter->pdev) {
		printk(KERN_ERR "i2c-parport: Unable to register with parport\n");
		goto ERROR0;
	}

	/* Fill the rest of the structure */
	adapter->adapter = parport_adapter;
	adapter->algo_data = parport_algo_data;
	if (!adapter_parm[type].getscl.val)
		adapter->algo_data.getscl = NULL;
	adapter->algo_data.data = port;
	adapter->adapter.algo_data = &adapter->algo_data;

	if (parport_claim_or_block(adapter->pdev) < 0) {
		printk(KERN_ERR "i2c-parport: Could not claim parallel port\n");
		goto ERROR1;
	}

	/* Reset hardware to a sane state (SCL and SDA high) */
	parport_setsda(port, 1);
	parport_setscl(port, 1);
	/* Other init if needed (power on...) */
	if (adapter_parm[type].init.val)
		line_set(port, 1, &adapter_parm[type].init);

	parport_release(adapter->pdev);

	if (i2c_bit_add_bus(&adapter->adapter) < 0) {
		printk(KERN_ERR "i2c-parport: Unable to register with I2C\n");
		goto ERROR1;
	}

	/* Add the new adapter to the list */
	adapter->next = adapter_list;
	adapter_list = adapter;
        return;

ERROR1:
	parport_unregister_device(adapter->pdev);
ERROR0:
	kfree(adapter);
}

static void i2c_parport_detach (struct parport *port)
{
	struct i2c_par *adapter, *prev;

	/* Walk the list */
	for (prev = NULL, adapter = adapter_list; adapter;
	     prev = adapter, adapter = adapter->next) {
		if (adapter->pdev->port == port) {
			/* Un-init if needed (power off...) */
			if (adapter_parm[type].init.val)
				line_set(port, 0, &adapter_parm[type].init);
				
			i2c_bit_del_bus(&adapter->adapter);
			parport_unregister_device(adapter->pdev);
			if (prev)
				prev->next = adapter->next;
			else
				adapter_list = adapter->next;
			kfree(adapter);
			return;
		}
	}
}

static struct parport_driver i2c_driver = {
	.name	= "i2c-parport",
	.attach	= i2c_parport_attach,
	.detach	= i2c_parport_detach,
};

/* ----- Module loading, unloading and information ------------------------ */

static int __init i2c_parport_init(void)
{
	int type_count;

	type_count = sizeof(adapter_parm)/sizeof(struct adapter_parm);
	if (type < 0 || type >= type_count) {
		printk(KERN_WARNING "i2c-parport: invalid type (%d)\n", type);
		type = 0;
	}
	
	return parport_register_driver(&i2c_driver);
}

static void __exit i2c_parport_exit(void)
{
	parport_unregister_driver(&i2c_driver);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("I2C bus over parallel port");
MODULE_LICENSE("GPL");

module_init(i2c_parport_init);
module_exit(i2c_parport_exit);
