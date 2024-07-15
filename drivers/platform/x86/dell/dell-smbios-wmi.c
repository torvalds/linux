// SPDX-License-Identifier: GPL-2.0-only
/*
 *  WMI methods for use with dell-smbios
 *
 *  Copyright (c) 2017 Dell Inc.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/wmi.h>
#include <uapi/linux/wmi.h>
#include "dell-smbios.h"
#include "dell-wmi-descriptor.h"

static DEFINE_MUTEX(call_mutex);
static DEFINE_MUTEX(list_mutex);
static int wmi_supported;

struct misc_bios_flags_structure {
	struct dmi_header header;
	u16 flags0;
} __packed;
#define FLAG_HAS_ACPI_WMI 0x02

#define DELL_WMI_SMBIOS_GUID "A80593CE-A997-11DA-B012-B622A1EF5492"

struct wmi_smbios_priv {
	struct dell_wmi_smbios_buffer *buf;
	struct list_head list;
	struct wmi_device *wdev;
	struct device *child;
	u64 req_buf_size;
	struct miscdevice char_dev;
};
static LIST_HEAD(wmi_list);

static inline struct wmi_smbios_priv *get_first_smbios_priv(void)
{
	return list_first_entry_or_null(&wmi_list,
					struct wmi_smbios_priv,
					list);
}

static int run_smbios_call(struct wmi_device *wdev)
{
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	struct wmi_smbios_priv *priv;
	struct acpi_buffer input;
	union acpi_object *obj;
	acpi_status status;

	priv = dev_get_drvdata(&wdev->dev);
	input.length = priv->req_buf_size - sizeof(u64);
	input.pointer = &priv->buf->std;

	dev_dbg(&wdev->dev, "evaluating: %u/%u [%x,%x,%x,%x]\n",
		priv->buf->std.cmd_class, priv->buf->std.cmd_select,
		priv->buf->std.input[0], priv->buf->std.input[1],
		priv->buf->std.input[2], priv->buf->std.input[3]);

	status = wmidev_evaluate_method(wdev, 0, 1, &input, &output);
	if (ACPI_FAILURE(status))
		return -EIO;
	obj = (union acpi_object *)output.pointer;
	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_dbg(&wdev->dev, "received type: %d\n", obj->type);
		if (obj->type == ACPI_TYPE_INTEGER)
			dev_dbg(&wdev->dev, "SMBIOS call failed: %llu\n",
				obj->integer.value);
		kfree(output.pointer);
		return -EIO;
	}
	memcpy(input.pointer, obj->buffer.pointer, obj->buffer.length);
	dev_dbg(&wdev->dev, "result: [%08x,%08x,%08x,%08x]\n",
		priv->buf->std.output[0], priv->buf->std.output[1],
		priv->buf->std.output[2], priv->buf->std.output[3]);
	kfree(output.pointer);

	return 0;
}

static int dell_smbios_wmi_call(struct calling_interface_buffer *buffer)
{
	struct wmi_smbios_priv *priv;
	size_t difference;
	size_t size;
	int ret;

	mutex_lock(&call_mutex);
	priv = get_first_smbios_priv();
	if (!priv) {
		ret = -ENODEV;
		goto out_wmi_call;
	}

	size = sizeof(struct calling_interface_buffer);
	difference = priv->req_buf_size - sizeof(u64) - size;

	memset(&priv->buf->ext, 0, difference);
	memcpy(&priv->buf->std, buffer, size);
	ret = run_smbios_call(priv->wdev);
	memcpy(buffer, &priv->buf->std, size);
out_wmi_call:
	mutex_unlock(&call_mutex);

	return ret;
}

static int dell_smbios_wmi_open(struct inode *inode, struct file *filp)
{
	struct wmi_smbios_priv *priv;

	priv = container_of(filp->private_data, struct wmi_smbios_priv, char_dev);
	filp->private_data = priv;

	return nonseekable_open(inode, filp);
}

static ssize_t dell_smbios_wmi_read(struct file *filp, char __user *buffer, size_t length,
				    loff_t *offset)
{
	struct wmi_smbios_priv *priv = filp->private_data;

	return simple_read_from_buffer(buffer, length, offset, &priv->req_buf_size,
				       sizeof(priv->req_buf_size));
}

static long dell_smbios_wmi_do_ioctl(struct wmi_smbios_priv *priv,
				     struct dell_wmi_smbios_buffer __user *arg)
{
	long ret;

	if (get_user(priv->buf->length, &arg->length))
		return -EFAULT;

	if (priv->buf->length < priv->req_buf_size)
		return -EINVAL;

	/* if it's too big, warn, driver will only use what is needed */
	if (priv->buf->length > priv->req_buf_size)
		dev_err(&priv->wdev->dev, "Buffer %llu is bigger than required %llu\n",
			priv->buf->length, priv->req_buf_size);

	if (copy_from_user(priv->buf, arg, priv->req_buf_size))
		return -EFAULT;

	if (dell_smbios_call_filter(&priv->wdev->dev, &priv->buf->std)) {
		dev_err(&priv->wdev->dev, "Invalid call %d/%d:%8x\n",
			priv->buf->std.cmd_class,
			priv->buf->std.cmd_select,
			priv->buf->std.input[0]);

		return -EINVAL;
	}

	ret = run_smbios_call(priv->wdev);
	if (ret)
		return ret;

	if (copy_to_user(arg, priv->buf, priv->req_buf_size))
		return -EFAULT;

	return 0;
}

static long dell_smbios_wmi_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct dell_wmi_smbios_buffer __user *input = (struct dell_wmi_smbios_buffer __user *)arg;
	struct wmi_smbios_priv *priv = filp->private_data;
	long ret;

	if (cmd != DELL_WMI_SMBIOS_CMD)
		return -ENOIOCTLCMD;

	mutex_lock(&call_mutex);
	ret = dell_smbios_wmi_do_ioctl(priv, input);
	mutex_unlock(&call_mutex);

	return ret;
}

static const struct file_operations dell_smbios_wmi_fops = {
	.owner		= THIS_MODULE,
	.open		= dell_smbios_wmi_open,
	.read		= dell_smbios_wmi_read,
	.unlocked_ioctl	= dell_smbios_wmi_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

static void dell_smbios_wmi_unregister_chardev(void *data)
{
	struct miscdevice *char_dev = data;

	misc_deregister(char_dev);
}

static int dell_smbios_wmi_register_chardev(struct wmi_smbios_priv *priv)
{
	int ret;

	priv->char_dev.minor = MISC_DYNAMIC_MINOR;
	priv->char_dev.name = "wmi/dell-smbios";
	priv->char_dev.fops = &dell_smbios_wmi_fops;
	priv->char_dev.mode = 0444;

	ret = misc_register(&priv->char_dev);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(&priv->wdev->dev, dell_smbios_wmi_unregister_chardev,
					&priv->char_dev);
}

static int dell_smbios_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct wmi_smbios_priv *priv;
	u32 buffer_size, hotfix;
	int count;
	int ret;

	ret = dell_wmi_get_descriptor_valid();
	if (ret)
		return ret;

	priv = devm_kzalloc(&wdev->dev, sizeof(struct wmi_smbios_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, priv);

	/* WMI buffer size will be either 4k or 32k depending on machine */
	if (!dell_wmi_get_size(&buffer_size))
		return -EPROBE_DEFER;

	priv->req_buf_size = buffer_size;

	/* some SMBIOS calls fail unless BIOS contains hotfix */
	if (!dell_wmi_get_hotfix(&hotfix))
		return -EPROBE_DEFER;

	if (!hotfix)
		dev_warn(&wdev->dev,
			"WMI SMBIOS userspace interface not supported(%u), try upgrading to a newer BIOS\n",
			hotfix);

	/* add in the length object we will use internally with ioctl */
	priv->req_buf_size += sizeof(u64);

	count = get_order(priv->req_buf_size);
	priv->buf = (void *)devm_get_free_pages(&wdev->dev, GFP_KERNEL, count);
	if (!priv->buf)
		return -ENOMEM;

	ret = dell_smbios_wmi_register_chardev(priv);
	if (ret)
		return ret;

	/* ID is used by dell-smbios to set priority of drivers */
	wdev->dev.id = 1;
	ret = dell_smbios_register_device(&wdev->dev, &dell_smbios_wmi_call);
	if (ret)
		return ret;

	mutex_lock(&list_mutex);
	list_add_tail(&priv->list, &wmi_list);
	mutex_unlock(&list_mutex);

	return 0;
}

static void dell_smbios_wmi_remove(struct wmi_device *wdev)
{
	struct wmi_smbios_priv *priv = dev_get_drvdata(&wdev->dev);

	mutex_lock(&call_mutex);
	mutex_lock(&list_mutex);
	list_del(&priv->list);
	mutex_unlock(&list_mutex);
	dell_smbios_unregister_device(&wdev->dev);
	mutex_unlock(&call_mutex);
}

static const struct wmi_device_id dell_smbios_wmi_id_table[] = {
	{ .guid_string = DELL_WMI_SMBIOS_GUID },
	{ },
};

static void parse_b1_table(const struct dmi_header *dm)
{
	struct misc_bios_flags_structure *flags =
	container_of(dm, struct misc_bios_flags_structure, header);

	/* 4 bytes header, 8 bytes flags */
	if (dm->length < 12)
		return;
	if (dm->handle != 0xb100)
		return;
	if ((flags->flags0 & FLAG_HAS_ACPI_WMI))
		wmi_supported = 1;
}

static void find_b1(const struct dmi_header *dm, void *dummy)
{
	switch (dm->type) {
	case 0xb1: /* misc bios flags */
		parse_b1_table(dm);
		break;
	}
}

static struct wmi_driver dell_smbios_wmi_driver = {
	.driver = {
		.name = "dell-smbios",
	},
	.probe = dell_smbios_wmi_probe,
	.remove = dell_smbios_wmi_remove,
	.id_table = dell_smbios_wmi_id_table,
};

int init_dell_smbios_wmi(void)
{
	dmi_walk(find_b1, NULL);

	if (!wmi_supported)
		return -ENODEV;

	return wmi_driver_register(&dell_smbios_wmi_driver);
}

void exit_dell_smbios_wmi(void)
{
	if (wmi_supported)
		wmi_driver_unregister(&dell_smbios_wmi_driver);
}

MODULE_DEVICE_TABLE(wmi, dell_smbios_wmi_id_table);
