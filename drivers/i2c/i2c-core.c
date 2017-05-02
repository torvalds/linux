/* i2c-core.c - a device driver for the iic-bus interface		     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-99 Simon G. Vogl

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.			     */
/* ------------------------------------------------------------------------- */

/* With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi>.
   All SMBus-related things are written by Frodo Looijaard <frodol@dds.nl>
   SMBus 2.0 support by Mark Studebaker <mdsxyz123@yahoo.com> and
   Jean Delvare <jdelvare@suse.de>
   Mux support by Rodolfo Giometti <giometti@enneenne.com> and
   Michael Lawnick <michael.lawnick.ext@nsn.com>
   OF support is copyright (c) 2008 Jochen Friedrich <jochen@scram.de>
   (based on a previous patch from Jon Smirl <jonsmirl@gmail.com>) and
   (c) 2013  Wolfram Sang <wsa@the-dreams.de>
   I2C ACPI code Copyright (C) 2014 Intel Corp
   Author: Lan Tianyu <tianyu.lan@intel.com>
   I2C slave support (c) 2014 by Wolfram Sang <wsa@sang-engineering.com>
 */

#define pr_fmt(fmt) "i2c-core: " fmt

#include <dt-bindings/i2c/i2c.h>
#include <linux/uaccess.h>
#include <linux/acpi.h>
#include <linux/clk/clk-conf.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/hardirq.h>
#include <linux/i2c.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <linux/property.h>
#include <linux/rwsem.h>
#include <linux/slab.h>

#include "i2c-core.h"

#define CREATE_TRACE_POINTS
#include <trace/events/i2c.h>

#define I2C_ADDR_OFFSET_TEN_BIT	0xa000
#define I2C_ADDR_OFFSET_SLAVE	0x1000

#define I2C_ADDR_7BITS_MAX	0x77
#define I2C_ADDR_7BITS_COUNT	(I2C_ADDR_7BITS_MAX + 1)

/* core_lock protects i2c_adapter_idr, and guarantees
   that device detection, deletion of detected devices, and attach_adapter
   calls are serialized */
static DEFINE_MUTEX(core_lock);
static DEFINE_IDR(i2c_adapter_idr);

static struct device_type i2c_client_type;
static int i2c_detect(struct i2c_adapter *adapter, struct i2c_driver *driver);

static struct static_key i2c_trace_msg = STATIC_KEY_INIT_FALSE;
static bool is_registered;

int i2c_transfer_trace_reg(void)
{
	static_key_slow_inc(&i2c_trace_msg);
	return 0;
}

void i2c_transfer_trace_unreg(void)
{
	static_key_slow_dec(&i2c_trace_msg);
}

#if defined(CONFIG_ACPI)
struct i2c_acpi_handler_data {
	struct acpi_connection_info info;
	struct i2c_adapter *adapter;
};

struct gsb_buffer {
	u8	status;
	u8	len;
	union {
		u16	wdata;
		u8	bdata;
		u8	data[0];
	};
} __packed;

struct i2c_acpi_lookup {
	struct i2c_board_info *info;
	acpi_handle adapter_handle;
	acpi_handle device_handle;
	acpi_handle search_handle;
	u32 speed;
	u32 min_speed;
};

static int i2c_acpi_fill_info(struct acpi_resource *ares, void *data)
{
	struct i2c_acpi_lookup *lookup = data;
	struct i2c_board_info *info = lookup->info;
	struct acpi_resource_i2c_serialbus *sb;
	acpi_status status;

	if (info->addr || ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
		return 1;

	sb = &ares->data.i2c_serial_bus;
	if (sb->type != ACPI_RESOURCE_SERIAL_TYPE_I2C)
		return 1;

	status = acpi_get_handle(lookup->device_handle,
				 sb->resource_source.string_ptr,
				 &lookup->adapter_handle);
	if (!ACPI_SUCCESS(status))
		return 1;

	info->addr = sb->slave_address;
	lookup->speed = sb->connection_speed;
	if (sb->access_mode == ACPI_I2C_10BIT_MODE)
		info->flags |= I2C_CLIENT_TEN;

	return 1;
}

static int i2c_acpi_do_lookup(struct acpi_device *adev,
			      struct i2c_acpi_lookup *lookup)
{
	struct i2c_board_info *info = lookup->info;
	struct list_head resource_list;
	int ret;

	if (acpi_bus_get_status(adev) || !adev->status.present ||
	    acpi_device_enumerated(adev))
		return -EINVAL;

	memset(info, 0, sizeof(*info));
	lookup->device_handle = acpi_device_handle(adev);

	/* Look up for I2cSerialBus resource */
	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list,
				     i2c_acpi_fill_info, lookup);
	acpi_dev_free_resource_list(&resource_list);

	if (ret < 0 || !info->addr)
		return -EINVAL;

	return 0;
}

static int i2c_acpi_get_info(struct acpi_device *adev,
			     struct i2c_board_info *info,
			     struct i2c_adapter *adapter,
			     acpi_handle *adapter_handle)
{
	struct list_head resource_list;
	struct resource_entry *entry;
	struct i2c_acpi_lookup lookup;
	int ret;

	memset(&lookup, 0, sizeof(lookup));
	lookup.info = info;

	ret = i2c_acpi_do_lookup(adev, &lookup);
	if (ret)
		return ret;

	if (adapter) {
		/* The adapter must match the one in I2cSerialBus() connector */
		if (ACPI_HANDLE(&adapter->dev) != lookup.adapter_handle)
			return -ENODEV;
	} else {
		struct acpi_device *adapter_adev;

		/* The adapter must be present */
		if (acpi_bus_get_device(lookup.adapter_handle, &adapter_adev))
			return -ENODEV;
		if (acpi_bus_get_status(adapter_adev) ||
		    !adapter_adev->status.present)
			return -ENODEV;
	}

	info->fwnode = acpi_fwnode_handle(adev);
	if (adapter_handle)
		*adapter_handle = lookup.adapter_handle;

	/* Then fill IRQ number if any */
	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list, NULL, NULL);
	if (ret < 0)
		return -EINVAL;

	resource_list_for_each_entry(entry, &resource_list) {
		if (resource_type(entry->res) == IORESOURCE_IRQ) {
			info->irq = entry->res->start;
			break;
		}
	}

	acpi_dev_free_resource_list(&resource_list);

	acpi_set_modalias(adev, dev_name(&adev->dev), info->type,
			  sizeof(info->type));

	return 0;
}

static void i2c_acpi_register_device(struct i2c_adapter *adapter,
				     struct acpi_device *adev,
				     struct i2c_board_info *info)
{
	adev->power.flags.ignore_parent = true;
	acpi_device_set_enumerated(adev);

	if (!i2c_new_device(adapter, info)) {
		adev->power.flags.ignore_parent = false;
		dev_err(&adapter->dev,
			"failed to add I2C device %s from ACPI\n",
			dev_name(&adev->dev));
	}
}

static acpi_status i2c_acpi_add_device(acpi_handle handle, u32 level,
				       void *data, void **return_value)
{
	struct i2c_adapter *adapter = data;
	struct acpi_device *adev;
	struct i2c_board_info info;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;

	if (i2c_acpi_get_info(adev, &info, adapter, NULL))
		return AE_OK;

	i2c_acpi_register_device(adapter, adev, &info);

	return AE_OK;
}

#define I2C_ACPI_MAX_SCAN_DEPTH 32

/**
 * i2c_acpi_register_devices - enumerate I2C slave devices behind adapter
 * @adap: pointer to adapter
 *
 * Enumerate all I2C slave devices behind this adapter by walking the ACPI
 * namespace. When a device is found it will be added to the Linux device
 * model and bound to the corresponding ACPI handle.
 */
static void i2c_acpi_register_devices(struct i2c_adapter *adap)
{
	acpi_status status;

	if (!has_acpi_companion(&adap->dev))
		return;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				     I2C_ACPI_MAX_SCAN_DEPTH,
				     i2c_acpi_add_device, NULL,
				     adap, NULL);
	if (ACPI_FAILURE(status))
		dev_warn(&adap->dev, "failed to enumerate I2C slaves\n");
}

static acpi_status i2c_acpi_lookup_speed(acpi_handle handle, u32 level,
					   void *data, void **return_value)
{
	struct i2c_acpi_lookup *lookup = data;
	struct acpi_device *adev;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;

	if (i2c_acpi_do_lookup(adev, lookup))
		return AE_OK;

	if (lookup->search_handle != lookup->adapter_handle)
		return AE_OK;

	if (lookup->speed <= lookup->min_speed)
		lookup->min_speed = lookup->speed;

	return AE_OK;
}

/**
 * i2c_acpi_find_bus_speed - find I2C bus speed from ACPI
 * @dev: The device owning the bus
 *
 * Find the I2C bus speed by walking the ACPI namespace for all I2C slaves
 * devices connected to this bus and use the speed of slowest device.
 *
 * Returns the speed in Hz or zero
 */
u32 i2c_acpi_find_bus_speed(struct device *dev)
{
	struct i2c_acpi_lookup lookup;
	struct i2c_board_info dummy;
	acpi_status status;

	if (!has_acpi_companion(dev))
		return 0;

	memset(&lookup, 0, sizeof(lookup));
	lookup.search_handle = ACPI_HANDLE(dev);
	lookup.min_speed = UINT_MAX;
	lookup.info = &dummy;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				     I2C_ACPI_MAX_SCAN_DEPTH,
				     i2c_acpi_lookup_speed, NULL,
				     &lookup, NULL);

	if (ACPI_FAILURE(status)) {
		dev_warn(dev, "unable to find I2C bus speed from ACPI\n");
		return 0;
	}

	return lookup.min_speed != UINT_MAX ? lookup.min_speed : 0;
}
EXPORT_SYMBOL_GPL(i2c_acpi_find_bus_speed);

static int i2c_acpi_match_adapter(struct device *dev, void *data)
{
	struct i2c_adapter *adapter = i2c_verify_adapter(dev);

	if (!adapter)
		return 0;

	return ACPI_HANDLE(dev) == (acpi_handle)data;
}

static int i2c_acpi_match_device(struct device *dev, void *data)
{
	return ACPI_COMPANION(dev) == data;
}

static struct i2c_adapter *i2c_acpi_find_adapter_by_handle(acpi_handle handle)
{
	struct device *dev;

	dev = bus_find_device(&i2c_bus_type, NULL, handle,
			      i2c_acpi_match_adapter);
	return dev ? i2c_verify_adapter(dev) : NULL;
}

static struct i2c_client *i2c_acpi_find_client_by_adev(struct acpi_device *adev)
{
	struct device *dev;

	dev = bus_find_device(&i2c_bus_type, NULL, adev, i2c_acpi_match_device);
	return dev ? i2c_verify_client(dev) : NULL;
}

static int i2c_acpi_notify(struct notifier_block *nb, unsigned long value,
			   void *arg)
{
	struct acpi_device *adev = arg;
	struct i2c_board_info info;
	acpi_handle adapter_handle;
	struct i2c_adapter *adapter;
	struct i2c_client *client;

	switch (value) {
	case ACPI_RECONFIG_DEVICE_ADD:
		if (i2c_acpi_get_info(adev, &info, NULL, &adapter_handle))
			break;

		adapter = i2c_acpi_find_adapter_by_handle(adapter_handle);
		if (!adapter)
			break;

		i2c_acpi_register_device(adapter, adev, &info);
		break;
	case ACPI_RECONFIG_DEVICE_REMOVE:
		if (!acpi_device_enumerated(adev))
			break;

		client = i2c_acpi_find_client_by_adev(adev);
		if (!client)
			break;

		i2c_unregister_device(client);
		put_device(&client->dev);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block i2c_acpi_notifier = {
	.notifier_call = i2c_acpi_notify,
};
#else /* CONFIG_ACPI */
static inline void i2c_acpi_register_devices(struct i2c_adapter *adap) { }
extern struct notifier_block i2c_acpi_notifier;
#endif /* CONFIG_ACPI */

#ifdef CONFIG_ACPI_I2C_OPREGION
static int acpi_gsb_i2c_read_bytes(struct i2c_client *client,
		u8 cmd, u8 *data, u8 data_len)
{

	struct i2c_msg msgs[2];
	int ret;
	u8 *buffer;

	buffer = kzalloc(data_len, GFP_KERNEL);
	if (!buffer)
		return AE_NO_MEMORY;

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 1;
	msgs[0].buf = &cmd;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = data_len;
	msgs[1].buf = buffer;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		dev_err(&client->adapter->dev, "i2c read failed\n");
	else
		memcpy(data, buffer, data_len);

	kfree(buffer);
	return ret;
}

static int acpi_gsb_i2c_write_bytes(struct i2c_client *client,
		u8 cmd, u8 *data, u8 data_len)
{

	struct i2c_msg msgs[1];
	u8 *buffer;
	int ret = AE_OK;

	buffer = kzalloc(data_len + 1, GFP_KERNEL);
	if (!buffer)
		return AE_NO_MEMORY;

	buffer[0] = cmd;
	memcpy(buffer + 1, data, data_len);

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = data_len + 1;
	msgs[0].buf = buffer;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		dev_err(&client->adapter->dev, "i2c write failed\n");

	kfree(buffer);
	return ret;
}

static acpi_status
i2c_acpi_space_handler(u32 function, acpi_physical_address command,
			u32 bits, u64 *value64,
			void *handler_context, void *region_context)
{
	struct gsb_buffer *gsb = (struct gsb_buffer *)value64;
	struct i2c_acpi_handler_data *data = handler_context;
	struct acpi_connection_info *info = &data->info;
	struct acpi_resource_i2c_serialbus *sb;
	struct i2c_adapter *adapter = data->adapter;
	struct i2c_client *client;
	struct acpi_resource *ares;
	u32 accessor_type = function >> 16;
	u8 action = function & ACPI_IO_MASK;
	acpi_status ret;
	int status;

	ret = acpi_buffer_to_resource(info->connection, info->length, &ares);
	if (ACPI_FAILURE(ret))
		return ret;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		ret = AE_NO_MEMORY;
		goto err;
	}

	if (!value64 || ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS) {
		ret = AE_BAD_PARAMETER;
		goto err;
	}

	sb = &ares->data.i2c_serial_bus;
	if (sb->type != ACPI_RESOURCE_SERIAL_TYPE_I2C) {
		ret = AE_BAD_PARAMETER;
		goto err;
	}

	client->adapter = adapter;
	client->addr = sb->slave_address;

	if (sb->access_mode == ACPI_I2C_10BIT_MODE)
		client->flags |= I2C_CLIENT_TEN;

	switch (accessor_type) {
	case ACPI_GSB_ACCESS_ATTRIB_SEND_RCV:
		if (action == ACPI_READ) {
			status = i2c_smbus_read_byte(client);
			if (status >= 0) {
				gsb->bdata = status;
				status = 0;
			}
		} else {
			status = i2c_smbus_write_byte(client, gsb->bdata);
		}
		break;

	case ACPI_GSB_ACCESS_ATTRIB_BYTE:
		if (action == ACPI_READ) {
			status = i2c_smbus_read_byte_data(client, command);
			if (status >= 0) {
				gsb->bdata = status;
				status = 0;
			}
		} else {
			status = i2c_smbus_write_byte_data(client, command,
					gsb->bdata);
		}
		break;

	case ACPI_GSB_ACCESS_ATTRIB_WORD:
		if (action == ACPI_READ) {
			status = i2c_smbus_read_word_data(client, command);
			if (status >= 0) {
				gsb->wdata = status;
				status = 0;
			}
		} else {
			status = i2c_smbus_write_word_data(client, command,
					gsb->wdata);
		}
		break;

	case ACPI_GSB_ACCESS_ATTRIB_BLOCK:
		if (action == ACPI_READ) {
			status = i2c_smbus_read_block_data(client, command,
					gsb->data);
			if (status >= 0) {
				gsb->len = status;
				status = 0;
			}
		} else {
			status = i2c_smbus_write_block_data(client, command,
					gsb->len, gsb->data);
		}
		break;

	case ACPI_GSB_ACCESS_ATTRIB_MULTIBYTE:
		if (action == ACPI_READ) {
			status = acpi_gsb_i2c_read_bytes(client, command,
					gsb->data, info->access_length);
			if (status > 0)
				status = 0;
		} else {
			status = acpi_gsb_i2c_write_bytes(client, command,
					gsb->data, info->access_length);
		}
		break;

	default:
		dev_warn(&adapter->dev, "protocol 0x%02x not supported for client 0x%02x\n",
			 accessor_type, client->addr);
		ret = AE_BAD_PARAMETER;
		goto err;
	}

	gsb->status = status;

 err:
	kfree(client);
	ACPI_FREE(ares);
	return ret;
}


static int i2c_acpi_install_space_handler(struct i2c_adapter *adapter)
{
	acpi_handle handle;
	struct i2c_acpi_handler_data *data;
	acpi_status status;

	if (!adapter->dev.parent)
		return -ENODEV;

	handle = ACPI_HANDLE(adapter->dev.parent);

	if (!handle)
		return -ENODEV;

	data = kzalloc(sizeof(struct i2c_acpi_handler_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->adapter = adapter;
	status = acpi_bus_attach_private_data(handle, (void *)data);
	if (ACPI_FAILURE(status)) {
		kfree(data);
		return -ENOMEM;
	}

	status = acpi_install_address_space_handler(handle,
				ACPI_ADR_SPACE_GSBUS,
				&i2c_acpi_space_handler,
				NULL,
				data);
	if (ACPI_FAILURE(status)) {
		dev_err(&adapter->dev, "Error installing i2c space handler\n");
		acpi_bus_detach_private_data(handle);
		kfree(data);
		return -ENOMEM;
	}

	acpi_walk_dep_device_list(handle);
	return 0;
}

static void i2c_acpi_remove_space_handler(struct i2c_adapter *adapter)
{
	acpi_handle handle;
	struct i2c_acpi_handler_data *data;
	acpi_status status;

	if (!adapter->dev.parent)
		return;

	handle = ACPI_HANDLE(adapter->dev.parent);

	if (!handle)
		return;

	acpi_remove_address_space_handler(handle,
				ACPI_ADR_SPACE_GSBUS,
				&i2c_acpi_space_handler);

	status = acpi_bus_get_private_data(handle, (void **)&data);
	if (ACPI_SUCCESS(status))
		kfree(data);

	acpi_bus_detach_private_data(handle);
}
#else /* CONFIG_ACPI_I2C_OPREGION */
static inline void i2c_acpi_remove_space_handler(struct i2c_adapter *adapter)
{ }

static inline int i2c_acpi_install_space_handler(struct i2c_adapter *adapter)
{ return 0; }
#endif /* CONFIG_ACPI_I2C_OPREGION */

/* ------------------------------------------------------------------------- */

const struct i2c_device_id *i2c_match_id(const struct i2c_device_id *id,
						const struct i2c_client *client)
{
	if (!(id && client))
		return NULL;

	while (id->name[0]) {
		if (strcmp(client->name, id->name) == 0)
			return id;
		id++;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(i2c_match_id);

static int i2c_device_match(struct device *dev, struct device_driver *drv)
{
	struct i2c_client	*client = i2c_verify_client(dev);
	struct i2c_driver	*driver;


	/* Attempt an OF style match */
	if (i2c_of_match_device(drv->of_match_table, client))
		return 1;

	/* Then ACPI style match */
	if (acpi_driver_match_device(dev, drv))
		return 1;

	driver = to_i2c_driver(drv);

	/* Finally an I2C match */
	if (i2c_match_id(driver->id_table, client))
		return 1;

	return 0;
}

static int i2c_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct i2c_client *client = to_i2c_client(dev);
	int rc;

	rc = acpi_device_uevent_modalias(dev, env);
	if (rc != -ENODEV)
		return rc;

	return add_uevent_var(env, "MODALIAS=%s%s", I2C_MODULE_PREFIX, client->name);
}

/* i2c bus recovery routines */
static int get_scl_gpio_value(struct i2c_adapter *adap)
{
	return gpio_get_value(adap->bus_recovery_info->scl_gpio);
}

static void set_scl_gpio_value(struct i2c_adapter *adap, int val)
{
	gpio_set_value(adap->bus_recovery_info->scl_gpio, val);
}

static int get_sda_gpio_value(struct i2c_adapter *adap)
{
	return gpio_get_value(adap->bus_recovery_info->sda_gpio);
}

static int i2c_get_gpios_for_recovery(struct i2c_adapter *adap)
{
	struct i2c_bus_recovery_info *bri = adap->bus_recovery_info;
	struct device *dev = &adap->dev;
	int ret = 0;

	ret = gpio_request_one(bri->scl_gpio, GPIOF_OPEN_DRAIN |
			GPIOF_OUT_INIT_HIGH, "i2c-scl");
	if (ret) {
		dev_warn(dev, "Can't get SCL gpio: %d\n", bri->scl_gpio);
		return ret;
	}

	if (bri->get_sda) {
		if (gpio_request_one(bri->sda_gpio, GPIOF_IN, "i2c-sda")) {
			/* work without SDA polling */
			dev_warn(dev, "Can't get SDA gpio: %d. Not using SDA polling\n",
					bri->sda_gpio);
			bri->get_sda = NULL;
		}
	}

	return ret;
}

static void i2c_put_gpios_for_recovery(struct i2c_adapter *adap)
{
	struct i2c_bus_recovery_info *bri = adap->bus_recovery_info;

	if (bri->get_sda)
		gpio_free(bri->sda_gpio);

	gpio_free(bri->scl_gpio);
}

/*
 * We are generating clock pulses. ndelay() determines durating of clk pulses.
 * We will generate clock with rate 100 KHz and so duration of both clock levels
 * is: delay in ns = (10^6 / 100) / 2
 */
#define RECOVERY_NDELAY		5000
#define RECOVERY_CLK_CNT	9

static int i2c_generic_recovery(struct i2c_adapter *adap)
{
	struct i2c_bus_recovery_info *bri = adap->bus_recovery_info;
	int i = 0, val = 1, ret = 0;

	if (bri->prepare_recovery)
		bri->prepare_recovery(adap);

	bri->set_scl(adap, val);
	ndelay(RECOVERY_NDELAY);

	/*
	 * By this time SCL is high, as we need to give 9 falling-rising edges
	 */
	while (i++ < RECOVERY_CLK_CNT * 2) {
		if (val) {
			/* Break if SDA is high */
			if (bri->get_sda && bri->get_sda(adap))
					break;
			/* SCL shouldn't be low here */
			if (!bri->get_scl(adap)) {
				dev_err(&adap->dev,
					"SCL is stuck low, exit recovery\n");
				ret = -EBUSY;
				break;
			}
		}

		val = !val;
		bri->set_scl(adap, val);
		ndelay(RECOVERY_NDELAY);
	}

	if (bri->unprepare_recovery)
		bri->unprepare_recovery(adap);

	return ret;
}

int i2c_generic_scl_recovery(struct i2c_adapter *adap)
{
	return i2c_generic_recovery(adap);
}
EXPORT_SYMBOL_GPL(i2c_generic_scl_recovery);

int i2c_generic_gpio_recovery(struct i2c_adapter *adap)
{
	int ret;

	ret = i2c_get_gpios_for_recovery(adap);
	if (ret)
		return ret;

	ret = i2c_generic_recovery(adap);
	i2c_put_gpios_for_recovery(adap);

	return ret;
}
EXPORT_SYMBOL_GPL(i2c_generic_gpio_recovery);

int i2c_recover_bus(struct i2c_adapter *adap)
{
	if (!adap->bus_recovery_info)
		return -EOPNOTSUPP;

	dev_dbg(&adap->dev, "Trying i2c bus recovery\n");
	return adap->bus_recovery_info->recover_bus(adap);
}
EXPORT_SYMBOL_GPL(i2c_recover_bus);

static void i2c_init_recovery(struct i2c_adapter *adap)
{
	struct i2c_bus_recovery_info *bri = adap->bus_recovery_info;
	char *err_str;

	if (!bri)
		return;

	if (!bri->recover_bus) {
		err_str = "no recover_bus() found";
		goto err;
	}

	/* Generic GPIO recovery */
	if (bri->recover_bus == i2c_generic_gpio_recovery) {
		if (!gpio_is_valid(bri->scl_gpio)) {
			err_str = "invalid SCL gpio";
			goto err;
		}

		if (gpio_is_valid(bri->sda_gpio))
			bri->get_sda = get_sda_gpio_value;
		else
			bri->get_sda = NULL;

		bri->get_scl = get_scl_gpio_value;
		bri->set_scl = set_scl_gpio_value;
	} else if (bri->recover_bus == i2c_generic_scl_recovery) {
		/* Generic SCL recovery */
		if (!bri->set_scl || !bri->get_scl) {
			err_str = "no {get|set}_scl() found";
			goto err;
		}
	}

	return;
 err:
	dev_err(&adap->dev, "Not using recovery: %s\n", err_str);
	adap->bus_recovery_info = NULL;
}

static int i2c_smbus_host_notify_to_irq(const struct i2c_client *client)
{
	struct i2c_adapter *adap = client->adapter;
	unsigned int irq;

	if (!adap->host_notify_domain)
		return -ENXIO;

	if (client->flags & I2C_CLIENT_TEN)
		return -EINVAL;

	irq = irq_find_mapping(adap->host_notify_domain, client->addr);
	if (!irq)
		irq = irq_create_mapping(adap->host_notify_domain,
					 client->addr);

	return irq > 0 ? irq : -ENXIO;
}

static int i2c_device_probe(struct device *dev)
{
	struct i2c_client	*client = i2c_verify_client(dev);
	struct i2c_driver	*driver;
	int status;

	if (!client)
		return 0;

	if (!client->irq) {
		int irq = -ENOENT;

		if (client->flags & I2C_CLIENT_HOST_NOTIFY) {
			dev_dbg(dev, "Using Host Notify IRQ\n");
			irq = i2c_smbus_host_notify_to_irq(client);
		} else if (dev->of_node) {
			irq = of_irq_get_byname(dev->of_node, "irq");
			if (irq == -EINVAL || irq == -ENODATA)
				irq = of_irq_get(dev->of_node, 0);
		} else if (ACPI_COMPANION(dev)) {
			irq = acpi_dev_gpio_irq_get(ACPI_COMPANION(dev), 0);
		}
		if (irq == -EPROBE_DEFER)
			return irq;

		if (irq < 0)
			irq = 0;

		client->irq = irq;
	}

	driver = to_i2c_driver(dev->driver);

	/*
	 * An I2C ID table is not mandatory, if and only if, a suitable Device
	 * Tree match table entry is supplied for the probing device.
	 */
	if (!driver->id_table &&
	    !i2c_of_match_device(dev->driver->of_match_table, client))
		return -ENODEV;

	if (client->flags & I2C_CLIENT_WAKE) {
		int wakeirq = -ENOENT;

		if (dev->of_node) {
			wakeirq = of_irq_get_byname(dev->of_node, "wakeup");
			if (wakeirq == -EPROBE_DEFER)
				return wakeirq;
		}

		device_init_wakeup(&client->dev, true);

		if (wakeirq > 0 && wakeirq != client->irq)
			status = dev_pm_set_dedicated_wake_irq(dev, wakeirq);
		else if (client->irq > 0)
			status = dev_pm_set_wake_irq(dev, client->irq);
		else
			status = 0;

		if (status)
			dev_warn(&client->dev, "failed to set up wakeup irq\n");
	}

	dev_dbg(dev, "probe\n");

	status = of_clk_set_defaults(dev->of_node, false);
	if (status < 0)
		goto err_clear_wakeup_irq;

	status = dev_pm_domain_attach(&client->dev, true);
	if (status == -EPROBE_DEFER)
		goto err_clear_wakeup_irq;

	/*
	 * When there are no more users of probe(),
	 * rename probe_new to probe.
	 */
	if (driver->probe_new)
		status = driver->probe_new(client);
	else if (driver->probe)
		status = driver->probe(client,
				       i2c_match_id(driver->id_table, client));
	else
		status = -EINVAL;

	if (status)
		goto err_detach_pm_domain;

	return 0;

err_detach_pm_domain:
	dev_pm_domain_detach(&client->dev, true);
err_clear_wakeup_irq:
	dev_pm_clear_wake_irq(&client->dev);
	device_init_wakeup(&client->dev, false);
	return status;
}

static int i2c_device_remove(struct device *dev)
{
	struct i2c_client	*client = i2c_verify_client(dev);
	struct i2c_driver	*driver;
	int status = 0;

	if (!client || !dev->driver)
		return 0;

	driver = to_i2c_driver(dev->driver);
	if (driver->remove) {
		dev_dbg(dev, "remove\n");
		status = driver->remove(client);
	}

	dev_pm_domain_detach(&client->dev, true);

	dev_pm_clear_wake_irq(&client->dev);
	device_init_wakeup(&client->dev, false);

	return status;
}

static void i2c_device_shutdown(struct device *dev)
{
	struct i2c_client *client = i2c_verify_client(dev);
	struct i2c_driver *driver;

	if (!client || !dev->driver)
		return;
	driver = to_i2c_driver(dev->driver);
	if (driver->shutdown)
		driver->shutdown(client);
}

static void i2c_client_dev_release(struct device *dev)
{
	kfree(to_i2c_client(dev));
}

static ssize_t
show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", dev->type == &i2c_client_type ?
		       to_i2c_client(dev)->name : to_i2c_adapter(dev)->name);
}
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static ssize_t
show_modalias(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int len;

	len = acpi_device_modalias(dev, buf, PAGE_SIZE -1);
	if (len != -ENODEV)
		return len;

	return sprintf(buf, "%s%s\n", I2C_MODULE_PREFIX, client->name);
}
static DEVICE_ATTR(modalias, S_IRUGO, show_modalias, NULL);

static struct attribute *i2c_dev_attrs[] = {
	&dev_attr_name.attr,
	/* modalias helps coldplug:  modprobe $(cat .../modalias) */
	&dev_attr_modalias.attr,
	NULL
};
ATTRIBUTE_GROUPS(i2c_dev);

struct bus_type i2c_bus_type = {
	.name		= "i2c",
	.match		= i2c_device_match,
	.probe		= i2c_device_probe,
	.remove		= i2c_device_remove,
	.shutdown	= i2c_device_shutdown,
};
EXPORT_SYMBOL_GPL(i2c_bus_type);

static struct device_type i2c_client_type = {
	.groups		= i2c_dev_groups,
	.uevent		= i2c_device_uevent,
	.release	= i2c_client_dev_release,
};


/**
 * i2c_verify_client - return parameter as i2c_client, or NULL
 * @dev: device, probably from some driver model iterator
 *
 * When traversing the driver model tree, perhaps using driver model
 * iterators like @device_for_each_child(), you can't assume very much
 * about the nodes you find.  Use this function to avoid oopses caused
 * by wrongly treating some non-I2C device as an i2c_client.
 */
struct i2c_client *i2c_verify_client(struct device *dev)
{
	return (dev->type == &i2c_client_type)
			? to_i2c_client(dev)
			: NULL;
}
EXPORT_SYMBOL(i2c_verify_client);


/* Return a unique address which takes the flags of the client into account */
static unsigned short i2c_encode_flags_to_addr(struct i2c_client *client)
{
	unsigned short addr = client->addr;

	/* For some client flags, add an arbitrary offset to avoid collisions */
	if (client->flags & I2C_CLIENT_TEN)
		addr |= I2C_ADDR_OFFSET_TEN_BIT;

	if (client->flags & I2C_CLIENT_SLAVE)
		addr |= I2C_ADDR_OFFSET_SLAVE;

	return addr;
}

/* This is a permissive address validity check, I2C address map constraints
 * are purposely not enforced, except for the general call address. */
static int i2c_check_addr_validity(unsigned addr, unsigned short flags)
{
	if (flags & I2C_CLIENT_TEN) {
		/* 10-bit address, all values are valid */
		if (addr > 0x3ff)
			return -EINVAL;
	} else {
		/* 7-bit address, reject the general call address */
		if (addr == 0x00 || addr > 0x7f)
			return -EINVAL;
	}
	return 0;
}

/* And this is a strict address validity check, used when probing. If a
 * device uses a reserved address, then it shouldn't be probed. 7-bit
 * addressing is assumed, 10-bit address devices are rare and should be
 * explicitly enumerated. */
static int i2c_check_7bit_addr_validity_strict(unsigned short addr)
{
	/*
	 * Reserved addresses per I2C specification:
	 *  0x00       General call address / START byte
	 *  0x01       CBUS address
	 *  0x02       Reserved for different bus format
	 *  0x03       Reserved for future purposes
	 *  0x04-0x07  Hs-mode master code
	 *  0x78-0x7b  10-bit slave addressing
	 *  0x7c-0x7f  Reserved for future purposes
	 */
	if (addr < 0x08 || addr > 0x77)
		return -EINVAL;
	return 0;
}

static int __i2c_check_addr_busy(struct device *dev, void *addrp)
{
	struct i2c_client	*client = i2c_verify_client(dev);
	int			addr = *(int *)addrp;

	if (client && i2c_encode_flags_to_addr(client) == addr)
		return -EBUSY;
	return 0;
}

/* walk up mux tree */
static int i2c_check_mux_parents(struct i2c_adapter *adapter, int addr)
{
	struct i2c_adapter *parent = i2c_parent_is_i2c_adapter(adapter);
	int result;

	result = device_for_each_child(&adapter->dev, &addr,
					__i2c_check_addr_busy);

	if (!result && parent)
		result = i2c_check_mux_parents(parent, addr);

	return result;
}

/* recurse down mux tree */
static int i2c_check_mux_children(struct device *dev, void *addrp)
{
	int result;

	if (dev->type == &i2c_adapter_type)
		result = device_for_each_child(dev, addrp,
						i2c_check_mux_children);
	else
		result = __i2c_check_addr_busy(dev, addrp);

	return result;
}

static int i2c_check_addr_busy(struct i2c_adapter *adapter, int addr)
{
	struct i2c_adapter *parent = i2c_parent_is_i2c_adapter(adapter);
	int result = 0;

	if (parent)
		result = i2c_check_mux_parents(parent, addr);

	if (!result)
		result = device_for_each_child(&adapter->dev, &addr,
						i2c_check_mux_children);

	return result;
}

/**
 * i2c_adapter_lock_bus - Get exclusive access to an I2C bus segment
 * @adapter: Target I2C bus segment
 * @flags: I2C_LOCK_ROOT_ADAPTER locks the root i2c adapter, I2C_LOCK_SEGMENT
 *	locks only this branch in the adapter tree
 */
static void i2c_adapter_lock_bus(struct i2c_adapter *adapter,
				 unsigned int flags)
{
	rt_mutex_lock(&adapter->bus_lock);
}

/**
 * i2c_adapter_trylock_bus - Try to get exclusive access to an I2C bus segment
 * @adapter: Target I2C bus segment
 * @flags: I2C_LOCK_ROOT_ADAPTER trylocks the root i2c adapter, I2C_LOCK_SEGMENT
 *	trylocks only this branch in the adapter tree
 */
static int i2c_adapter_trylock_bus(struct i2c_adapter *adapter,
				   unsigned int flags)
{
	return rt_mutex_trylock(&adapter->bus_lock);
}

/**
 * i2c_adapter_unlock_bus - Release exclusive access to an I2C bus segment
 * @adapter: Target I2C bus segment
 * @flags: I2C_LOCK_ROOT_ADAPTER unlocks the root i2c adapter, I2C_LOCK_SEGMENT
 *	unlocks only this branch in the adapter tree
 */
static void i2c_adapter_unlock_bus(struct i2c_adapter *adapter,
				   unsigned int flags)
{
	rt_mutex_unlock(&adapter->bus_lock);
}

static void i2c_dev_set_name(struct i2c_adapter *adap,
			     struct i2c_client *client)
{
	struct acpi_device *adev = ACPI_COMPANION(&client->dev);

	if (adev) {
		dev_set_name(&client->dev, "i2c-%s", acpi_dev_name(adev));
		return;
	}

	dev_set_name(&client->dev, "%d-%04x", i2c_adapter_id(adap),
		     i2c_encode_flags_to_addr(client));
}

/**
 * i2c_new_device - instantiate an i2c device
 * @adap: the adapter managing the device
 * @info: describes one I2C device; bus_num is ignored
 * Context: can sleep
 *
 * Create an i2c device. Binding is handled through driver model
 * probe()/remove() methods.  A driver may be bound to this device when we
 * return from this function, or any later moment (e.g. maybe hotplugging will
 * load the driver module).  This call is not appropriate for use by mainboard
 * initialization logic, which usually runs during an arch_initcall() long
 * before any i2c_adapter could exist.
 *
 * This returns the new i2c client, which may be saved for later use with
 * i2c_unregister_device(); or NULL to indicate an error.
 */
struct i2c_client *
i2c_new_device(struct i2c_adapter *adap, struct i2c_board_info const *info)
{
	struct i2c_client	*client;
	int			status;

	client = kzalloc(sizeof *client, GFP_KERNEL);
	if (!client)
		return NULL;

	client->adapter = adap;

	client->dev.platform_data = info->platform_data;

	if (info->archdata)
		client->dev.archdata = *info->archdata;

	client->flags = info->flags;
	client->addr = info->addr;
	client->irq = info->irq;

	strlcpy(client->name, info->type, sizeof(client->name));

	status = i2c_check_addr_validity(client->addr, client->flags);
	if (status) {
		dev_err(&adap->dev, "Invalid %d-bit I2C address 0x%02hx\n",
			client->flags & I2C_CLIENT_TEN ? 10 : 7, client->addr);
		goto out_err_silent;
	}

	/* Check for address business */
	status = i2c_check_addr_busy(adap, i2c_encode_flags_to_addr(client));
	if (status)
		goto out_err;

	client->dev.parent = &client->adapter->dev;
	client->dev.bus = &i2c_bus_type;
	client->dev.type = &i2c_client_type;
	client->dev.of_node = info->of_node;
	client->dev.fwnode = info->fwnode;

	i2c_dev_set_name(adap, client);

	if (info->properties) {
		status = device_add_properties(&client->dev, info->properties);
		if (status) {
			dev_err(&adap->dev,
				"Failed to add properties to client %s: %d\n",
				client->name, status);
			goto out_err;
		}
	}

	status = device_register(&client->dev);
	if (status)
		goto out_free_props;

	dev_dbg(&adap->dev, "client [%s] registered with bus id %s\n",
		client->name, dev_name(&client->dev));

	return client;

out_free_props:
	if (info->properties)
		device_remove_properties(&client->dev);
out_err:
	dev_err(&adap->dev,
		"Failed to register i2c client %s at 0x%02x (%d)\n",
		client->name, client->addr, status);
out_err_silent:
	kfree(client);
	return NULL;
}
EXPORT_SYMBOL_GPL(i2c_new_device);


/**
 * i2c_unregister_device - reverse effect of i2c_new_device()
 * @client: value returned from i2c_new_device()
 * Context: can sleep
 */
void i2c_unregister_device(struct i2c_client *client)
{
	if (client->dev.of_node)
		of_node_clear_flag(client->dev.of_node, OF_POPULATED);
	if (ACPI_COMPANION(&client->dev))
		acpi_device_clear_enumerated(ACPI_COMPANION(&client->dev));
	device_unregister(&client->dev);
}
EXPORT_SYMBOL_GPL(i2c_unregister_device);


static const struct i2c_device_id dummy_id[] = {
	{ "dummy", 0 },
	{ },
};

static int dummy_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	return 0;
}

static int dummy_remove(struct i2c_client *client)
{
	return 0;
}

static struct i2c_driver dummy_driver = {
	.driver.name	= "dummy",
	.probe		= dummy_probe,
	.remove		= dummy_remove,
	.id_table	= dummy_id,
};

/**
 * i2c_new_dummy - return a new i2c device bound to a dummy driver
 * @adapter: the adapter managing the device
 * @address: seven bit address to be used
 * Context: can sleep
 *
 * This returns an I2C client bound to the "dummy" driver, intended for use
 * with devices that consume multiple addresses.  Examples of such chips
 * include various EEPROMS (like 24c04 and 24c08 models).
 *
 * These dummy devices have two main uses.  First, most I2C and SMBus calls
 * except i2c_transfer() need a client handle; the dummy will be that handle.
 * And second, this prevents the specified address from being bound to a
 * different driver.
 *
 * This returns the new i2c client, which should be saved for later use with
 * i2c_unregister_device(); or NULL to indicate an error.
 */
struct i2c_client *i2c_new_dummy(struct i2c_adapter *adapter, u16 address)
{
	struct i2c_board_info info = {
		I2C_BOARD_INFO("dummy", address),
	};

	return i2c_new_device(adapter, &info);
}
EXPORT_SYMBOL_GPL(i2c_new_dummy);

/**
 * i2c_new_secondary_device - Helper to get the instantiated secondary address
 * and create the associated device
 * @client: Handle to the primary client
 * @name: Handle to specify which secondary address to get
 * @default_addr: Used as a fallback if no secondary address was specified
 * Context: can sleep
 *
 * I2C clients can be composed of multiple I2C slaves bound together in a single
 * component. The I2C client driver then binds to the master I2C slave and needs
 * to create I2C dummy clients to communicate with all the other slaves.
 *
 * This function creates and returns an I2C dummy client whose I2C address is
 * retrieved from the platform firmware based on the given slave name. If no
 * address is specified by the firmware default_addr is used.
 *
 * On DT-based platforms the address is retrieved from the "reg" property entry
 * cell whose "reg-names" value matches the slave name.
 *
 * This returns the new i2c client, which should be saved for later use with
 * i2c_unregister_device(); or NULL to indicate an error.
 */
struct i2c_client *i2c_new_secondary_device(struct i2c_client *client,
						const char *name,
						u16 default_addr)
{
	struct device_node *np = client->dev.of_node;
	u32 addr = default_addr;
	int i;

	if (np) {
		i = of_property_match_string(np, "reg-names", name);
		if (i >= 0)
			of_property_read_u32_index(np, "reg", i, &addr);
	}

	dev_dbg(&client->adapter->dev, "Address for %s : 0x%x\n", name, addr);
	return i2c_new_dummy(client->adapter, addr);
}
EXPORT_SYMBOL_GPL(i2c_new_secondary_device);

/* ------------------------------------------------------------------------- */

/* I2C bus adapters -- one roots each I2C or SMBUS segment */

static void i2c_adapter_dev_release(struct device *dev)
{
	struct i2c_adapter *adap = to_i2c_adapter(dev);
	complete(&adap->dev_released);
}

unsigned int i2c_adapter_depth(struct i2c_adapter *adapter)
{
	unsigned int depth = 0;

	while ((adapter = i2c_parent_is_i2c_adapter(adapter)))
		depth++;

	WARN_ONCE(depth >= MAX_LOCKDEP_SUBCLASSES,
		  "adapter depth exceeds lockdep subclass limit\n");

	return depth;
}
EXPORT_SYMBOL_GPL(i2c_adapter_depth);

/*
 * Let users instantiate I2C devices through sysfs. This can be used when
 * platform initialization code doesn't contain the proper data for
 * whatever reason. Also useful for drivers that do device detection and
 * detection fails, either because the device uses an unexpected address,
 * or this is a compatible device with different ID register values.
 *
 * Parameter checking may look overzealous, but we really don't want
 * the user to provide incorrect parameters.
 */
static ssize_t
i2c_sysfs_new_device(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct i2c_adapter *adap = to_i2c_adapter(dev);
	struct i2c_board_info info;
	struct i2c_client *client;
	char *blank, end;
	int res;

	memset(&info, 0, sizeof(struct i2c_board_info));

	blank = strchr(buf, ' ');
	if (!blank) {
		dev_err(dev, "%s: Missing parameters\n", "new_device");
		return -EINVAL;
	}
	if (blank - buf > I2C_NAME_SIZE - 1) {
		dev_err(dev, "%s: Invalid device name\n", "new_device");
		return -EINVAL;
	}
	memcpy(info.type, buf, blank - buf);

	/* Parse remaining parameters, reject extra parameters */
	res = sscanf(++blank, "%hi%c", &info.addr, &end);
	if (res < 1) {
		dev_err(dev, "%s: Can't parse I2C address\n", "new_device");
		return -EINVAL;
	}
	if (res > 1  && end != '\n') {
		dev_err(dev, "%s: Extra parameters\n", "new_device");
		return -EINVAL;
	}

	if ((info.addr & I2C_ADDR_OFFSET_TEN_BIT) == I2C_ADDR_OFFSET_TEN_BIT) {
		info.addr &= ~I2C_ADDR_OFFSET_TEN_BIT;
		info.flags |= I2C_CLIENT_TEN;
	}

	if (info.addr & I2C_ADDR_OFFSET_SLAVE) {
		info.addr &= ~I2C_ADDR_OFFSET_SLAVE;
		info.flags |= I2C_CLIENT_SLAVE;
	}

	client = i2c_new_device(adap, &info);
	if (!client)
		return -EINVAL;

	/* Keep track of the added device */
	mutex_lock(&adap->userspace_clients_lock);
	list_add_tail(&client->detected, &adap->userspace_clients);
	mutex_unlock(&adap->userspace_clients_lock);
	dev_info(dev, "%s: Instantiated device %s at 0x%02hx\n", "new_device",
		 info.type, info.addr);

	return count;
}
static DEVICE_ATTR(new_device, S_IWUSR, NULL, i2c_sysfs_new_device);

/*
 * And of course let the users delete the devices they instantiated, if
 * they got it wrong. This interface can only be used to delete devices
 * instantiated by i2c_sysfs_new_device above. This guarantees that we
 * don't delete devices to which some kernel code still has references.
 *
 * Parameter checking may look overzealous, but we really don't want
 * the user to delete the wrong device.
 */
static ssize_t
i2c_sysfs_delete_device(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_adapter *adap = to_i2c_adapter(dev);
	struct i2c_client *client, *next;
	unsigned short addr;
	char end;
	int res;

	/* Parse parameters, reject extra parameters */
	res = sscanf(buf, "%hi%c", &addr, &end);
	if (res < 1) {
		dev_err(dev, "%s: Can't parse I2C address\n", "delete_device");
		return -EINVAL;
	}
	if (res > 1  && end != '\n') {
		dev_err(dev, "%s: Extra parameters\n", "delete_device");
		return -EINVAL;
	}

	/* Make sure the device was added through sysfs */
	res = -ENOENT;
	mutex_lock_nested(&adap->userspace_clients_lock,
			  i2c_adapter_depth(adap));
	list_for_each_entry_safe(client, next, &adap->userspace_clients,
				 detected) {
		if (i2c_encode_flags_to_addr(client) == addr) {
			dev_info(dev, "%s: Deleting device %s at 0x%02hx\n",
				 "delete_device", client->name, client->addr);

			list_del(&client->detected);
			i2c_unregister_device(client);
			res = count;
			break;
		}
	}
	mutex_unlock(&adap->userspace_clients_lock);

	if (res < 0)
		dev_err(dev, "%s: Can't find device in list\n",
			"delete_device");
	return res;
}
static DEVICE_ATTR_IGNORE_LOCKDEP(delete_device, S_IWUSR, NULL,
				   i2c_sysfs_delete_device);

static struct attribute *i2c_adapter_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_new_device.attr,
	&dev_attr_delete_device.attr,
	NULL
};
ATTRIBUTE_GROUPS(i2c_adapter);

struct device_type i2c_adapter_type = {
	.groups		= i2c_adapter_groups,
	.release	= i2c_adapter_dev_release,
};
EXPORT_SYMBOL_GPL(i2c_adapter_type);

/**
 * i2c_verify_adapter - return parameter as i2c_adapter or NULL
 * @dev: device, probably from some driver model iterator
 *
 * When traversing the driver model tree, perhaps using driver model
 * iterators like @device_for_each_child(), you can't assume very much
 * about the nodes you find.  Use this function to avoid oopses caused
 * by wrongly treating some non-I2C device as an i2c_adapter.
 */
struct i2c_adapter *i2c_verify_adapter(struct device *dev)
{
	return (dev->type == &i2c_adapter_type)
			? to_i2c_adapter(dev)
			: NULL;
}
EXPORT_SYMBOL(i2c_verify_adapter);

#ifdef CONFIG_I2C_COMPAT
static struct class_compat *i2c_adapter_compat_class;
#endif

static void i2c_scan_static_board_info(struct i2c_adapter *adapter)
{
	struct i2c_devinfo	*devinfo;

	down_read(&__i2c_board_lock);
	list_for_each_entry(devinfo, &__i2c_board_list, list) {
		if (devinfo->busnum == adapter->nr
				&& !i2c_new_device(adapter,
						&devinfo->board_info))
			dev_err(&adapter->dev,
				"Can't create device at 0x%02x\n",
				devinfo->board_info.addr);
	}
	up_read(&__i2c_board_lock);
}

/* OF support code */

#if IS_ENABLED(CONFIG_OF)
static struct i2c_client *of_i2c_register_device(struct i2c_adapter *adap,
						 struct device_node *node)
{
	struct i2c_client *result;
	struct i2c_board_info info = {};
	struct dev_archdata dev_ad = {};
	const __be32 *addr_be;
	u32 addr;
	int len;

	dev_dbg(&adap->dev, "of_i2c: register %s\n", node->full_name);

	if (of_modalias_node(node, info.type, sizeof(info.type)) < 0) {
		dev_err(&adap->dev, "of_i2c: modalias failure on %s\n",
			node->full_name);
		return ERR_PTR(-EINVAL);
	}

	addr_be = of_get_property(node, "reg", &len);
	if (!addr_be || (len < sizeof(*addr_be))) {
		dev_err(&adap->dev, "of_i2c: invalid reg on %s\n",
			node->full_name);
		return ERR_PTR(-EINVAL);
	}

	addr = be32_to_cpup(addr_be);
	if (addr & I2C_TEN_BIT_ADDRESS) {
		addr &= ~I2C_TEN_BIT_ADDRESS;
		info.flags |= I2C_CLIENT_TEN;
	}

	if (addr & I2C_OWN_SLAVE_ADDRESS) {
		addr &= ~I2C_OWN_SLAVE_ADDRESS;
		info.flags |= I2C_CLIENT_SLAVE;
	}

	if (i2c_check_addr_validity(addr, info.flags)) {
		dev_err(&adap->dev, "of_i2c: invalid addr=%x on %s\n",
			addr, node->full_name);
		return ERR_PTR(-EINVAL);
	}

	info.addr = addr;
	info.of_node = of_node_get(node);
	info.archdata = &dev_ad;

	if (of_property_read_bool(node, "host-notify"))
		info.flags |= I2C_CLIENT_HOST_NOTIFY;

	if (of_get_property(node, "wakeup-source", NULL))
		info.flags |= I2C_CLIENT_WAKE;

	result = i2c_new_device(adap, &info);
	if (result == NULL) {
		dev_err(&adap->dev, "of_i2c: Failure registering %s\n",
			node->full_name);
		of_node_put(node);
		return ERR_PTR(-EINVAL);
	}
	return result;
}

static void of_i2c_register_devices(struct i2c_adapter *adap)
{
	struct device_node *bus, *node;
	struct i2c_client *client;

	/* Only register child devices if the adapter has a node pointer set */
	if (!adap->dev.of_node)
		return;

	dev_dbg(&adap->dev, "of_i2c: walking child nodes\n");

	bus = of_get_child_by_name(adap->dev.of_node, "i2c-bus");
	if (!bus)
		bus = of_node_get(adap->dev.of_node);

	for_each_available_child_of_node(bus, node) {
		if (of_node_test_and_set_flag(node, OF_POPULATED))
			continue;

		client = of_i2c_register_device(adap, node);
		if (IS_ERR(client)) {
			dev_warn(&adap->dev,
				 "Failed to create I2C device for %s\n",
				 node->full_name);
			of_node_clear_flag(node, OF_POPULATED);
		}
	}

	of_node_put(bus);
}

static int of_dev_node_match(struct device *dev, void *data)
{
	return dev->of_node == data;
}

/* must call put_device() when done with returned i2c_client device */
struct i2c_client *of_find_i2c_device_by_node(struct device_node *node)
{
	struct device *dev;
	struct i2c_client *client;

	dev = bus_find_device(&i2c_bus_type, NULL, node, of_dev_node_match);
	if (!dev)
		return NULL;

	client = i2c_verify_client(dev);
	if (!client)
		put_device(dev);

	return client;
}
EXPORT_SYMBOL(of_find_i2c_device_by_node);

/* must call put_device() when done with returned i2c_adapter device */
struct i2c_adapter *of_find_i2c_adapter_by_node(struct device_node *node)
{
	struct device *dev;
	struct i2c_adapter *adapter;

	dev = bus_find_device(&i2c_bus_type, NULL, node, of_dev_node_match);
	if (!dev)
		return NULL;

	adapter = i2c_verify_adapter(dev);
	if (!adapter)
		put_device(dev);

	return adapter;
}
EXPORT_SYMBOL(of_find_i2c_adapter_by_node);

/* must call i2c_put_adapter() when done with returned i2c_adapter device */
struct i2c_adapter *of_get_i2c_adapter_by_node(struct device_node *node)
{
	struct i2c_adapter *adapter;

	adapter = of_find_i2c_adapter_by_node(node);
	if (!adapter)
		return NULL;

	if (!try_module_get(adapter->owner)) {
		put_device(&adapter->dev);
		adapter = NULL;
	}

	return adapter;
}
EXPORT_SYMBOL(of_get_i2c_adapter_by_node);

static const struct of_device_id*
i2c_of_match_device_sysfs(const struct of_device_id *matches,
				  struct i2c_client *client)
{
	const char *name;

	for (; matches->compatible[0]; matches++) {
		/*
		 * Adding devices through the i2c sysfs interface provides us
		 * a string to match which may be compatible with the device
		 * tree compatible strings, however with no actual of_node the
		 * of_match_device() will not match
		 */
		if (sysfs_streq(client->name, matches->compatible))
			return matches;

		name = strchr(matches->compatible, ',');
		if (!name)
			name = matches->compatible;
		else
			name++;

		if (sysfs_streq(client->name, name))
			return matches;
	}

	return NULL;
}

const struct of_device_id
*i2c_of_match_device(const struct of_device_id *matches,
		     struct i2c_client *client)
{
	const struct of_device_id *match;

	if (!(client && matches))
		return NULL;

	match = of_match_device(matches, &client->dev);
	if (match)
		return match;

	return i2c_of_match_device_sysfs(matches, client);
}
EXPORT_SYMBOL_GPL(i2c_of_match_device);
#else
static void of_i2c_register_devices(struct i2c_adapter *adap) { }
#endif /* CONFIG_OF */

static int i2c_do_add_adapter(struct i2c_driver *driver,
			      struct i2c_adapter *adap)
{
	/* Detect supported devices on that bus, and instantiate them */
	i2c_detect(adap, driver);

	/* Let legacy drivers scan this bus for matching devices */
	if (driver->attach_adapter) {
		dev_warn(&adap->dev, "%s: attach_adapter method is deprecated\n",
			 driver->driver.name);
		dev_warn(&adap->dev,
			 "Please use another way to instantiate your i2c_client\n");
		/* We ignore the return code; if it fails, too bad */
		driver->attach_adapter(adap);
	}
	return 0;
}

static int __process_new_adapter(struct device_driver *d, void *data)
{
	return i2c_do_add_adapter(to_i2c_driver(d), data);
}

static const struct i2c_lock_operations i2c_adapter_lock_ops = {
	.lock_bus =    i2c_adapter_lock_bus,
	.trylock_bus = i2c_adapter_trylock_bus,
	.unlock_bus =  i2c_adapter_unlock_bus,
};

static void i2c_host_notify_irq_teardown(struct i2c_adapter *adap)
{
	struct irq_domain *domain = adap->host_notify_domain;
	irq_hw_number_t hwirq;

	if (!domain)
		return;

	for (hwirq = 0 ; hwirq < I2C_ADDR_7BITS_COUNT ; hwirq++)
		irq_dispose_mapping(irq_find_mapping(domain, hwirq));

	irq_domain_remove(domain);
	adap->host_notify_domain = NULL;
}

static int i2c_host_notify_irq_map(struct irq_domain *h,
					  unsigned int virq,
					  irq_hw_number_t hw_irq_num)
{
	irq_set_chip_and_handler(virq, &dummy_irq_chip, handle_simple_irq);

	return 0;
}

static const struct irq_domain_ops i2c_host_notify_irq_ops = {
	.map = i2c_host_notify_irq_map,
};

static int i2c_setup_host_notify_irq_domain(struct i2c_adapter *adap)
{
	struct irq_domain *domain;

	if (!i2c_check_functionality(adap, I2C_FUNC_SMBUS_HOST_NOTIFY))
		return 0;

	domain = irq_domain_create_linear(adap->dev.fwnode,
					  I2C_ADDR_7BITS_COUNT,
					  &i2c_host_notify_irq_ops, adap);
	if (!domain)
		return -ENOMEM;

	adap->host_notify_domain = domain;

	return 0;
}

/**
 * i2c_handle_smbus_host_notify - Forward a Host Notify event to the correct
 * I2C client.
 * @adap: the adapter
 * @addr: the I2C address of the notifying device
 * Context: can't sleep
 *
 * Helper function to be called from an I2C bus driver's interrupt
 * handler. It will schedule the Host Notify IRQ.
 */
int i2c_handle_smbus_host_notify(struct i2c_adapter *adap, unsigned short addr)
{
	int irq;

	if (!adap)
		return -EINVAL;

	irq = irq_find_mapping(adap->host_notify_domain, addr);
	if (irq <= 0)
		return -ENXIO;

	generic_handle_irq(irq);

	return 0;
}
EXPORT_SYMBOL_GPL(i2c_handle_smbus_host_notify);

static int i2c_register_adapter(struct i2c_adapter *adap)
{
	int res = -EINVAL;

	/* Can't register until after driver model init */
	if (WARN_ON(!is_registered)) {
		res = -EAGAIN;
		goto out_list;
	}

	/* Sanity checks */
	if (WARN(!adap->name[0], "i2c adapter has no name"))
		goto out_list;

	if (!adap->algo) {
		pr_err("adapter '%s': no algo supplied!\n", adap->name);
		goto out_list;
	}

	if (!adap->lock_ops)
		adap->lock_ops = &i2c_adapter_lock_ops;

	rt_mutex_init(&adap->bus_lock);
	rt_mutex_init(&adap->mux_lock);
	mutex_init(&adap->userspace_clients_lock);
	INIT_LIST_HEAD(&adap->userspace_clients);

	/* Set default timeout to 1 second if not already set */
	if (adap->timeout == 0)
		adap->timeout = HZ;

	/* register soft irqs for Host Notify */
	res = i2c_setup_host_notify_irq_domain(adap);
	if (res) {
		pr_err("adapter '%s': can't create Host Notify IRQs (%d)\n",
		       adap->name, res);
		goto out_list;
	}

	dev_set_name(&adap->dev, "i2c-%d", adap->nr);
	adap->dev.bus = &i2c_bus_type;
	adap->dev.type = &i2c_adapter_type;
	res = device_register(&adap->dev);
	if (res) {
		pr_err("adapter '%s': can't register device (%d)\n", adap->name, res);
		goto out_list;
	}

	dev_dbg(&adap->dev, "adapter [%s] registered\n", adap->name);

	pm_runtime_no_callbacks(&adap->dev);
	pm_suspend_ignore_children(&adap->dev, true);
	pm_runtime_enable(&adap->dev);

#ifdef CONFIG_I2C_COMPAT
	res = class_compat_create_link(i2c_adapter_compat_class, &adap->dev,
				       adap->dev.parent);
	if (res)
		dev_warn(&adap->dev,
			 "Failed to create compatibility class link\n");
#endif

	i2c_init_recovery(adap);

	/* create pre-declared device nodes */
	of_i2c_register_devices(adap);
	i2c_acpi_register_devices(adap);
	i2c_acpi_install_space_handler(adap);

	if (adap->nr < __i2c_first_dynamic_bus_num)
		i2c_scan_static_board_info(adap);

	/* Notify drivers */
	mutex_lock(&core_lock);
	bus_for_each_drv(&i2c_bus_type, NULL, adap, __process_new_adapter);
	mutex_unlock(&core_lock);

	return 0;

out_list:
	mutex_lock(&core_lock);
	idr_remove(&i2c_adapter_idr, adap->nr);
	mutex_unlock(&core_lock);
	return res;
}

/**
 * __i2c_add_numbered_adapter - i2c_add_numbered_adapter where nr is never -1
 * @adap: the adapter to register (with adap->nr initialized)
 * Context: can sleep
 *
 * See i2c_add_numbered_adapter() for details.
 */
static int __i2c_add_numbered_adapter(struct i2c_adapter *adap)
{
	int id;

	mutex_lock(&core_lock);
	id = idr_alloc(&i2c_adapter_idr, adap, adap->nr, adap->nr + 1, GFP_KERNEL);
	mutex_unlock(&core_lock);
	if (WARN(id < 0, "couldn't get idr"))
		return id == -ENOSPC ? -EBUSY : id;

	return i2c_register_adapter(adap);
}

/**
 * i2c_add_adapter - declare i2c adapter, use dynamic bus number
 * @adapter: the adapter to add
 * Context: can sleep
 *
 * This routine is used to declare an I2C adapter when its bus number
 * doesn't matter or when its bus number is specified by an dt alias.
 * Examples of bases when the bus number doesn't matter: I2C adapters
 * dynamically added by USB links or PCI plugin cards.
 *
 * When this returns zero, a new bus number was allocated and stored
 * in adap->nr, and the specified adapter became available for clients.
 * Otherwise, a negative errno value is returned.
 */
int i2c_add_adapter(struct i2c_adapter *adapter)
{
	struct device *dev = &adapter->dev;
	int id;

	if (dev->of_node) {
		id = of_alias_get_id(dev->of_node, "i2c");
		if (id >= 0) {
			adapter->nr = id;
			return __i2c_add_numbered_adapter(adapter);
		}
	}

	mutex_lock(&core_lock);
	id = idr_alloc(&i2c_adapter_idr, adapter,
		       __i2c_first_dynamic_bus_num, 0, GFP_KERNEL);
	mutex_unlock(&core_lock);
	if (WARN(id < 0, "couldn't get idr"))
		return id;

	adapter->nr = id;

	return i2c_register_adapter(adapter);
}
EXPORT_SYMBOL(i2c_add_adapter);

/**
 * i2c_add_numbered_adapter - declare i2c adapter, use static bus number
 * @adap: the adapter to register (with adap->nr initialized)
 * Context: can sleep
 *
 * This routine is used to declare an I2C adapter when its bus number
 * matters.  For example, use it for I2C adapters from system-on-chip CPUs,
 * or otherwise built in to the system's mainboard, and where i2c_board_info
 * is used to properly configure I2C devices.
 *
 * If the requested bus number is set to -1, then this function will behave
 * identically to i2c_add_adapter, and will dynamically assign a bus number.
 *
 * If no devices have pre-been declared for this bus, then be sure to
 * register the adapter before any dynamically allocated ones.  Otherwise
 * the required bus ID may not be available.
 *
 * When this returns zero, the specified adapter became available for
 * clients using the bus number provided in adap->nr.  Also, the table
 * of I2C devices pre-declared using i2c_register_board_info() is scanned,
 * and the appropriate driver model device nodes are created.  Otherwise, a
 * negative errno value is returned.
 */
int i2c_add_numbered_adapter(struct i2c_adapter *adap)
{
	if (adap->nr == -1) /* -1 means dynamically assign bus id */
		return i2c_add_adapter(adap);

	return __i2c_add_numbered_adapter(adap);
}
EXPORT_SYMBOL_GPL(i2c_add_numbered_adapter);

static void i2c_do_del_adapter(struct i2c_driver *driver,
			      struct i2c_adapter *adapter)
{
	struct i2c_client *client, *_n;

	/* Remove the devices we created ourselves as the result of hardware
	 * probing (using a driver's detect method) */
	list_for_each_entry_safe(client, _n, &driver->clients, detected) {
		if (client->adapter == adapter) {
			dev_dbg(&adapter->dev, "Removing %s at 0x%x\n",
				client->name, client->addr);
			list_del(&client->detected);
			i2c_unregister_device(client);
		}
	}
}

static int __unregister_client(struct device *dev, void *dummy)
{
	struct i2c_client *client = i2c_verify_client(dev);
	if (client && strcmp(client->name, "dummy"))
		i2c_unregister_device(client);
	return 0;
}

static int __unregister_dummy(struct device *dev, void *dummy)
{
	struct i2c_client *client = i2c_verify_client(dev);
	if (client)
		i2c_unregister_device(client);
	return 0;
}

static int __process_removed_adapter(struct device_driver *d, void *data)
{
	i2c_do_del_adapter(to_i2c_driver(d), data);
	return 0;
}

/**
 * i2c_del_adapter - unregister I2C adapter
 * @adap: the adapter being unregistered
 * Context: can sleep
 *
 * This unregisters an I2C adapter which was previously registered
 * by @i2c_add_adapter or @i2c_add_numbered_adapter.
 */
void i2c_del_adapter(struct i2c_adapter *adap)
{
	struct i2c_adapter *found;
	struct i2c_client *client, *next;

	/* First make sure that this adapter was ever added */
	mutex_lock(&core_lock);
	found = idr_find(&i2c_adapter_idr, adap->nr);
	mutex_unlock(&core_lock);
	if (found != adap) {
		pr_debug("attempting to delete unregistered adapter [%s]\n", adap->name);
		return;
	}

	i2c_acpi_remove_space_handler(adap);
	/* Tell drivers about this removal */
	mutex_lock(&core_lock);
	bus_for_each_drv(&i2c_bus_type, NULL, adap,
			       __process_removed_adapter);
	mutex_unlock(&core_lock);

	/* Remove devices instantiated from sysfs */
	mutex_lock_nested(&adap->userspace_clients_lock,
			  i2c_adapter_depth(adap));
	list_for_each_entry_safe(client, next, &adap->userspace_clients,
				 detected) {
		dev_dbg(&adap->dev, "Removing %s at 0x%x\n", client->name,
			client->addr);
		list_del(&client->detected);
		i2c_unregister_device(client);
	}
	mutex_unlock(&adap->userspace_clients_lock);

	/* Detach any active clients. This can't fail, thus we do not
	 * check the returned value. This is a two-pass process, because
	 * we can't remove the dummy devices during the first pass: they
	 * could have been instantiated by real devices wishing to clean
	 * them up properly, so we give them a chance to do that first. */
	device_for_each_child(&adap->dev, NULL, __unregister_client);
	device_for_each_child(&adap->dev, NULL, __unregister_dummy);

#ifdef CONFIG_I2C_COMPAT
	class_compat_remove_link(i2c_adapter_compat_class, &adap->dev,
				 adap->dev.parent);
#endif

	/* device name is gone after device_unregister */
	dev_dbg(&adap->dev, "adapter [%s] unregistered\n", adap->name);

	pm_runtime_disable(&adap->dev);

	i2c_host_notify_irq_teardown(adap);

	/* wait until all references to the device are gone
	 *
	 * FIXME: This is old code and should ideally be replaced by an
	 * alternative which results in decoupling the lifetime of the struct
	 * device from the i2c_adapter, like spi or netdev do. Any solution
	 * should be thoroughly tested with DEBUG_KOBJECT_RELEASE enabled!
	 */
	init_completion(&adap->dev_released);
	device_unregister(&adap->dev);
	wait_for_completion(&adap->dev_released);

	/* free bus id */
	mutex_lock(&core_lock);
	idr_remove(&i2c_adapter_idr, adap->nr);
	mutex_unlock(&core_lock);

	/* Clear the device structure in case this adapter is ever going to be
	   added again */
	memset(&adap->dev, 0, sizeof(adap->dev));
}
EXPORT_SYMBOL(i2c_del_adapter);

/**
 * i2c_parse_fw_timings - get I2C related timing parameters from firmware
 * @dev: The device to scan for I2C timing properties
 * @t: the i2c_timings struct to be filled with values
 * @use_defaults: bool to use sane defaults derived from the I2C specification
 *		  when properties are not found, otherwise use 0
 *
 * Scan the device for the generic I2C properties describing timing parameters
 * for the signal and fill the given struct with the results. If a property was
 * not found and use_defaults was true, then maximum timings are assumed which
 * are derived from the I2C specification. If use_defaults is not used, the
 * results will be 0, so drivers can apply their own defaults later. The latter
 * is mainly intended for avoiding regressions of existing drivers which want
 * to switch to this function. New drivers almost always should use the defaults.
 */

void i2c_parse_fw_timings(struct device *dev, struct i2c_timings *t, bool use_defaults)
{
	int ret;

	memset(t, 0, sizeof(*t));

	ret = device_property_read_u32(dev, "clock-frequency", &t->bus_freq_hz);
	if (ret && use_defaults)
		t->bus_freq_hz = 100000;

	ret = device_property_read_u32(dev, "i2c-scl-rising-time-ns", &t->scl_rise_ns);
	if (ret && use_defaults) {
		if (t->bus_freq_hz <= 100000)
			t->scl_rise_ns = 1000;
		else if (t->bus_freq_hz <= 400000)
			t->scl_rise_ns = 300;
		else
			t->scl_rise_ns = 120;
	}

	ret = device_property_read_u32(dev, "i2c-scl-falling-time-ns", &t->scl_fall_ns);
	if (ret && use_defaults) {
		if (t->bus_freq_hz <= 400000)
			t->scl_fall_ns = 300;
		else
			t->scl_fall_ns = 120;
	}

	device_property_read_u32(dev, "i2c-scl-internal-delay-ns", &t->scl_int_delay_ns);

	ret = device_property_read_u32(dev, "i2c-sda-falling-time-ns", &t->sda_fall_ns);
	if (ret && use_defaults)
		t->sda_fall_ns = t->scl_fall_ns;
}
EXPORT_SYMBOL_GPL(i2c_parse_fw_timings);

/* ------------------------------------------------------------------------- */

int i2c_for_each_dev(void *data, int (*fn)(struct device *, void *))
{
	int res;

	mutex_lock(&core_lock);
	res = bus_for_each_dev(&i2c_bus_type, NULL, data, fn);
	mutex_unlock(&core_lock);

	return res;
}
EXPORT_SYMBOL_GPL(i2c_for_each_dev);

static int __process_new_driver(struct device *dev, void *data)
{
	if (dev->type != &i2c_adapter_type)
		return 0;
	return i2c_do_add_adapter(data, to_i2c_adapter(dev));
}

/*
 * An i2c_driver is used with one or more i2c_client (device) nodes to access
 * i2c slave chips, on a bus instance associated with some i2c_adapter.
 */

int i2c_register_driver(struct module *owner, struct i2c_driver *driver)
{
	int res;

	/* Can't register until after driver model init */
	if (WARN_ON(!is_registered))
		return -EAGAIN;

	/* add the driver to the list of i2c drivers in the driver core */
	driver->driver.owner = owner;
	driver->driver.bus = &i2c_bus_type;
	INIT_LIST_HEAD(&driver->clients);

	/* When registration returns, the driver core
	 * will have called probe() for all matching-but-unbound devices.
	 */
	res = driver_register(&driver->driver);
	if (res)
		return res;

	pr_debug("driver [%s] registered\n", driver->driver.name);

	/* Walk the adapters that are already present */
	i2c_for_each_dev(driver, __process_new_driver);

	return 0;
}
EXPORT_SYMBOL(i2c_register_driver);

static int __process_removed_driver(struct device *dev, void *data)
{
	if (dev->type == &i2c_adapter_type)
		i2c_do_del_adapter(data, to_i2c_adapter(dev));
	return 0;
}

/**
 * i2c_del_driver - unregister I2C driver
 * @driver: the driver being unregistered
 * Context: can sleep
 */
void i2c_del_driver(struct i2c_driver *driver)
{
	i2c_for_each_dev(driver, __process_removed_driver);

	driver_unregister(&driver->driver);
	pr_debug("driver [%s] unregistered\n", driver->driver.name);
}
EXPORT_SYMBOL(i2c_del_driver);

/* ------------------------------------------------------------------------- */

/**
 * i2c_use_client - increments the reference count of the i2c client structure
 * @client: the client being referenced
 *
 * Each live reference to a client should be refcounted. The driver model does
 * that automatically as part of driver binding, so that most drivers don't
 * need to do this explicitly: they hold a reference until they're unbound
 * from the device.
 *
 * A pointer to the client with the incremented reference counter is returned.
 */
struct i2c_client *i2c_use_client(struct i2c_client *client)
{
	if (client && get_device(&client->dev))
		return client;
	return NULL;
}
EXPORT_SYMBOL(i2c_use_client);

/**
 * i2c_release_client - release a use of the i2c client structure
 * @client: the client being no longer referenced
 *
 * Must be called when a user of a client is finished with it.
 */
void i2c_release_client(struct i2c_client *client)
{
	if (client)
		put_device(&client->dev);
}
EXPORT_SYMBOL(i2c_release_client);

struct i2c_cmd_arg {
	unsigned	cmd;
	void		*arg;
};

static int i2c_cmd(struct device *dev, void *_arg)
{
	struct i2c_client	*client = i2c_verify_client(dev);
	struct i2c_cmd_arg	*arg = _arg;
	struct i2c_driver	*driver;

	if (!client || !client->dev.driver)
		return 0;

	driver = to_i2c_driver(client->dev.driver);
	if (driver->command)
		driver->command(client, arg->cmd, arg->arg);
	return 0;
}

void i2c_clients_command(struct i2c_adapter *adap, unsigned int cmd, void *arg)
{
	struct i2c_cmd_arg	cmd_arg;

	cmd_arg.cmd = cmd;
	cmd_arg.arg = arg;
	device_for_each_child(&adap->dev, &cmd_arg, i2c_cmd);
}
EXPORT_SYMBOL(i2c_clients_command);

#if IS_ENABLED(CONFIG_OF_DYNAMIC)
static int of_i2c_notify(struct notifier_block *nb, unsigned long action,
			 void *arg)
{
	struct of_reconfig_data *rd = arg;
	struct i2c_adapter *adap;
	struct i2c_client *client;

	switch (of_reconfig_get_state_change(action, rd)) {
	case OF_RECONFIG_CHANGE_ADD:
		adap = of_find_i2c_adapter_by_node(rd->dn->parent);
		if (adap == NULL)
			return NOTIFY_OK;	/* not for us */

		if (of_node_test_and_set_flag(rd->dn, OF_POPULATED)) {
			put_device(&adap->dev);
			return NOTIFY_OK;
		}

		client = of_i2c_register_device(adap, rd->dn);
		put_device(&adap->dev);

		if (IS_ERR(client)) {
			dev_err(&adap->dev, "failed to create client for '%s'\n",
				 rd->dn->full_name);
			of_node_clear_flag(rd->dn, OF_POPULATED);
			return notifier_from_errno(PTR_ERR(client));
		}
		break;
	case OF_RECONFIG_CHANGE_REMOVE:
		/* already depopulated? */
		if (!of_node_check_flag(rd->dn, OF_POPULATED))
			return NOTIFY_OK;

		/* find our device by node */
		client = of_find_i2c_device_by_node(rd->dn);
		if (client == NULL)
			return NOTIFY_OK;	/* no? not meant for us */

		/* unregister takes one ref away */
		i2c_unregister_device(client);

		/* and put the reference of the find */
		put_device(&client->dev);
		break;
	}

	return NOTIFY_OK;
}
static struct notifier_block i2c_of_notifier = {
	.notifier_call = of_i2c_notify,
};
#else
extern struct notifier_block i2c_of_notifier;
#endif /* CONFIG_OF_DYNAMIC */

static int __init i2c_init(void)
{
	int retval;

	retval = of_alias_get_highest_id("i2c");

	down_write(&__i2c_board_lock);
	if (retval >= __i2c_first_dynamic_bus_num)
		__i2c_first_dynamic_bus_num = retval + 1;
	up_write(&__i2c_board_lock);

	retval = bus_register(&i2c_bus_type);
	if (retval)
		return retval;

	is_registered = true;

#ifdef CONFIG_I2C_COMPAT
	i2c_adapter_compat_class = class_compat_register("i2c-adapter");
	if (!i2c_adapter_compat_class) {
		retval = -ENOMEM;
		goto bus_err;
	}
#endif
	retval = i2c_add_driver(&dummy_driver);
	if (retval)
		goto class_err;

	if (IS_ENABLED(CONFIG_OF_DYNAMIC))
		WARN_ON(of_reconfig_notifier_register(&i2c_of_notifier));
	if (IS_ENABLED(CONFIG_ACPI))
		WARN_ON(acpi_reconfig_notifier_register(&i2c_acpi_notifier));

	return 0;

class_err:
#ifdef CONFIG_I2C_COMPAT
	class_compat_unregister(i2c_adapter_compat_class);
bus_err:
#endif
	is_registered = false;
	bus_unregister(&i2c_bus_type);
	return retval;
}

static void __exit i2c_exit(void)
{
	if (IS_ENABLED(CONFIG_ACPI))
		WARN_ON(acpi_reconfig_notifier_unregister(&i2c_acpi_notifier));
	if (IS_ENABLED(CONFIG_OF_DYNAMIC))
		WARN_ON(of_reconfig_notifier_unregister(&i2c_of_notifier));
	i2c_del_driver(&dummy_driver);
#ifdef CONFIG_I2C_COMPAT
	class_compat_unregister(i2c_adapter_compat_class);
#endif
	bus_unregister(&i2c_bus_type);
	tracepoint_synchronize_unregister();
}

/* We must initialize early, because some subsystems register i2c drivers
 * in subsys_initcall() code, but are linked (and initialized) before i2c.
 */
postcore_initcall(i2c_init);
module_exit(i2c_exit);

/* ----------------------------------------------------
 * the functional interface to the i2c busses.
 * ----------------------------------------------------
 */

/* Check if val is exceeding the quirk IFF quirk is non 0 */
#define i2c_quirk_exceeded(val, quirk) ((quirk) && ((val) > (quirk)))

static int i2c_quirk_error(struct i2c_adapter *adap, struct i2c_msg *msg, char *err_msg)
{
	dev_err_ratelimited(&adap->dev, "adapter quirk: %s (addr 0x%04x, size %u, %s)\n",
			    err_msg, msg->addr, msg->len,
			    msg->flags & I2C_M_RD ? "read" : "write");
	return -EOPNOTSUPP;
}

static int i2c_check_for_quirks(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	const struct i2c_adapter_quirks *q = adap->quirks;
	int max_num = q->max_num_msgs, i;
	bool do_len_check = true;

	if (q->flags & I2C_AQ_COMB) {
		max_num = 2;

		/* special checks for combined messages */
		if (num == 2) {
			if (q->flags & I2C_AQ_COMB_WRITE_FIRST && msgs[0].flags & I2C_M_RD)
				return i2c_quirk_error(adap, &msgs[0], "1st comb msg must be write");

			if (q->flags & I2C_AQ_COMB_READ_SECOND && !(msgs[1].flags & I2C_M_RD))
				return i2c_quirk_error(adap, &msgs[1], "2nd comb msg must be read");

			if (q->flags & I2C_AQ_COMB_SAME_ADDR && msgs[0].addr != msgs[1].addr)
				return i2c_quirk_error(adap, &msgs[0], "comb msg only to same addr");

			if (i2c_quirk_exceeded(msgs[0].len, q->max_comb_1st_msg_len))
				return i2c_quirk_error(adap, &msgs[0], "msg too long");

			if (i2c_quirk_exceeded(msgs[1].len, q->max_comb_2nd_msg_len))
				return i2c_quirk_error(adap, &msgs[1], "msg too long");

			do_len_check = false;
		}
	}

	if (i2c_quirk_exceeded(num, max_num))
		return i2c_quirk_error(adap, &msgs[0], "too many messages");

	for (i = 0; i < num; i++) {
		u16 len = msgs[i].len;

		if (msgs[i].flags & I2C_M_RD) {
			if (do_len_check && i2c_quirk_exceeded(len, q->max_read_len))
				return i2c_quirk_error(adap, &msgs[i], "msg too long");
		} else {
			if (do_len_check && i2c_quirk_exceeded(len, q->max_write_len))
				return i2c_quirk_error(adap, &msgs[i], "msg too long");
		}
	}

	return 0;
}

/**
 * __i2c_transfer - unlocked flavor of i2c_transfer
 * @adap: Handle to I2C bus
 * @msgs: One or more messages to execute before STOP is issued to
 *	terminate the operation; each message begins with a START.
 * @num: Number of messages to be executed.
 *
 * Returns negative errno, else the number of messages executed.
 *
 * Adapter lock must be held when calling this function. No debug logging
 * takes place. adap->algo->master_xfer existence isn't checked.
 */
int __i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	unsigned long orig_jiffies;
	int ret, try;

	if (adap->quirks && i2c_check_for_quirks(adap, msgs, num))
		return -EOPNOTSUPP;

	/* i2c_trace_msg gets enabled when tracepoint i2c_transfer gets
	 * enabled.  This is an efficient way of keeping the for-loop from
	 * being executed when not needed.
	 */
	if (static_key_false(&i2c_trace_msg)) {
		int i;
		for (i = 0; i < num; i++)
			if (msgs[i].flags & I2C_M_RD)
				trace_i2c_read(adap, &msgs[i], i);
			else
				trace_i2c_write(adap, &msgs[i], i);
	}

	/* Retry automatically on arbitration loss */
	orig_jiffies = jiffies;
	for (ret = 0, try = 0; try <= adap->retries; try++) {
		ret = adap->algo->master_xfer(adap, msgs, num);
		if (ret != -EAGAIN)
			break;
		if (time_after(jiffies, orig_jiffies + adap->timeout))
			break;
	}

	if (static_key_false(&i2c_trace_msg)) {
		int i;
		for (i = 0; i < ret; i++)
			if (msgs[i].flags & I2C_M_RD)
				trace_i2c_reply(adap, &msgs[i], i);
		trace_i2c_result(adap, i, ret);
	}

	return ret;
}
EXPORT_SYMBOL(__i2c_transfer);

/**
 * i2c_transfer - execute a single or combined I2C message
 * @adap: Handle to I2C bus
 * @msgs: One or more messages to execute before STOP is issued to
 *	terminate the operation; each message begins with a START.
 * @num: Number of messages to be executed.
 *
 * Returns negative errno, else the number of messages executed.
 *
 * Note that there is no requirement that each message be sent to
 * the same slave address, although that is the most common model.
 */
int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int ret;

	/* REVISIT the fault reporting model here is weak:
	 *
	 *  - When we get an error after receiving N bytes from a slave,
	 *    there is no way to report "N".
	 *
	 *  - When we get a NAK after transmitting N bytes to a slave,
	 *    there is no way to report "N" ... or to let the master
	 *    continue executing the rest of this combined message, if
	 *    that's the appropriate response.
	 *
	 *  - When for example "num" is two and we successfully complete
	 *    the first message but get an error part way through the
	 *    second, it's unclear whether that should be reported as
	 *    one (discarding status on the second message) or errno
	 *    (discarding status on the first one).
	 */

	if (adap->algo->master_xfer) {
#ifdef DEBUG
		for (ret = 0; ret < num; ret++) {
			dev_dbg(&adap->dev,
				"master_xfer[%d] %c, addr=0x%02x, len=%d%s\n",
				ret, (msgs[ret].flags & I2C_M_RD) ? 'R' : 'W',
				msgs[ret].addr, msgs[ret].len,
				(msgs[ret].flags & I2C_M_RECV_LEN) ? "+" : "");
		}
#endif

		if (in_atomic() || irqs_disabled()) {
			ret = i2c_trylock_bus(adap, I2C_LOCK_SEGMENT);
			if (!ret)
				/* I2C activity is ongoing. */
				return -EAGAIN;
		} else {
			i2c_lock_bus(adap, I2C_LOCK_SEGMENT);
		}

		ret = __i2c_transfer(adap, msgs, num);
		i2c_unlock_bus(adap, I2C_LOCK_SEGMENT);

		return ret;
	} else {
		dev_dbg(&adap->dev, "I2C level transfers not supported\n");
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL(i2c_transfer);

/**
 * i2c_master_send - issue a single I2C message in master transmit mode
 * @client: Handle to slave device
 * @buf: Data that will be written to the slave
 * @count: How many bytes to write, must be less than 64k since msg.len is u16
 *
 * Returns negative errno, or else the number of bytes written.
 */
int i2c_master_send(const struct i2c_client *client, const char *buf, int count)
{
	int ret;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;

	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (char *)buf;

	ret = i2c_transfer(adap, &msg, 1);

	/*
	 * If everything went ok (i.e. 1 msg transmitted), return #bytes
	 * transmitted, else error code.
	 */
	return (ret == 1) ? count : ret;
}
EXPORT_SYMBOL(i2c_master_send);

/**
 * i2c_master_recv - issue a single I2C message in master receive mode
 * @client: Handle to slave device
 * @buf: Where to store data read from slave
 * @count: How many bytes to read, must be less than 64k since msg.len is u16
 *
 * Returns negative errno, or else the number of bytes read.
 */
int i2c_master_recv(const struct i2c_client *client, char *buf, int count)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = buf;

	ret = i2c_transfer(adap, &msg, 1);

	/*
	 * If everything went ok (i.e. 1 msg received), return #bytes received,
	 * else error code.
	 */
	return (ret == 1) ? count : ret;
}
EXPORT_SYMBOL(i2c_master_recv);

/* ----------------------------------------------------
 * the i2c address scanning function
 * Will not work for 10-bit addresses!
 * ----------------------------------------------------
 */

/*
 * Legacy default probe function, mostly relevant for SMBus. The default
 * probe method is a quick write, but it is known to corrupt the 24RF08
 * EEPROMs due to a state machine bug, and could also irreversibly
 * write-protect some EEPROMs, so for address ranges 0x30-0x37 and 0x50-0x5f,
 * we use a short byte read instead. Also, some bus drivers don't implement
 * quick write, so we fallback to a byte read in that case too.
 * On x86, there is another special case for FSC hardware monitoring chips,
 * which want regular byte reads (address 0x73.) Fortunately, these are the
 * only known chips using this I2C address on PC hardware.
 * Returns 1 if probe succeeded, 0 if not.
 */
static int i2c_default_probe(struct i2c_adapter *adap, unsigned short addr)
{
	int err;
	union i2c_smbus_data dummy;

#ifdef CONFIG_X86
	if (addr == 0x73 && (adap->class & I2C_CLASS_HWMON)
	 && i2c_check_functionality(adap, I2C_FUNC_SMBUS_READ_BYTE_DATA))
		err = i2c_smbus_xfer(adap, addr, 0, I2C_SMBUS_READ, 0,
				     I2C_SMBUS_BYTE_DATA, &dummy);
	else
#endif
	if (!((addr & ~0x07) == 0x30 || (addr & ~0x0f) == 0x50)
	 && i2c_check_functionality(adap, I2C_FUNC_SMBUS_QUICK))
		err = i2c_smbus_xfer(adap, addr, 0, I2C_SMBUS_WRITE, 0,
				     I2C_SMBUS_QUICK, NULL);
	else if (i2c_check_functionality(adap, I2C_FUNC_SMBUS_READ_BYTE))
		err = i2c_smbus_xfer(adap, addr, 0, I2C_SMBUS_READ, 0,
				     I2C_SMBUS_BYTE, &dummy);
	else {
		dev_warn(&adap->dev, "No suitable probing method supported for address 0x%02X\n",
			 addr);
		err = -EOPNOTSUPP;
	}

	return err >= 0;
}

static int i2c_detect_address(struct i2c_client *temp_client,
			      struct i2c_driver *driver)
{
	struct i2c_board_info info;
	struct i2c_adapter *adapter = temp_client->adapter;
	int addr = temp_client->addr;
	int err;

	/* Make sure the address is valid */
	err = i2c_check_7bit_addr_validity_strict(addr);
	if (err) {
		dev_warn(&adapter->dev, "Invalid probe address 0x%02x\n",
			 addr);
		return err;
	}

	/* Skip if already in use (7 bit, no need to encode flags) */
	if (i2c_check_addr_busy(adapter, addr))
		return 0;

	/* Make sure there is something at this address */
	if (!i2c_default_probe(adapter, addr))
		return 0;

	/* Finally call the custom detection function */
	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = addr;
	err = driver->detect(temp_client, &info);
	if (err) {
		/* -ENODEV is returned if the detection fails. We catch it
		   here as this isn't an error. */
		return err == -ENODEV ? 0 : err;
	}

	/* Consistency check */
	if (info.type[0] == '\0') {
		dev_err(&adapter->dev,
			"%s detection function provided no name for 0x%x\n",
			driver->driver.name, addr);
	} else {
		struct i2c_client *client;

		/* Detection succeeded, instantiate the device */
		if (adapter->class & I2C_CLASS_DEPRECATED)
			dev_warn(&adapter->dev,
				"This adapter will soon drop class based instantiation of devices. "
				"Please make sure client 0x%02x gets instantiated by other means. "
				"Check 'Documentation/i2c/instantiating-devices' for details.\n",
				info.addr);

		dev_dbg(&adapter->dev, "Creating %s at 0x%02x\n",
			info.type, info.addr);
		client = i2c_new_device(adapter, &info);
		if (client)
			list_add_tail(&client->detected, &driver->clients);
		else
			dev_err(&adapter->dev, "Failed creating %s at 0x%02x\n",
				info.type, info.addr);
	}
	return 0;
}

static int i2c_detect(struct i2c_adapter *adapter, struct i2c_driver *driver)
{
	const unsigned short *address_list;
	struct i2c_client *temp_client;
	int i, err = 0;
	int adap_id = i2c_adapter_id(adapter);

	address_list = driver->address_list;
	if (!driver->detect || !address_list)
		return 0;

	/* Warn that the adapter lost class based instantiation */
	if (adapter->class == I2C_CLASS_DEPRECATED) {
		dev_dbg(&adapter->dev,
			"This adapter dropped support for I2C classes and won't auto-detect %s devices anymore. "
			"If you need it, check 'Documentation/i2c/instantiating-devices' for alternatives.\n",
			driver->driver.name);
		return 0;
	}

	/* Stop here if the classes do not match */
	if (!(adapter->class & driver->class))
		return 0;

	/* Set up a temporary client to help detect callback */
	temp_client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!temp_client)
		return -ENOMEM;
	temp_client->adapter = adapter;

	for (i = 0; address_list[i] != I2C_CLIENT_END; i += 1) {
		dev_dbg(&adapter->dev,
			"found normal entry for adapter %d, addr 0x%02x\n",
			adap_id, address_list[i]);
		temp_client->addr = address_list[i];
		err = i2c_detect_address(temp_client, driver);
		if (unlikely(err))
			break;
	}

	kfree(temp_client);
	return err;
}

int i2c_probe_func_quick_read(struct i2c_adapter *adap, unsigned short addr)
{
	return i2c_smbus_xfer(adap, addr, 0, I2C_SMBUS_READ, 0,
			      I2C_SMBUS_QUICK, NULL) >= 0;
}
EXPORT_SYMBOL_GPL(i2c_probe_func_quick_read);

struct i2c_client *
i2c_new_probed_device(struct i2c_adapter *adap,
		      struct i2c_board_info *info,
		      unsigned short const *addr_list,
		      int (*probe)(struct i2c_adapter *, unsigned short addr))
{
	int i;

	if (!probe)
		probe = i2c_default_probe;

	for (i = 0; addr_list[i] != I2C_CLIENT_END; i++) {
		/* Check address validity */
		if (i2c_check_7bit_addr_validity_strict(addr_list[i]) < 0) {
			dev_warn(&adap->dev, "Invalid 7-bit address 0x%02x\n",
				 addr_list[i]);
			continue;
		}

		/* Check address availability (7 bit, no need to encode flags) */
		if (i2c_check_addr_busy(adap, addr_list[i])) {
			dev_dbg(&adap->dev,
				"Address 0x%02x already in use, not probing\n",
				addr_list[i]);
			continue;
		}

		/* Test address responsiveness */
		if (probe(adap, addr_list[i]))
			break;
	}

	if (addr_list[i] == I2C_CLIENT_END) {
		dev_dbg(&adap->dev, "Probing failed, no device found\n");
		return NULL;
	}

	info->addr = addr_list[i];
	return i2c_new_device(adap, info);
}
EXPORT_SYMBOL_GPL(i2c_new_probed_device);

struct i2c_adapter *i2c_get_adapter(int nr)
{
	struct i2c_adapter *adapter;

	mutex_lock(&core_lock);
	adapter = idr_find(&i2c_adapter_idr, nr);
	if (!adapter)
		goto exit;

	if (try_module_get(adapter->owner))
		get_device(&adapter->dev);
	else
		adapter = NULL;

 exit:
	mutex_unlock(&core_lock);
	return adapter;
}
EXPORT_SYMBOL(i2c_get_adapter);

void i2c_put_adapter(struct i2c_adapter *adap)
{
	if (!adap)
		return;

	put_device(&adap->dev);
	module_put(adap->owner);
}
EXPORT_SYMBOL(i2c_put_adapter);

/* The SMBus parts */

#define POLY    (0x1070U << 3)
static u8 crc8(u16 data)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (data & 0x8000)
			data = data ^ POLY;
		data = data << 1;
	}
	return (u8)(data >> 8);
}

/* Incremental CRC8 over count bytes in the array pointed to by p */
static u8 i2c_smbus_pec(u8 crc, u8 *p, size_t count)
{
	int i;

	for (i = 0; i < count; i++)
		crc = crc8((crc ^ p[i]) << 8);
	return crc;
}

/* Assume a 7-bit address, which is reasonable for SMBus */
static u8 i2c_smbus_msg_pec(u8 pec, struct i2c_msg *msg)
{
	/* The address will be sent first */
	u8 addr = i2c_8bit_addr_from_msg(msg);
	pec = i2c_smbus_pec(pec, &addr, 1);

	/* The data buffer follows */
	return i2c_smbus_pec(pec, msg->buf, msg->len);
}

/* Used for write only transactions */
static inline void i2c_smbus_add_pec(struct i2c_msg *msg)
{
	msg->buf[msg->len] = i2c_smbus_msg_pec(0, msg);
	msg->len++;
}

/* Return <0 on CRC error
   If there was a write before this read (most cases) we need to take the
   partial CRC from the write part into account.
   Note that this function does modify the message (we need to decrease the
   message length to hide the CRC byte from the caller). */
static int i2c_smbus_check_pec(u8 cpec, struct i2c_msg *msg)
{
	u8 rpec = msg->buf[--msg->len];
	cpec = i2c_smbus_msg_pec(cpec, msg);

	if (rpec != cpec) {
		pr_debug("Bad PEC 0x%02x vs. 0x%02x\n",
			rpec, cpec);
		return -EBADMSG;
	}
	return 0;
}

/**
 * i2c_smbus_read_byte - SMBus "receive byte" protocol
 * @client: Handle to slave device
 *
 * This executes the SMBus "receive byte" protocol, returning negative errno
 * else the byte received from the device.
 */
s32 i2c_smbus_read_byte(const struct i2c_client *client)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
				I2C_SMBUS_READ, 0,
				I2C_SMBUS_BYTE, &data);
	return (status < 0) ? status : data.byte;
}
EXPORT_SYMBOL(i2c_smbus_read_byte);

/**
 * i2c_smbus_write_byte - SMBus "send byte" protocol
 * @client: Handle to slave device
 * @value: Byte to be sent
 *
 * This executes the SMBus "send byte" protocol, returning negative errno
 * else zero on success.
 */
s32 i2c_smbus_write_byte(const struct i2c_client *client, u8 value)
{
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
	                      I2C_SMBUS_WRITE, value, I2C_SMBUS_BYTE, NULL);
}
EXPORT_SYMBOL(i2c_smbus_write_byte);

/**
 * i2c_smbus_read_byte_data - SMBus "read byte" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 *
 * This executes the SMBus "read byte" protocol, returning negative errno
 * else a data byte received from the device.
 */
s32 i2c_smbus_read_byte_data(const struct i2c_client *client, u8 command)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
				I2C_SMBUS_READ, command,
				I2C_SMBUS_BYTE_DATA, &data);
	return (status < 0) ? status : data.byte;
}
EXPORT_SYMBOL(i2c_smbus_read_byte_data);

/**
 * i2c_smbus_write_byte_data - SMBus "write byte" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @value: Byte being written
 *
 * This executes the SMBus "write byte" protocol, returning negative errno
 * else zero on success.
 */
s32 i2c_smbus_write_byte_data(const struct i2c_client *client, u8 command,
			      u8 value)
{
	union i2c_smbus_data data;
	data.byte = value;
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			      I2C_SMBUS_WRITE, command,
			      I2C_SMBUS_BYTE_DATA, &data);
}
EXPORT_SYMBOL(i2c_smbus_write_byte_data);

/**
 * i2c_smbus_read_word_data - SMBus "read word" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 *
 * This executes the SMBus "read word" protocol, returning negative errno
 * else a 16-bit unsigned "word" received from the device.
 */
s32 i2c_smbus_read_word_data(const struct i2c_client *client, u8 command)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
				I2C_SMBUS_READ, command,
				I2C_SMBUS_WORD_DATA, &data);
	return (status < 0) ? status : data.word;
}
EXPORT_SYMBOL(i2c_smbus_read_word_data);

/**
 * i2c_smbus_write_word_data - SMBus "write word" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @value: 16-bit "word" being written
 *
 * This executes the SMBus "write word" protocol, returning negative errno
 * else zero on success.
 */
s32 i2c_smbus_write_word_data(const struct i2c_client *client, u8 command,
			      u16 value)
{
	union i2c_smbus_data data;
	data.word = value;
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			      I2C_SMBUS_WRITE, command,
			      I2C_SMBUS_WORD_DATA, &data);
}
EXPORT_SYMBOL(i2c_smbus_write_word_data);

/**
 * i2c_smbus_read_block_data - SMBus "block read" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @values: Byte array into which data will be read; big enough to hold
 *	the data returned by the slave.  SMBus allows at most 32 bytes.
 *
 * This executes the SMBus "block read" protocol, returning negative errno
 * else the number of data bytes in the slave's response.
 *
 * Note that using this function requires that the client's adapter support
 * the I2C_FUNC_SMBUS_READ_BLOCK_DATA functionality.  Not all adapter drivers
 * support this; its emulation through I2C messaging relies on a specific
 * mechanism (I2C_M_RECV_LEN) which may not be implemented.
 */
s32 i2c_smbus_read_block_data(const struct i2c_client *client, u8 command,
			      u8 *values)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
				I2C_SMBUS_READ, command,
				I2C_SMBUS_BLOCK_DATA, &data);
	if (status)
		return status;

	memcpy(values, &data.block[1], data.block[0]);
	return data.block[0];
}
EXPORT_SYMBOL(i2c_smbus_read_block_data);

/**
 * i2c_smbus_write_block_data - SMBus "block write" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @length: Size of data block; SMBus allows at most 32 bytes
 * @values: Byte array which will be written.
 *
 * This executes the SMBus "block write" protocol, returning negative errno
 * else zero on success.
 */
s32 i2c_smbus_write_block_data(const struct i2c_client *client, u8 command,
			       u8 length, const u8 *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	memcpy(&data.block[1], values, length);
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			      I2C_SMBUS_WRITE, command,
			      I2C_SMBUS_BLOCK_DATA, &data);
}
EXPORT_SYMBOL(i2c_smbus_write_block_data);

/* Returns the number of read bytes */
s32 i2c_smbus_read_i2c_block_data(const struct i2c_client *client, u8 command,
				  u8 length, u8 *values)
{
	union i2c_smbus_data data;
	int status;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
				I2C_SMBUS_READ, command,
				I2C_SMBUS_I2C_BLOCK_DATA, &data);
	if (status < 0)
		return status;

	memcpy(values, &data.block[1], data.block[0]);
	return data.block[0];
}
EXPORT_SYMBOL(i2c_smbus_read_i2c_block_data);

s32 i2c_smbus_write_i2c_block_data(const struct i2c_client *client, u8 command,
				   u8 length, const u8 *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	memcpy(data.block + 1, values, length);
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			      I2C_SMBUS_WRITE, command,
			      I2C_SMBUS_I2C_BLOCK_DATA, &data);
}
EXPORT_SYMBOL(i2c_smbus_write_i2c_block_data);

/* Simulate a SMBus command using the i2c protocol
   No checking of parameters is done!  */
static s32 i2c_smbus_xfer_emulated(struct i2c_adapter *adapter, u16 addr,
				   unsigned short flags,
				   char read_write, u8 command, int size,
				   union i2c_smbus_data *data)
{
	/* So we need to generate a series of msgs. In the case of writing, we
	  need to use only one message; when reading, we need two. We initialize
	  most things with sane defaults, to keep the code below somewhat
	  simpler. */
	unsigned char msgbuf0[I2C_SMBUS_BLOCK_MAX+3];
	unsigned char msgbuf1[I2C_SMBUS_BLOCK_MAX+2];
	int num = read_write == I2C_SMBUS_READ ? 2 : 1;
	int i;
	u8 partial_pec = 0;
	int status;
	struct i2c_msg msg[2] = {
		{
			.addr = addr,
			.flags = flags,
			.len = 1,
			.buf = msgbuf0,
		}, {
			.addr = addr,
			.flags = flags | I2C_M_RD,
			.len = 0,
			.buf = msgbuf1,
		},
	};

	msgbuf0[0] = command;
	switch (size) {
	case I2C_SMBUS_QUICK:
		msg[0].len = 0;
		/* Special case: The read/write field is used as data */
		msg[0].flags = flags | (read_write == I2C_SMBUS_READ ?
					I2C_M_RD : 0);
		num = 1;
		break;
	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_READ) {
			/* Special case: only a read! */
			msg[0].flags = I2C_M_RD | flags;
			num = 1;
		}
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_READ)
			msg[1].len = 1;
		else {
			msg[0].len = 2;
			msgbuf0[1] = data->byte;
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_READ)
			msg[1].len = 2;
		else {
			msg[0].len = 3;
			msgbuf0[1] = data->word & 0xff;
			msgbuf0[2] = data->word >> 8;
		}
		break;
	case I2C_SMBUS_PROC_CALL:
		num = 2; /* Special case */
		read_write = I2C_SMBUS_READ;
		msg[0].len = 3;
		msg[1].len = 2;
		msgbuf0[1] = data->word & 0xff;
		msgbuf0[2] = data->word >> 8;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			msg[1].flags |= I2C_M_RECV_LEN;
			msg[1].len = 1; /* block length will be added by
					   the underlying bus driver */
		} else {
			msg[0].len = data->block[0] + 2;
			if (msg[0].len > I2C_SMBUS_BLOCK_MAX + 2) {
				dev_err(&adapter->dev,
					"Invalid block write size %d\n",
					data->block[0]);
				return -EINVAL;
			}
			for (i = 1; i < msg[0].len; i++)
				msgbuf0[i] = data->block[i-1];
		}
		break;
	case I2C_SMBUS_BLOCK_PROC_CALL:
		num = 2; /* Another special case */
		read_write = I2C_SMBUS_READ;
		if (data->block[0] > I2C_SMBUS_BLOCK_MAX) {
			dev_err(&adapter->dev,
				"Invalid block write size %d\n",
				data->block[0]);
			return -EINVAL;
		}
		msg[0].len = data->block[0] + 2;
		for (i = 1; i < msg[0].len; i++)
			msgbuf0[i] = data->block[i-1];
		msg[1].flags |= I2C_M_RECV_LEN;
		msg[1].len = 1; /* block length will be added by
				   the underlying bus driver */
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			msg[1].len = data->block[0];
		} else {
			msg[0].len = data->block[0] + 1;
			if (msg[0].len > I2C_SMBUS_BLOCK_MAX + 1) {
				dev_err(&adapter->dev,
					"Invalid block write size %d\n",
					data->block[0]);
				return -EINVAL;
			}
			for (i = 1; i <= data->block[0]; i++)
				msgbuf0[i] = data->block[i];
		}
		break;
	default:
		dev_err(&adapter->dev, "Unsupported transaction %d\n", size);
		return -EOPNOTSUPP;
	}

	i = ((flags & I2C_CLIENT_PEC) && size != I2C_SMBUS_QUICK
				      && size != I2C_SMBUS_I2C_BLOCK_DATA);
	if (i) {
		/* Compute PEC if first message is a write */
		if (!(msg[0].flags & I2C_M_RD)) {
			if (num == 1) /* Write only */
				i2c_smbus_add_pec(&msg[0]);
			else /* Write followed by read */
				partial_pec = i2c_smbus_msg_pec(0, &msg[0]);
		}
		/* Ask for PEC if last message is a read */
		if (msg[num-1].flags & I2C_M_RD)
			msg[num-1].len++;
	}

	status = i2c_transfer(adapter, msg, num);
	if (status < 0)
		return status;

	/* Check PEC if last message is a read */
	if (i && (msg[num-1].flags & I2C_M_RD)) {
		status = i2c_smbus_check_pec(partial_pec, &msg[num-1]);
		if (status < 0)
			return status;
	}

	if (read_write == I2C_SMBUS_READ)
		switch (size) {
		case I2C_SMBUS_BYTE:
			data->byte = msgbuf0[0];
			break;
		case I2C_SMBUS_BYTE_DATA:
			data->byte = msgbuf1[0];
			break;
		case I2C_SMBUS_WORD_DATA:
		case I2C_SMBUS_PROC_CALL:
			data->word = msgbuf1[0] | (msgbuf1[1] << 8);
			break;
		case I2C_SMBUS_I2C_BLOCK_DATA:
			for (i = 0; i < data->block[0]; i++)
				data->block[i+1] = msgbuf1[i];
			break;
		case I2C_SMBUS_BLOCK_DATA:
		case I2C_SMBUS_BLOCK_PROC_CALL:
			for (i = 0; i < msgbuf1[0] + 1; i++)
				data->block[i] = msgbuf1[i];
			break;
		}
	return 0;
}

/**
 * i2c_smbus_xfer - execute SMBus protocol operations
 * @adapter: Handle to I2C bus
 * @addr: Address of SMBus slave on that bus
 * @flags: I2C_CLIENT_* flags (usually zero or I2C_CLIENT_PEC)
 * @read_write: I2C_SMBUS_READ or I2C_SMBUS_WRITE
 * @command: Byte interpreted by slave, for protocols which use such bytes
 * @protocol: SMBus protocol operation to execute, such as I2C_SMBUS_PROC_CALL
 * @data: Data to be read or written
 *
 * This executes an SMBus protocol operation, and returns a negative
 * errno code else zero on success.
 */
s32 i2c_smbus_xfer(struct i2c_adapter *adapter, u16 addr, unsigned short flags,
		   char read_write, u8 command, int protocol,
		   union i2c_smbus_data *data)
{
	unsigned long orig_jiffies;
	int try;
	s32 res;

	/* If enabled, the following two tracepoints are conditional on
	 * read_write and protocol.
	 */
	trace_smbus_write(adapter, addr, flags, read_write,
			  command, protocol, data);
	trace_smbus_read(adapter, addr, flags, read_write,
			 command, protocol);

	flags &= I2C_M_TEN | I2C_CLIENT_PEC | I2C_CLIENT_SCCB;

	if (adapter->algo->smbus_xfer) {
		i2c_lock_bus(adapter, I2C_LOCK_SEGMENT);

		/* Retry automatically on arbitration loss */
		orig_jiffies = jiffies;
		for (res = 0, try = 0; try <= adapter->retries; try++) {
			res = adapter->algo->smbus_xfer(adapter, addr, flags,
							read_write, command,
							protocol, data);
			if (res != -EAGAIN)
				break;
			if (time_after(jiffies,
				       orig_jiffies + adapter->timeout))
				break;
		}
		i2c_unlock_bus(adapter, I2C_LOCK_SEGMENT);

		if (res != -EOPNOTSUPP || !adapter->algo->master_xfer)
			goto trace;
		/*
		 * Fall back to i2c_smbus_xfer_emulated if the adapter doesn't
		 * implement native support for the SMBus operation.
		 */
	}

	res = i2c_smbus_xfer_emulated(adapter, addr, flags, read_write,
				      command, protocol, data);

trace:
	/* If enabled, the reply tracepoint is conditional on read_write. */
	trace_smbus_reply(adapter, addr, flags, read_write,
			  command, protocol, data);
	trace_smbus_result(adapter, addr, flags, read_write,
			   command, protocol, res);

	return res;
}
EXPORT_SYMBOL(i2c_smbus_xfer);

/**
 * i2c_smbus_read_i2c_block_data_or_emulated - read block or emulate
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @length: Size of data block; SMBus allows at most I2C_SMBUS_BLOCK_MAX bytes
 * @values: Byte array into which data will be read; big enough to hold
 *	the data returned by the slave.  SMBus allows at most
 *	I2C_SMBUS_BLOCK_MAX bytes.
 *
 * This executes the SMBus "block read" protocol if supported by the adapter.
 * If block read is not supported, it emulates it using either word or byte
 * read protocols depending on availability.
 *
 * The addresses of the I2C slave device that are accessed with this function
 * must be mapped to a linear region, so that a block read will have the same
 * effect as a byte read. Before using this function you must double-check
 * if the I2C slave does support exchanging a block transfer with a byte
 * transfer.
 */
s32 i2c_smbus_read_i2c_block_data_or_emulated(const struct i2c_client *client,
					      u8 command, u8 length, u8 *values)
{
	u8 i = 0;
	int status;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;

	if (i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		return i2c_smbus_read_i2c_block_data(client, command, length, values);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_BYTE_DATA))
		return -EOPNOTSUPP;

	if (i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_WORD_DATA)) {
		while ((i + 2) <= length) {
			status = i2c_smbus_read_word_data(client, command + i);
			if (status < 0)
				return status;
			values[i] = status & 0xff;
			values[i + 1] = status >> 8;
			i += 2;
		}
	}

	while (i < length) {
		status = i2c_smbus_read_byte_data(client, command + i);
		if (status < 0)
			return status;
		values[i] = status;
		i++;
	}

	return i;
}
EXPORT_SYMBOL(i2c_smbus_read_i2c_block_data_or_emulated);

#if IS_ENABLED(CONFIG_I2C_SLAVE)
int i2c_slave_register(struct i2c_client *client, i2c_slave_cb_t slave_cb)
{
	int ret;

	if (!client || !slave_cb) {
		WARN(1, "insufficient data\n");
		return -EINVAL;
	}

	if (!(client->flags & I2C_CLIENT_SLAVE))
		dev_warn(&client->dev, "%s: client slave flag not set. You might see address collisions\n",
			 __func__);

	if (!(client->flags & I2C_CLIENT_TEN)) {
		/* Enforce stricter address checking */
		ret = i2c_check_7bit_addr_validity_strict(client->addr);
		if (ret) {
			dev_err(&client->dev, "%s: invalid address\n", __func__);
			return ret;
		}
	}

	if (!client->adapter->algo->reg_slave) {
		dev_err(&client->dev, "%s: not supported by adapter\n", __func__);
		return -EOPNOTSUPP;
	}

	client->slave_cb = slave_cb;

	i2c_lock_adapter(client->adapter);
	ret = client->adapter->algo->reg_slave(client);
	i2c_unlock_adapter(client->adapter);

	if (ret) {
		client->slave_cb = NULL;
		dev_err(&client->dev, "%s: adapter returned error %d\n", __func__, ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(i2c_slave_register);

int i2c_slave_unregister(struct i2c_client *client)
{
	int ret;

	if (!client->adapter->algo->unreg_slave) {
		dev_err(&client->dev, "%s: not supported by adapter\n", __func__);
		return -EOPNOTSUPP;
	}

	i2c_lock_adapter(client->adapter);
	ret = client->adapter->algo->unreg_slave(client);
	i2c_unlock_adapter(client->adapter);

	if (ret == 0)
		client->slave_cb = NULL;
	else
		dev_err(&client->dev, "%s: adapter returned error %d\n", __func__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(i2c_slave_unregister);
#endif

MODULE_AUTHOR("Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C-Bus main module");
MODULE_LICENSE("GPL");
