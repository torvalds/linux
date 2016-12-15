/*
 * drivers/misc/usb_cam_gpio.c
 *
 * Copyright (C) 2012-2016 Rockchip Co.,Ltd.
 * Author: Bin Yang <yangbin@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>

static struct class *usb_cam_gpio_class;

struct usb_cam_gpio {
	struct device *dev;
	struct device sys_dev;

	struct gpio_desc *gpio_cam_hd;
	struct gpio_desc *gpio_cam_ir;
};

static ssize_t hd_camera_on_show(struct device *sys_dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct usb_cam_gpio *gpiod = container_of(sys_dev, struct usb_cam_gpio,
						  sys_dev);

	return sprintf(buf, "%d\n", gpiod_get_value(gpiod->gpio_cam_hd));
}

static ssize_t hd_camera_on_store(struct device *sys_dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct usb_cam_gpio *gpiod = container_of(sys_dev, struct usb_cam_gpio,
						  sys_dev);
	int val = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;
	if (val)
		val = 1;
	gpiod_set_value(gpiod->gpio_cam_hd, val);

	return count;
}
static DEVICE_ATTR_RW(hd_camera_on);

static ssize_t ir_camera_on_show(struct device *sys_dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct usb_cam_gpio *gpiod = container_of(sys_dev,
						  struct usb_cam_gpio, sys_dev);

	return sprintf(buf, "%d\n", gpiod_get_value(gpiod->gpio_cam_ir));
}

static ssize_t ir_camera_on_store(struct device *sys_dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct usb_cam_gpio *gpiod = container_of(sys_dev,
						  struct usb_cam_gpio, sys_dev);
	int val;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;
	if (val)
		val = 1;
	gpiod_set_value(gpiod->gpio_cam_ir, val);

	return count;
}
static DEVICE_ATTR_RW(ir_camera_on);

static struct attribute *usb_cam_gpio_attrs[] = {
	&dev_attr_hd_camera_on.attr,
	&dev_attr_ir_camera_on.attr,
	NULL,
};
ATTRIBUTE_GROUPS(usb_cam_gpio);

static int usb_cam_gpio_device_register(struct usb_cam_gpio *gpiod)
{
	int ret;
	struct device *dev = &gpiod->sys_dev;
	const char *name = {"usb_camera_on"};

	dev->class = usb_cam_gpio_class;
	dev_set_name(dev, "%s", name);
	dev_set_drvdata(dev, gpiod);
	ret = device_register(dev);

	return ret;
}

static int usb_cam_gpio_probe(struct platform_device *pdev)
{
	struct usb_cam_gpio *gpiod;
	int ret = 0;

	usb_cam_gpio_class = class_create(THIS_MODULE, "usb_cam_gpio");
	if (IS_ERR(usb_cam_gpio_class)) {
		pr_err("create uvc_detection class failed (%ld)\n",
		       PTR_ERR(usb_cam_gpio_class));
		return PTR_ERR(usb_cam_gpio_class);
	}
	usb_cam_gpio_class->dev_groups = usb_cam_gpio_groups;

	gpiod = devm_kzalloc(&pdev->dev, sizeof(*gpiod), GFP_KERNEL);
	if (!gpiod)
		return -ENOMEM;

	gpiod->dev = &pdev->dev;

	gpiod->gpio_cam_hd = devm_gpiod_get_optional(gpiod->dev,
						     "hd-cam", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod->gpio_cam_hd)) {
		dev_warn(gpiod->dev, "Could not get hd-cam-gpios!\n");
		gpiod->gpio_cam_hd = NULL;
	}

	gpiod->gpio_cam_ir = devm_gpiod_get_optional(gpiod->dev,
						     "ir-cam", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod->gpio_cam_ir)) {
		dev_warn(gpiod->dev, "Could not get ir-cam-gpios!\n");
		gpiod->gpio_cam_ir = NULL;
	}

	ret = usb_cam_gpio_device_register(gpiod);
	if (ret < 0) {
		dev_err(gpiod->dev, "usb_cam_gpio device register fail\n");
		return ret;
	}

	dev_info(gpiod->dev, "usb_cam_gpio_probe success\n");

	return 0;
}

static const struct of_device_id usb_cam_gpio_match[] = {
	{ .compatible = "usb-cam-gpio" },
	{ /* Sentinel */ }
};

static int usb_cam_gpio_remove(struct platform_device *pdev)
{
	if (!IS_ERR(usb_cam_gpio_class))
		class_destroy(usb_cam_gpio_class);

	return 0;
}

static struct platform_driver usb_cam_gpio_driver = {
	.probe = usb_cam_gpio_probe,
	.remove = usb_cam_gpio_remove,
	.driver = {
		.name = "usb_cam_gpio",
		.owner = THIS_MODULE,
		.of_match_table	= usb_cam_gpio_match,
	},
};

module_platform_driver(usb_cam_gpio_driver);

MODULE_ALIAS("platform:usb_cam_gpio");
MODULE_AUTHOR("Bin Yang <yangbin@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("usb camera gpio driver");
