/*
 * Copyright (C) 2011 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/radio_ctrl/radio_class.h>

static struct class *radio_cls;

/* show the device status */
static ssize_t radio_status_show(struct device *dev,
		struct device_attribute *attr, char *buff)
{
	struct radio_dev *rdev = dev_get_drvdata(dev);

	if (!rdev || !rdev->status) {
		pr_err("%s: supporting function not found\n", __func__);
		return -ENODEV;
	}

	return rdev->status(rdev, buff);
}

/* show the device power state */
static ssize_t radio_power_show(struct device *dev,
		struct device_attribute *attr, char *buff)
{
	struct radio_dev *rdev = dev_get_drvdata(dev);

	if (!rdev || !rdev->power_status) {
		pr_err("%s: supporting function not found\n", __func__);
		return -ENODEV;
	}
	return rdev->power_status(rdev, buff);
}

/* control the radio device */
static ssize_t radio_command(struct device *dev,
		struct device_attribute *attr, const char *buff, size_t size)
{
	struct radio_dev *rdev = dev_get_drvdata(dev);
	char tmp[RADIO_COMMAND_MAX_LENGTH];
	char *post_strip = NULL;
	ssize_t ret;

	if (!rdev || !rdev->command) {
		pr_err("%s: supporting function not found\n", __func__);
		return -ENODEV;
	}
	pr_debug("%s: rdev.name = %s\n", __func__, rdev->name);

	if (size > RADIO_COMMAND_MAX_LENGTH - 1) {
		pr_err("%s: command too long\n", __func__);
		return -EINVAL;
	}

	/* strip whitespaces if any */
	memcpy(tmp, buff, size);
	tmp[size] = '\0';
	post_strip = strim(tmp);

	pr_debug("%s: command = %s size = %d\n", __func__, post_strip, size);

	ret = rdev->command(rdev, post_strip);
	return ret >= 0 ? size : ret;
}

static DEVICE_ATTR(status, S_IRUGO, radio_status_show, NULL);
static DEVICE_ATTR(power_status, S_IRUGO, radio_power_show, NULL);
static DEVICE_ATTR(command, S_IWUSR, NULL, radio_command);

int radio_dev_register(struct radio_dev *rdev)
{
	int err = -1;

	rdev->dev = device_create(radio_cls, NULL, 0, rdev,
		"%s",  rdev->name);
	if (IS_ERR(rdev->dev))
		return PTR_ERR(rdev->dev);

	pr_info("%s: register %s\n", __func__, rdev->name);
	pr_debug("%s: dev        = %p\n", __func__, rdev->dev);

	/* /sys/class/radio/<dev>/status */
	if (rdev->status) {
		err = device_create_file(rdev->dev, &dev_attr_status);
		if (err < 0) {
			pr_err("%s: status file create failed for %s\n",
			       __func__, rdev->name);
			goto err_status;
		}
	}

	/* /sys/class/radio/<dev>/power_status */
	if (rdev->power_status) {
		err = device_create_file(rdev->dev, &dev_attr_power_status);
		if (err < 0) {
			pr_err("%s: power_status file create failed for %s\n",
			       __func__, rdev->name);
			goto err_pwr_status;
		}
	}

	/* /sys/class/radio/<dev>/command */
	if (rdev->command) {
		err = device_create_file(rdev->dev, &dev_attr_command);
		if (err < 0) {
			pr_err("%s: command file create failed for %s\n",
			       __func__, rdev->name);
			goto err_command;
		}
	}

	return 0;

err_command:
	device_remove_file(rdev->dev, &dev_attr_power_status);
err_pwr_status:
	device_remove_file(rdev->dev, &dev_attr_status);
err_status:
	put_device(rdev->dev);
	device_unregister(rdev->dev);
	dev_set_drvdata(rdev->dev, NULL);

	 return err;
}
EXPORT_SYMBOL(radio_dev_register);

void radio_dev_unregister(struct radio_dev *rdev)
{
	if (rdev) {
		if (rdev->command)
			device_remove_file(rdev->dev, &dev_attr_command);
		if (rdev->power_status)
			device_remove_file(rdev->dev, &dev_attr_power_status);
		if (rdev->status)
			device_remove_file(rdev->dev, &dev_attr_status);

		put_device(rdev->dev);
		device_unregister(rdev->dev);
		dev_set_drvdata(rdev->dev, NULL);
	}
}
EXPORT_SYMBOL(radio_dev_unregister);

static int __init radio_class_init(void)
{
	pr_info("%s: initialized radio_class\n", __func__);

	/* /sys/class/radio */
	radio_cls = class_create(THIS_MODULE, "radio");
	if (IS_ERR(radio_cls)) {
		pr_err("%s: failed to initialize radio class\n", __func__);
		return PTR_ERR(radio_cls);
	}

	return 0;
}

static void __exit radio_class_exit(void)
{
	pr_info("%s: destroy radio_class\n", __func__);
	class_destroy(radio_cls);
}

module_init(radio_class_init);
module_exit(radio_class_exit);

MODULE_AUTHOR("Jim Wylder <james.wylder@motorola.com>");
MODULE_DESCRIPTION("Radio Control Class");
MODULE_LICENSE("GPL");
