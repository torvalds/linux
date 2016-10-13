/*
 * i2c-smbus.c - SMBus extensions to the I2C protocol
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2010 Jean Delvare <jdelvare@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

struct i2c_smbus_alert {
	unsigned int		alert_edge_triggered:1;
	int			irq;
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
static void smbus_alert(struct work_struct *work)
{
	struct i2c_smbus_alert *alert;
	struct i2c_client *ara;
	unsigned short prev_addr = 0;	/* Not a valid address */

	alert = container_of(work, struct i2c_smbus_alert, alert);
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

		if (data.addr == prev_addr) {
			dev_warn(&ara->dev, "Duplicate SMBALERT# from dev "
				"0x%02x, skipping\n", data.addr);
			break;
		}
		dev_dbg(&ara->dev, "SMBALERT# from dev 0x%02x, flag %d\n",
			data.addr, data.data);

		/* Notify driver for the device which issued the alert */
		device_for_each_child(&ara->adapter->dev, &data,
				      smbus_do_alert);
		prev_addr = data.addr;
	}

	/* We handled all alerts; re-enable level-triggered IRQs */
	if (!alert->alert_edge_triggered)
		enable_irq(alert->irq);
}

static irqreturn_t smbalert_irq(int irq, void *d)
{
	struct i2c_smbus_alert *alert = d;

	/* Disable level-triggered IRQs until we handle them */
	if (!alert->alert_edge_triggered)
		disable_irq_nosync(irq);

	schedule_work(&alert->alert);
	return IRQ_HANDLED;
}

/* Setup SMBALERT# infrastructure */
static int smbalert_probe(struct i2c_client *ara,
			  const struct i2c_device_id *id)
{
	struct i2c_smbus_alert_setup *setup = dev_get_platdata(&ara->dev);
	struct i2c_smbus_alert *alert;
	struct i2c_adapter *adapter = ara->adapter;
	int res;

	alert = devm_kzalloc(&ara->dev, sizeof(struct i2c_smbus_alert),
			     GFP_KERNEL);
	if (!alert)
		return -ENOMEM;

	alert->alert_edge_triggered = setup->alert_edge_triggered;
	alert->irq = setup->irq;
	INIT_WORK(&alert->alert, smbus_alert);
	alert->ara = ara;

	if (setup->irq > 0) {
		res = devm_request_irq(&ara->dev, setup->irq, smbalert_irq,
				       0, "smbus_alert", alert);
		if (res)
			return res;
	}

	i2c_set_clientdata(ara, alert);
	dev_info(&adapter->dev, "supports SMBALERT#, %s trigger\n",
		 setup->alert_edge_triggered ? "edge" : "level");

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
 * i2c_setup_smbus_alert - Setup SMBus alert support
 * @adapter: the target adapter
 * @setup: setup data for the SMBus alert handler
 * Context: can sleep
 *
 * Setup handling of the SMBus alert protocol on a given I2C bus segment.
 *
 * Handling can be done either through our IRQ handler, or by the
 * adapter (from its handler, periodic polling, or whatever).
 *
 * NOTE that if we manage the IRQ, we *MUST* know if it's level or
 * edge triggered in order to hand it to the workqueue correctly.
 * If triggering the alert seems to wedge the system, you probably
 * should have said it's level triggered.
 *
 * This returns the ara client, which should be saved for later use with
 * i2c_handle_smbus_alert() and ultimately i2c_unregister_device(); or NULL
 * to indicate an error.
 */
struct i2c_client *i2c_setup_smbus_alert(struct i2c_adapter *adapter,
					 struct i2c_smbus_alert_setup *setup)
{
	struct i2c_board_info ara_board_info = {
		I2C_BOARD_INFO("smbus_alert", 0x0c),
		.platform_data = setup,
	};

	return i2c_new_device(adapter, &ara_board_info);
}
EXPORT_SYMBOL_GPL(i2c_setup_smbus_alert);

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
 * i2c_setup_smbus_alert().
 */
int i2c_handle_smbus_alert(struct i2c_client *ara)
{
	struct i2c_smbus_alert *alert = i2c_get_clientdata(ara);

	return schedule_work(&alert->alert);
}
EXPORT_SYMBOL_GPL(i2c_handle_smbus_alert);

static void smbus_host_notify_work(struct work_struct *work)
{
	struct alert_data alert;
	struct i2c_adapter *adapter;
	unsigned long flags;
	u16 payload;
	u8 addr;
	struct smbus_host_notify *data;

	data = container_of(work, struct smbus_host_notify, work);

	spin_lock_irqsave(&data->lock, flags);
	payload = data->payload;
	addr = data->addr;
	adapter = data->adapter;

	/* clear the pending bit and release the spinlock */
	data->pending = false;
	spin_unlock_irqrestore(&data->lock, flags);

	if (!adapter || !addr)
		return;

	alert.type = I2C_PROTOCOL_SMBUS_HOST_NOTIFY;
	alert.addr = addr;
	alert.data = payload;

	device_for_each_child(&adapter->dev, &alert, smbus_do_alert);
}

/**
 * i2c_setup_smbus_host_notify - Allocate a new smbus_host_notify for the given
 * I2C adapter.
 * @adapter: the adapter we want to associate a Host Notify function
 *
 * Returns a struct smbus_host_notify pointer on success, and NULL on failure.
 * The resulting smbus_host_notify must not be freed afterwards, it is a
 * managed resource already.
 */
struct smbus_host_notify *i2c_setup_smbus_host_notify(struct i2c_adapter *adap)
{
	struct smbus_host_notify *host_notify;

	host_notify = devm_kzalloc(&adap->dev, sizeof(struct smbus_host_notify),
				   GFP_KERNEL);
	if (!host_notify)
		return NULL;

	host_notify->adapter = adap;

	spin_lock_init(&host_notify->lock);
	INIT_WORK(&host_notify->work, smbus_host_notify_work);

	return host_notify;
}
EXPORT_SYMBOL_GPL(i2c_setup_smbus_host_notify);

/**
 * i2c_handle_smbus_host_notify - Forward a Host Notify event to the correct
 * I2C client.
 * @host_notify: the struct host_notify attached to the relevant adapter
 * @addr: the I2C address of the notifying device
 * @data: the payload of the notification
 * Context: can't sleep
 *
 * Helper function to be called from an I2C bus driver's interrupt
 * handler. It will schedule the Host Notify work, in turn calling the
 * corresponding I2C device driver's alert function.
 *
 * host_notify should be a valid pointer previously returned by
 * i2c_setup_smbus_host_notify().
 */
int i2c_handle_smbus_host_notify(struct smbus_host_notify *host_notify,
				 unsigned short addr, unsigned int data)
{
	unsigned long flags;
	struct i2c_adapter *adapter;

	if (!host_notify || !host_notify->adapter)
		return -EINVAL;

	adapter = host_notify->adapter;

	spin_lock_irqsave(&host_notify->lock, flags);

	if (host_notify->pending) {
		spin_unlock_irqrestore(&host_notify->lock, flags);
		dev_warn(&adapter->dev, "Host Notify already scheduled.\n");
		return -EBUSY;
	}

	host_notify->payload = data;
	host_notify->addr = addr;

	/* Mark that there is a pending notification and release the lock */
	host_notify->pending = true;
	spin_unlock_irqrestore(&host_notify->lock, flags);

	return schedule_work(&host_notify->work);
}
EXPORT_SYMBOL_GPL(i2c_handle_smbus_host_notify);

module_i2c_driver(smbalert_driver);

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("SMBus protocol extensions support");
MODULE_LICENSE("GPL");
