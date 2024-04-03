// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * i2c-smbus.c - SMBus extensions to the I2C protocol
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2010-2019 Jean Delvare <jdelvare@suse.de>
 */

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
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
static int smbalert_probe(struct i2c_client *ara)
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
		irq = fwnode_irq_get_byname(dev_fwnode(adapter->dev.parent),
					    "smbus_alert");
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
static void smbalert_remove(struct i2c_client *ara)
{
	struct i2c_smbus_alert *alert = i2c_get_clientdata(ara);

	cancel_work_sync(&alert->alert);
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

#if IS_ENABLED(CONFIG_I2C_SLAVE)
#define SMBUS_HOST_NOTIFY_LEN	3
struct i2c_slave_host_notify_status {
	u8 index;
	u8 addr;
};

static int i2c_slave_host_notify_cb(struct i2c_client *client,
				    enum i2c_slave_event event, u8 *val)
{
	struct i2c_slave_host_notify_status *status = client->dev.platform_data;

	switch (event) {
	case I2C_SLAVE_WRITE_RECEIVED:
		/* We only retrieve the first byte received (addr)
		 * since there is currently no support to retrieve the data
		 * parameter from the client.
		 */
		if (status->index == 0)
			status->addr = *val;
		if (status->index < U8_MAX)
			status->index++;
		break;
	case I2C_SLAVE_STOP:
		if (status->index == SMBUS_HOST_NOTIFY_LEN)
			i2c_handle_smbus_host_notify(client->adapter,
						     status->addr);
		fallthrough;
	case I2C_SLAVE_WRITE_REQUESTED:
		status->index = 0;
		break;
	case I2C_SLAVE_READ_REQUESTED:
	case I2C_SLAVE_READ_PROCESSED:
		*val = 0xff;
		break;
	}

	return 0;
}

/**
 * i2c_new_slave_host_notify_device - get a client for SMBus host-notify support
 * @adapter: the target adapter
 * Context: can sleep
 *
 * Setup handling of the SMBus host-notify protocol on a given I2C bus segment.
 *
 * Handling is done by creating a device and its callback and handling data
 * received via the SMBus host-notify address (0x8)
 *
 * This returns the client, which should be ultimately freed using
 * i2c_free_slave_host_notify_device(); or an ERRPTR to indicate an error.
 */
struct i2c_client *i2c_new_slave_host_notify_device(struct i2c_adapter *adapter)
{
	struct i2c_board_info host_notify_board_info = {
		I2C_BOARD_INFO("smbus_host_notify", 0x08),
		.flags  = I2C_CLIENT_SLAVE,
	};
	struct i2c_slave_host_notify_status *status;
	struct i2c_client *client;
	int ret;

	status = kzalloc(sizeof(struct i2c_slave_host_notify_status),
			 GFP_KERNEL);
	if (!status)
		return ERR_PTR(-ENOMEM);

	host_notify_board_info.platform_data = status;

	client = i2c_new_client_device(adapter, &host_notify_board_info);
	if (IS_ERR(client)) {
		kfree(status);
		return client;
	}

	ret = i2c_slave_register(client, i2c_slave_host_notify_cb);
	if (ret) {
		i2c_unregister_device(client);
		kfree(status);
		return ERR_PTR(ret);
	}

	return client;
}
EXPORT_SYMBOL_GPL(i2c_new_slave_host_notify_device);

/**
 * i2c_free_slave_host_notify_device - free the client for SMBus host-notify
 * support
 * @client: the client to free
 * Context: can sleep
 *
 * Free the i2c_client allocated via i2c_new_slave_host_notify_device
 */
void i2c_free_slave_host_notify_device(struct i2c_client *client)
{
	if (IS_ERR_OR_NULL(client))
		return;

	i2c_slave_unregister(client);
	kfree(client->dev.platform_data);
	i2c_unregister_device(client);
}
EXPORT_SYMBOL_GPL(i2c_free_slave_host_notify_device);
#endif

/*
 * SPD is not part of SMBus but we include it here for convenience as the
 * target systems are the same.
 * Restrictions to automatic SPD instantiation:
 *  - Only works if all filled slots have the same memory type
 *  - Only works for DDR, DDR2, DDR3 and DDR4 for now
 *  - Only works on systems with 1 to 8 memory slots
 */
#if IS_ENABLED(CONFIG_DMI)
void i2c_register_spd(struct i2c_adapter *adap)
{
	int n, slot_count = 0, dimm_count = 0;
	u16 handle;
	u8 common_mem_type = 0x0, mem_type;
	u64 mem_size;
	const char *name;

	while ((handle = dmi_memdev_handle(slot_count)) != 0xffff) {
		slot_count++;

		/* Skip empty slots */
		mem_size = dmi_memdev_size(handle);
		if (!mem_size)
			continue;

		/* Skip undefined memory type */
		mem_type = dmi_memdev_type(handle);
		if (mem_type <= 0x02)		/* Invalid, Other, Unknown */
			continue;

		if (!common_mem_type) {
			/* First filled slot */
			common_mem_type = mem_type;
		} else {
			/* Check that all filled slots have the same type */
			if (mem_type != common_mem_type) {
				dev_warn(&adap->dev,
					 "Different memory types mixed, not instantiating SPD\n");
				return;
			}
		}
		dimm_count++;
	}

	/* No useful DMI data, bail out */
	if (!dimm_count)
		return;

	/*
	 * If we're a child adapter on a muxed segment, then limit slots to 8,
	 * as this is the max number of SPD EEPROMs that can be addressed per bus.
	 */
	if (i2c_parent_is_i2c_adapter(adap)) {
		slot_count = 8;
	} else {
		if (slot_count > 8) {
			dev_warn(&adap->dev,
				 "More than 8 memory slots on a single bus, contact i801 maintainer to add missing mux config\n");
			return;
		}
	}

	/*
	 * Memory types could be found at section 7.18.2 (Memory Device — Type), table 78
	 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.6.0.pdf
	 */
	switch (common_mem_type) {
	case 0x12:	/* DDR */
	case 0x13:	/* DDR2 */
	case 0x18:	/* DDR3 */
	case 0x1B:	/* LPDDR */
	case 0x1C:	/* LPDDR2 */
	case 0x1D:	/* LPDDR3 */
		name = "spd";
		break;
	case 0x1A:	/* DDR4 */
	case 0x1E:	/* LPDDR4 */
		name = "ee1004";
		break;
	default:
		dev_info(&adap->dev,
			 "Memory type 0x%02x not supported yet, not instantiating SPD\n",
			 common_mem_type);
		return;
	}

	/*
	 * We don't know in which slots the memory modules are. We could
	 * try to guess from the slot names, but that would be rather complex
	 * and unreliable, so better probe all possible addresses until we
	 * have found all memory modules.
	 */
	for (n = 0; n < slot_count && dimm_count; n++) {
		struct i2c_board_info info;
		unsigned short addr_list[2];

		memset(&info, 0, sizeof(struct i2c_board_info));
		strscpy(info.type, name, I2C_NAME_SIZE);
		addr_list[0] = 0x50 + n;
		addr_list[1] = I2C_CLIENT_END;

		if (!IS_ERR(i2c_new_scanned_device(adap, &info, addr_list, NULL))) {
			dev_info(&adap->dev,
				 "Successfully instantiated SPD at 0x%hx\n",
				 addr_list[0]);
			dimm_count--;
		}
	}
}
EXPORT_SYMBOL_GPL(i2c_register_spd);
#endif

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("SMBus protocol extensions support");
MODULE_LICENSE("GPL");
