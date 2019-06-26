// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Linaro Limited
 */
#include "nvmem.h"

static const char * const nvmem_type_str[] = {
	[NVMEM_TYPE_UNKNOWN] = "Unknown",
	[NVMEM_TYPE_EEPROM] = "EEPROM",
	[NVMEM_TYPE_OTP] = "OTP",
	[NVMEM_TYPE_BATTERY_BACKED] = "Battery backed",
};

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key eeprom_lock_key;
#endif

static ssize_t type_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct nvmem_device *nvmem = to_nvmem_device(dev);

	return sprintf(buf, "%s\n", nvmem_type_str[nvmem->type]);
}

static DEVICE_ATTR_RO(type);

static struct attribute *nvmem_attrs[] = {
	&dev_attr_type.attr,
	NULL,
};

static ssize_t bin_attr_nvmem_read(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *attr,
				    char *buf, loff_t pos, size_t count)
{
	struct device *dev;
	struct nvmem_device *nvmem;
	int rc;

	if (attr->private)
		dev = attr->private;
	else
		dev = container_of(kobj, struct device, kobj);
	nvmem = to_nvmem_device(dev);

	/* Stop the user from reading */
	if (pos >= nvmem->size)
		return 0;

	if (count < nvmem->word_size)
		return -EINVAL;

	if (pos + count > nvmem->size)
		count = nvmem->size - pos;

	count = round_down(count, nvmem->word_size);

	rc = nvmem->reg_read(nvmem->priv, pos, buf, count);

	if (rc)
		return rc;

	return count;
}

static ssize_t bin_attr_nvmem_write(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *attr,
				     char *buf, loff_t pos, size_t count)
{
	struct device *dev;
	struct nvmem_device *nvmem;
	int rc;

	if (attr->private)
		dev = attr->private;
	else
		dev = container_of(kobj, struct device, kobj);
	nvmem = to_nvmem_device(dev);

	/* Stop the user from writing */
	if (pos >= nvmem->size)
		return -EFBIG;

	if (count < nvmem->word_size)
		return -EINVAL;

	if (pos + count > nvmem->size)
		count = nvmem->size - pos;

	count = round_down(count, nvmem->word_size);

	rc = nvmem->reg_write(nvmem->priv, pos, buf, count);

	if (rc)
		return rc;

	return count;
}

/* default read/write permissions */
static struct bin_attribute bin_attr_rw_nvmem = {
	.attr	= {
		.name	= "nvmem",
		.mode	= 0644,
	},
	.read	= bin_attr_nvmem_read,
	.write	= bin_attr_nvmem_write,
};

static struct bin_attribute *nvmem_bin_rw_attributes[] = {
	&bin_attr_rw_nvmem,
	NULL,
};

static const struct attribute_group nvmem_bin_rw_group = {
	.bin_attrs	= nvmem_bin_rw_attributes,
	.attrs		= nvmem_attrs,
};

static const struct attribute_group *nvmem_rw_dev_groups[] = {
	&nvmem_bin_rw_group,
	NULL,
};

/* read only permission */
static struct bin_attribute bin_attr_ro_nvmem = {
	.attr	= {
		.name	= "nvmem",
		.mode	= 0444,
	},
	.read	= bin_attr_nvmem_read,
};

static struct bin_attribute *nvmem_bin_ro_attributes[] = {
	&bin_attr_ro_nvmem,
	NULL,
};

static const struct attribute_group nvmem_bin_ro_group = {
	.bin_attrs	= nvmem_bin_ro_attributes,
	.attrs		= nvmem_attrs,
};

static const struct attribute_group *nvmem_ro_dev_groups[] = {
	&nvmem_bin_ro_group,
	NULL,
};

/* default read/write permissions, root only */
static struct bin_attribute bin_attr_rw_root_nvmem = {
	.attr	= {
		.name	= "nvmem",
		.mode	= 0600,
	},
	.read	= bin_attr_nvmem_read,
	.write	= bin_attr_nvmem_write,
};

static struct bin_attribute *nvmem_bin_rw_root_attributes[] = {
	&bin_attr_rw_root_nvmem,
	NULL,
};

static const struct attribute_group nvmem_bin_rw_root_group = {
	.bin_attrs	= nvmem_bin_rw_root_attributes,
	.attrs		= nvmem_attrs,
};

static const struct attribute_group *nvmem_rw_root_dev_groups[] = {
	&nvmem_bin_rw_root_group,
	NULL,
};

/* read only permission, root only */
static struct bin_attribute bin_attr_ro_root_nvmem = {
	.attr	= {
		.name	= "nvmem",
		.mode	= 0400,
	},
	.read	= bin_attr_nvmem_read,
};

static struct bin_attribute *nvmem_bin_ro_root_attributes[] = {
	&bin_attr_ro_root_nvmem,
	NULL,
};

static const struct attribute_group nvmem_bin_ro_root_group = {
	.bin_attrs	= nvmem_bin_ro_root_attributes,
	.attrs		= nvmem_attrs,
};

static const struct attribute_group *nvmem_ro_root_dev_groups[] = {
	&nvmem_bin_ro_root_group,
	NULL,
};

const struct attribute_group **nvmem_sysfs_get_groups(
					struct nvmem_device *nvmem,
					const struct nvmem_config *config)
{
	if (config->root_only)
		return nvmem->read_only ?
			nvmem_ro_root_dev_groups :
			nvmem_rw_root_dev_groups;

	return nvmem->read_only ? nvmem_ro_dev_groups : nvmem_rw_dev_groups;
}

/*
 * nvmem_setup_compat() - Create an additional binary entry in
 * drivers sys directory, to be backwards compatible with the older
 * drivers/misc/eeprom drivers.
 */
int nvmem_sysfs_setup_compat(struct nvmem_device *nvmem,
			      const struct nvmem_config *config)
{
	int rval;

	if (!config->compat)
		return 0;

	if (!config->base_dev)
		return -EINVAL;

	if (nvmem->read_only)
		nvmem->eeprom = bin_attr_ro_root_nvmem;
	else
		nvmem->eeprom = bin_attr_rw_root_nvmem;
	nvmem->eeprom.attr.name = "eeprom";
	nvmem->eeprom.size = nvmem->size;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	nvmem->eeprom.attr.key = &eeprom_lock_key;
#endif
	nvmem->eeprom.private = &nvmem->dev;
	nvmem->base_dev = config->base_dev;

	rval = device_create_bin_file(nvmem->base_dev, &nvmem->eeprom);
	if (rval) {
		dev_err(&nvmem->dev,
			"Failed to create eeprom binary file %d\n", rval);
		return rval;
	}

	nvmem->flags |= FLAG_COMPAT;

	return 0;
}

void nvmem_sysfs_remove_compat(struct nvmem_device *nvmem,
			      const struct nvmem_config *config)
{
	if (config->compat)
		device_remove_bin_file(nvmem->base_dev, &nvmem->eeprom);
}
