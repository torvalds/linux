// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Lee Jones <lee.jones@linaro.org> for ST-Ericsson.
 */

#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/spinlock.h>
#include <linux/sys_soc.h>
#include <linux/err.h>
#include <linux/glob.h>

static DEFINE_IDA(soc_ida);

static ssize_t soc_info_get(struct device *dev,
			    struct device_attribute *attr,
			    char *buf);

struct soc_device {
	struct device dev;
	struct soc_device_attribute *attr;
	int soc_dev_num;
};

static struct bus_type soc_bus_type = {
	.name  = "soc",
};

static DEVICE_ATTR(machine,  S_IRUGO, soc_info_get,  NULL);
static DEVICE_ATTR(family,   S_IRUGO, soc_info_get,  NULL);
static DEVICE_ATTR(serial_number, S_IRUGO, soc_info_get,  NULL);
static DEVICE_ATTR(soc_id,   S_IRUGO, soc_info_get,  NULL);
static DEVICE_ATTR(revision, S_IRUGO, soc_info_get,  NULL);

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

	if ((attr == &dev_attr_machine.attr)
	    && (soc_dev->attr->machine != NULL))
		return attr->mode;
	if ((attr == &dev_attr_family.attr)
	    && (soc_dev->attr->family != NULL))
		return attr->mode;
	if ((attr == &dev_attr_revision.attr)
	    && (soc_dev->attr->revision != NULL))
		return attr->mode;
	if ((attr == &dev_attr_serial_number.attr)
	    && (soc_dev->attr->serial_number != NULL))
		return attr->mode;
	if ((attr == &dev_attr_soc_id.attr)
	    && (soc_dev->attr->soc_id != NULL))
		return attr->mode;

	/* Unknown or unfilled attribute. */
	return 0;
}

static ssize_t soc_info_get(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct soc_device *soc_dev = container_of(dev, struct soc_device, dev);

	if (attr == &dev_attr_machine)
		return sprintf(buf, "%s\n", soc_dev->attr->machine);
	if (attr == &dev_attr_family)
		return sprintf(buf, "%s\n", soc_dev->attr->family);
	if (attr == &dev_attr_revision)
		return sprintf(buf, "%s\n", soc_dev->attr->revision);
	if (attr == &dev_attr_serial_number)
		return sprintf(buf, "%s\n", soc_dev->attr->serial_number);
	if (attr == &dev_attr_soc_id)
		return sprintf(buf, "%s\n", soc_dev->attr->soc_id);

	return -EINVAL;

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

	ida_simple_remove(&soc_ida, soc_dev->soc_dev_num);
	kfree(soc_dev->dev.groups);
	kfree(soc_dev);
}

static struct soc_device_attribute *early_soc_dev_attr;

struct soc_device *soc_device_register(struct soc_device_attribute *soc_dev_attr)
{
	struct soc_device *soc_dev;
	const struct attribute_group **soc_attr_groups;
	int ret;

	if (!soc_bus_type.p) {
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
	ret = ida_simple_get(&soc_ida, 0, 0, GFP_KERNEL);
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

/* Ensure soc_dev->attr is freed prior to calling soc_device_unregister. */
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
	int ret = 0;

	if (!matches)
		return NULL;

	while (!ret) {
		if (!(matches->machine || matches->family ||
		      matches->revision || matches->soc_id))
			break;
		ret = bus_for_each_dev(&soc_bus_type, NULL, (void *)matches,
				       soc_device_match_one);
		if (ret < 0 && early_soc_dev_attr)
			ret = soc_device_match_attr(early_soc_dev_attr,
						    matches);
		if (ret < 0)
			return NULL;
		if (!ret)
			matches++;
		else
			return matches;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(soc_device_match);
