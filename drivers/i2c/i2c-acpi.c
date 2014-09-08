/*
 * I2C ACPI code
 *
 * Copyright (C) 2014 Intel Corp
 *
 * Author: Lan Tianyu <tianyu.lan@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#define pr_fmt(fmt) "I2C/ACPI : " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/acpi.h>

struct acpi_i2c_handler_data {
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

static int acpi_i2c_add_resource(struct acpi_resource *ares, void *data)
{
	struct i2c_board_info *info = data;

	if (ares->type == ACPI_RESOURCE_TYPE_SERIAL_BUS) {
		struct acpi_resource_i2c_serialbus *sb;

		sb = &ares->data.i2c_serial_bus;
		if (sb->type == ACPI_RESOURCE_SERIAL_TYPE_I2C) {
			info->addr = sb->slave_address;
			if (sb->access_mode == ACPI_I2C_10BIT_MODE)
				info->flags |= I2C_CLIENT_TEN;
		}
	} else if (info->irq < 0) {
		struct resource r;

		if (acpi_dev_resource_interrupt(ares, 0, &r))
			info->irq = r.start;
	}

	/* Tell the ACPI core to skip this resource */
	return 1;
}

static acpi_status acpi_i2c_add_device(acpi_handle handle, u32 level,
				       void *data, void **return_value)
{
	struct i2c_adapter *adapter = data;
	struct list_head resource_list;
	struct i2c_board_info info;
	struct acpi_device *adev;
	int ret;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;
	if (acpi_bus_get_status(adev) || !adev->status.present)
		return AE_OK;

	memset(&info, 0, sizeof(info));
	info.acpi_node.companion = adev;
	info.irq = -1;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list,
				     acpi_i2c_add_resource, &info);
	acpi_dev_free_resource_list(&resource_list);

	if (ret < 0 || !info.addr)
		return AE_OK;

	adev->power.flags.ignore_parent = true;
	strlcpy(info.type, dev_name(&adev->dev), sizeof(info.type));
	if (!i2c_new_device(adapter, &info)) {
		adev->power.flags.ignore_parent = false;
		dev_err(&adapter->dev,
			"failed to add I2C device %s from ACPI\n",
			dev_name(&adev->dev));
	}

	return AE_OK;
}

/**
 * acpi_i2c_register_devices - enumerate I2C slave devices behind adapter
 * @adap: pointer to adapter
 *
 * Enumerate all I2C slave devices behind this adapter by walking the ACPI
 * namespace. When a device is found it will be added to the Linux device
 * model and bound to the corresponding ACPI handle.
 */
void acpi_i2c_register_devices(struct i2c_adapter *adap)
{
	acpi_handle handle;
	acpi_status status;

	if (!adap->dev.parent)
		return;

	handle = ACPI_HANDLE(adap->dev.parent);
	if (!handle)
		return;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, 1,
				     acpi_i2c_add_device, NULL,
				     adap, NULL);
	if (ACPI_FAILURE(status))
		dev_warn(&adap->dev, "failed to enumerate I2C slaves\n");
}

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
acpi_i2c_space_handler(u32 function, acpi_physical_address command,
			u32 bits, u64 *value64,
			void *handler_context, void *region_context)
{
	struct gsb_buffer *gsb = (struct gsb_buffer *)value64;
	struct acpi_i2c_handler_data *data = handler_context;
	struct acpi_connection_info *info = &data->info;
	struct acpi_resource_i2c_serialbus *sb;
	struct i2c_adapter *adapter = data->adapter;
	struct i2c_client client;
	struct acpi_resource *ares;
	u32 accessor_type = function >> 16;
	u8 action = function & ACPI_IO_MASK;
	acpi_status ret = AE_OK;
	int status;

	ret = acpi_buffer_to_resource(info->connection, info->length, &ares);
	if (ACPI_FAILURE(ret))
		return ret;

	if (!value64 || ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS) {
		ret = AE_BAD_PARAMETER;
		goto err;
	}

	sb = &ares->data.i2c_serial_bus;
	if (sb->type != ACPI_RESOURCE_SERIAL_TYPE_I2C) {
		ret = AE_BAD_PARAMETER;
		goto err;
	}

	memset(&client, 0, sizeof(client));
	client.adapter = adapter;
	client.addr = sb->slave_address;
	client.flags = 0;

	if (sb->access_mode == ACPI_I2C_10BIT_MODE)
		client.flags |= I2C_CLIENT_TEN;

	switch (accessor_type) {
	case ACPI_GSB_ACCESS_ATTRIB_SEND_RCV:
		if (action == ACPI_READ) {
			status = i2c_smbus_read_byte(&client);
			if (status >= 0) {
				gsb->bdata = status;
				status = 0;
			}
		} else {
			status = i2c_smbus_write_byte(&client, gsb->bdata);
		}
		break;

	case ACPI_GSB_ACCESS_ATTRIB_BYTE:
		if (action == ACPI_READ) {
			status = i2c_smbus_read_byte_data(&client, command);
			if (status >= 0) {
				gsb->bdata = status;
				status = 0;
			}
		} else {
			status = i2c_smbus_write_byte_data(&client, command,
					gsb->bdata);
		}
		break;

	case ACPI_GSB_ACCESS_ATTRIB_WORD:
		if (action == ACPI_READ) {
			status = i2c_smbus_read_word_data(&client, command);
			if (status >= 0) {
				gsb->wdata = status;
				status = 0;
			}
		} else {
			status = i2c_smbus_write_word_data(&client, command,
					gsb->wdata);
		}
		break;

	case ACPI_GSB_ACCESS_ATTRIB_BLOCK:
		if (action == ACPI_READ) {
			status = i2c_smbus_read_block_data(&client, command,
					gsb->data);
			if (status >= 0) {
				gsb->len = status;
				status = 0;
			}
		} else {
			status = i2c_smbus_write_block_data(&client, command,
					gsb->len, gsb->data);
		}
		break;

	case ACPI_GSB_ACCESS_ATTRIB_MULTIBYTE:
		if (action == ACPI_READ) {
			status = acpi_gsb_i2c_read_bytes(&client, command,
					gsb->data, info->access_length);
			if (status > 0)
				status = 0;
		} else {
			status = acpi_gsb_i2c_write_bytes(&client, command,
					gsb->data, info->access_length);
		}
		break;

	default:
		pr_info("protocol(0x%02x) is not supported.\n", accessor_type);
		ret = AE_BAD_PARAMETER;
		goto err;
	}

	gsb->status = status;

 err:
	ACPI_FREE(ares);
	return ret;
}


int acpi_i2c_install_space_handler(struct i2c_adapter *adapter)
{
	acpi_handle handle = ACPI_HANDLE(adapter->dev.parent);
	struct acpi_i2c_handler_data *data;
	acpi_status status;

	if (!handle)
		return -ENODEV;

	data = kzalloc(sizeof(struct acpi_i2c_handler_data),
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
				&acpi_i2c_space_handler,
				NULL,
				data);
	if (ACPI_FAILURE(status)) {
		dev_err(&adapter->dev, "Error installing i2c space handler\n");
		acpi_bus_detach_private_data(handle);
		kfree(data);
		return -ENOMEM;
	}

	return 0;
}

void acpi_i2c_remove_space_handler(struct i2c_adapter *adapter)
{
	acpi_handle handle = ACPI_HANDLE(adapter->dev.parent);
	struct acpi_i2c_handler_data *data;
	acpi_status status;

	if (!handle)
		return;

	acpi_remove_address_space_handler(handle,
				ACPI_ADR_SPACE_GSBUS,
				&acpi_i2c_space_handler);

	status = acpi_bus_get_private_data(handle, (void **)&data);
	if (ACPI_SUCCESS(status))
		kfree(data);

	acpi_bus_detach_private_data(handle);
}
#endif
