/*
 * i2c-smbus.c - SMBus extensions to the I2C protocol
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2010 Jean Delvare <khali@linux-fr.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/slab.h>

struct i2c_smbus_alert {
	unsigned int		alert_edge_triggered:1;
	int			irq;
	struct work_struct	alert;
	struct i2c_client	*ara;		/* Alert response address */
};

struct alert_data {
	unsigned short		addr;
	u8			flag:1;
};

/* If this is the alerting device, notify its driver */
static int smbus_do_alert(struct device *dev, void *addrp)
{
	struct i2c_client *client = i2c_verify_client(dev);
	struct alert_data *data = addrp;

	if (!client || client->addr != data->addr)
		return 0;
	if (client->flags & I2C_CLIENT_TEN)
		return 0;

	/*
	 * Drivers should either disable alerts, or provide at least
	 * a minimal handler.  Lock so client->driver won't change.
	 */
	device_lock(dev);
	if (client->driver) {
		if (client->driver->alert)
			client->driver->alert(client, data->flag);
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
		 * Note that SMBus 2.0 reserves 10-bit addresess for future
		 * use.  We neither handle them, nor try to use PEC here.
		 */
		status = i2c_smbus_read_byte(ara);
		if (status < 0)
			break;

		data.flag = status & 1;
		data.addr = status >> 1;

		if (data.addr == prev_addr) {
			dev_warn(&ara->dev, "Duplicate SMBALERT# from dev "
				"0x%02x, skipping\n", data.addr);
			break;
		}
		dev_dbg(&ara->dev, "SMBALERT# from dev 0x%02x, flag %d\n",
			data.addr, data.flag);

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

module_i2c_driver(smbalert_driver);

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("SMBus protocol extensions support");
MODULE_LICENSE("GPL");
