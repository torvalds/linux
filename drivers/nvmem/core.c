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
	if (pos >= nvmem->size)
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
	if (pos >= nvmem->size)
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
	mutex_lock(&nvmem_mutex);
	if (nvmem->users) {
		mutex_unlock(&nvmem_mutex);
		return -EBUSY;
	}
	mutex_unlock(&nvmem_mutex);

	nvmem_device_remove_all_cells(nvmem);
	device_del(&nvmem->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(nvmem_unregister);

static struct nvmem_device *__nvmem_device_get(struct device_node *np,
					       struct nvmem_cell **cellp,
					       const char *cell_id)
{
	struct nvmem_device *nvmem = NULL;

	mutex_lock(&nvmem_mutex);

	if (np) {
		nvmem = of_nvmem_find(np);
		if (!nvmem) {
			mutex_unlock(&nvmem_mutex);
			return ERR_PTR(-EPROBE_DEFER);
		}
	} else {
		struct nvmem_cell *cell = nvmem_find_cell(cell_id);

		if (cell) {
			nvmem = cell->nvmem;
			*cellp = cell;
		}

		if (!nvmem) {
			mutex_unlock(&nvmem_mutex);
			return ERR_PTR(-ENOENT);
		}
	}

	nvmem->users++;
	mutex_unlock(&nvmem_mutex);

	if (!try_module_get(nvmem->owner)) {
		dev_err(&nvmem->dev,
			"could not increase module refcount for cell %s\n",
			nvmem->name);

		mutex_lock(&nvmem_mutex);
		nvmem->users--;
		mutex_unlock(&nvmem_mutex);

		return ERR_PTR(-EINVAL);
	}

	return nvmem;
}

static void __nvmem_device_put(struct nvmem_device *nvmem)
{
	module_put(nvmem->owner);
	mutex_lock(&nvmem_mutex);
	nvmem->users--;
	mutex_unlock(&nvmem_mutex);
}

static int nvmem_match(struct device *dev, void *data)
{
	return !strcmp(dev_name(dev), data);
}

static struct nvmem_device *nvmem_find(const char *name)
{
	struct device *d;

	d = bus_find_device(&nvmem_bus_type, NULL, (void *)name, nvmem_match);

	if (!d)
		return NULL;

	return to_nvmem_device(d);
}

#if IS_ENABLED(CONFIG_NVMEM) && IS_ENABLED(CONFIG_OF)
/**
 * of_nvmem_device_get() - Get nvmem device from a given id
 *
 * @dev node: Device tree node that uses the nvmem device
 * @id: nvmem name from nvmem-names property.
 *
 * Return: ERR_PTR() on error or a valid pointer to a struct nvmem_device
 * on success.
 */
struct nvmem_device *of_nvmem_device_get(struct device_node *np, const char *id)
{

	struct device_node *nvmem_np;
	int index;

	index = of_property_match_string(np, "nvmem-names", id);

	nvmem_np = of_parse_phandle(np, "nvmem", index);
	if (!nvmem_np)
		return ERR_PTR(-EINVAL);

	return __nvmem_device_get(nvmem_np, NULL, NULL);
}
EXPORT_SYMBOL_GPL(of_nvmem_device_get);
#endif

/**
 * nvmem_device_get() - Get nvmem device from a given id
 *
 * @dev : Device that uses the nvmem device
 * @id: nvmem name from nvmem-names property.
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

	return nvmem_find(dev_name);
}
EXPORT_SYMBOL_GPL(nvmem_device_get);

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
 * devm_nvmem_device_put() - put alredy got nvmem device
 *
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
 * nvmem_device_put() - put alredy got nvmem device
 *
 * @nvmem: pointer to nvmem device that needs to be released.
 */
void nvmem_device_put(struct nvmem_device *nvmem)
{
	__nvmem_device_put(nvmem);
}
EXPORT_SYMBOL_GPL(nvmem_device_put);

/**
 * devm_nvmem_device_get() - Get nvmem cell of device form a given id
 *
 * @dev node: Device tree node that uses the nvmem cell
 * @id: nvmem name in nvmems property.
 *
 * Return: ERR_PTR() on error or a valid pointer to a struct nvmem_cell
 * on success.  The nvmem_cell will be freed by the automatically once the
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

static struct nvmem_cell *nvmem_cell_get_from_list(const char *cell_id)
{
	struct nvmem_cell *cell = NULL;
	struct nvmem_device *nvmem;

	nvmem = __nvmem_device_get(NULL, &cell, cell_id);
	if (IS_ERR(nvmem))
		return ERR_CAST(nvmem);

	return cell;
}

#if IS_ENABLED(CONFIG_NVMEM) && IS_ENABLED(CONFIG_OF)
/**
 * of_nvmem_cell_get() - Get a nvmem cell from given device node and cell id
 *
 * @dev node: Device tree node that uses the nvmem cell
 * @id: nvmem cell name from nvmem-cell-names property.
 *
 * Return: Will be an ERR_PTR() on error or a valid pointer
 * to a struct nvmem_cell.  The nvmem_cell will be freed by the
 * nvmem_cell_put().
 */
struct nvmem_cell *of_nvmem_cell_get(struct device_node *np,
					    const char *name)
{
	struct device_node *cell_np, *nvmem_np;
	struct nvmem_cell *cell;
	struct nvmem_device *nvmem;
	const __be32 *addr;
	int rval, len, index;

	index = of_property_match_string(np, "nvmem-cell-names", name);

	cell_np = of_parse_phandle(np, "nvmem-cells", index);
	if (!cell_np)
		return ERR_PTR(-EINVAL);

	nvmem_np = of_get_next_parent(cell_np);
	if (!nvmem_np)
		return ERR_PTR(-EINVAL);

	nvmem = __nvmem_device_get(nvmem_np, NULL, NULL);
	if (IS_ERR(nvmem))
		return ERR_CAST(nvmem);

	addr = of_get_property(cell_np, "reg", &len);
	if (!addr || (len < 2 * sizeof(u32))) {
		dev_err(&nvmem->dev, "nvmem: invalid reg on %s\n",
			cell_np->full_name);
		rval  = -EINVAL;
		goto err_mem;
	}

	cell = kzalloc(sizeof(*cell), GFP_KERNEL);
	if (!cell) {
		rval = -ENOMEM;
		goto err_mem;
	}

	cell->nvmem = nvmem;
	cell->offset = be32_to_cpup(addr++);
	cell->bytes = be32_to_cpup(addr);
	cell->name = cell_np->name;

	addr = of_get_property(cell_np, "bits", &len);
	if (addr && len == (2 * sizeof(u32))) {
		cell->bit_offset = be32_to_cpup(addr++);
		cell->nbits = be32_to_cpup(addr);
	}

	if (cell->nbits)
		cell->bytes = DIV_ROUND_UP(cell->nbits + cell->bit_offset,
					   BITS_PER_BYTE);

	if (!IS_ALIGNED(cell->offset, nvmem->stride)) {
			dev_err(&nvmem->dev,
				"cell %s unaligned to nvmem stride %d\n",
				cell->name, nvmem->stride);
		rval  = -EINVAL;
		goto err_sanity;
	}

	nvmem_cell_add(cell);

	return cell;

err_sanity:
	kfree(cell);

err_mem:
	__nvmem_device_put(nvmem);

	return ERR_PTR(rval);
}
EXPORT_SYMBOL_GPL(of_nvmem_cell_get);
#endif

/**
 * nvmem_cell_get() - Get nvmem cell of device form a given cell name
 *
 * @dev node: Device tree node that uses the nvmem cell
 * @id: nvmem cell name to get.
 *
 * Return: Will be an ERR_PTR() on error or a valid pointer
 * to a struct nvmem_cell.  The nvmem_cell will be freed by the
 * nvmem_cell_put().
 */
struct nvmem_cell *nvmem_cell_get(struct device *dev, const char *cell_id)
{
	struct nvmem_cell *cell;

	if (dev->of_node) { /* try dt first */
		cell = of_nvmem_cell_get(dev->of_node, cell_id);
		if (!IS_ERR(cell) || PTR_ERR(cell) == -EPROBE_DEFER)
			return cell;
	}

	return nvmem_cell_get_from_list(cell_id);
}
EXPORT_SYMBOL_GPL(nvmem_cell_get);

static void devm_nvmem_cell_release(struct device *dev, void *res)
{
	nvmem_cell_put(*(struct nvmem_cell **)res);
}

/**
 * devm_nvmem_cell_get() - Get nvmem cell of device form a given id
 *
 * @dev node: Device tree node that uses the nvmem cell
 * @id: nvmem id in nvmem-names property.
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
 * @cell: Previously allocated nvmem cell by devm_nvmem_cell_get()
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
 * @cell: Previously allocated nvmem cell by nvmem_cell_get()
 */
void nvmem_cell_put(struct nvmem_cell *cell)
{
	struct nvmem_device *nvmem = cell->nvmem;

	__nvmem_device_put(nvmem);
	nvmem_cell_drop(cell);
}
EXPORT_SYMBOL_GPL(nvmem_cell_put);

static inline void nvmem_shift_read_buffer_in_place(struct nvmem_cell *cell,
						    void *buf)
{
	u8 *p, *b;
	int i, bit_offset = cell->bit_offset;

	p = b = buf;
	if (bit_offset) {
		/* First shift */
		*b++ >>= bit_offset;

		/* setup rest of the bytes if any */
		for (i = 1; i < cell->bytes; i++) {
			/* Get bits from next byte and shift them towards msb */
			*p |= *b << (BITS_PER_BYTE - bit_offset);

			p = b;
			*b++ >>= bit_offset;
		}

		/* result fits in less bytes */
		if (cell->bytes != DIV_ROUND_UP(cell->nbits, BITS_PER_BYTE))
			*p-- = 0;
	}
	/* clear msb bits if any leftover in the last byte */
	*p &= GENMASK((cell->nbits%BITS_PER_BYTE) - 1, 0);
}

static int __nvmem_cell_read(struct nvmem_device *nvmem,
		      struct nvmem_cell *cell,
		      void *buf, size_t *len)
{
	int rc;

	rc = regmap_raw_read(nvmem->regmap, cell->offset, buf, cell->bytes);

	if (IS_ERR_VALUE(rc))
		return rc;

	/* shift bits in-place */
	if (cell->bit_offset || cell->bit_offset)
		nvmem_shift_read_buffer_in_place(cell, buf);

	*len = cell->bytes;

	return 0;
}

/**
 * nvmem_cell_read() - Read a given nvmem cell
 *
 * @cell: nvmem cell to be read.
 * @len: pointer to length of cell which will be populated on successful read.
 *
 * Return: ERR_PTR() on error or a valid pointer to a char * buffer on success.
 * The buffer should be freed by the consumer with a kfree().
 */
void *nvmem_cell_read(struct nvmem_cell *cell, size_t *len)
{
	struct nvmem_device *nvmem = cell->nvmem;
	u8 *buf;
	int rc;

	if (!nvmem || !nvmem->regmap)
		return ERR_PTR(-EINVAL);

	buf = kzalloc(cell->bytes, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	rc = __nvmem_cell_read(nvmem, cell, buf, len);
	if (IS_ERR_VALUE(rc)) {
		kfree(buf);
		return ERR_PTR(rc);
	}

	return buf;
}
EXPORT_SYMBOL_GPL(nvmem_cell_read);

static inline void *nvmem_cell_prepare_write_buffer(struct nvmem_cell *cell,
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
		rc = regmap_raw_read(nvmem->regmap, cell->offset, &v, 1);
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
		rc = regmap_raw_read(nvmem->regmap,
				    cell->offset + cell->bytes - 1, &v, 1);
		*p |= GENMASK(7, (nbits + bit_offset) % BITS_PER_BYTE) & v;

	}

	return buf;
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
	struct nvmem_device *nvmem = cell->nvmem;
	int rc;

	if (!nvmem || !nvmem->regmap || nvmem->read_only ||
	    (cell->bit_offset == 0 && len != cell->bytes))
		return -EINVAL;

	if (cell->bit_offset || cell->nbits) {
		buf = nvmem_cell_prepare_write_buffer(cell, buf, len);
		if (IS_ERR(buf))
			return PTR_ERR(buf);
	}

	rc = regmap_raw_write(nvmem->regmap, cell->offset, buf, cell->bytes);

	/* free the tmp buffer */
	if (cell->bit_offset)
		kfree(buf);

	if (IS_ERR_VALUE(rc))
		return rc;

	return len;
}
EXPORT_SYMBOL_GPL(nvmem_cell_write);

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
	struct nvmem_cell cell;
	int rc;
	ssize_t len;

	if (!nvmem || !nvmem->regmap)
		return -EINVAL;

	rc = nvmem_cell_info_to_nvmem_cell(nvmem, info, &cell);
	if (IS_ERR_VALUE(rc))
		return rc;

	rc = __nvmem_cell_read(nvmem, &cell, buf, &len);
	if (IS_ERR_VALUE(rc))
		return rc;

	return len;
}
EXPORT_SYMBOL_GPL(nvmem_device_cell_read);

/**
 * nvmem_device_cell_write() - Write cell to a given nvmem device
 *
 * @nvmem: nvmem device to be written to.
 * @info: nvmem cell info to be written
 * @buf: buffer to be written to cell.
 *
 * Return: length of bytes written or negative error code on failure.
 * */
int nvmem_device_cell_write(struct nvmem_device *nvmem,
			    struct nvmem_cell_info *info, void *buf)
{
	struct nvmem_cell cell;
	int rc;

	if (!nvmem || !nvmem->regmap)
		return -EINVAL;

	rc = nvmem_cell_info_to_nvmem_cell(nvmem, info, &cell);
	if (IS_ERR_VALUE(rc))
		return rc;

	return nvmem_cell_write(&cell, buf, cell.bytes);
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

	if (!nvmem || !nvmem->regmap)
		return -EINVAL;

	rc = regmap_raw_read(nvmem->regmap, offset, buf, bytes);

	if (IS_ERR_VALUE(rc))
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
 * */
int nvmem_device_write(struct nvmem_device *nvmem,
		       unsigned int offset,
		       size_t bytes, void *buf)
{
	int rc;

	if (!nvmem || !nvmem->regmap)
		return -EINVAL;

	rc = regmap_raw_write(nvmem->regmap, offset, buf, bytes);

	if (IS_ERR_VALUE(rc))
		return rc;


	return bytes;
}
EXPORT_SYMBOL_GPL(nvmem_device_write);

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
