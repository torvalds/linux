/*
 * nvmem framework core.
 *
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 * Copyright (C) 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct nvmem_device {
	const char		*name;
	struct regmap		*regmap;
	struct module		*owner;
	struct device		dev;
	int			stride;
	int			word_size;
	int			ncells;
	int			id;
	int			users;
	size_t			size;
	bool			read_only;
};

struct nvmem_cell {
	const char		*name;
	int			offset;
	int			bytes;
	int			bit_offset;
	int			nbits;
	struct nvmem_device	*nvmem;
	struct list_head	node;
};

static DEFINE_MUTEX(nvmem_mutex);
static DEFINE_IDA(nvmem_ida);

static LIST_HEAD(nvmem_cells);
static DEFINE_MUTEX(nvmem_cells_mutex);

#define to_nvmem_device(d) container_of(d, struct nvmem_device, dev)

static ssize_t bin_attr_nvmem_read(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *attr,
				    char *buf, loff_t pos, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nvmem_device *nvmem = to_nvmem_device(dev);
	int rc;

	/* Stop the user from reading */
	if (pos > nvmem->size)
		return 0;

	if (pos + count > nvmem->size)
		count = nvmem->size - pos;

	count = round_down(count, nvmem->word_size);

	rc = regmap_raw_read(nvmem->regmap, pos, buf, count);

	if (IS_ERR_VALUE(rc))
		return rc;

	return count;
}

static ssize_t bin_attr_nvmem_write(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *attr,
				     char *buf, loff_t pos, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nvmem_device *nvmem = to_nvmem_device(dev);
	int rc;

	/* Stop the user from writing */
	if (pos > nvmem->size)
		return 0;

	if (pos + count > nvmem->size)
		count = nvmem->size - pos;

	count = round_down(count, nvmem->word_size);

	rc = regmap_raw_write(nvmem->regmap, pos, buf, count);

	if (IS_ERR_VALUE(rc))
		return rc;

	return count;
}

/* default read/write permissions */
static struct bin_attribute bin_attr_rw_nvmem = {
	.attr	= {
		.name	= "nvmem",
		.mode	= S_IWUSR | S_IRUGO,
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
};

static const struct attribute_group *nvmem_rw_dev_groups[] = {
	&nvmem_bin_rw_group,
	NULL,
};

/* read only permission */
static struct bin_attribute bin_attr_ro_nvmem = {
	.attr	= {
		.name	= "nvmem",
		.mode	= S_IRUGO,
	},
	.read	= bin_attr_nvmem_read,
};

static struct bin_attribute *nvmem_bin_ro_attributes[] = {
	&bin_attr_ro_nvmem,
	NULL,
};

static const struct attribute_group nvmem_bin_ro_group = {
	.bin_attrs	= nvmem_bin_ro_attributes,
};

static const struct attribute_group *nvmem_ro_dev_groups[] = {
	&nvmem_bin_ro_group,
	NULL,
};

static void nvmem_release(struct device *dev)
{
	struct nvmem_device *nvmem = to_nvmem_device(dev);

	ida_simple_remove(&nvmem_ida, nvmem->id);
	kfree(nvmem);
}

static const struct device_type nvmem_provider_type = {
	.release	= nvmem_release,
};

static struct bus_type nvmem_bus_type = {
	.name		= "nvmem",
};

static int of_nvmem_match(struct device *dev, void *nvmem_np)
{
	return dev->of_node == nvmem_np;
}

static struct nvmem_device *of_nvmem_find(struct device_node *nvmem_np)
{
	struct device *d;

	if (!nvmem_np)
		return NULL;

	d = bus_find_device(&nvmem_bus_type, NULL, nvmem_np, of_nvmem_match);

	if (!d)
		return NULL;

	return to_nvmem_device(d);
}

static struct nvmem_cell *nvmem_find_cell(const char *cell_id)
{
	struct nvmem_cell *p;

	list_for_each_entry(p, &nvmem_cells, node)
		if (p && !strcmp(p->name, cell_id))
			return p;

	return NULL;
}

static void nvmem_cell_drop(struct nvmem_cell *cell)
{
	mutex_lock(&nvmem_cells_mutex);
	list_del(&cell->node);
	mutex_unlock(&nvmem_cells_mutex);
	kfree(cell);
}

static void nvmem_device_remove_all_cells(const struct nvmem_device *nvmem)
{
	struct nvmem_cell *cell;
	struct list_head *p, *n;

	list_for_each_safe(p, n, &nvmem_cells) {
		cell = list_entry(p, struct nvmem_cell, node);
		if (cell->nvmem == nvmem)
			nvmem_cell_drop(cell);
	}
}

static void nvmem_cell_add(struct nvmem_cell *cell)
{
	mutex_lock(&nvmem_cells_mutex);
	list_add_tail(&cell->node, &nvmem_cells);
	mutex_unlock(&nvmem_cells_mutex);
}

static int nvmem_cell_info_to_nvmem_cell(struct nvmem_device *nvmem,
				   const struct nvmem_cell_info *info,
				   struct nvmem_cell *cell)
{
	cell->nvmem = nvmem;
	cell->offset = info->offset;
	cell->bytes = info->bytes;
	cell->name = info->name;

	cell->bit_offset = info->bit_offset;
	cell->nbits = info->nbits;

	if (cell->nbits)
		cell->bytes = DIV_ROUND_UP(cell->nbits + cell->bit_offset,
					   BITS_PER_BYTE);

	if (!IS_ALIGNED(cell->offset, nvmem->stride)) {
		dev_err(&nvmem->dev,
			"cell %s unaligned to nvmem stride %d\n",
			cell->name, nvmem->stride);
		return -EINVAL;
	}

	return 0;
}

static int nvmem_add_cells(struct nvmem_device *nvmem,
			   const struct nvmem_config *cfg)
{
	struct nvmem_cell **cells;
	const struct nvmem_cell_info *info = cfg->cells;
	int i, rval;

	cells = kcalloc(cfg->ncells, sizeof(*cells), GFP_KERNEL);
	if (!cells)
		return -ENOMEM;

	for (i = 0; i < cfg->ncells; i++) {
		cells[i] = kzalloc(sizeof(**cells), GFP_KERNEL);
		if (!cells[i]) {
			rval = -ENOMEM;
			goto err;
		}

		rval = nvmem_cell_info_to_nvmem_cell(nvmem, &info[i], cells[i]);
		if (IS_ERR_VALUE(rval)) {
			kfree(cells[i]);
			goto err;
		}

		nvmem_cell_add(cells[i]);
	}

	nvmem->ncells = cfg->ncells;
	/* remove tmp array */
	kfree(cells);

	return 0;
err:
	while (--i)
		nvmem_cell_drop(cells[i]);

	return rval;
}

/**
 * nvmem_register() - Register a nvmem device for given nvmem_config.
 * Also creates an binary entry in /sys/bus/nvmem/devices/dev-name/nvmem
 *
 * @config: nvmem device configuration with which nvmem device is created.
 *
 * Return: Will be an ERR_PTR() on error or a valid pointer to nvmem_device
 * on success.
 */

struct nvmem_device *nvmem_register(const struct nvmem_config *config)
{
	struct nvmem_device *nvmem;
	struct device_node *np;
	struct regmap *rm;
	int rval;

	if (!config->dev)
		return ERR_PTR(-EINVAL);

	rm = dev_get_regmap(config->dev, NULL);
	if (!rm) {
		dev_err(config->dev, "Regmap not found\n");
		return ERR_PTR(-EINVAL);
	}

	nvmem = kzalloc(sizeof(*nvmem), GFP_KERNEL);
	if (!nvmem)
		return ERR_PTR(-ENOMEM);

	rval  = ida_simple_get(&nvmem_ida, 0, 0, GFP_KERNEL);
	if (rval < 0) {
		kfree(nvmem);
		return ERR_PTR(rval);
	}

	nvmem->id = rval;
	nvmem->regmap = rm;
	nvmem->owner = config->owner;
	nvmem->stride = regmap_get_reg_stride(rm);
	nvmem->word_size = regmap_get_val_bytes(rm);
	nvmem->size = regmap_get_max_register(rm) + nvmem->stride;
	nvmem->dev.type = &nvmem_provider_type;
	nvmem->dev.bus = &nvmem_bus_type;
	nvmem->dev.parent = config->dev;
	np = config->dev->of_node;
	nvmem->dev.of_node = np;
	dev_set_name(&nvmem->dev, "%s%d",
		     config->name ? : "nvmem", config->id);

	nvmem->read_only = of_property_read_bool(np, "read-only") |
			   config->read_only;

	nvmem->dev.groups = nvmem->read_only ? nvmem_ro_dev_groups :
					       nvmem_rw_dev_groups;

	device_initialize(&nvmem->dev);

	dev_dbg(&nvmem->dev, "Registering nvmem device %s\n", config->name);

	rval = device_add(&nvmem->dev);
	if (rval) {
		ida_simple_remove(&nvmem_ida, nvmem->id);
		kfree(nvmem);
		return ERR_PTR(rval);
	}

	if (config->cells)
		nvmem_add_cells(nvmem, config);

	return nvmem;
}
EXPORT_SYMBOL_GPL(nvmem_register);

/**
 * nvmem_unregister() - Unregister previously registered nvmem device
 *
 * @nvmem: Pointer to previously registered nvmem device.
 *
 * Return: Will be an negative on error or a zero on success.
 */
int nvmem_unregister(struct nvmem_device *nvmem)
{
	if (nvmem->users)
		return -EBUSY;

	nvmem_device_remove_all_cells(nvmem);
	device_del(&nvmem->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(nvmem_unregister);

static int __init nvmem_init(void)
{
	return bus_register(&nvmem_bus_type);
}

static void __exit nvmem_exit(void)
{
	bus_unregister(&nvmem_bus_type);
}

subsys_initcall(nvmem_init);
module_exit(nvmem_exit);

MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com");
MODULE_DESCRIPTION("nvmem Driver Core");
MODULE_LICENSE("GPL v2");
