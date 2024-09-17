// SPDX-License-Identifier: GPL-2.0-only
/*
 * ChromeOS specific ACPI extensions
 *
 * Copyright 2022 Google LLC
 *
 * This driver attaches to the ChromeOS ACPI device and then exports the
 * values reported by the ACPI in a sysfs directory. All values are
 * presented in the string form (numbers as decimal values) and can be
 * accessed as the contents of the appropriate read only files in the
 * sysfs directory tree.
 */
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>

#define ACPI_ATTR_NAME_LEN 4

#define DEV_ATTR(_var, _name)					\
	static struct device_attribute dev_attr_##_var =	\
		__ATTR(_name, 0444, chromeos_first_level_attr_show, NULL);

#define GPIO_ATTR_GROUP(_group, _name, _num)						\
	static umode_t attr_is_visible_gpio_##_num(struct kobject *kobj,		\
						   struct attribute *attr, int n)	\
	{										\
		if (_num < chromeos_acpi_gpio_groups)					\
			return attr->mode;						\
		return 0;								\
	}										\
	static ssize_t chromeos_attr_show_gpio_##_num(struct device *dev,		\
						      struct device_attribute *attr,	\
						      char *buf)			\
	{										\
		char name[ACPI_ATTR_NAME_LEN + 1];					\
		int ret, num;								\
											\
		ret = parse_attr_name(attr->attr.name, name, &num);			\
		if (ret)								\
			return ret;							\
		return chromeos_acpi_evaluate_method(dev, _num, num, name, buf);	\
	}										\
	static struct device_attribute dev_attr_0_##_group =				\
		__ATTR(GPIO.0, 0444, chromeos_attr_show_gpio_##_num, NULL);		\
	static struct device_attribute dev_attr_1_##_group =				\
		__ATTR(GPIO.1, 0444, chromeos_attr_show_gpio_##_num, NULL);		\
	static struct device_attribute dev_attr_2_##_group =				\
		__ATTR(GPIO.2, 0444, chromeos_attr_show_gpio_##_num, NULL);		\
	static struct device_attribute dev_attr_3_##_group =				\
		__ATTR(GPIO.3, 0444, chromeos_attr_show_gpio_##_num, NULL);		\
											\
	static struct attribute *attrs_##_group[] = {					\
		&dev_attr_0_##_group.attr,						\
		&dev_attr_1_##_group.attr,						\
		&dev_attr_2_##_group.attr,						\
		&dev_attr_3_##_group.attr,						\
		NULL									\
	};										\
	static const struct attribute_group attr_group_##_group = {			\
		.name = _name,								\
		.is_visible = attr_is_visible_gpio_##_num,				\
		.attrs = attrs_##_group,						\
	};

static unsigned int chromeos_acpi_gpio_groups;

/* Parse the ACPI package and return the data related to that attribute */
static int chromeos_acpi_handle_package(struct device *dev, union acpi_object *obj,
					int pkg_num, int sub_pkg_num, char *name, char *buf)
{
	union acpi_object *element = obj->package.elements;

	if (pkg_num >= obj->package.count)
		return -EINVAL;
	element += pkg_num;

	if (element->type == ACPI_TYPE_PACKAGE) {
		if (sub_pkg_num >= element->package.count)
			return -EINVAL;
		/* select sub element inside this package */
		element = element->package.elements;
		element += sub_pkg_num;
	}

	switch (element->type) {
	case ACPI_TYPE_INTEGER:
		return sysfs_emit(buf, "%d\n", (int)element->integer.value);
	case ACPI_TYPE_STRING:
		return sysfs_emit(buf, "%s\n", element->string.pointer);
	case ACPI_TYPE_BUFFER:
		{
			int i, r, at, room_left;
			const int byte_per_line = 16;

			at = 0;
			room_left = PAGE_SIZE - 1;
			for (i = 0; i < element->buffer.length && room_left; i += byte_per_line) {
				r = hex_dump_to_buffer(element->buffer.pointer + i,
						       element->buffer.length - i,
						       byte_per_line, 1, buf + at, room_left,
						       false);
				if (r > room_left)
					goto truncating;
				at += r;
				room_left -= r;

				r = sysfs_emit_at(buf, at, "\n");
				if (!r)
					goto truncating;
				at += r;
				room_left -= r;
			}

			buf[at] = 0;
			return at;
truncating:
			dev_info_once(dev, "truncating sysfs content for %s\n", name);
			sysfs_emit_at(buf, PAGE_SIZE - 4, "..\n");
			return PAGE_SIZE - 1;
		}
	default:
		dev_err(dev, "element type %d not supported\n", element->type);
		return -EINVAL;
	}
}

static int chromeos_acpi_evaluate_method(struct device *dev, int pkg_num, int sub_pkg_num,
					 char *name, char *buf)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;
	int ret = -EINVAL;

	status = acpi_evaluate_object(ACPI_HANDLE(dev), name, NULL, &output);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "failed to retrieve %s. %s\n", name, acpi_format_exception(status));
		return ret;
	}

	if (((union acpi_object *)output.pointer)->type == ACPI_TYPE_PACKAGE)
		ret = chromeos_acpi_handle_package(dev, output.pointer, pkg_num, sub_pkg_num,
						   name, buf);

	kfree(output.pointer);
	return ret;
}

static int parse_attr_name(const char *name, char *attr_name, int *attr_num)
{
	int ret;

	ret = strscpy(attr_name, name, ACPI_ATTR_NAME_LEN + 1);
	if (ret == -E2BIG)
		return kstrtoint(&name[ACPI_ATTR_NAME_LEN + 1], 0, attr_num);
	return 0;
}

static ssize_t chromeos_first_level_attr_show(struct device *dev, struct device_attribute *attr,
					      char *buf)
{
	char attr_name[ACPI_ATTR_NAME_LEN + 1];
	int ret, attr_num = 0;

	ret = parse_attr_name(attr->attr.name, attr_name, &attr_num);
	if (ret)
		return ret;
	return chromeos_acpi_evaluate_method(dev, attr_num, 0, attr_name, buf);
}

static unsigned int get_gpio_pkg_num(struct device *dev)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	unsigned int count = 0;
	char *name = "GPIO";

	status = acpi_evaluate_object(ACPI_HANDLE(dev), name, NULL, &output);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "failed to retrieve %s. %s\n", name, acpi_format_exception(status));
		return count;
	}

	obj = output.pointer;

	if (obj->type == ACPI_TYPE_PACKAGE)
		count = obj->package.count;

	kfree(output.pointer);
	return count;
}

DEV_ATTR(binf2, BINF.2)
DEV_ATTR(binf3, BINF.3)
DEV_ATTR(chsw, CHSW)
DEV_ATTR(fmap, FMAP)
DEV_ATTR(frid, FRID)
DEV_ATTR(fwid, FWID)
DEV_ATTR(hwid, HWID)
DEV_ATTR(meck, MECK)
DEV_ATTR(vbnv0, VBNV.0)
DEV_ATTR(vbnv1, VBNV.1)
DEV_ATTR(vdat, VDAT)

static struct attribute *first_level_attrs[] = {
	&dev_attr_binf2.attr,
	&dev_attr_binf3.attr,
	&dev_attr_chsw.attr,
	&dev_attr_fmap.attr,
	&dev_attr_frid.attr,
	&dev_attr_fwid.attr,
	&dev_attr_hwid.attr,
	&dev_attr_meck.attr,
	&dev_attr_vbnv0.attr,
	&dev_attr_vbnv1.attr,
	&dev_attr_vdat.attr,
	NULL
};

static const struct attribute_group first_level_attr_group = {
	.attrs = first_level_attrs,
};

/*
 * Every platform can have a different number of GPIO attribute groups.
 * Define upper limit groups. At run time, the platform decides to show
 * the present number of groups only, others are hidden.
 */
GPIO_ATTR_GROUP(gpio0, "GPIO.0", 0)
GPIO_ATTR_GROUP(gpio1, "GPIO.1", 1)
GPIO_ATTR_GROUP(gpio2, "GPIO.2", 2)
GPIO_ATTR_GROUP(gpio3, "GPIO.3", 3)
GPIO_ATTR_GROUP(gpio4, "GPIO.4", 4)
GPIO_ATTR_GROUP(gpio5, "GPIO.5", 5)
GPIO_ATTR_GROUP(gpio6, "GPIO.6", 6)
GPIO_ATTR_GROUP(gpio7, "GPIO.7", 7)

static const struct attribute_group *chromeos_acpi_all_groups[] = {
	&first_level_attr_group,
	&attr_group_gpio0,
	&attr_group_gpio1,
	&attr_group_gpio2,
	&attr_group_gpio3,
	&attr_group_gpio4,
	&attr_group_gpio5,
	&attr_group_gpio6,
	&attr_group_gpio7,
	NULL
};

static int chromeos_acpi_device_probe(struct platform_device *pdev)
{
	chromeos_acpi_gpio_groups = get_gpio_pkg_num(&pdev->dev);

	/*
	 * If the platform has more GPIO attribute groups than the number of
	 * groups this driver supports, give out a warning message.
	 */
	if (chromeos_acpi_gpio_groups > ARRAY_SIZE(chromeos_acpi_all_groups) - 2)
		dev_warn(&pdev->dev, "Only %zu GPIO attr groups supported by the driver out of total %u.\n",
			 ARRAY_SIZE(chromeos_acpi_all_groups) - 2, chromeos_acpi_gpio_groups);
	return 0;
}

/* GGL is valid PNP ID of Google. PNP ID can be used with the ACPI devices. */
static const struct acpi_device_id chromeos_device_ids[] = {
	{ "GGL0001", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, chromeos_device_ids);

static struct platform_driver chromeos_acpi_device_driver = {
	.probe = chromeos_acpi_device_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.dev_groups = chromeos_acpi_all_groups,
		.acpi_match_table = chromeos_device_ids,
	}
};
module_platform_driver(chromeos_acpi_device_driver);

MODULE_AUTHOR("Muhammad Usama Anjum <usama.anjum@collabora.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS specific ACPI extensions");
