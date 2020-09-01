// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux I2C core ACPI support code
 *
 * Copyright (C) 2014 Intel Corp, Author: Lan Tianyu <tianyu.lan@intel.com>
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "i2c-core.h"

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
	int n;
	int index;
	u32 speed;
	u32 min_speed;
	u32 force_speed;
};

/**
 * i2c_acpi_get_i2c_resource - Gets I2cSerialBus resource if type matches
 * @ares:	ACPI resource
 * @i2c:	Pointer to I2cSerialBus resource will be returned here
 *
 * Checks if the given ACPI resource is of type I2cSerialBus.
 * In this case, returns a pointer to it to the caller.
 *
 * Returns true if resource type is of I2cSerialBus, otherwise false.
 */
bool i2c_acpi_get_i2c_resource(struct acpi_resource *ares,
			       struct acpi_resource_i2c_serialbus **i2c)
{
	struct acpi_resource_i2c_serialbus *sb;

	if (ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
		return false;

	sb = &ares->data.i2c_serial_bus;
	if (sb->type != ACPI_RESOURCE_SERIAL_TYPE_I2C)
		return false;

	*i2c = sb;
	return true;
}
EXPORT_SYMBOL_GPL(i2c_acpi_get_i2c_resource);

static int i2c_acpi_fill_info(struct acpi_resource *ares, void *data)
{
	struct i2c_acpi_lookup *lookup = data;
	struct i2c_board_info *info = lookup->info;
	struct acpi_resource_i2c_serialbus *sb;
	acpi_status status;

	if (info->addr || !i2c_acpi_get_i2c_resource(ares, &sb))
		return 1;

	if (lookup->index != -1 && lookup->n++ != lookup->index)
		return 1;

	status = acpi_get_handle(lookup->device_handle,
				 sb->resource_source.string_ptr,
				 &lookup->adapter_handle);
	if (ACPI_FAILURE(status))
		return 1;

	info->addr = sb->slave_address;
	lookup->speed = sb->connection_speed;
	if (sb->access_mode == ACPI_I2C_10BIT_MODE)
		info->flags |= I2C_CLIENT_TEN;

	return 1;
}

static const struct acpi_device_id i2c_acpi_ignored_device_ids[] = {
	/*
	 * ACPI video acpi_devices, which are handled by the acpi-video driver
	 * sometimes contain a SERIAL_TYPE_I2C ACPI resource, ignore these.
	 */
	{ ACPI_VIDEO_HID, 0 },
	{}
};

static int i2c_acpi_do_lookup(struct acpi_device *adev,
			      struct i2c_acpi_lookup *lookup)
{
	struct i2c_board_info *info = lookup->info;
	struct list_head resource_list;
	int ret;

	if (acpi_bus_get_status(adev) || !adev->status.present)
		return -EINVAL;

	if (acpi_match_device_ids(adev, i2c_acpi_ignored_device_ids) == 0)
		return -ENODEV;

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

static int i2c_acpi_add_resource(struct acpi_resource *ares, void *data)
{
	int *irq = data;
	struct resource r;

	if (*irq <= 0 && acpi_dev_resource_interrupt(ares, 0, &r))
		*irq = i2c_dev_irq_from_resources(&r, 1);

	return 1; /* No need to add resource to the list */
}

/**
 * i2c_acpi_get_irq - get device IRQ number from ACPI
 * @client: Pointer to the I2C client device
 *
 * Find the IRQ number used by a specific client device.
 *
 * Return: The IRQ number or an error code.
 */
int i2c_acpi_get_irq(struct i2c_client *client)
{
	struct acpi_device *adev = ACPI_COMPANION(&client->dev);
	struct list_head resource_list;
	int irq = -ENOENT;
	int ret;

	INIT_LIST_HEAD(&resource_list);

	ret = acpi_dev_get_resources(adev, &resource_list,
				     i2c_acpi_add_resource, &irq);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&resource_list);

	if (irq == -ENOENT)
		irq = acpi_dev_gpio_irq_get(adev, 0);

	return irq;
}

static int i2c_acpi_get_info(struct acpi_device *adev,
			     struct i2c_board_info *info,
			     struct i2c_adapter *adapter,
			     acpi_handle *adapter_handle)
{
	struct i2c_acpi_lookup lookup;
	int ret;

	memset(&lookup, 0, sizeof(lookup));
	lookup.info = info;
	lookup.index = -1;

	if (acpi_device_enumerated(adev))
		return -EINVAL;

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

	if (IS_ERR(i2c_new_client_device(adapter, info))) {
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
void i2c_acpi_register_devices(struct i2c_adapter *adap)
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

static const struct acpi_device_id i2c_acpi_force_400khz_device_ids[] = {
	/*
	 * These Silead touchscreen controllers only work at 400KHz, for
	 * some reason they do not work at 100KHz. On some devices the ACPI
	 * tables list another device at their bus as only being capable
	 * of 100KHz, testing has shown that these other devices work fine
	 * at 400KHz (as can be expected of any recent i2c hw) so we force
	 * the speed of the bus to 400 KHz if a Silead device is present.
	 */
	{ "MSSL1680", 0 },
	{}
};

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

	if (acpi_match_device_ids(adev, i2c_acpi_force_400khz_device_ids) == 0)
		lookup->force_speed = I2C_MAX_FAST_MODE_FREQ;

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
	lookup.index = -1;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				     I2C_ACPI_MAX_SCAN_DEPTH,
				     i2c_acpi_lookup_speed, NULL,
				     &lookup, NULL);

	if (ACPI_FAILURE(status)) {
		dev_warn(dev, "unable to find I2C bus speed from ACPI\n");
		return 0;
	}

	if (lookup.force_speed) {
		if (lookup.force_speed != lookup.min_speed)
			dev_warn(dev, FW_BUG "DSDT uses known not-working I2C bus speed %d, forcing it to %d\n",
				 lookup.min_speed, lookup.force_speed);
		return lookup.force_speed;
	} else if (lookup.min_speed != UINT_MAX) {
		return lookup.min_speed;
	} else {
		return 0;
	}
}
EXPORT_SYMBOL_GPL(i2c_acpi_find_bus_speed);

static int i2c_acpi_find_match_adapter(struct device *dev, const void *data)
{
	struct i2c_adapter *adapter = i2c_verify_adapter(dev);

	if (!adapter)
		return 0;

	return ACPI_HANDLE(dev) == (acpi_handle)data;
}

struct i2c_adapter *i2c_acpi_find_adapter_by_handle(acpi_handle handle)
{
	struct device *dev;

	dev = bus_find_device(&i2c_bus_type, NULL, handle,
			      i2c_acpi_find_match_adapter);

	return dev ? i2c_verify_adapter(dev) : NULL;
}
EXPORT_SYMBOL_GPL(i2c_acpi_find_adapter_by_handle);

static struct i2c_client *i2c_acpi_find_client_by_adev(struct acpi_device *adev)
{
	struct device *dev;
	struct i2c_client *client;

	dev = bus_find_device_by_acpi_dev(&i2c_bus_type, adev);
	if (!dev)
		return NULL;

	client = i2c_verify_client(dev);
	if (!client)
		put_device(dev);

	return client;
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

struct notifier_block i2c_acpi_notifier = {
	.notifier_call = i2c_acpi_notify,
};

/**
 * i2c_acpi_new_device - Create i2c-client for the Nth I2cSerialBus resource
 * @dev:     Device owning the ACPI resources to get the client from
 * @index:   Index of ACPI resource to get
 * @info:    describes the I2C device; note this is modified (addr gets set)
 * Context: can sleep
 *
 * By default the i2c subsys creates an i2c-client for the first I2cSerialBus
 * resource of an acpi_device, but some acpi_devices have multiple I2cSerialBus
 * resources, in that case this function can be used to create an i2c-client
 * for other I2cSerialBus resources in the Current Resource Settings table.
 *
 * Also see i2c_new_client_device, which this function calls to create the
 * i2c-client.
 *
 * Returns a pointer to the new i2c-client, or error pointer in case of failure.
 * Specifically, -EPROBE_DEFER is returned if the adapter is not found.
 */
struct i2c_client *i2c_acpi_new_device(struct device *dev, int index,
				       struct i2c_board_info *info)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct i2c_acpi_lookup lookup;
	struct i2c_adapter *adapter;
	LIST_HEAD(resource_list);
	int ret;

	memset(&lookup, 0, sizeof(lookup));
	lookup.info = info;
	lookup.device_handle = acpi_device_handle(adev);
	lookup.index = index;

	ret = acpi_dev_get_resources(adev, &resource_list,
				     i2c_acpi_fill_info, &lookup);
	if (ret < 0)
		return ERR_PTR(ret);

	acpi_dev_free_resource_list(&resource_list);

	if (!info->addr)
		return ERR_PTR(-EADDRNOTAVAIL);

	adapter = i2c_acpi_find_adapter_by_handle(lookup.adapter_handle);
	if (!adapter)
		return ERR_PTR(-EPROBE_DEFER);

	return i2c_new_client_device(adapter, info);
}
EXPORT_SYMBOL_GPL(i2c_acpi_new_device);

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
	if (ret < 0) {
		/* Getting a NACK is unfortunately normal with some DSTDs */
		if (ret == -EREMOTEIO)
			dev_dbg(&client->adapter->dev, "i2c read %d bytes from client@%#x starting at reg %#x failed, error: %d\n",
				data_len, client->addr, cmd, ret);
		else
			dev_err(&client->adapter->dev, "i2c read %d bytes from client@%#x starting at reg %#x failed, error: %d\n",
				data_len, client->addr, cmd, ret);
	/* 2 transfers must have completed successfully */
	} else if (ret == 2) {
		memcpy(data, buffer, data_len);
		ret = 0;
	} else {
		ret = -EIO;
	}

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

	kfree(buffer);

	if (ret < 0) {
		dev_err(&client->adapter->dev, "i2c write failed: %d\n", ret);
		return ret;
	}

	/* 1 transfer must have completed successfully */
	return (ret == 1) ? 0 : -EIO;
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

	if (!value64 || !i2c_acpi_get_i2c_resource(ares, &sb)) {
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


int i2c_acpi_install_space_handler(struct i2c_adapter *adapter)
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

void i2c_acpi_remove_space_handler(struct i2c_adapter *adapter)
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
#endif /* CONFIG_ACPI_I2C_OPREGION */
