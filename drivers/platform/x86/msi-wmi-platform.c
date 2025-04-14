// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux driver for WMI platform features on MSI notebooks.
 *
 * Copyright (C) 2024 Armin Wolf <W_Armin@gmx.de>
 */

#define pr_format(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/device/driver.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include <linux/unaligned.h>

#define DRIVER_NAME	"msi-wmi-platform"

#define MSI_PLATFORM_GUID	"ABBC0F6E-8EA1-11d1-00A0-C90629100000"

#define MSI_WMI_PLATFORM_INTERFACE_VERSION	2

#define MSI_PLATFORM_WMI_MAJOR_OFFSET	1
#define MSI_PLATFORM_WMI_MINOR_OFFSET	2

#define MSI_PLATFORM_EC_FLAGS_OFFSET	1
#define MSI_PLATFORM_EC_MINOR_MASK	GENMASK(3, 0)
#define MSI_PLATFORM_EC_MAJOR_MASK	GENMASK(5, 4)
#define MSI_PLATFORM_EC_CHANGED_PAGE	BIT(6)
#define MSI_PLATFORM_EC_IS_TIGERLAKE	BIT(7)
#define MSI_PLATFORM_EC_VERSION_OFFSET	2

static bool force;
module_param_unsafe(force, bool, 0);
MODULE_PARM_DESC(force, "Force loading without checking for supported WMI interface versions");

enum msi_wmi_platform_method {
	MSI_PLATFORM_GET_PACKAGE	= 0x01,
	MSI_PLATFORM_SET_PACKAGE	= 0x02,
	MSI_PLATFORM_GET_EC		= 0x03,
	MSI_PLATFORM_SET_EC		= 0x04,
	MSI_PLATFORM_GET_BIOS		= 0x05,
	MSI_PLATFORM_SET_BIOS		= 0x06,
	MSI_PLATFORM_GET_SMBUS		= 0x07,
	MSI_PLATFORM_SET_SMBUS		= 0x08,
	MSI_PLATFORM_GET_MASTER_BATTERY = 0x09,
	MSI_PLATFORM_SET_MASTER_BATTERY = 0x0a,
	MSI_PLATFORM_GET_SLAVE_BATTERY	= 0x0b,
	MSI_PLATFORM_SET_SLAVE_BATTERY	= 0x0c,
	MSI_PLATFORM_GET_TEMPERATURE	= 0x0d,
	MSI_PLATFORM_SET_TEMPERATURE	= 0x0e,
	MSI_PLATFORM_GET_THERMAL	= 0x0f,
	MSI_PLATFORM_SET_THERMAL	= 0x10,
	MSI_PLATFORM_GET_FAN		= 0x11,
	MSI_PLATFORM_SET_FAN		= 0x12,
	MSI_PLATFORM_GET_DEVICE		= 0x13,
	MSI_PLATFORM_SET_DEVICE		= 0x14,
	MSI_PLATFORM_GET_POWER		= 0x15,
	MSI_PLATFORM_SET_POWER		= 0x16,
	MSI_PLATFORM_GET_DEBUG		= 0x17,
	MSI_PLATFORM_SET_DEBUG		= 0x18,
	MSI_PLATFORM_GET_AP		= 0x19,
	MSI_PLATFORM_SET_AP		= 0x1a,
	MSI_PLATFORM_GET_DATA		= 0x1b,
	MSI_PLATFORM_SET_DATA		= 0x1c,
	MSI_PLATFORM_GET_WMI		= 0x1d,
};

struct msi_wmi_platform_data {
	struct wmi_device *wdev;
	struct mutex wmi_lock;	/* Necessary when calling WMI methods */
};

struct msi_wmi_platform_debugfs_data {
	struct msi_wmi_platform_data *data;
	enum msi_wmi_platform_method method;
	struct rw_semaphore buffer_lock;	/* Protects debugfs buffer */
	size_t length;
	u8 buffer[32];
};

static const char * const msi_wmi_platform_debugfs_names[] = {
	"get_package",
	"set_package",
	"get_ec",
	"set_ec",
	"get_bios",
	"set_bios",
	"get_smbus",
	"set_smbus",
	"get_master_battery",
	"set_master_battery",
	"get_slave_battery",
	"set_slave_battery",
	"get_temperature",
	"set_temperature",
	"get_thermal",
	"set_thermal",
	"get_fan",
	"set_fan",
	"get_device",
	"set_device",
	"get_power",
	"set_power",
	"get_debug",
	"set_debug",
	"get_ap",
	"set_ap",
	"get_data",
	"set_data",
	"get_wmi"
};

static int msi_wmi_platform_parse_buffer(union acpi_object *obj, u8 *output, size_t length)
{
	if (obj->type != ACPI_TYPE_BUFFER)
		return -ENOMSG;

	if (obj->buffer.length != length)
		return -EPROTO;

	if (!obj->buffer.pointer[0])
		return -EIO;

	memcpy(output, obj->buffer.pointer, obj->buffer.length);

	return 0;
}

static int msi_wmi_platform_query(struct msi_wmi_platform_data *data,
				  enum msi_wmi_platform_method method, u8 *input,
				  size_t input_length, u8 *output, size_t output_length)
{
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer in = {
		.length = input_length,
		.pointer = input
	};
	union acpi_object *obj;
	acpi_status status;
	int ret;

	if (!input_length || !output_length)
		return -EINVAL;

	/*
	 * The ACPI control method responsible for handling the WMI method calls
	 * is not thread-safe. Because of this we have to do the locking ourself.
	 */
	scoped_guard(mutex, &data->wmi_lock) {
		status = wmidev_evaluate_method(data->wdev, 0x0, method, &in, &out);
		if (ACPI_FAILURE(status))
			return -EIO;
	}

	obj = out.pointer;
	if (!obj)
		return -ENODATA;

	ret = msi_wmi_platform_parse_buffer(obj, output, output_length);
	kfree(obj);

	return ret;
}

static umode_t msi_wmi_platform_is_visible(const void *drvdata, enum hwmon_sensor_types type,
					   u32 attr, int channel)
{
	return 0444;
}

static int msi_wmi_platform_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
				 int channel, long *val)
{
	struct msi_wmi_platform_data *data = dev_get_drvdata(dev);
	u8 input[32] = { 0 };
	u8 output[32];
	u16 value;
	int ret;

	ret = msi_wmi_platform_query(data, MSI_PLATFORM_GET_FAN, input, sizeof(input), output,
				     sizeof(output));
	if (ret < 0)
		return ret;

	value = get_unaligned_be16(&output[channel * 2 + 1]);
	if (!value)
		*val = 0;
	else
		*val = 480000 / value;

	return 0;
}

static const struct hwmon_ops msi_wmi_platform_ops = {
	.is_visible = msi_wmi_platform_is_visible,
	.read = msi_wmi_platform_read,
};

static const struct hwmon_channel_info * const msi_wmi_platform_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT
			   ),
	NULL
};

static const struct hwmon_chip_info msi_wmi_platform_chip_info = {
	.ops = &msi_wmi_platform_ops,
	.info = msi_wmi_platform_info,
};

static ssize_t msi_wmi_platform_write(struct file *fp, const char __user *input, size_t length,
				      loff_t *offset)
{
	struct seq_file *seq = fp->private_data;
	struct msi_wmi_platform_debugfs_data *data = seq->private;
	u8 payload[32] = { };
	ssize_t ret;

	/* Do not allow partial writes */
	if (*offset != 0)
		return -EINVAL;

	/* Do not allow incomplete command buffers */
	if (length != data->length)
		return -EINVAL;

	ret = simple_write_to_buffer(payload, sizeof(payload), offset, input, length);
	if (ret < 0)
		return ret;

	down_write(&data->buffer_lock);
	ret = msi_wmi_platform_query(data->data, data->method, payload, data->length, data->buffer,
				     data->length);
	up_write(&data->buffer_lock);

	if (ret < 0)
		return ret;

	return length;
}

static int msi_wmi_platform_show(struct seq_file *seq, void *p)
{
	struct msi_wmi_platform_debugfs_data *data = seq->private;
	int ret;

	down_read(&data->buffer_lock);
	ret = seq_write(seq, data->buffer, data->length);
	up_read(&data->buffer_lock);

	return ret;
}

static int msi_wmi_platform_open(struct inode *inode, struct file *fp)
{
	struct msi_wmi_platform_debugfs_data *data = inode->i_private;

	/* The seq_file uses the last byte of the buffer for detecting buffer overflows */
	return single_open_size(fp, msi_wmi_platform_show, data, data->length + 1);
}

static const struct file_operations msi_wmi_platform_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = msi_wmi_platform_open,
	.read = seq_read,
	.write = msi_wmi_platform_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static void msi_wmi_platform_debugfs_remove(void *data)
{
	struct dentry *dir = data;

	debugfs_remove_recursive(dir);
}

static void msi_wmi_platform_debugfs_add(struct msi_wmi_platform_data *drvdata, struct dentry *dir,
					 const char *name, enum msi_wmi_platform_method method)
{
	struct msi_wmi_platform_debugfs_data *data;
	struct dentry *entry;

	data = devm_kzalloc(&drvdata->wdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	data->data = drvdata;
	data->method = method;
	init_rwsem(&data->buffer_lock);

	/* The ACPI firmware for now always requires a 32 byte input buffer due to
	 * a peculiarity in how Windows handles the CreateByteField() ACPI operator.
	 */
	data->length = 32;

	entry = debugfs_create_file(name, 0600, dir, data, &msi_wmi_platform_debugfs_fops);
	if (IS_ERR(entry))
		devm_kfree(&drvdata->wdev->dev, data);
}

static void msi_wmi_platform_debugfs_init(struct msi_wmi_platform_data *data)
{
	struct dentry *dir;
	char dir_name[64];
	int ret, method;

	scnprintf(dir_name, ARRAY_SIZE(dir_name), "%s-%s", DRIVER_NAME, dev_name(&data->wdev->dev));

	dir = debugfs_create_dir(dir_name, NULL);
	if (IS_ERR(dir))
		return;

	ret = devm_add_action_or_reset(&data->wdev->dev, msi_wmi_platform_debugfs_remove, dir);
	if (ret < 0)
		return;

	for (method = MSI_PLATFORM_GET_PACKAGE; method <= MSI_PLATFORM_GET_WMI; method++)
		msi_wmi_platform_debugfs_add(data, dir, msi_wmi_platform_debugfs_names[method - 1],
					     method);
}

static int msi_wmi_platform_hwmon_init(struct msi_wmi_platform_data *data)
{
	struct device *hdev;

	hdev = devm_hwmon_device_register_with_info(&data->wdev->dev, "msi_wmi_platform", data,
						    &msi_wmi_platform_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hdev);
}

static int msi_wmi_platform_ec_init(struct msi_wmi_platform_data *data)
{
	u8 input[32] = { 0 };
	u8 output[32];
	u8 flags;
	int ret;

	ret = msi_wmi_platform_query(data, MSI_PLATFORM_GET_EC, input, sizeof(input), output,
				     sizeof(output));
	if (ret < 0)
		return ret;

	flags = output[MSI_PLATFORM_EC_FLAGS_OFFSET];

	dev_dbg(&data->wdev->dev, "EC RAM version %lu.%lu\n",
		FIELD_GET(MSI_PLATFORM_EC_MAJOR_MASK, flags),
		FIELD_GET(MSI_PLATFORM_EC_MINOR_MASK, flags));
	dev_dbg(&data->wdev->dev, "EC firmware version %.28s\n",
		&output[MSI_PLATFORM_EC_VERSION_OFFSET]);

	if (!(flags & MSI_PLATFORM_EC_IS_TIGERLAKE)) {
		if (!force)
			return -ENODEV;

		dev_warn(&data->wdev->dev, "Loading on a non-Tigerlake platform\n");
	}

	return 0;
}

static int msi_wmi_platform_init(struct msi_wmi_platform_data *data)
{
	u8 input[32] = { 0 };
	u8 output[32];
	int ret;

	ret = msi_wmi_platform_query(data, MSI_PLATFORM_GET_WMI, input, sizeof(input), output,
				     sizeof(output));
	if (ret < 0)
		return ret;

	dev_dbg(&data->wdev->dev, "WMI interface version %u.%u\n",
		output[MSI_PLATFORM_WMI_MAJOR_OFFSET],
		output[MSI_PLATFORM_WMI_MINOR_OFFSET]);

	if (output[MSI_PLATFORM_WMI_MAJOR_OFFSET] != MSI_WMI_PLATFORM_INTERFACE_VERSION) {
		if (!force)
			return -ENODEV;

		dev_warn(&data->wdev->dev,
			 "Loading despite unsupported WMI interface version (%u.%u)\n",
			 output[MSI_PLATFORM_WMI_MAJOR_OFFSET],
			 output[MSI_PLATFORM_WMI_MINOR_OFFSET]);
	}

	return 0;
}

static int msi_wmi_platform_probe(struct wmi_device *wdev, const void *context)
{
	struct msi_wmi_platform_data *data;
	int ret;

	data = devm_kzalloc(&wdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->wdev = wdev;
	dev_set_drvdata(&wdev->dev, data);

	ret = devm_mutex_init(&wdev->dev, &data->wmi_lock);
	if (ret < 0)
		return ret;

	ret = msi_wmi_platform_init(data);
	if (ret < 0)
		return ret;

	ret = msi_wmi_platform_ec_init(data);
	if (ret < 0)
		return ret;

	msi_wmi_platform_debugfs_init(data);

	return msi_wmi_platform_hwmon_init(data);
}

static const struct wmi_device_id msi_wmi_platform_id_table[] = {
	{ MSI_PLATFORM_GUID, NULL },
	{ }
};
MODULE_DEVICE_TABLE(wmi, msi_wmi_platform_id_table);

static struct wmi_driver msi_wmi_platform_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = msi_wmi_platform_id_table,
	.probe = msi_wmi_platform_probe,
	.no_singleton = true,
};
module_wmi_driver(msi_wmi_platform_driver);

MODULE_AUTHOR("Armin Wolf <W_Armin@gmx.de>");
MODULE_DESCRIPTION("MSI WMI platform features");
MODULE_LICENSE("GPL");
