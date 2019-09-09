// SPDX-License-Identifier: GPL-2.0-or-later
/* ------------------------------------------------------------------------ *
 * i2c-parport.c I2C bus over parallel port                                 *
 * ------------------------------------------------------------------------ *
   Copyright (C) 2003-2011 Jean Delvare <jdelvare@suse.de>

   Based on older i2c-philips-par.c driver
   Copyright (C) 1995-2000 Simon G. Vogl
   With some changes from:
   Frodo Looijaard <frodol@dds.nl>
   Kyösti Mälkki <kmalkki@cc.hut.fi>

 * ------------------------------------------------------------------------ */

#define pr_fmt(fmt) "i2c-parport: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/parport.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c-smbus.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include "i2c-parport.h"

/* ----- Device list ------------------------------------------------------ */

struct i2c_par {
	struct pardevice *pdev;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo_data;
	struct i2c_smbus_alert_setup alert_data;
	struct i2c_client *ara;
	struct list_head node;
};

static LIST_HEAD(adapter_list);
static DEFINE_MUTEX(adapter_list_lock);
#define MAX_DEVICE 4
static int parport[MAX_DEVICE] = {0, -1, -1, -1};


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

static void (* const port_write[])(struct parport *, unsigned char) = {
	port_write_data,
	NULL,
	port_write_control,
};

static unsigned char (* const port_read[])(struct parport *) = {
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
static const struct i2c_algo_bit_data parport_algo_data = {
	.setsda		= parport_setsda,
	.setscl		= parport_setscl,
	.getsda		= parport_getsda,
	.getscl		= parport_getscl,
	.udelay		= 10, /* ~50 kbps */
	.timeout	= HZ,
};

/* ----- I2c and parallel port call-back functions and structures --------- */

static void i2c_parport_irq(void *data)
{
	struct i2c_par *adapter = data;
	struct i2c_client *ara = adapter->ara;

	if (ara) {
		dev_dbg(&ara->dev, "SMBus alert received\n");
		i2c_handle_smbus_alert(ara);
	} else
		dev_dbg(&adapter->adapter.dev,
			"SMBus alert received but no ARA client!\n");
}

static void i2c_parport_attach(struct parport *port)
{
	struct i2c_par *adapter;
	int i;
	struct pardev_cb i2c_parport_cb;

	for (i = 0; i < MAX_DEVICE; i++) {
		if (parport[i] == -1)
			continue;
		if (port->number == parport[i])
			break;
	}
	if (i == MAX_DEVICE) {
		pr_debug("Not using parport%d.\n", port->number);
		return;
	}

	adapter = kzalloc(sizeof(struct i2c_par), GFP_KERNEL);
	if (!adapter)
		return;
	memset(&i2c_parport_cb, 0, sizeof(i2c_parport_cb));
	i2c_parport_cb.flags = PARPORT_FLAG_EXCL;
	i2c_parport_cb.irq_func = i2c_parport_irq;
	i2c_parport_cb.private = adapter;

	pr_debug("attaching to %s\n", port->name);
	parport_disable_irq(port);
	adapter->pdev = parport_register_dev_model(port, "i2c-parport",
						   &i2c_parport_cb, i);
	if (!adapter->pdev) {
		pr_err("Unable to register with parport\n");
		goto err_free;
	}

	/* Fill the rest of the structure */
	adapter->adapter.owner = THIS_MODULE;
	adapter->adapter.class = I2C_CLASS_HWMON;
	strlcpy(adapter->adapter.name, "Parallel port adapter",
		sizeof(adapter->adapter.name));
	adapter->algo_data = parport_algo_data;
	/* Slow down if we can't sense SCL */
	if (!adapter_parm[type].getscl.val) {
		adapter->algo_data.getscl = NULL;
		adapter->algo_data.udelay = 50; /* ~10 kbps */
	}
	adapter->algo_data.data = port;
	adapter->adapter.algo_data = &adapter->algo_data;
	adapter->adapter.dev.parent = port->physport->dev;

	if (parport_claim_or_block(adapter->pdev) < 0) {
		dev_err(&adapter->pdev->dev,
			"Could not claim parallel port\n");
		goto err_unregister;
	}

	/* Reset hardware to a sane state (SCL and SDA high) */
	parport_setsda(port, 1);
	parport_setscl(port, 1);
	/* Other init if needed (power on...) */
	if (adapter_parm[type].init.val) {
		line_set(port, 1, &adapter_parm[type].init);
		/* Give powered devices some time to settle */
		msleep(100);
	}

	if (i2c_bit_add_bus(&adapter->adapter) < 0) {
		dev_err(&adapter->pdev->dev, "Unable to register with I2C\n");
		goto err_unregister;
	}

	/* Setup SMBus alert if supported */
	if (adapter_parm[type].smbus_alert) {
		adapter->ara = i2c_setup_smbus_alert(&adapter->adapter,
						     &adapter->alert_data);
		if (adapter->ara)
			parport_enable_irq(port);
		else
			dev_warn(&adapter->pdev->dev,
				 "Failed to register ARA client\n");
	}

	/* Add the new adapter to the list */
	mutex_lock(&adapter_list_lock);
	list_add_tail(&adapter->node, &adapter_list);
	mutex_unlock(&adapter_list_lock);
	return;

 err_unregister:
	parport_release(adapter->pdev);
	parport_unregister_device(adapter->pdev);
 err_free:
	kfree(adapter);
}

static void i2c_parport_detach(struct parport *port)
{
	struct i2c_par *adapter, *_n;

	/* Walk the list */
	mutex_lock(&adapter_list_lock);
	list_for_each_entry_safe(adapter, _n, &adapter_list, node) {
		if (adapter->pdev->port == port) {
			if (adapter->ara) {
				parport_disable_irq(port);
				i2c_unregister_device(adapter->ara);
			}
			i2c_del_adapter(&adapter->adapter);

			/* Un-init if needed (power off...) */
			if (adapter_parm[type].init.val)
				line_set(port, 0, &adapter_parm[type].init);

			parport_release(adapter->pdev);
			parport_unregister_device(adapter->pdev);
			list_del(&adapter->node);
			kfree(adapter);
		}
	}
	mutex_unlock(&adapter_list_lock);
}

static struct parport_driver i2c_parport_driver = {
	.name = "i2c-parport",
	.match_port = i2c_parport_attach,
	.detach = i2c_parport_detach,
	.devmodel = true,
};

/* ----- Module loading, unloading and information ------------------------ */

static int __init i2c_parport_init(void)
{
	if (type < 0) {
		pr_warn("adapter type unspecified\n");
		return -ENODEV;
	}

	if (type >= ARRAY_SIZE(adapter_parm)) {
		pr_warn("invalid type (%d)\n", type);
		return -ENODEV;
	}

	return parport_register_driver(&i2c_parport_driver);
}

static void __exit i2c_parport_exit(void)
{
	parport_unregister_driver(&i2c_parport_driver);
}

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("I2C bus over parallel port");
MODULE_LICENSE("GPL");

module_param_array(parport, int, NULL, 0);
MODULE_PARM_DESC(parport,
		 "List of parallel ports to bind to, by index.\n"
		 " Atmost " __stringify(MAX_DEVICE) " devices are supported.\n"
		 " Default is one device connected to parport0.\n"
);

module_init(i2c_parport_init);
module_exit(i2c_parport_exit);
