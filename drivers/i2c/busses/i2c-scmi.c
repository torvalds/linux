// SPDX-License-Identifier: GPL-2.0-only
/*
 * SMBus driver for ACPI SMBus CMI
 *
 * Copyright (C) 2009 Crane Cai <crane.cai@amd.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/i2c.h>
#include <linux/acpi.h>

/* SMBUS HID definition as supported by Microsoft Windows */
#define ACPI_SMBUS_MS_HID		"SMB0001"

struct smbus_methods_t {
	char *mt_info;
	char *mt_sbr;
	char *mt_sbw;
};

struct acpi_smbus_cmi {
	acpi_handle handle;
	struct i2c_adapter adapter;
	u8 cap_info:1;
	u8 cap_read:1;
	u8 cap_write:1;
	const struct smbus_methods_t *methods;
};

static const struct smbus_methods_t smbus_methods = {
	.mt_info = "_SBI",
	.mt_sbr  = "_SBR",
	.mt_sbw  = "_SBW",
};

/* Some IBM BIOSes omit the leading underscore */
static const struct smbus_methods_t ibm_smbus_methods = {
	.mt_info = "SBI_",
	.mt_sbr  = "SBR_",
	.mt_sbw  = "SBW_",
};

static const struct acpi_device_id acpi_smbus_cmi_ids[] = {
	{"SMBUS01", (kernel_ulong_t)&smbus_methods},
	{ACPI_SMBUS_IBM_HID, (kernel_ulong_t)&ibm_smbus_methods},
	{ACPI_SMBUS_MS_HID, (kernel_ulong_t)&smbus_methods},
	{"", 0}
};
MODULE_DEVICE_TABLE(acpi, acpi_smbus_cmi_ids);

#define ACPI_SMBUS_STATUS_OK			0x00
#define ACPI_SMBUS_STATUS_FAIL			0x07
#define ACPI_SMBUS_STATUS_DNAK			0x10
#define ACPI_SMBUS_STATUS_DERR			0x11
#define ACPI_SMBUS_STATUS_CMD_DENY		0x12
#define ACPI_SMBUS_STATUS_UNKNOWN		0x13
#define ACPI_SMBUS_STATUS_ACC_DENY		0x17
#define ACPI_SMBUS_STATUS_TIMEOUT		0x18
#define ACPI_SMBUS_STATUS_NOTSUP		0x19
#define ACPI_SMBUS_STATUS_BUSY			0x1a
#define ACPI_SMBUS_STATUS_PEC			0x1f

#define ACPI_SMBUS_PRTCL_WRITE			0x00
#define ACPI_SMBUS_PRTCL_READ			0x01
#define ACPI_SMBUS_PRTCL_QUICK			0x02
#define ACPI_SMBUS_PRTCL_BYTE			0x04
#define ACPI_SMBUS_PRTCL_BYTE_DATA		0x06
#define ACPI_SMBUS_PRTCL_WORD_DATA		0x08
#define ACPI_SMBUS_PRTCL_BLOCK_DATA		0x0a


static int
acpi_smbus_cmi_access(struct i2c_adapter *adap, u16 addr, unsigned short flags,
		   char read_write, u8 command, int size,
		   union i2c_smbus_data *data)
{
	int result = 0;
	struct acpi_smbus_cmi *smbus_cmi = adap->algo_data;
	unsigned char protocol;
	acpi_status status = 0;
	struct acpi_object_list input;
	union acpi_object mt_params[5];
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	union acpi_object *pkg;
	char *method;
	int len = 0;

	dev_dbg(&adap->dev, "access size: %d %s\n", size,
		(read_write) ? "READ" : "WRITE");
	switch (size) {
	case I2C_SMBUS_QUICK:
		protocol = ACPI_SMBUS_PRTCL_QUICK;
		command = 0;
		if (read_write == I2C_SMBUS_WRITE) {
			mt_params[3].type = ACPI_TYPE_INTEGER;
			mt_params[3].integer.value = 0;
			mt_params[4].type = ACPI_TYPE_INTEGER;
			mt_params[4].integer.value = 0;
		}
		break;

	case I2C_SMBUS_BYTE:
		protocol = ACPI_SMBUS_PRTCL_BYTE;
		if (read_write == I2C_SMBUS_WRITE) {
			mt_params[3].type = ACPI_TYPE_INTEGER;
			mt_params[3].integer.value = 0;
			mt_params[4].type = ACPI_TYPE_INTEGER;
			mt_params[4].integer.value = 0;
		} else {
			command = 0;
		}
		break;

	case I2C_SMBUS_BYTE_DATA:
		protocol = ACPI_SMBUS_PRTCL_BYTE_DATA;
		if (read_write == I2C_SMBUS_WRITE) {
			mt_params[3].type = ACPI_TYPE_INTEGER;
			mt_params[3].integer.value = 1;
			mt_params[4].type = ACPI_TYPE_INTEGER;
			mt_params[4].integer.value = data->byte;
		}
		break;

	case I2C_SMBUS_WORD_DATA:
		protocol = ACPI_SMBUS_PRTCL_WORD_DATA;
		if (read_write == I2C_SMBUS_WRITE) {
			mt_params[3].type = ACPI_TYPE_INTEGER;
			mt_params[3].integer.value = 2;
			mt_params[4].type = ACPI_TYPE_INTEGER;
			mt_params[4].integer.value = data->word;
		}
		break;

	case I2C_SMBUS_BLOCK_DATA:
		protocol = ACPI_SMBUS_PRTCL_BLOCK_DATA;
		if (read_write == I2C_SMBUS_WRITE) {
			len = data->block[0];
			if (len == 0 || len > I2C_SMBUS_BLOCK_MAX)
				return -EINVAL;
			mt_params[3].type = ACPI_TYPE_INTEGER;
			mt_params[3].integer.value = len;
			mt_params[4].type = ACPI_TYPE_BUFFER;
			mt_params[4].buffer.length = len;
			mt_params[4].buffer.pointer = data->block + 1;
		}
		break;

	default:
		dev_warn(&adap->dev, "Unsupported transaction %d\n", size);
		return -EOPNOTSUPP;
	}

	if (read_write == I2C_SMBUS_READ) {
		protocol |= ACPI_SMBUS_PRTCL_READ;
		method = smbus_cmi->methods->mt_sbr;
		input.count = 3;
	} else {
		protocol |= ACPI_SMBUS_PRTCL_WRITE;
		method = smbus_cmi->methods->mt_sbw;
		input.count = 5;
	}

	input.pointer = mt_params;
	mt_params[0].type = ACPI_TYPE_INTEGER;
	mt_params[0].integer.value = protocol;
	mt_params[1].type = ACPI_TYPE_INTEGER;
	mt_params[1].integer.value = addr;
	mt_params[2].type = ACPI_TYPE_INTEGER;
	mt_params[2].integer.value = command;

	status = acpi_evaluate_object(smbus_cmi->handle, method, &input,
				      &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(smbus_cmi->handle,
				"Failed to evaluate %s: %i\n", method, status);
		return -EIO;
	}

	pkg = buffer.pointer;
	if (pkg && pkg->type == ACPI_TYPE_PACKAGE)
		obj = pkg->package.elements;
	else {
		acpi_handle_err(smbus_cmi->handle, "Invalid argument type\n");
		result = -EIO;
		goto out;
	}
	if (obj == NULL || obj->type != ACPI_TYPE_INTEGER) {
		acpi_handle_err(smbus_cmi->handle, "Invalid argument type\n");
		result = -EIO;
		goto out;
	}

	result = obj->integer.value;
	acpi_handle_debug(smbus_cmi->handle,  "%s return status: %i\n", method,
			  result);

	switch (result) {
	case ACPI_SMBUS_STATUS_OK:
		result = 0;
		break;
	case ACPI_SMBUS_STATUS_BUSY:
		result = -EBUSY;
		goto out;
	case ACPI_SMBUS_STATUS_TIMEOUT:
		result = -ETIMEDOUT;
		goto out;
	case ACPI_SMBUS_STATUS_DNAK:
		result = -ENXIO;
		goto out;
	default:
		result = -EIO;
		goto out;
	}

	if (read_write == I2C_SMBUS_WRITE || size == I2C_SMBUS_QUICK)
		goto out;

	obj = pkg->package.elements + 1;
	if (obj->type != ACPI_TYPE_INTEGER) {
		acpi_handle_err(smbus_cmi->handle, "Invalid argument type\n");
		result = -EIO;
		goto out;
	}

	len = obj->integer.value;
	obj = pkg->package.elements + 2;
	switch (size) {
	case I2C_SMBUS_BYTE:
	case I2C_SMBUS_BYTE_DATA:
	case I2C_SMBUS_WORD_DATA:
		if (obj->type != ACPI_TYPE_INTEGER) {
			acpi_handle_err(smbus_cmi->handle,
					"Invalid argument type\n");
			result = -EIO;
			goto out;
		}
		if (len == 2)
			data->word = obj->integer.value;
		else
			data->byte = obj->integer.value;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (obj->type != ACPI_TYPE_BUFFER) {
			acpi_handle_err(smbus_cmi->handle,
					"Invalid argument type\n");
			result = -EIO;
			goto out;
		}
		if (len == 0 || len > I2C_SMBUS_BLOCK_MAX)
			return -EPROTO;
		data->block[0] = len;
		memcpy(data->block + 1, obj->buffer.pointer, len);
		break;
	}

out:
	kfree(buffer.pointer);
	dev_dbg(&adap->dev, "Transaction status: %i\n", result);
	return result;
}

static u32 acpi_smbus_cmi_func(struct i2c_adapter *adapter)
{
	struct acpi_smbus_cmi *smbus_cmi = adapter->algo_data;
	u32 ret;

	ret = smbus_cmi->cap_read | smbus_cmi->cap_write ?
		I2C_FUNC_SMBUS_QUICK : 0;

	ret |= smbus_cmi->cap_read ?
		(I2C_FUNC_SMBUS_READ_BYTE |
		I2C_FUNC_SMBUS_READ_BYTE_DATA |
		I2C_FUNC_SMBUS_READ_WORD_DATA |
		I2C_FUNC_SMBUS_READ_BLOCK_DATA) : 0;

	ret |= smbus_cmi->cap_write ?
		(I2C_FUNC_SMBUS_WRITE_BYTE |
		I2C_FUNC_SMBUS_WRITE_BYTE_DATA |
		I2C_FUNC_SMBUS_WRITE_WORD_DATA |
		I2C_FUNC_SMBUS_WRITE_BLOCK_DATA) : 0;

	return ret;
}

static const struct i2c_algorithm acpi_smbus_cmi_algorithm = {
	.smbus_xfer = acpi_smbus_cmi_access,
	.functionality = acpi_smbus_cmi_func,
};


static int acpi_smbus_cmi_add_cap(struct acpi_smbus_cmi *smbus_cmi,
				  const char *name)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_handle *handle = smbus_cmi->handle;
	union acpi_object *obj;
	acpi_status status;

	if (!strcmp(name, smbus_cmi->methods->mt_info)) {
		status = acpi_evaluate_object(smbus_cmi->handle,
					smbus_cmi->methods->mt_info,
					NULL, &buffer);
		if (ACPI_FAILURE(status)) {
			acpi_handle_err(handle, "Failed to evaluate %s: %i\n",
					smbus_cmi->methods->mt_info, status);
			return -EIO;
		}

		obj = buffer.pointer;
		if (obj && obj->type == ACPI_TYPE_PACKAGE)
			obj = obj->package.elements;
		else {
			acpi_handle_err(handle, "Invalid argument type\n");
			kfree(buffer.pointer);
			return -EIO;
		}

		if (obj->type != ACPI_TYPE_INTEGER) {
			acpi_handle_err(handle, "Invalid argument type\n");
			kfree(buffer.pointer);
			return -EIO;
		} else
			acpi_handle_debug(handle, "SMBus CMI Version %x\n",
					  (int)obj->integer.value);

		kfree(buffer.pointer);
		smbus_cmi->cap_info = 1;
	} else if (!strcmp(name, smbus_cmi->methods->mt_sbr))
		smbus_cmi->cap_read = 1;
	else if (!strcmp(name, smbus_cmi->methods->mt_sbw))
		smbus_cmi->cap_write = 1;
	else
		acpi_handle_debug(handle, "Unsupported CMI method: %s\n", name);

	return 0;
}

static acpi_status acpi_smbus_cmi_query_methods(acpi_handle handle, u32 level,
			void *context, void **return_value)
{
	char node_name[5];
	struct acpi_buffer buffer = { sizeof(node_name), node_name };
	struct acpi_smbus_cmi *smbus_cmi = context;
	acpi_status status;

	status = acpi_get_name(handle, ACPI_SINGLE_NAME, &buffer);

	if (ACPI_SUCCESS(status))
		acpi_smbus_cmi_add_cap(smbus_cmi, node_name);

	return AE_OK;
}

static int smbus_cmi_probe(struct platform_device *device)
{
	struct device *dev = &device->dev;
	struct acpi_smbus_cmi *smbus_cmi;
	int ret;

	smbus_cmi = kzalloc(sizeof(struct acpi_smbus_cmi), GFP_KERNEL);
	if (!smbus_cmi)
		return -ENOMEM;

	smbus_cmi->handle = ACPI_HANDLE(dev);
	smbus_cmi->methods = device_get_match_data(dev);

	platform_set_drvdata(device, smbus_cmi);

	smbus_cmi->cap_info = 0;
	smbus_cmi->cap_read = 0;
	smbus_cmi->cap_write = 0;

	acpi_walk_namespace(ACPI_TYPE_METHOD, smbus_cmi->handle, 1,
			    acpi_smbus_cmi_query_methods, NULL, smbus_cmi, NULL);

	if (smbus_cmi->cap_info == 0) {
		ret = -ENODEV;
		goto err;
	}

	snprintf(smbus_cmi->adapter.name, sizeof(smbus_cmi->adapter.name),
		 "SMBus CMI adapter %s", dev_name(dev));
	smbus_cmi->adapter.owner = THIS_MODULE;
	smbus_cmi->adapter.algo = &acpi_smbus_cmi_algorithm;
	smbus_cmi->adapter.algo_data = smbus_cmi;
	smbus_cmi->adapter.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	smbus_cmi->adapter.dev.parent = &device->dev;

	ret = i2c_add_adapter(&smbus_cmi->adapter);
	if (ret) {
		dev_err(&device->dev, "Couldn't register adapter!\n");
		goto err;
	}

	return 0;

err:
	kfree(smbus_cmi);
	return ret;
}

static int smbus_cmi_remove(struct platform_device *device)
{
	struct acpi_smbus_cmi *smbus_cmi = platform_get_drvdata(device);

	i2c_del_adapter(&smbus_cmi->adapter);
	kfree(smbus_cmi);

	return 0;
}

static struct platform_driver smbus_cmi_driver = {
	.probe = smbus_cmi_probe,
	.remove = smbus_cmi_remove,
	.driver = {
		.name   = "smbus_cmi",
		.acpi_match_table = acpi_smbus_cmi_ids,
	},
};
module_platform_driver(smbus_cmi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Crane Cai <crane.cai@amd.com>");
MODULE_DESCRIPTION("ACPI SMBus CMI driver");
