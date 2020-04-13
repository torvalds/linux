// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * i2c-smbus.c - SMBus extensions to the I2C protocol
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2010 Jean Delvare <jdelvare@suse.de>
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

struct i2c_smbus_alert {
	struct work_struct	alert;
	struct i2c_client	*ara;		/* Alert response address */
};

struct alert_data {
	unsigned short		addr;
	enum i2c_alert_protocol	type;
	unsigned int		data;
};

/* If this is the alerting device, notify its driver */
static int smbus_do_alert(struct device *dev, void *addrp)
{
	struct i2c_client *client = i2c_verify_client(dev);
	struct alert_data *data = addrp;
	struct i2c_driver *driver;

	if (!client || client->addr != data->addr)
		return 0;
	if (client->flags & I2C_CLIENT_TEN)
		return 0;

	/*
	 * Drivers should either disable alerts, or provide at least
	 * a minimal handler.  Lock so the driver won't change.
	 */
	device_lock(dev);
	if (client->dev.driver) {
		driver = to_i2c_driver(client->dev.driver);
		if (driver->alert)
			driver->alert(client, data->type, data->data);
		else
			dev_warn(&client->dev, "no driver alert()!\n");
	} else
		dev_dbg(&client->dev, "alert with no driver\n");
	device_unlock(dev);

	/* Stop iterating after we find the device */
	return -EBUSY;
}

/*
 * The alert IRQ handler needs to hand work off to a task which can issue
 * SMBus calls, because those sleeping calls can't be made in IRQ context.
 */
static irqreturn_t smbus_alert(int irq, void *d)
{
	struct i2c_smbus_alert *alert = d;
	struct i2c_client *ara;

	ara = alert->ara;

	for (;;) {
		s32 status;
		struct alert_data data;

		/*
		 * Devices with pending alerts reply in address order, low
		 * to high, because of slave transmit arbitration.  After
		 * responding, an SMBus device stops asserting SMBALERT#.
		 *
		 * Note that SMBus 2.0 reserves 10-bit addresses for future
		 * use.  We neither handle them, nor try to use PEC here.
		 */
		status = i2c_smbus_read_byte(ara);
		if (status < 0)
			break;

		data.data = status & 1;
		data.addr = status >> 1;
		data.type = I2C_PROTOCOL_SMBUS_ALERT;

		dev_dbg(&ara->dev, "SMBALERT# from dev 0x%02x, flag %d\n",
			data.addr, data.data);

		/* Notify driver for the device which issued the alert */
		device_for_each_child(&ara->adapter->dev, &data,
				      smbus_do_alert);
	}

	return IRQ_HANDLED;
}

static void smbalert_work(struct work_struct *work)
{
	struct i2c_smbus_alert *alert;

	alert = container_of(work, struct i2c_smbus_alert, alert);

	smbus_alert(0, alert);

}

/* Setup SMBALERT# infrastructure */
static int smbalert_probe(struct i2c_client *ara,
			  const struct i2c_device_id *id)
{
	struct i2c_smbus_alert_setup *setup = dev_get_platdata(&ara->dev);
	struct i2c_smbus_alert *alert;
	struct i2c_adapter *adapter = ara->adapter;
	int res, irq;

	alert = devm_kzalloc(&ara->dev, sizeof(struct i2c_smbus_alert),
			     GFP_KERNEL);
	if (!alert)
		return -ENOMEM;

	if (setup) {
		irq = setup->irq;
	} else {
		irq = of_irq_get_byname(adapter->dev.of_node, "smbus_alert");
		if (irq <= 0)
			return irq;
	}

	INIT_WORK(&alert->alert, smbalert_work);
	alert->ara = ara;

	if (irq > 0) {
		res = devm_request_threaded_irq(&ara->dev, irq,
						NULL, smbus_alert,
						IRQF_SHARED | IRQF_ONESHOT,
						"smbus_alert", alert);
		if (res)
			return res;
	}

	i2c_set_clientdata(ara, alert);
	dev_info(&adapter->dev, "supports SMBALERT#\n");

	return 0;
}

/* IRQ and memory resources are managed so they are freed automatically */
static int smbalert_remove(struct i2c_client *ara)
{
	struct i2c_smbus_alert *alert = i2c_get_clientdata(ara);

	cancel_work_sync(&alert->alert);
	return 0;
}

static const struct i2c_device_id smbalert_ids[] = {
	{ "smbus_alert", 0 },
	{ /* LIST END */ }
};
MODULE_DEVICE_TABLE(i2c, smbalert_ids);

static struct i2c_driver smbalert_driver = {
	.driver = {
		.name	= "smbus_alert",
	},
	.probe		= smbalert_probe,
	.remove		= smbalert_remove,
	.id_table	= smbalert_ids,
};

/**
 * i2c_handle_smbus_alert - Handle an SMBus alert
 * @ara: the ARA client on the relevant adapter
 * Context: can't sleep
 *
 * Helper function to be called from an I2C bus driver's interrupt
 * handler. It will schedule the alert work, in turn calling the
 * corresponding I2C device driver's alert function.
 *
 * It is assumed that ara is a valid i2c client previously returned by
 * i2c_new_smbus_alert_device().
 */
int i2c_handle_smbus_alert(struct i2c_client *ara)
{
	struct i2c_smbus_alert *alert = i2c_get_clientdata(ara);

	return schedule_work(&alert->alert);
}
EXPORT_SYMBOL_GPL(i2c_handle_smbus_alert);

module_i2c_driver(smbalert_driver);

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("SMBus protocol extensions support");
MODULE_LICENSE("GPL");
