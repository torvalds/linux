// SPDX-License-Identifier: GPL-2.0
/*
 * nvmem framework core.
 *
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 * Copyright (C) 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "internals.h"

#define to_nvmem_device(d) container_of(d, struct nvmem_device, dev)

#define FLAG_COMPAT		BIT(0)
struct nvmem_cell_entry {
	const char		*name;
	int			offset;
	size_t			raw_len;
	int			bytes;
	int			bit_offset;
	int			nbits;
	nvmem_cell_post_process_t read_post_process;
	void			*priv;
	struct device_node	*np;
	struct nvmem_device	*nvmem;
	struct list_head	node;
};

struct nvmem_cell {
	struct nvmem_cell_entry *entry;
	const char		*id;
	int			index;
};

static DEFINE_MUTEX(nvmem_mutex);
static DEFINE_IDA(nvmem_ida);

static DEFINE_MUTEX(nvmem_lookup_mutex);
static LIST_HEAD(nvmem_lookup_list);

static BLOCKING_NOTIFIER_HEAD(nvmem_notifier);

static int __nvmem_reg_read(struct nvmem_device *nvmem, unsigned int offset,
			    void *val, size_t bytes)
{
	if (nvmem->reg_read)
		return nvmem->reg_read(nvmem->priv, offset, val, bytes);

	return -EINVAL;
}

static int __nvmem_reg_write(struct nvmem_device *nvmem, unsigned int offset,
			     void *val, size_t bytes)
{
	int ret;

	if (nvmem->reg_write) {
		gpiod_set_value_cansleep(nvmem->wp_gpio, 0);
		ret = nvmem->reg_write(nvmem->priv, offset, val, bytes);
		gpiod_set_value_cansleep(nvmem->wp_gpio, 1);
		return ret;
	}

	return -EINVAL;
}

static int nvmem_access_with_keepouts(struct nvmem_device *nvmem,
				      unsigned int offset, void *val,
				      size_t bytes, int write)
{

	unsigned int end = offset + bytes;
	unsigned int kend, ksize;
	const struct nvmem_keepout *keepout = nvmem->keepout;
	const struct nvmem_keepout *keepoutend = keepout + nvmem->nkeepout;
	int rc;

	/*
	 * Skip all keepouts before the range being accessed.
	 * Keepouts are sorted.
	 */
	while ((keepout < keepoutend) && (keepout->end <= offset))
		keepout++;

	while ((offset < end) && (keepout < keepoutend)) {
		/* Access the valid portion before the keepout. */
		if (offset < keepout->start) {
			kend = min(end, keepout->start);
			ksize = kend - offset;
			if (write)
				rc = __nvmem_reg_write(nvmem, offset, val, ksize);
			else
				rc = __nvmem_reg_read(nvmem, offset, val, ksize);

			if (rc)
				return rc;

			offset += ksize;
			val += ksize;
		}

		/*
		 * Now we're aligned to the start of this keepout zone. Go
		 * through it.
		 */
		kend = min(end, keepout->end);
		ksize = kend - offset;
		if (!write)
			memset(val, keepout->value, ksize);

		val += ksize;
		offset += ksize;
		keepout++;
	}

	/*
	 * If we ran out of keepouts but there's still stuff to do, send it
	 * down directly
	 */
	if (offset < end) {
		ksize = end - offset;
		if (write)
			return __nvmem_reg_write(nvmem, offset, val, ksize);
		else
			return __nvmem_reg_read(nvmem, offset, val, ksize);
	}

	return 0;
}

static int nvmem_reg_read(struct nvmem_device *nvmem, unsigned int offset,
			  void *val, size_t bytes)
{
	if (!nvmem->nkeepout)
		return __nvmem_reg_read(nvmem, offset, val, bytes);

	return nvmem_access_with_keepouts(nvmem, offset, val, bytes, false);
}

static int nvmem_reg_write(struct nvmem_device *nvmem, unsigned int offset,
			   void *val, size_t bytes)
{
	if (!nvmem->nkeepout)
		return __nvmem_reg_write(nvmem, offset, val, bytes);

	return nvmem_access_with_keepouts(nvmem, offset, val, bytes, true);
}

#ifdef CONFIG_NVMEM_SYSFS
static const char * const nvmem_type_str[] = {
	[NVMEM_TYPE_UNKNOWN] = "Unknown",
	[NVMEM_TYPE_EEPROM] = "EEPROM",
	[NVMEM_TYPE_OTP] = "OTP",
	[NVMEM_TYPE_BATTERY_BACKED] = "Battery backed",
	[NVMEM_TYPE_FRAM] = "FRAM",
};

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key eeprom_lock_key;
#endif

static ssize_t type_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct nvmem_device *nvmem = to_nvmem_device(dev);

	return sysfs_emit(buf, "%s\n", nvmem_type_str[nvmem->type]);
}

static DEVICE_ATTR_RO(type);

static ssize_t force_ro_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct nvmem_device *nvmem = to_nvmem_device(dev);

	return sysfs_emit(buf, "%d\n", nvmem->read_only);
}

static ssize_t force_ro_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct nvmem_device *nvmem = to_nvmem_device(dev);
	int ret = kstrtobool(buf, &nvmem->read_only);

	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(force_ro);

static struct attribute *nvmem_attrs[] = {
	&dev_attr_force_ro.attr,
	&dev_attr_type.attr,
	NULL,
};

static ssize_t bin_attr_nvmem_read(struct file *filp, struct kobject *kobj,
				   const struct bin_attribute *attr, char *buf,
				   loff_t pos, size_t count)
{
	struct device *dev;
	struct nvmem_device *nvmem;
	int rc;

	if (attr->private)
		dev = attr->private;
	else
		dev = kobj_to_dev(kobj);
	nvmem = to_nvmem_device(dev);

	if (!IS_ALIGNED(pos, nvmem->stride))
		return -EINVAL;

	if (count < nvmem->word_size)
		return -EINVAL;

	count = round_down(count, nvmem->word_size);

	if (!nvmem->reg_read)
		return -EPERM;

	rc = nvmem_reg_read(nvmem, pos, buf, count);

	if (rc)
		return rc;

	return count;
}

static ssize_t bin_attr_nvmem_write(struct file *filp, struct kobject *kobj,
				    const struct bin_attribute *attr, char *buf,
				    loff_t pos, size_t count)
{
	struct device *dev;
	struct nvmem_device *nvmem;
	int rc;

	if (attr->private)
		dev = attr->private;
	else
		dev = kobj_to_dev(kobj);
	nvmem = to_nvmem_device(dev);

	if (!IS_ALIGNED(pos, nvmem->stride))
		return -EINVAL;

	if (count < nvmem->word_size)
		return -EINVAL;

	count = round_down(count, nvmem->word_size);

	if (!nvmem->reg_write || nvmem->read_only)
		return -EPERM;

	rc = nvmem_reg_write(nvmem, pos, buf, count);

	if (rc)
		return rc;

	return count;
}

static umode_t nvmem_bin_attr_get_umode(struct nvmem_device *nvmem)
{
	umode_t mode = 0400;

	if (!nvmem->root_only)
		mode |= 0044;

	if (!nvmem->read_only)
		mode |= 0200;

	if (!nvmem->reg_write)
		mode &= ~0200;

	if (!nvmem->reg_read)
		mode &= ~0444;

	return mode;
}

static umode_t nvmem_bin_attr_is_visible(struct kobject *kobj,
					 const struct bin_attribute *attr,
					 int i)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nvmem_device *nvmem = to_nvmem_device(dev);

	return nvmem_bin_attr_get_umode(nvmem);
}

static size_t nvmem_bin_attr_size(struct kobject *kobj,
				  const struct bin_attribute *attr,
				  int i)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nvmem_device *nvmem = to_nvmem_device(dev);

	return nvmem->size;
}

static umode_t nvmem_attr_is_visible(struct kobject *kobj,
				     struct attribute *attr, int i)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nvmem_device *nvmem = to_nvmem_device(dev);

	/*
	 * If the device has no .reg_write operation, do not allow
	 * configuration as read-write.
	 * If the device is set as read-only by configuration, it
	 * can be forced into read-write mode using the 'force_ro'
	 * attribute.
	 */
	if (attr == &dev_attr_force_ro.attr && !nvmem->reg_write)
		return 0;	/* Attribute not visible */

	return attr->mode;
}

static struct nvmem_cell *nvmem_create_cell(struct nvmem_cell_entry *entry,
					    const char *id, int index);

static ssize_t nvmem_cell_attr_read(struct file *filp, struct kobject *kobj,
				    const struct bin_attribute *attr, char *buf,
				    loff_t pos, size_t count)
{
	struct nvmem_cell_entry *entry;
	struct nvmem_cell *cell = NULL;
	size_t cell_sz, read_len;
	void *content;

	entry = attr->private;
	cell = nvmem_create_cell(entry, entry->name, 0);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	if (!cell)
		return -EINVAL;

	content = nvmem_cell_read(cell, &cell_sz);
	if (IS_ERR(content)) {
		read_len = PTR_ERR(content);
		goto destroy_cell;
	}

	read_len = min_t(unsigned int, cell_sz - pos, count);
	memcpy(buf, content + pos, read_len);
	kfree(content);

destroy_cell:
	kfree_const(cell->id);
	kfree(cell);

	return read_len;
}

/* default read/write permissions */
static const struct bin_attribute bin_attr_rw_nvmem = {
	.attr	= {
		.name	= "nvmem",
		.mode	= 0644,
	},
	.read_new	= bin_attr_nvmem_read,
	.write_new	= bin_attr_nvmem_write,
};

static const struct bin_attribute *const nvmem_bin_attributes[] = {
	&bin_attr_rw_nvmem,
	NULL,
};

static const struct attribute_group nvmem_bin_group = {
	.bin_attrs_new	= nvmem_bin_attributes,
	.attrs		= nvmem_attrs,
	.is_bin_visible = nvmem_bin_attr_is_visible,
	.bin_size	= nvmem_bin_attr_size,
	.is_visible	= nvmem_attr_is_visible,
};

static const struct attribute_group *nvmem_dev_groups[] = {
	&nvmem_bin_group,
	NULL,
};

static const struct bin_attribute bin_attr_nvmem_eeprom_compat = {
	.attr	= {
		.name	= "eeprom",
	},
	.read_new	= bin_attr_nvmem_read,
	.write_new	= bin_attr_nvmem_write,
};

/*
 * nvmem_setup_compat() - Create an additional binary entry in
 * drivers sys directory, to be backwards compatible with the older
 * drivers/misc/eeprom drivers.
 */
static int nvmem_sysfs_setup_compat(struct nvmem_device *nvmem,
				    const struct nvmem_config *config)
{
	int rval;

	if (!config->compat)
		return 0;

	if (!config->base_dev)
		return -EINVAL;

	nvmem->eeprom = bin_attr_nvmem_eeprom_compat;
	if (config->type == NVMEM_TYPE_FRAM)
		nvmem->eeprom.attr.name = "fram";
	nvmem->eeprom.attr.mode = nvmem_bin_attr_get_umode(nvmem);
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

static void nvmem_sysfs_remove_compat(struct nvmem_device *nvmem,
			      const struct nvmem_config *config)
{
	if (config->compat)
		device_remove_bin_file(nvmem->base_dev, &nvmem->eeprom);
}

static int nvmem_populate_sysfs_cells(struct nvmem_device *nvmem)
{
	struct attribute_group group = {
		.name	= "cells",
	};
	struct nvmem_cell_entry *entry;
	const struct bin_attribute **pattrs;
	struct bin_attribute *attrs;
	unsigned int ncells = 0, i = 0;
	int ret = 0;

	mutex_lock(&nvmem_mutex);

	if (list_empty(&nvmem->cells) || nvmem->sysfs_cells_populated)
		goto unlock_mutex;

	/* Allocate an array of attributes with a sentinel */
	ncells = list_count_nodes(&nvmem->cells);
	pattrs = devm_kcalloc(&nvmem->dev, ncells + 1,
			      sizeof(struct bin_attribute *), GFP_KERNEL);
	if (!pattrs) {
		ret = -ENOMEM;
		goto unlock_mutex;
	}

	attrs = devm_kcalloc(&nvmem->dev, ncells, sizeof(struct bin_attribute), GFP_KERNEL);
	if (!attrs) {
		ret = -ENOMEM;
		goto unlock_mutex;
	}

	/* Initialize each attribute to take the name and size of the cell */
	list_for_each_entry(entry, &nvmem->cells, node) {
		sysfs_bin_attr_init(&attrs[i]);
		attrs[i].attr.name = devm_kasprintf(&nvmem->dev, GFP_KERNEL,
						    "%s@%x,%x", entry->name,
						    entry->offset,
						    entry->bit_offset);
		attrs[i].attr.mode = 0444 & nvmem_bin_attr_get_umode(nvmem);
		attrs[i].size = entry->bytes;
		attrs[i].read_new = &nvmem_cell_attr_read;
		attrs[i].private = entry;
		if (!attrs[i].attr.name) {
			ret = -ENOMEM;
			goto unlock_mutex;
		}

		pattrs[i] = &attrs[i];
		i++;
	}

	group.bin_attrs_new = pattrs;

	ret = device_add_group(&nvmem->dev, &group);
	if (ret)
		goto unlock_mutex;

	nvmem->sysfs_cells_populated = true;

unlock_mutex:
	mutex_unlock(&nvmem_mutex);

	return ret;
}

#else /* CONFIG_NVMEM_SYSFS */

static int nvmem_sysfs_setup_compat(struct nvmem_device *nvmem,
				    const struct nvmem_config *config)
{
	return -ENOSYS;
}
static void nvmem_sysfs_remove_compat(struct nvmem_device *nvmem,
				      const struct nvmem_config *config)
{
}

#endif /* CONFIG_NVMEM_SYSFS */

static void nvmem_release(struct device *dev)
{
	struct nvmem_device *nvmem = to_nvmem_device(dev);

	ida_free(&nvmem_ida, nvmem->id);
	gpiod_put(nvmem->wp_gpio);
	kfree(nvmem);
}

static const struct device_type nvmem_provider_type = {
	.release	= nvmem_release,
};

static struct bus_type nvmem_bus_type = {
	.name		= "nvmem",
};

static void nvmem_cell_entry_drop(struct nvmem_cell_entry *cell)
{
	blocking_notifier_call_chain(&nvmem_notifier, NVMEM_CELL_REMOVE, cell);
	mutex_lock(&nvmem_mutex);
	list_del(&cell->node);
	mutex_unlock(&nvmem_mutex);
	of_node_put(cell->np);
	kfree_const(cell->name);
	kfree(cell);
}

static void nvmem_device_remove_all_cells(const struct nvmem_device *nvmem)
{
	struct nvmem_cell_entry *cell, *p;

	list_for_each_entry_safe(cell, p, &nvmem->cells, node)
		nvmem_cell_entry_drop(cell);
}

static void nvmem_cell_entry_add(struct nvmem_cell_entry *cell)
{
	mutex_lock(&nvmem_mutex);
	list_add_tail(&cell->node, &cell->nvmem->cells);
	mutex_unlock(&nvmem_mutex);
	blocking_notifier_call_chain(&nvmem_notifier, NVMEM_CELL_ADD, cell);
}

static int nvmem_cell_info_to_nvmem_cell_entry_nodup(struct nvmem_device *nvmem,
						     const struct nvmem_cell_info *info,
						     struct nvmem_cell_entry *cell)
{
	cell->nvmem = nvmem;
	cell->offset = info->offset;
	cell->raw_len = info->raw_len ?: info->bytes;
	cell->bytes = info->bytes;
	cell->name = info->name;
	cell->read_post_process = info->read_post_process;
	cell->priv = info->priv;

	cell->bit_offset = info->bit_offset;
	cell->nbits = info->nbits;
	cell->np = info->np;

	if (cell->nbits) {
		cell->bytes = DIV_ROUND_UP(cell->nbits + cell->bit_offset,
					   BITS_PER_BYTE);
		cell->raw_len = ALIGN(cell->bytes, nvmem->word_size);
	}

	if (!IS_ALIGNED(cell->offset, nvmem->stride)) {
		dev_err(&nvmem->dev,
			"cell %s unaligned to nvmem stride %d\n",
			cell->name ?: "<unknown>", nvmem->stride);
		return -EINVAL;
	}

	if (!IS_ALIGNED(cell->raw_len, nvmem->word_size)) {
		dev_err(&nvmem->dev,
			"cell %s raw len %zd unaligned to nvmem word size %d\n",
			cell->name ?: "<unknown>", cell->raw_len,
			nvmem->word_size);

		if (info->raw_len)
			return -EINVAL;

		cell->raw_len = ALIGN(cell->raw_len, nvmem->word_size);
	}

	return 0;
}

static int nvmem_cell_info_to_nvmem_cell_entry(struct nvmem_device *nvmem,
					       const struct nvmem_cell_info *info,
					       struct nvmem_cell_entry *cell)
{
	int err;

	err = nvmem_cell_info_to_nvmem_cell_entry_nodup(nvmem, info, cell);
	if (err)
		return err;

	cell->name = kstrdup_const(info->name, GFP_KERNEL);
	if (!cell->name)
		return -ENOMEM;

	return 0;
}

/**
 * nvmem_add_one_cell() - Add one cell information to an nvmem device
 *
 * @nvmem: nvmem device to add cells to.
 * @info: nvmem cell info to add to the device
 *
 * Return: 0 or negative error code on failure.
 */
int nvmem_add_one_cell(struct nvmem_device *nvmem,
		       const struct nvmem_cell_info *info)
{
	struct nvmem_cell_entry *cell;
	int rval;

	cell = kzalloc(sizeof(*cell), GFP_KERNEL);
	if (!cell)
		return -ENOMEM;

	rval = nvmem_cell_info_to_nvmem_cell_entry(nvmem, info, cell);
	if (rval) {
		kfree(cell);
		return rval;
	}

	nvmem_cell_entry_add(cell);

	return 0;
}
EXPORT_SYMBOL_GPL(nvmem_add_one_cell);

/**
 * nvmem_add_cells() - Add cell information to an nvmem device
 *
 * @nvmem: nvmem device to add cells to.
 * @info: nvmem cell info to add to the device
 * @ncells: number of cells in info
 *
 * Return: 0 or negative error code on failure.
 */
static int nvmem_add_cells(struct nvmem_device *nvmem,
		    const struct nvmem_cell_info *info,
		    int ncells)
{
	int i, rval;

	for (i = 0; i < ncells; i++) {
		rval = nvmem_add_one_cell(nvmem, &info[i]);
		if (rval)
			return rval;
	}

	return 0;
}

/**
 * nvmem_register_notifier() - Register a notifier block for nvmem events.
 *
 * @nb: notifier block to be called on nvmem events.
 *
 * Return: 0 on success, negative error number on failure.
 */
int nvmem_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&nvmem_notifier, nb);
}
EXPORT_SYMBOL_GPL(nvmem_register_notifier);

/**
 * nvmem_unregister_notifier() - Unregister a notifier block for nvmem events.
 *
 * @nb: notifier block to be unregistered.
 *
 * Return: 0 on success, negative error number on failure.
 */
int nvmem_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&nvmem_notifier, nb);
}
EXPORT_SYMBOL_GPL(nvmem_unregister_notifier);

static struct nvmem_cell_entry *
nvmem_find_cell_entry_by_name(struct nvmem_device *nvmem, const char *cell_id)
{
	struct nvmem_cell_entry *iter, *cell = NULL;

	mutex_lock(&nvmem_mutex);
	list_for_each_entry(iter, &nvmem->cells, node) {
		if (strcmp(cell_id, iter->name) == 0) {
			cell = iter;
			break;
		}
	}
	mutex_unlock(&nvmem_mutex);

	return cell;
}

static int nvmem_validate_keepouts(struct nvmem_device *nvmem)
{
	unsigned int cur = 0;
	const struct nvmem_keepout *keepout = nvmem->keepout;
	const struct nvmem_keepout *keepoutend = keepout + nvmem->nkeepout;

	while (keepout < keepoutend) {
		/* Ensure keepouts are sorted and don't overlap. */
		if (keepout->start < cur) {
			dev_err(&nvmem->dev,
				"Keepout regions aren't sorted or overlap.\n");

			return -ERANGE;
		}

		if (keepout->end < keepout->start) {
			dev_err(&nvmem->dev,
				"Invalid keepout region.\n");

			return -EINVAL;
		}

		/*
		 * Validate keepouts (and holes between) don't violate
		 * word_size constraints.
		 */
		if ((keepout->end - keepout->start < nvmem->word_size) ||
		    ((keepout->start != cur) &&
		     (keepout->start - cur < nvmem->word_size))) {

			dev_err(&nvmem->dev,
				"Keepout regions violate word_size constraints.\n");

			return -ERANGE;
		}

		/* Validate keepouts don't violate stride (alignment). */
		if (!IS_ALIGNED(keepout->start, nvmem->stride) ||
		    !IS_ALIGNED(keepout->end, nvmem->stride)) {

			dev_err(&nvmem->dev,
				"Keepout regions violate stride.\n");

			return -EINVAL;
		}

		cur = keepout->end;
		keepout++;
	}

	return 0;
}

static int nvmem_add_cells_from_dt(struct nvmem_device *nvmem, struct device_node *np)
{
	struct device *dev = &nvmem->dev;
	struct device_node *child;
	const __be32 *addr;
	int len, ret;

	for_each_child_of_node(np, child) {
		struct nvmem_cell_info info = {0};

		addr = of_get_property(child, "reg", &len);
		if (!addr)
			continue;
		if (len < 2 * sizeof(u32)) {
			dev_err(dev, "nvmem: invalid reg on %pOF\n", child);
			of_node_put(child);
			return -EINVAL;
		}

		info.offset = be32_to_cpup(addr++);
		info.bytes = be32_to_cpup(addr);
		info.name = kasprintf(GFP_KERNEL, "%pOFn", child);

		addr = of_get_property(child, "bits", &len);
		if (addr && len == (2 * sizeof(u32))) {
			info.bit_offset = be32_to_cpup(addr++);
			info.nbits = be32_to_cpup(addr);
			if (info.bit_offset >= BITS_PER_BYTE * info.bytes ||
			    info.nbits < 1 ||
			    info.bit_offset + info.nbits > BITS_PER_BYTE * info.bytes) {
				dev_err(dev, "nvmem: invalid bits on %pOF\n", child);
				of_node_put(child);
				return -EINVAL;
			}
		}

		info.np = of_node_get(child);

		if (nvmem->fixup_dt_cell_info)
			nvmem->fixup_dt_cell_info(nvmem, &info);

		ret = nvmem_add_one_cell(nvmem, &info);
		kfree(info.name);
		if (ret) {
			of_node_put(child);
			return ret;
		}
	}

	return 0;
}

static int nvmem_add_cells_from_legacy_of(struct nvmem_device *nvmem)
{
	return nvmem_add_cells_from_dt(nvmem, nvmem->dev.of_node);
}

static int nvmem_add_cells_from_fixed_layout(struct nvmem_device *nvmem)
{
	struct device_node *layout_np;
	int err = 0;

	layout_np = of_nvmem_layout_get_container(nvmem);
	if (!layout_np)
		return 0;

	if (of_device_is_compatible(layout_np, "fixed-layout"))
		err = nvmem_add_cells_from_dt(nvmem, layout_np);

	of_node_put(layout_np);

	return err;
}

int nvmem_layout_register(struct nvmem_layout *layout)
{
	int ret;

	if (!layout->add_cells)
		return -EINVAL;

	/* Populate the cells */
	ret = layout->add_cells(layout);
	if (ret)
		return ret;

#ifdef CONFIG_NVMEM_SYSFS
	ret = nvmem_populate_sysfs_cells(layout->nvmem);
	if (ret) {
		nvmem_device_remove_all_cells(layout->nvmem);
		return ret;
	}
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(nvmem_layout_register);

void nvmem_layout_unregister(struct nvmem_layout *layout)
{
	/* Keep the API even with an empty stub in case we need it later */
}
EXPORT_SYMBOL_GPL(nvmem_layout_unregister);

/**
 * nvmem_register() - Register a nvmem device for given nvmem_config.
 * Also creates a binary entry in /sys/bus/nvmem/devices/dev-name/nvmem
 *
 * @config: nvmem device configuration with which nvmem device is created.
 *
 * Return: Will be an ERR_PTR() on error or a valid pointer to nvmem_device
 * on success.
 */

struct nvmem_device *nvmem_register(const struct nvmem_config *config)
{
	struct nvmem_device *nvmem;
	int rval;

	if (!config->dev)
		return ERR_PTR(-EINVAL);

	if (!config->reg_read && !config->reg_write)
		return ERR_PTR(-EINVAL);

	nvmem = kzalloc(sizeof(*nvmem), GFP_KERNEL);
	if (!nvmem)
		return ERR_PTR(-ENOMEM);

	rval = ida_alloc(&nvmem_ida, GFP_KERNEL);
	if (rval < 0) {
		kfree(nvmem);
		return ERR_PTR(rval);
	}

	nvmem->id = rval;

	nvmem->dev.type = &nvmem_provider_type;
	nvmem->dev.bus = &nvmem_bus_type;
	nvmem->dev.parent = config->dev;

	device_initialize(&nvmem->dev);

	if (!config->ignore_wp)
		nvmem->wp_gpio = gpiod_get_optional(config->dev, "wp",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(nvmem->wp_gpio)) {
		rval = PTR_ERR(nvmem->wp_gpio);
		nvmem->wp_gpio = NULL;
		goto err_put_device;
	}

	kref_init(&nvmem->refcnt);
	INIT_LIST_HEAD(&nvmem->cells);
	nvmem->fixup_dt_cell_info = config->fixup_dt_cell_info;

	nvmem->owner = config->owner;
	if (!nvmem->owner && config->dev->driver)
		nvmem->owner = config->dev->driver->owner;
	nvmem->stride = config->stride ?: 1;
	nvmem->word_size = config->word_size ?: 1;
	nvmem->size = config->size;
	nvmem->root_only = config->root_only;
	nvmem->priv = config->priv;
	nvmem->type = config->type;
	nvmem->reg_read = config->reg_read;
	nvmem->reg_write = config->reg_write;
	nvmem->keepout = config->keepout;
	nvmem->nkeepout = config->nkeepout;
	if (config->of_node)
		nvmem->dev.of_node = config->of_node;
	else
		nvmem->dev.of_node = config->dev->of_node;

	switch (config->id) {
	case NVMEM_DEVID_NONE:
		rval = dev_set_name(&nvmem->dev, "%s", config->name);
		break;
	case NVMEM_DEVID_AUTO:
		rval = dev_set_name(&nvmem->dev, "%s%d", config->name, nvmem->id);
		break;
	default:
		rval = dev_set_name(&nvmem->dev, "%s%d",
			     config->name ? : "nvmem",
			     config->name ? config->id : nvmem->id);
		break;
	}

	if (rval)
		goto err_put_device;

	nvmem->read_only = device_property_present(config->dev, "read-only") ||
			   config->read_only || !nvmem->reg_write;

#ifdef CONFIG_NVMEM_SYSFS
	nvmem->dev.groups = nvmem_dev_groups;
#endif

	if (nvmem->nkeepout) {
		rval = nvmem_validate_keepouts(nvmem);
		if (rval)
			goto err_put_device;
	}

	if (config->compat) {
		rval = nvmem_sysfs_setup_compat(nvmem, config);
		if (rval)
			goto err_put_device;
	}

	if (config->cells) {
		rval = nvmem_add_cells(nvmem, config->cells, config->ncells);
		if (rval)
			goto err_remove_cells;
	}

	if (config->add_legacy_fixed_of_cells) {
		rval = nvmem_add_cells_from_legacy_of(nvmem);
		if (rval)
			goto err_remove_cells;
	}

	rval = nvmem_add_cells_from_fixed_layout(nvmem);
	if (rval)
		goto err_remove_cells;

	dev_dbg(&nvmem->dev, "Registering nvmem device %s\n", config->name);

	rval = device_add(&nvmem->dev);
	if (rval)
		goto err_remove_cells;

	rval = nvmem_populate_layout(nvmem);
	if (rval)
		goto err_remove_dev;

#ifdef CONFIG_NVMEM_SYSFS
	rval = nvmem_populate_sysfs_cells(nvmem);
	if (rval)
		goto err_destroy_layout;
#endif

	blocking_notifier_call_chain(&nvmem_notifier, NVMEM_ADD, nvmem);

	return nvmem;

#ifdef CONFIG_NVMEM_SYSFS
err_destroy_layout:
	nvmem_destroy_layout(nvmem);
#endif
err_remove_dev:
	device_del(&nvmem->dev);
err_remove_cells:
	nvmem_device_remove_all_cells(nvmem);
	if (config->compat)
		nvmem_sysfs_remove_compat(nvmem, config);
err_put_device:
	put_device(&nvmem->dev);

	return ERR_PTR(rval);
}
EXPORT_SYMBOL_GPL(nvmem_register);

static void nvmem_device_release(struct kref *kref)
{
	struct nvmem_device *nvmem;

	nvmem = container_of(kref, struct nvmem_device, refcnt);

	blocking_notifier_call_chain(&nvmem_notifier, NVMEM_REMOVE, nvmem);

	if (nvmem->flags & FLAG_COMPAT)
		device_remove_bin_file(nvmem->base_dev, &nvmem->eeprom);

	nvmem_device_remove_all_cells(nvmem);
	nvmem_destroy_layout(nvmem);
	device_unregister(&nvmem->dev);
}

/**
 * nvmem_unregister() - Unregister previously registered nvmem device
 *
 * @nvmem: Pointer to previously registered nvmem device.
 */
void nvmem_unregister(struct nvmem_device *nvmem)
{
	if (nvmem)
		kref_put(&nvmem->refcnt, nvmem_device_release);
}
EXPORT_SYMBOL_GPL(nvmem_unregister);

static void devm_nvmem_unregister(void *nvmem)
{
	nvmem_unregister(nvmem);
}

/**
 * devm_nvmem_register() - Register a managed nvmem device for given
 * nvmem_config.
 * Also creates a binary entry in /sys/bus/nvmem/devices/dev-name/nvmem
 *
 * @dev: Device that uses the nvmem device.
 * @config: nvmem device configuration with which nvmem device is created.
 *
 * Return: Will be an ERR_PTR() on error or a valid pointer to nvmem_device
 * on success.
 */
struct nvmem_device *devm_nvmem_register(struct device *dev,
					 const struct nvmem_config *config)
{
	struct nvmem_device *nvmem;
	int ret;

	nvmem = nvmem_register(config);
	if (IS_ERR(nvmem))
		return nvmem;

	ret = devm_add_action_or_reset(dev, devm_nvmem_unregister, nvmem);
	if (ret)
		return ERR_PTR(ret);

	return nvmem;
}
EXPORT_SYMBOL_GPL(devm_nvmem_register);

static struct nvmem_device *__nvmem_device_get(void *data,
			int (*match)(struct device *dev, const void *data))
{
	struct nvmem_device *nvmem = NULL;
	struct device *dev;

	mutex_lock(&nvmem_mutex);
	dev = bus_find_device(&nvmem_bus_type, NULL, data, match);
	if (dev)
		nvmem = to_nvmem_device(dev);
	mutex_unlock(&nvmem_mutex);
	if (!nvmem)
		return ERR_PTR(-EPROBE_DEFER);

	if (!try_module_get(nvmem->owner)) {
		dev_err(&nvmem->dev,
			"could not increase module refcount for cell %s\n",
			nvmem_dev_name(nvmem));

		put_device(&nvmem->dev);
		return ERR_PTR(-EINVAL);
	}

	kref_get(&nvmem->refcnt);

	return nvmem;
}

static void __nvmem_device_put(struct nvmem_device *nvmem)
{
	put_device(&nvmem->dev);
	module_put(nvmem->owner);
	kref_put(&nvmem->refcnt, nvmem_device_release);
}

#if IS_ENABLED(CONFIG_OF)
/**
 * of_nvmem_device_get() - Get nvmem device from a given id
 *
 * @np: Device tree node that uses the nvmem device.
 * @id: nvmem name from nvmem-names property.
 *
 * Return: ERR_PTR() on error or a valid pointer to a struct nvmem_device
 * on success.
 */
struct nvmem_device *of_nvmem_device_get(struct device_node *np, const char *id)
{

	struct device_node *nvmem_np;
	struct nvmem_device *nvmem;
	int index = 0;

	if (id)
		index = of_property_match_string(np, "nvmem-names", id);

	nvmem_np = of_parse_phandle(np, "nvmem", index);
	if (!nvmem_np)
		return ERR_PTR(-ENOENT);

	nvmem = __nvmem_device_get(nvmem_np, device_match_of_node);
	of_node_put(nvmem_np);
	return nvmem;
}
EXPORT_SYMBOL_GPL(of_nvmem_device_get);
#endif

/**
 * nvmem_device_get() - Get nvmem device from a given id
 *
 * @dev: Device that uses the nvmem device.
 * @dev_name: name of the requested nvmem device.
 *
 * Return: ERR_PTR() on error or a valid pointer to a struct nvmem_device
 * on success.
 */
struct nvmem_device *nvmem_device_get(struct device *dev, const char *dev_name)
{
	if (dev->of_node) { /* try dt first */
		struct nvmem_device *nvmem;

		nvmem = of_nvmem_device_get(dev->of_node, dev_name);

		if (!IS_ERR(nvmem) || PTR_ERR(nvmem) == -EPROBE_DEFER)
			return nvmem;

	}

	return __nvmem_device_get((void *)dev_name, device_match_name);
}
EXPORT_SYMBOL_GPL(nvmem_device_get);

/**
 * nvmem_device_find() - Find nvmem device with matching function
 *
 * @data: Data to pass to match function
 * @match: Callback function to check device
 *
 * Return: ERR_PTR() on error or a valid pointer to a struct nvmem_device
 * on success.
 */
struct nvmem_device *nvmem_device_find(void *data,
			int (*match)(struct device *dev, const void *data))
{
	return __nvmem_device_get(data, match);
}
EXPORT_SYMBOL_GPL(nvmem_device_find);

static int devm_nvmem_device_match(struct device *dev, void *res, void *data)
{
	struct nvmem_device **nvmem = res;

	if (WARN_ON(!nvmem || !*nvmem))
		return 0;

	return *nvmem == data;
}

static void devm_nvmem_device_release(struct device *dev, void *res)
{
	nvmem_device_put(*(struct nvmem_device **)res);
}

/**
 * devm_nvmem_device_put() - put already got nvmem device
 *
 * @dev: Device that uses the nvmem device.
 * @nvmem: pointer to nvmem device allocated by devm_nvmem_cell_get(),
 * that needs to be released.
 */
void devm_nvmem_device_put(struct device *dev, struct nvmem_device *nvmem)
{
	int ret;

	ret = devres_release(dev, devm_nvmem_device_release,
			     devm_nvmem_device_match, nvmem);

	WARN_ON(ret);
}
EXPORT_SYMBOL_GPL(devm_nvmem_device_put);

/**
 * nvmem_device_put() - put already got nvmem device
 *
 * @nvmem: pointer to nvmem device that needs to be released.
 */
void nvmem_device_put(struct nvmem_device *nvmem)
{
	__nvmem_device_put(nvmem);
}
EXPORT_SYMBOL_GPL(nvmem_device_put);

/**
 * devm_nvmem_device_get() - Get nvmem device of device form a given id
 *
 * @dev: Device that requests the nvmem device.
 * @id: name id for the requested nvmem device.
 *
 * Return: ERR_PTR() on error or a valid pointer to a struct nvmem_device
 * on success.  The nvmem_device will be freed by the automatically once the
 * device is freed.
 */
struct nvmem_device *devm_nvmem_device_get(struct device *dev, const char *id)
{
	struct nvmem_device **ptr, *nvmem;

	ptr = devres_alloc(devm_nvmem_device_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	nvmem = nvmem_device_get(dev, id);
	if (!IS_ERR(nvmem)) {
		*ptr = nvmem;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return nvmem;
}
EXPORT_SYMBOL_GPL(devm_nvmem_device_get);

static struct nvmem_cell *nvmem_create_cell(struct nvmem_cell_entry *entry,
					    const char *id, int index)
{
	struct nvmem_cell *cell;
	const char *name = NULL;

	cell = kzalloc(sizeof(*cell), GFP_KERNEL);
	if (!cell)
		return ERR_PTR(-ENOMEM);

	if (id) {
		name = kstrdup_const(id, GFP_KERNEL);
		if (!name) {
			kfree(cell);
			return ERR_PTR(-ENOMEM);
		}
	}

	cell->id = name;
	cell->entry = entry;
	cell->index = index;

	return cell;
}

static struct nvmem_cell *
nvmem_cell_get_from_lookup(struct device *dev, const char *con_id)
{
	struct nvmem_cell_entry *cell_entry;
	struct nvmem_cell *cell = ERR_PTR(-ENOENT);
	struct nvmem_cell_lookup *lookup;
	struct nvmem_device *nvmem;
	const char *dev_id;

	if (!dev)
		return ERR_PTR(-EINVAL);

	dev_id = dev_name(dev);

	mutex_lock(&nvmem_lookup_mutex);

	list_for_each_entry(lookup, &nvmem_lookup_list, node) {
		if ((strcmp(lookup->dev_id, dev_id) == 0) &&
		    (strcmp(lookup->con_id, con_id) == 0)) {
			/* This is the right entry. */
			nvmem = __nvmem_device_get((void *)lookup->nvmem_name,
						   device_match_name);
			if (IS_ERR(nvmem)) {
				/* Provider may not be registered yet. */
				cell = ERR_CAST(nvmem);
				break;
			}

			cell_entry = nvmem_find_cell_entry_by_name(nvmem,
								   lookup->cell_name);
			if (!cell_entry) {
				__nvmem_device_put(nvmem);
				cell = ERR_PTR(-ENOENT);
			} else {
				cell = nvmem_create_cell(cell_entry, con_id, 0);
				if (IS_ERR(cell))
					__nvmem_device_put(nvmem);
			}
			break;
		}
	}

	mutex_unlock(&nvmem_lookup_mutex);
	return cell;
}

static void nvmem_layout_module_put(struct nvmem_device *nvmem)
{
	if (nvmem->layout && nvmem->layout->dev.driver)
		module_put(nvmem->layout->dev.driver->owner);
}

#if IS_ENABLED(CONFIG_OF)
static struct nvmem_cell_entry *
nvmem_find_cell_entry_by_node(struct nvmem_device *nvmem, struct device_node *np)
{
	struct nvmem_cell_entry *iter, *cell = NULL;

	mutex_lock(&nvmem_mutex);
	list_for_each_entry(iter, &nvmem->cells, node) {
		if (np == iter->np) {
			cell = iter;
			break;
		}
	}
	mutex_unlock(&nvmem_mutex);

	return cell;
}

static int nvmem_layout_module_get_optional(struct nvmem_device *nvmem)
{
	if (!nvmem->layout)
		return 0;

	if (!nvmem->layout->dev.driver ||
	    !try_module_get(nvmem->layout->dev.driver->owner))
		return -EPROBE_DEFER;

	return 0;
}

/**
 * of_nvmem_cell_get() - Get a nvmem cell from given device node and cell id
 *
 * @np: Device tree node that uses the nvmem cell.
 * @id: nvmem cell name from nvmem-cell-names property, or NULL
 *      for the cell at index 0 (the lone cell with no accompanying
 *      nvmem-cell-names property).
 *
 * Return: Will be an ERR_PTR() on error or a valid pointer
 * to a struct nvmem_cell.  The nvmem_cell will be freed by the
 * nvmem_cell_put().
 */
struct nvmem_cell *of_nvmem_cell_get(struct device_node *np, const char *id)
{
	struct device_node *cell_np, *nvmem_np;
	struct nvmem_device *nvmem;
	struct nvmem_cell_entry *cell_entry;
	struct nvmem_cell *cell;
	struct of_phandle_args cell_spec;
	int index = 0;
	int cell_index = 0;
	int ret;

	/* if cell name exists, find index to the name */
	if (id)
		index = of_property_match_string(np, "nvmem-cell-names", id);

	ret = of_parse_phandle_with_optional_args(np, "nvmem-cells",
						  "#nvmem-cell-cells",
						  index, &cell_spec);
	if (ret)
		return ERR_PTR(-ENOENT);

	if (cell_spec.args_count > 1)
		return ERR_PTR(-EINVAL);

	cell_np = cell_spec.np;
	if (cell_spec.args_count)
		cell_index = cell_spec.args[0];

	nvmem_np = of_get_parent(cell_np);
	if (!nvmem_np) {
		of_node_put(cell_np);
		return ERR_PTR(-EINVAL);
	}

	/* nvmem layouts produce cells within the nvmem-layout container */
	if (of_node_name_eq(nvmem_np, "nvmem-layout")) {
		nvmem_np = of_get_next_parent(nvmem_np);
		if (!nvmem_np) {
			of_node_put(cell_np);
			return ERR_PTR(-EINVAL);
		}
	}

	nvmem = __nvmem_device_get(nvmem_np, device_match_of_node);
	of_node_put(nvmem_np);
	if (IS_ERR(nvmem)) {
		of_node_put(cell_np);
		return ERR_CAST(nvmem);
	}

	ret = nvmem_layout_module_get_optional(nvmem);
	if (ret) {
		of_node_put(cell_np);
		__nvmem_device_put(nvmem);
		return ERR_PTR(ret);
	}

	cell_entry = nvmem_find_cell_entry_by_node(nvmem, cell_np);
	of_node_put(cell_np);
	if (!cell_entry) {
		__nvmem_device_put(nvmem);
		nvmem_layout_module_put(nvmem);
		if (nvmem->layout)
			return ERR_PTR(-EPROBE_DEFER);
		else
			return ERR_PTR(-ENOENT);
	}

	cell = nvmem_create_cell(cell_entry, id, cell_index);
	if (IS_ERR(cell)) {
		__nvmem_device_put(nvmem);
		nvmem_layout_module_put(nvmem);
	}

	return cell;
}
EXPORT_SYMBOL_GPL(of_nvmem_cell_get);
#endif

/**
 * nvmem_cell_get() - Get nvmem cell of device form a given cell name
 *
 * @dev: Device that requests the nvmem cell.
 * @id: nvmem cell name to get (this corresponds with the name from the
 *      nvmem-cell-names property for DT systems and with the con_id from
 *      the lookup entry for non-DT systems).
 *
 * Return: Will be an ERR_PTR() on error or a valid pointer
 * to a struct nvmem_cell.  The nvmem_cell will be freed by the
 * nvmem_cell_put().
 */
struct nvmem_cell *nvmem_cell_get(struct device *dev, const char *id)
{
	struct nvmem_cell *cell;

	if (dev->of_node) { /* try dt first */
		cell = of_nvmem_cell_get(dev->of_node, id);
		if (!IS_ERR(cell) || PTR_ERR(cell) == -EPROBE_DEFER)
			return cell;
	}

	/* NULL cell id only allowed for device tree; invalid otherwise */
	if (!id)
		return ERR_PTR(-EINVAL);

	return nvmem_cell_get_from_lookup(dev, id);
}
EXPORT_SYMBOL_GPL(nvmem_cell_get);

static void devm_nvmem_cell_release(struct device *dev, void *res)
{
	nvmem_cell_put(*(struct nvmem_cell **)res);
}

/**
 * devm_nvmem_cell_get() - Get nvmem cell of device form a given id
 *
 * @dev: Device that requests the nvmem cell.
 * @id: nvmem cell name id to get.
 *
 * Return: Will be an ERR_PTR() on error or a valid pointer
 * to a struct nvmem_cell.  The nvmem_cell will be freed by the
 * automatically once the device is freed.
 */
struct nvmem_cell *devm_nvmem_cell_get(struct device *dev, const char *id)
{
	struct nvmem_cell **ptr, *cell;

	ptr = devres_alloc(devm_nvmem_cell_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	cell = nvmem_cell_get(dev, id);
	if (!IS_ERR(cell)) {
		*ptr = cell;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return cell;
}
EXPORT_SYMBOL_GPL(devm_nvmem_cell_get);

static int devm_nvmem_cell_match(struct device *dev, void *res, void *data)
{
	struct nvmem_cell **c = res;

	if (WARN_ON(!c || !*c))
		return 0;

	return *c == data;
}

/**
 * devm_nvmem_cell_put() - Release previously allocated nvmem cell
 * from devm_nvmem_cell_get.
 *
 * @dev: Device that requests the nvmem cell.
 * @cell: Previously allocated nvmem cell by devm_nvmem_cell_get().
 */
void devm_nvmem_cell_put(struct device *dev, struct nvmem_cell *cell)
{
	int ret;

	ret = devres_release(dev, devm_nvmem_cell_release,
				devm_nvmem_cell_match, cell);

	WARN_ON(ret);
}
EXPORT_SYMBOL(devm_nvmem_cell_put);

/**
 * nvmem_cell_put() - Release previously allocated nvmem cell.
 *
 * @cell: Previously allocated nvmem cell by nvmem_cell_get().
 */
void nvmem_cell_put(struct nvmem_cell *cell)
{
	struct nvmem_device *nvmem = cell->entry->nvmem;

	if (cell->id)
		kfree_const(cell->id);

	kfree(cell);
	__nvmem_device_put(nvmem);
	nvmem_layout_module_put(nvmem);
}
EXPORT_SYMBOL_GPL(nvmem_cell_put);

static void nvmem_shift_read_buffer_in_place(struct nvmem_cell_entry *cell, void *buf)
{
	u8 *p, *b;
	int i, extra, bytes_offset;
	int bit_offset = cell->bit_offset;

	p = b = buf;

	bytes_offset = bit_offset / BITS_PER_BYTE;
	b += bytes_offset;
	bit_offset %= BITS_PER_BYTE;

	if (bit_offset % BITS_PER_BYTE) {
		/* First shift */
		*p = *b++ >> bit_offset;

		/* setup rest of the bytes if any */
		for (i = 1; i < cell->bytes; i++) {
			/* Get bits from next byte and shift them towards msb */
			*p++ |= *b << (BITS_PER_BYTE - bit_offset);

			*p = *b++ >> bit_offset;
		}
	} else if (p != b) {
		memmove(p, b, cell->bytes - bytes_offset);
		p += cell->bytes - 1;
	} else {
		/* point to the msb */
		p += cell->bytes - 1;
	}

	/* result fits in less bytes */
	extra = cell->bytes - DIV_ROUND_UP(cell->nbits, BITS_PER_BYTE);
	while (--extra >= 0)
		*p-- = 0;

	/* clear msb bits if any leftover in the last byte */
	if (cell->nbits % BITS_PER_BYTE)
		*p &= GENMASK((cell->nbits % BITS_PER_BYTE) - 1, 0);
}

static int __nvmem_cell_read(struct nvmem_device *nvmem,
			     struct nvmem_cell_entry *cell,
			     void *buf, size_t *len, const char *id, int index)
{
	int rc;

	rc = nvmem_reg_read(nvmem, cell->offset, buf, cell->raw_len);

	if (rc)
		return rc;

	/* shift bits in-place */
	if (cell->bit_offset || cell->nbits)
		nvmem_shift_read_buffer_in_place(cell, buf);

	if (cell->read_post_process) {
		rc = cell->read_post_process(cell->priv, id, index,
					     cell->offset, buf, cell->raw_len);
		if (rc)
			return rc;
	}

	if (len)
		*len = cell->bytes;

	return 0;
}

/**
 * nvmem_cell_read() - Read a given nvmem cell
 *
 * @cell: nvmem cell to be read.
 * @len: pointer to length of cell which will be populated on successful read;
 *	 can be NULL.
 *
 * Return: ERR_PTR() on error or a valid pointer to a buffer on success. The
 * buffer should be freed by the consumer with a kfree().
 */
void *nvmem_cell_read(struct nvmem_cell *cell, size_t *len)
{
	struct nvmem_cell_entry *entry = cell->entry;
	struct nvmem_device *nvmem = entry->nvmem;
	u8 *buf;
	int rc;

	if (!nvmem)
		return ERR_PTR(-EINVAL);

	buf = kzalloc(max_t(size_t, entry->raw_len, entry->bytes), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	rc = __nvmem_cell_read(nvmem, cell->entry, buf, len, cell->id, cell->index);
	if (rc) {
		kfree(buf);
		return ERR_PTR(rc);
	}

	return buf;
}
EXPORT_SYMBOL_GPL(nvmem_cell_read);

static void *nvmem_cell_prepare_write_buffer(struct nvmem_cell_entry *cell,
					     u8 *_buf, int len)
{
	struct nvmem_device *nvmem = cell->nvmem;
	int i, rc, nbits, bit_offset = cell->bit_offset;
	u8 v, *p, *buf, *b, pbyte, pbits;

	nbits = cell->nbits;
	buf = kzalloc(cell->bytes, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	memcpy(buf, _buf, len);
	p = b = buf;

	if (bit_offset) {
		pbyte = *b;
		*b <<= bit_offset;

		/* setup the first byte with lsb bits from nvmem */
		rc = nvmem_reg_read(nvmem, cell->offset, &v, 1);
		if (rc)
			goto err;
		*b++ |= GENMASK(bit_offset - 1, 0) & v;

		/* setup rest of the byte if any */
		for (i = 1; i < cell->bytes; i++) {
			/* Get last byte bits and shift them towards lsb */
			pbits = pbyte >> (BITS_PER_BYTE - 1 - bit_offset);
			pbyte = *b;
			p = b;
			*b <<= bit_offset;
			*b++ |= pbits;
		}
	}

	/* if it's not end on byte boundary */
	if ((nbits + bit_offset) % BITS_PER_BYTE) {
		/* setup the last byte with msb bits from nvmem */
		rc = nvmem_reg_read(nvmem,
				    cell->offset + cell->bytes - 1, &v, 1);
		if (rc)
			goto err;
		*p |= GENMASK(7, (nbits + bit_offset) % BITS_PER_BYTE) & v;

	}

	return buf;
err:
	kfree(buf);
	return ERR_PTR(rc);
}

static int __nvmem_cell_entry_write(struct nvmem_cell_entry *cell, void *buf, size_t len)
{
	struct nvmem_device *nvmem = cell->nvmem;
	int rc;

	if (!nvmem || nvmem->read_only ||
	    (cell->bit_offset == 0 && len != cell->bytes))
		return -EINVAL;

	/*
	 * Any cells which have a read_post_process hook are read-only because
	 * we cannot reverse the operation and it might affect other cells,
	 * too.
	 */
	if (cell->read_post_process)
		return -EINVAL;

	if (cell->bit_offset || cell->nbits) {
		if (len != BITS_TO_BYTES(cell->nbits) && len != cell->bytes)
			return -EINVAL;
		buf = nvmem_cell_prepare_write_buffer(cell, buf, len);
		if (IS_ERR(buf))
			return PTR_ERR(buf);
	}

	rc = nvmem_reg_write(nvmem, cell->offset, buf, cell->bytes);

	/* free the tmp buffer */
	if (cell->bit_offset || cell->nbits)
		kfree(buf);

	if (rc)
		return rc;

	return len;
}

/**
 * nvmem_cell_write() - Write to a given nvmem cell
 *
 * @cell: nvmem cell to be written.
 * @buf: Buffer to be written.
 * @len: length of buffer to be written to nvmem cell.
 *
 * Return: length of bytes written or negative on failure.
 */
int nvmem_cell_write(struct nvmem_cell *cell, void *buf, size_t len)
{
	return __nvmem_cell_entry_write(cell->entry, buf, len);
}

EXPORT_SYMBOL_GPL(nvmem_cell_write);

static int nvmem_cell_read_common(struct device *dev, const char *cell_id,
				  void *val, size_t count)
{
	struct nvmem_cell *cell;
	void *buf;
	size_t len;

	cell = nvmem_cell_get(dev, cell_id);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	if (IS_ERR(buf)) {
		nvmem_cell_put(cell);
		return PTR_ERR(buf);
	}
	if (len != count) {
		kfree(buf);
		nvmem_cell_put(cell);
		return -EINVAL;
	}
	memcpy(val, buf, count);
	kfree(buf);
	nvmem_cell_put(cell);

	return 0;
}

/**
 * nvmem_cell_read_u8() - Read a cell value as a u8
 *
 * @dev: Device that requests the nvmem cell.
 * @cell_id: Name of nvmem cell to read.
 * @val: pointer to output value.
 *
 * Return: 0 on success or negative errno.
 */
int nvmem_cell_read_u8(struct device *dev, const char *cell_id, u8 *val)
{
	return nvmem_cell_read_common(dev, cell_id, val, sizeof(*val));
}
EXPORT_SYMBOL_GPL(nvmem_cell_read_u8);

/**
 * nvmem_cell_read_u16() - Read a cell value as a u16
 *
 * @dev: Device that requests the nvmem cell.
 * @cell_id: Name of nvmem cell to read.
 * @val: pointer to output value.
 *
 * Return: 0 on success or negative errno.
 */
int nvmem_cell_read_u16(struct device *dev, const char *cell_id, u16 *val)
{
	return nvmem_cell_read_common(dev, cell_id, val, sizeof(*val));
}
EXPORT_SYMBOL_GPL(nvmem_cell_read_u16);

/**
 * nvmem_cell_read_u32() - Read a cell value as a u32
 *
 * @dev: Device that requests the nvmem cell.
 * @cell_id: Name of nvmem cell to read.
 * @val: pointer to output value.
 *
 * Return: 0 on success or negative errno.
 */
int nvmem_cell_read_u32(struct device *dev, const char *cell_id, u32 *val)
{
	return nvmem_cell_read_common(dev, cell_id, val, sizeof(*val));
}
EXPORT_SYMBOL_GPL(nvmem_cell_read_u32);

/**
 * nvmem_cell_read_u64() - Read a cell value as a u64
 *
 * @dev: Device that requests the nvmem cell.
 * @cell_id: Name of nvmem cell to read.
 * @val: pointer to output value.
 *
 * Return: 0 on success or negative errno.
 */
int nvmem_cell_read_u64(struct device *dev, const char *cell_id, u64 *val)
{
	return nvmem_cell_read_common(dev, cell_id, val, sizeof(*val));
}
EXPORT_SYMBOL_GPL(nvmem_cell_read_u64);

static const void *nvmem_cell_read_variable_common(struct device *dev,
						   const char *cell_id,
						   size_t max_len, size_t *len)
{
	struct nvmem_cell *cell;
	int nbits;
	void *buf;

	cell = nvmem_cell_get(dev, cell_id);
	if (IS_ERR(cell))
		return cell;

	nbits = cell->entry->nbits;
	buf = nvmem_cell_read(cell, len);
	nvmem_cell_put(cell);
	if (IS_ERR(buf))
		return buf;

	/*
	 * If nbits is set then nvmem_cell_read() can significantly exaggerate
	 * the length of the real data. Throw away the extra junk.
	 */
	if (nbits)
		*len = DIV_ROUND_UP(nbits, 8);

	if (*len > max_len) {
		kfree(buf);
		return ERR_PTR(-ERANGE);
	}

	return buf;
}

/**
 * nvmem_cell_read_variable_le_u32() - Read up to 32-bits of data as a little endian number.
 *
 * @dev: Device that requests the nvmem cell.
 * @cell_id: Name of nvmem cell to read.
 * @val: pointer to output value.
 *
 * Return: 0 on success or negative errno.
 */
int nvmem_cell_read_variable_le_u32(struct device *dev, const char *cell_id,
				    u32 *val)
{
	size_t len;
	const u8 *buf;
	int i;

	buf = nvmem_cell_read_variable_common(dev, cell_id, sizeof(*val), &len);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	/* Copy w/ implicit endian conversion */
	*val = 0;
	for (i = 0; i < len; i++)
		*val |= buf[i] << (8 * i);

	kfree(buf);

	return 0;
}
EXPORT_SYMBOL_GPL(nvmem_cell_read_variable_le_u32);

/**
 * nvmem_cell_read_variable_le_u64() - Read up to 64-bits of data as a little endian number.
 *
 * @dev: Device that requests the nvmem cell.
 * @cell_id: Name of nvmem cell to read.
 * @val: pointer to output value.
 *
 * Return: 0 on success or negative errno.
 */
int nvmem_cell_read_variable_le_u64(struct device *dev, const char *cell_id,
				    u64 *val)
{
	size_t len;
	const u8 *buf;
	int i;

	buf = nvmem_cell_read_variable_common(dev, cell_id, sizeof(*val), &len);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	/* Copy w/ implicit endian conversion */
	*val = 0;
	for (i = 0; i < len; i++)
		*val |= (uint64_t)buf[i] << (8 * i);

	kfree(buf);

	return 0;
}
EXPORT_SYMBOL_GPL(nvmem_cell_read_variable_le_u64);

/**
 * nvmem_device_cell_read() - Read a given nvmem device and cell
 *
 * @nvmem: nvmem device to read from.
 * @info: nvmem cell info to be read.
 * @buf: buffer pointer which will be populated on successful read.
 *
 * Return: length of successful bytes read on success and negative
 * error code on error.
 */
ssize_t nvmem_device_cell_read(struct nvmem_device *nvmem,
			   struct nvmem_cell_info *info, void *buf)
{
	struct nvmem_cell_entry cell;
	int rc;
	ssize_t len;

	if (!nvmem)
		return -EINVAL;

	rc = nvmem_cell_info_to_nvmem_cell_entry_nodup(nvmem, info, &cell);
	if (rc)
		return rc;

	rc = __nvmem_cell_read(nvmem, &cell, buf, &len, NULL, 0);
	if (rc)
		return rc;

	return len;
}
EXPORT_SYMBOL_GPL(nvmem_device_cell_read);

/**
 * nvmem_device_cell_write() - Write cell to a given nvmem device
 *
 * @nvmem: nvmem device to be written to.
 * @info: nvmem cell info to be written.
 * @buf: buffer to be written to cell.
 *
 * Return: length of bytes written or negative error code on failure.
 */
int nvmem_device_cell_write(struct nvmem_device *nvmem,
			    struct nvmem_cell_info *info, void *buf)
{
	struct nvmem_cell_entry cell;
	int rc;

	if (!nvmem)
		return -EINVAL;

	rc = nvmem_cell_info_to_nvmem_cell_entry_nodup(nvmem, info, &cell);
	if (rc)
		return rc;

	return __nvmem_cell_entry_write(&cell, buf, cell.bytes);
}
EXPORT_SYMBOL_GPL(nvmem_device_cell_write);

/**
 * nvmem_device_read() - Read from a given nvmem device
 *
 * @nvmem: nvmem device to read from.
 * @offset: offset in nvmem device.
 * @bytes: number of bytes to read.
 * @buf: buffer pointer which will be populated on successful read.
 *
 * Return: length of successful bytes read on success and negative
 * error code on error.
 */
int nvmem_device_read(struct nvmem_device *nvmem,
		      unsigned int offset,
		      size_t bytes, void *buf)
{
	int rc;

	if (!nvmem)
		return -EINVAL;

	rc = nvmem_reg_read(nvmem, offset, buf, bytes);

	if (rc)
		return rc;

	return bytes;
}
EXPORT_SYMBOL_GPL(nvmem_device_read);

/**
 * nvmem_device_write() - Write cell to a given nvmem device
 *
 * @nvmem: nvmem device to be written to.
 * @offset: offset in nvmem device.
 * @bytes: number of bytes to write.
 * @buf: buffer to be written.
 *
 * Return: length of bytes written or negative error code on failure.
 */
int nvmem_device_write(struct nvmem_device *nvmem,
		       unsigned int offset,
		       size_t bytes, void *buf)
{
	int rc;

	if (!nvmem)
		return -EINVAL;

	rc = nvmem_reg_write(nvmem, offset, buf, bytes);

	if (rc)
		return rc;


	return bytes;
}
EXPORT_SYMBOL_GPL(nvmem_device_write);

/**
 * nvmem_add_cell_lookups() - register a list of cell lookup entries
 *
 * @entries: array of cell lookup entries
 * @nentries: number of cell lookup entries in the array
 */
void nvmem_add_cell_lookups(struct nvmem_cell_lookup *entries, size_t nentries)
{
	int i;

	mutex_lock(&nvmem_lookup_mutex);
	for (i = 0; i < nentries; i++)
		list_add_tail(&entries[i].node, &nvmem_lookup_list);
	mutex_unlock(&nvmem_lookup_mutex);
}
EXPORT_SYMBOL_GPL(nvmem_add_cell_lookups);

/**
 * nvmem_del_cell_lookups() - remove a list of previously added cell lookup
 *                            entries
 *
 * @entries: array of cell lookup entries
 * @nentries: number of cell lookup entries in the array
 */
void nvmem_del_cell_lookups(struct nvmem_cell_lookup *entries, size_t nentries)
{
	int i;

	mutex_lock(&nvmem_lookup_mutex);
	for (i = 0; i < nentries; i++)
		list_del(&entries[i].node);
	mutex_unlock(&nvmem_lookup_mutex);
}
EXPORT_SYMBOL_GPL(nvmem_del_cell_lookups);

/**
 * nvmem_dev_name() - Get the name of a given nvmem device.
 *
 * @nvmem: nvmem device.
 *
 * Return: name of the nvmem device.
 */
const char *nvmem_dev_name(struct nvmem_device *nvmem)
{
	return dev_name(&nvmem->dev);
}
EXPORT_SYMBOL_GPL(nvmem_dev_name);

/**
 * nvmem_dev_size() - Get the size of a given nvmem device.
 *
 * @nvmem: nvmem device.
 *
 * Return: size of the nvmem device.
 */
size_t nvmem_dev_size(struct nvmem_device *nvmem)
{
	return nvmem->size;
}
EXPORT_SYMBOL_GPL(nvmem_dev_size);

static int __init nvmem_init(void)
{
	int ret;

	ret = bus_register(&nvmem_bus_type);
	if (ret)
		return ret;

	ret = nvmem_layout_bus_register();
	if (ret)
		bus_unregister(&nvmem_bus_type);

	return ret;
}

static void __exit nvmem_exit(void)
{
	nvmem_layout_bus_unregister();
	bus_unregister(&nvmem_bus_type);
}

subsys_initcall(nvmem_init);
module_exit(nvmem_exit);

MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com");
MODULE_DESCRIPTION("nvmem Driver Core");
