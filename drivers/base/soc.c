// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Lee Jones <lee.jones@linaro.org> for ST-Ericsson.
 */

#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/spinlock.h>
#include <linux/sys_soc.h>
#include <linux/err.h>
#include <linux/glob.h>

static DEFINE_IDA(soc_ida);

/* Prototype to allow declarations of DEVICE_ATTR(<foo>) before soc_info_show */
static ssize_t soc_info_show(struct device *dev, struct device_attribute *attr,
			     char *buf);

struct soc_device {
	struct device dev;
	struct soc_device_attribute *attr;
	int soc_dev_num;
};

static const struct bus_type soc_bus_type = {
	.name  = "soc",
};
static bool soc_bus_registered;

static DEVICE_ATTR(machine,		0444, soc_info_show,  NULL);
static DEVICE_ATTR(family,		0444, soc_info_show,  NULL);
static DEVICE_ATTR(serial_number,	0444, soc_info_show,  NULL);
static DEVICE_ATTR(soc_id,		0444, soc_info_show,  NULL);
static DEVICE_ATTR(revision,		0444, soc_info_show,  NULL);

struct device *soc_device_to_device(struct soc_device *soc_dev)
{
	return &soc_dev->dev;
}

static umode_t soc_attribute_mode(struct kobject *kobj,
				struct attribute *attr,
				int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct soc_device *soc_dev = container_of(dev, struct soc_device, dev);

	if ((attr == &dev_attr_machine.attr) && soc_dev->attr->machine)
		return attr->mode;
	if ((attr == &dev_attr_family.attr) && soc_dev->attr->family)
		return attr->mode;
	if ((attr == &dev_attr_revision.attr) && soc_dev->attr->revision)
		return attr->mode;
	if ((attr == &dev_attr_serial_number.attr) && soc_dev->attr->serial_number)
		return attr->mode;
	if ((attr == &dev_attr_soc_id.attr) && soc_dev->attr->soc_id)
		return attr->mode;

	/* Unknown or unfilled attribute */
	return 0;
}

static ssize_t soc_info_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct soc_device *soc_dev = container_of(dev, struct soc_device, dev);
	const char *output;

	if (attr == &dev_attr_machine)
		output = soc_dev->attr->machine;
	else if (attr == &dev_attr_family)
		output = soc_dev->attr->family;
	else if (attr == &dev_attr_revision)
		output = soc_dev->attr->revision;
	else if (attr == &dev_attr_serial_number)
		output = soc_dev->attr->serial_number;
	else if (attr == &dev_attr_soc_id)
		output = soc_dev->attr->soc_id;
	else
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", output);
}

static struct attribute *soc_attr[] = {
	&dev_attr_machine.attr,
	&dev_attr_family.attr,
	&dev_attr_serial_number.attr,
	&dev_attr_soc_id.attr,
	&dev_attr_revision.attr,
	NULL,
};

static const struct attribute_group soc_attr_group = {
	.attrs = soc_attr,
	.is_visible = soc_attribute_mode,
};

static void soc_release(struct device *dev)
{
	struct soc_device *soc_dev = container_of(dev, struct soc_device, dev);

	ida_free(&soc_ida, soc_dev->soc_dev_num);
	kfree(soc_dev->dev.groups);
	kfree(soc_dev);
}

static void soc_device_get_machine(struct soc_device_attribute *soc_dev_attr)
{
	struct device_node *np;

	if (soc_dev_attr->machine)
		return;

	np = of_find_node_by_path("/");
	of_property_read_string(np, "model", &soc_dev_attr->machine);
	of_node_put(np);
}

static struct soc_device_attribute *early_soc_dev_attr;

struct soc_device *soc_device_register(struct soc_device_attribute *soc_dev_attr)
{
	struct soc_device *soc_dev;
	const struct attribute_group **soc_attr_groups;
	int ret;

	soc_device_get_machine(soc_dev_attr);

	if (!soc_bus_registered) {
		if (early_soc_dev_attr)
			return ERR_PTR(-EBUSY);
		early_soc_dev_attr = soc_dev_attr;
		return NULL;
	}

	soc_dev = kzalloc(sizeof(*soc_dev), GFP_KERNEL);
	if (!soc_dev) {
		ret = -ENOMEM;
		goto out1;
	}

	soc_attr_groups = kcalloc(3, sizeof(*soc_attr_groups), GFP_KERNEL);
	if (!soc_attr_groups) {
		ret = -ENOMEM;
		goto out2;
	}
	soc_attr_groups[0] = &soc_attr_group;
	soc_attr_groups[1] = soc_dev_attr->custom_attr_group;

	/* Fetch a unique (reclaimable) SOC ID. */
	ret = ida_alloc(&soc_ida, GFP_KERNEL);
	if (ret < 0)
		goto out3;
	soc_dev->soc_dev_num = ret;

	soc_dev->attr = soc_dev_attr;
	soc_dev->dev.bus = &soc_bus_type;
	soc_dev->dev.groups = soc_attr_groups;
	soc_dev->dev.release = soc_release;

	dev_set_name(&soc_dev->dev, "soc%d", soc_dev->soc_dev_num);

	ret = device_register(&soc_dev->dev);
	if (ret) {
		put_device(&soc_dev->dev);
		return ERR_PTR(ret);
	}

	return soc_dev;

out3:
	kfree(soc_attr_groups);
out2:
	kfree(soc_dev);
out1:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(soc_device_register);

/* Ensure soc_dev->attr is freed after calling soc_device_unregister. */
void soc_device_unregister(struct soc_device *soc_dev)
{
	device_unregister(&soc_dev->dev);
	early_soc_dev_attr = NULL;
}
EXPORT_SYMBOL_GPL(soc_device_unregister);

static int __init soc_bus_register(void)
{
	int ret;

	ret = bus_register(&soc_bus_type);
	if (ret)
		return ret;
	soc_bus_registered = true;

	if (early_soc_dev_attr)
		return PTR_ERR(soc_device_register(early_soc_dev_attr));

	return 0;
}
core_initcall(soc_bus_register);

static int soc_device_match_attr(const struct soc_device_attribute *attr,
				 const struct soc_device_attribute *match)
{
	if (match->machine &&
	    (!attr->machine || !glob_match(match->machine, attr->machine)))
		return 0;

	if (match->family &&
	    (!attr->family || !glob_match(match->family, attr->family)))
		return 0;

	if (match->revision &&
	    (!attr->revision || !glob_match(match->revision, attr->revision)))
		return 0;

	if (match->soc_id &&
	    (!attr->soc_id || !glob_match(match->soc_id, attr->soc_id)))
		return 0;

	return 1;
}

static int soc_device_match_one(struct device *dev, void *arg)
{
	struct soc_device *soc_dev = container_of(dev, struct soc_device, dev);

	return soc_device_match_attr(soc_dev->attr, arg);
}

/*
 * soc_device_match - identify the SoC in the machine
 * @matches: zero-terminated array of possible matches
 *
 * returns the first matching entry of the argument array, or NULL
 * if none of them match.
 *
 * This function is meant as a helper in place of of_match_node()
 * in cases where either no device tree is available or the information
 * in a device node is insufficient to identify a particular variant
 * by its compatible strings or other properties. For new devices,
 * the DT binding should always provide unique compatible strings
 * that allow the use of of_match_node() instead.
 *
 * The calling function can use the .data entry of the
 * soc_device_attribute to pass a structure or function pointer for
 * each entry.
 */
const struct soc_device_attribute *soc_device_match(
	const struct soc_device_attribute *matches)
{
	int ret;

	if (!matches)
		return NULL;

	while (matches->machine || matches->family || matches->revision ||
	       matches->soc_id) {
		ret = bus_for_each_dev(&soc_bus_type, NULL, (void *)matches,
				       soc_device_match_one);
		if (ret < 0 && early_soc_dev_attr)
			ret = soc_device_match_attr(early_soc_dev_attr,
						    matches);
		if (ret < 0)
			return NULL;
		if (ret)
			return matches;

		matches++;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(soc_device_match);
