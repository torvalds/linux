// SPDX-License-Identifier: GPL-2.0
// LED Multicolor class interface
// Copyright (C) 2019-20 Texas Instruments Incorporated - http://www.ti.com/
// Author: Dan Murphy <dmurphy@ti.com>

#include <linux/device.h>
#include <linux/init.h>
#include <linux/led-class-multicolor.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

int led_mc_calc_color_components(struct led_classdev_mc *mcled_cdev,
				 enum led_brightness brightness)
{
	struct led_classdev *led_cdev = &mcled_cdev->led_cdev;
	int i;

	for (i = 0; i < mcled_cdev->num_colors; i++)
		mcled_cdev->subled_info[i].brightness =
			DIV_ROUND_CLOSEST(brightness *
					  mcled_cdev->subled_info[i].intensity,
					  led_cdev->max_brightness);

	return 0;
}
EXPORT_SYMBOL_GPL(led_mc_calc_color_components);

static ssize_t multi_intensity_store(struct device *dev,
				struct device_attribute *intensity_attr,
				const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_mc *mcled_cdev = lcdev_to_mccdev(led_cdev);
	int nrchars, offset = 0;
	int intensity_value[LED_COLOR_ID_MAX];
	int i;
	ssize_t ret;

	mutex_lock(&led_cdev->led_access);

	for (i = 0; i < mcled_cdev->num_colors; i++) {
		ret = sscanf(buf + offset, "%i%n",
			     &intensity_value[i], &nrchars);
		if (ret != 1) {
			ret = -EINVAL;
			goto err_out;
		}
		offset += nrchars;
	}

	offset++;
	if (offset < size) {
		ret = -EINVAL;
		goto err_out;
	}

	for (i = 0; i < mcled_cdev->num_colors; i++)
		mcled_cdev->subled_info[i].intensity = intensity_value[i];

	if (!test_bit(LED_BLINK_SW, &led_cdev->work_flags))
		led_set_brightness(led_cdev, led_cdev->brightness);
	ret = size;
err_out:
	mutex_unlock(&led_cdev->led_access);
	return ret;
}

static ssize_t multi_intensity_show(struct device *dev,
			      struct device_attribute *intensity_attr,
			      char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_mc *mcled_cdev = lcdev_to_mccdev(led_cdev);
	int len = 0;
	int i;

	for (i = 0; i < mcled_cdev->num_colors; i++) {
		len += sprintf(buf + len, "%d",
			       mcled_cdev->subled_info[i].intensity);
		if (i < mcled_cdev->num_colors - 1)
			len += sprintf(buf + len, " ");
	}

	buf[len++] = '\n';
	return len;
}
static DEVICE_ATTR_RW(multi_intensity);

static ssize_t multi_index_show(struct device *dev,
			      struct device_attribute *multi_index_attr,
			      char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_mc *mcled_cdev = lcdev_to_mccdev(led_cdev);
	int len = 0;
	int index;
	int i;

	for (i = 0; i < mcled_cdev->num_colors; i++) {
		index = mcled_cdev->subled_info[i].color_index;
		len += sprintf(buf + len, "%s", led_get_color_name(index));
		if (i < mcled_cdev->num_colors - 1)
			len += sprintf(buf + len, " ");
	}

	buf[len++] = '\n';
	return len;
}
static DEVICE_ATTR_RO(multi_index);

static struct attribute *led_multicolor_attrs[] = {
	&dev_attr_multi_intensity.attr,
	&dev_attr_multi_index.attr,
	NULL,
};
ATTRIBUTE_GROUPS(led_multicolor);

int led_classdev_multicolor_register_ext(struct device *parent,
				     struct led_classdev_mc *mcled_cdev,
				     struct led_init_data *init_data)
{
	struct led_classdev *led_cdev;

	if (!mcled_cdev)
		return -EINVAL;

	if (mcled_cdev->num_colors <= 0)
		return -EINVAL;

	if (mcled_cdev->num_colors > LED_COLOR_ID_MAX)
		return -EINVAL;

	led_cdev = &mcled_cdev->led_cdev;
	led_cdev->flags |= LED_MULTI_COLOR;
	mcled_cdev->led_cdev.groups = led_multicolor_groups;

	return led_classdev_register_ext(parent, led_cdev, init_data);
}
EXPORT_SYMBOL_GPL(led_classdev_multicolor_register_ext);

void led_classdev_multicolor_unregister(struct led_classdev_mc *mcled_cdev)
{
	if (!mcled_cdev)
		return;

	led_classdev_unregister(&mcled_cdev->led_cdev);
}
EXPORT_SYMBOL_GPL(led_classdev_multicolor_unregister);

static void devm_led_classdev_multicolor_release(struct device *dev, void *res)
{
	led_classdev_multicolor_unregister(*(struct led_classdev_mc **)res);
}

int devm_led_classdev_multicolor_register_ext(struct device *parent,
					     struct led_classdev_mc *mcled_cdev,
					     struct led_init_data *init_data)
{
	struct led_classdev_mc **dr;
	int ret;

	dr = devres_alloc(devm_led_classdev_multicolor_release,
			  sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	ret = led_classdev_multicolor_register_ext(parent, mcled_cdev,
						   init_data);
	if (ret) {
		devres_free(dr);
		return ret;
	}

	*dr = mcled_cdev;
	devres_add(parent, dr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_led_classdev_multicolor_register_ext);

static int devm_led_classdev_multicolor_match(struct device *dev,
					      void *res, void *data)
{
	struct led_classdev_mc **p = res;

	if (WARN_ON(!p || !*p))
		return 0;

	return *p == data;
}

void devm_led_classdev_multicolor_unregister(struct device *dev,
					     struct led_classdev_mc *mcled_cdev)
{
	WARN_ON(devres_release(dev,
			       devm_led_classdev_multicolor_release,
			       devm_led_classdev_multicolor_match, mcled_cdev));
}
EXPORT_SYMBOL_GPL(devm_led_classdev_multicolor_unregister);

MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_DESCRIPTION("Multicolor LED class interface");
MODULE_LICENSE("GPL v2");
