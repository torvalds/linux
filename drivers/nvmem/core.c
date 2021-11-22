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

struct nvmem_device {
	struct module		*owner;
	struct device		dev;
	int			stride;
	int			word_size;
	int			id;
	struct kref		refcnt;
	size_t			size;
	bool			read_only;
	bool			root_only;
	int			flags;
	enum nvmem_type		type;
	struct bin_attribute	eeprom;
	struct device		*base_dev;
	struct list_head	cells;
	const struct nvmem_keepout *keepout;
	unsigned int		nkeepout;
	nvmem_reg_read_t	reg_read;
	nvmem_reg_write_t	reg_write;
	nvmem_cell_post_process_t cell_post_process;
	struct gpio_desc	*wp_gpio;
	void *priv;
};

#define to_nvmem_device(d) container_of(d, struct nvmem_device, dev)

#define FLAG_COMPAT		BIT(0)
struct nvmem_cell_entry {
	const char		*name;
	int			offset;
	int			bytes;
	int			bit_offset;
	int			nbits;
	struct device_node	*np;
	struct nvmem_device	*nvmem;
	struct list_head	node;
};

struct nvmem_cell {
	struct nvmem_cell_entry *entry;
	const char		*id;
};

static DEFINE_MUTEX(nvmem_mutex);
static DEFINE_IDA(nvmem_ida);

static DEFINE_MUTEX(nvmem_cell_mutex);
static LIST_HEAD(nvmem_cell_tables);

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

	return sprintf(buf, "%s\n", nvmem_type_str[nvmem->type]);
}

static DEVICE_ATTR_RO(type);

static struct attribute *nvmem_attrs[] = {
	&dev_attr_type.attr,
	NULL,
};

static ssize_t bin_attr_nvmem_read(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *attr, char *buf,
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

	/* Stop the user from reading */
	if (pos >= nvmem->size)
		return 0;

	if (!IS_ALIGNED(pos, nvmem->stride))
		return -EINVAL;

	if (count < nvmem->word_size)
		return -EINVAL;

	if (pos + count > nvmem->size)
		count = nvmem->size - pos;

	count = round_down(count, nvmem->word_size);

	if (!nvmem->reg_read)
		return -EPERM;

	rc = nvmem_reg_read(nvmem, pos, buf, count);

	if (rc)
		return rc;

	return count;
}

static ssize_t bin_attr_nvmem_write(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *attr, char *buf,
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

	/* Stop the user from writing */
	if (pos >= nvmem->size)
		return -EFBIG;

	if (!IS_ALIGNED(pos, nvmem->stride))
		return -EINVAL;

	if (count < nvmem->word_size)
		return -EINVAL;

	if (pos + count > nvmem->size)
		count = nvmem->size - pos;

	count = round_down(count, nvmem->word_size);

	if (!nvmem->reg_write)
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
					 struct bin_attribute *attr, int i)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nvmem_device *nvmem = to_nvmem_device(dev);

	return nvmem_bin_attr_get_umode(nvmem);
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

static struct bin_attribute *nvmem_bin_attributes[] = {
	&bin_attr_rw_nvmem,
	NULL,
};

static const struct attribute_group nvmem_bin_group = {
	.bin_attrs	= nvmem_bin_attributes,
	.attrs		= nvmem_attrs,
	.is_bin_visible = nvmem_bin_attr_is_visible,
};

static const struct attribute_group *nvmem_dev_groups[] = {
	&nvmem_bin_group,
	NULL,
};

static struct bin_attribute bin_attr_nvmem_eeprom_compat = {
	.attr	= {
		.name	= "eeprom",
	},
	.read	= bin_attr_nvmem_read,
	.write	= bin_attr_nvmem_write,
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

	if (config->type == NVMEM_TYPE_FRAM)
		bin_attr_nvmem_eeprom_compat.attr.name = "fram";

	nvmem->eeprom = bin_attr_nvmem_eeprom_compat;
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
			cell->name ?: "<unknown>", nvmem->stride);
		return -EINVAL;
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
	struct nvmem_cell_entry **cells;
	int i, rval;

	cells = kcalloc(ncells, sizeof(*cells), GFP_KERNEL);
	if (!cells)
		return -ENOMEM;

	for (i = 0; i < ncells; i++) {
		cells[i] = kzalloc(sizeof(**cells), GFP_KERNEL);
		if (!cells[i]) {
			rval = -ENOMEM;
			goto err;
		}

		rval = nvmem_cell_info_to_nvmem_cell_entry(nvmem, &info[i], cells[i]);
		if (rval) {
			kfree(cells[i]);
			goto err;
		}

		nvmem_cell_entry_add(cells[i]);
	}

	/* remove tmp array */
	kfree(cells);

	return 0;
err:
	while (i--)
		nvmem_cell_entry_drop(cells[i]);

	kfree(cells);

	return rval;
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

static int nvmem_add_cells_from_table(struct nvmem_device *nvmem)
{
	const struct nvmem_cell_info *info;
	struct nvmem_cell_table *table;
	struct nvmem_cell_entry *cell;
	int rval = 0, i;

	mutex_lock(&nvmem_cell_mutex);
	list_for_each_entry(table, &nvmem_cell_tables, node) {
		if (strcmp(nvmem_dev_name(nvmem), table->nvmem_name) == 0) {
			for (i = 0; i < table->ncells; i++) {
				info = &table->cells[i];

				cell = kzalloc(sizeof(*cell), GFP_KERNEL);
				if (!cell) {
					rval = -ENOMEM;
					goto out;
				}

				rval = nvmem_cell_info_to_nvmem_cell_entry(nvmem, info, cell);
				if (rval) {
					kfree(cell);
					goto out;
				}

				nvmem_cell_entry_add(cell);
			}
		}
	}

out:
	mutex_unlock(&nvmem_cell_mutex);
	return rval;
}

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

static int nvmem_add_cells_from_of(struct nvmem_device *nvmem)
{
	struct device_node *parent, *child;
	struct device *dev = &nvmem->dev;
	struct nvmem_cell_entry *cell;
	const __be32 *addr;
	int len;

	parent = dev->of_node;

	for_each_child_of_node(parent, child) {
		addr = of_get_property(child, "reg", &len);
		if (!addr)
			continue;
		if (len < 2 * sizeof(u32)) {
			dev_err(dev, "nvmem: invalid reg on %pOF\n", child);
			of_node_put(child);
			return -EINVAL;
		}

		cell = kzalloc(sizeof(*cell), GFP_KERNEL);
		if (!cell) {
			of_node_put(child);
			return -ENOMEM;
		}

		cell->nvmem = nvmem;
		cell->offset = be32_to_cpup(addr++);
		cell->bytes = be32_to_cpup(addr);
		cell->name = kasprintf(GFP_KERNEL, "%pOFn", child);

		addr = of_get_property(child, "bits", &len);
		if (addr && len == (2 * sizeof(u32))) {
			cell->bit_offset = be32_to_cpup(addr++);
			cell->nbits = be32_to_cpup(addr);
		}

		if (cell->nbits)
			cell->bytes = DIV_ROUND_UP(
					cell->nbits + cell->bit_offset,
					BITS_PER_BYTE);

		if (!IS_ALIGNED(cell->offset, nvmem->stride)) {
			dev_err(dev, "cell %s unaligned to nvmem stride %d\n",
				cell->name, nvmem->stride);
			/* Cells already added will be freed later. */
			kfree_const(cell->name);
			kfree(cell);
			of_node_put(child);
			return -EINVAL;
		}

		cell->np = of_node_get(child);
		nvmem_cell_entry_add(cell);
	}

	return 0;
}

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

	rval  = ida_alloc(&nvmem_ida, GFP_KERNEL);
	if (rval < 0) {
		kfree(nvmem);
		return ERR_PTR(rval);
	}

	if (config->wp_gpio)
		nvmem->wp_gpio = config->wp_gpio;
	else
		nvmem->wp_gpio = gpiod_get_optional(config->dev, "wp",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(nvmem->wp_gpio)) {
		ida_free(&nvmem_ida, nvmem->id);
		rval = PTR_ERR(nvmem->wp_gpio);
		kfree(nvmem);
		return ERR_PTR(rval);
	}

	kref_init(&nvmem->refcnt);
	INIT_LIST_HEAD(&nvmem->cells);

	nvmem->id = rval;
	nvmem->owner = config->owner;
	if (!nvmem->owner && config->dev->driver)
		nvmem->owner = config->dev->driver->owner;
	nvmem->stride = config->stride ?: 1;
	nvmem->word_size = config->word_size ?: 1;
	nvmem->size = config->size;
	nvmem->dev.type = &nvmem_provider_type;
	nvmem->dev.bus = &nvmem_bus_type;
	nvmem->dev.parent = config->dev;
	nvmem->root_only = config->root_only;
	nvmem->priv = config->priv;
	nvmem->type = config->type;
	nvmem->reg_read = config->reg_read;
	nvmem->reg_write = config->reg_write;
	nvmem->cell_post_process = config->cell_post_process;
	nvmem->keepout = config->keepout;
	nvmem->nkeepout = config->nkeepout;
	if (config->of_node)
		nvmem->dev.of_node = config->of_node;
	else if (!config->no_of_node)
		nvmem->dev.of_node = config->dev->of_node;

	switch (config->id) {
	case NVMEM_DEVID_NONE:
		dev_set_name(&nvmem->dev, "%s", config->name);
		break;
	case NVMEM_DEVID_AUTO:
		dev_set_name(&nvmem->dev, "%s%d", config->name, nvmem->id);
		break;
	default:
		dev_set_name(&nvmem->dev, "%s%d",
			     config->name ? : "nvmem",
			     config->name ? config->id : nvmem->id);
		break;
	}

	nvmem->read_only = device_property_present(config->dev, "read-only") ||
			   config->read_only || !nvmem->reg_write;

#ifdef CONFIG_NVMEM_SYSFS
	nvmem->dev.groups = nvmem_dev_groups;
#endif

	if (nvmem->nkeepout) {
		rval = nvmem_validate_keepouts(nvmem);
		if (rval) {
			ida_free(&nvmem_ida, nvmem->id);
			kfree(nvmem);
			return ERR_PTR(rval);
		}
	}

	dev_dbg(&nvmem->dev, "Registering nvmem device %s\n", config->name);

	rval = device_register(&nvmem->dev);
	if (rval)
		goto err_put_device;

	if (config->compat) {
		rval = nvmem_sysfs_setup_compat(nvmem, config);
		if (rval)
			goto err_device_del;
	}

	if (config->cells) {
		rval = nvmem_add_cells(nvmem, config->cells, config->ncells);
		if (rval)
			goto err_teardown_compat;
	}

	rval = nvmem_add_cells_from_table(nvmem);
	if (rval)
		goto err_remove_cells;

	rval = nvmem_add_cells_from_of(nvmem);
	if (rval)
		goto err_remove_cells;

	blocking_notifier_call_chain(&nvmem_notifier, NVMEM_ADD, nvmem);

	return nvmem;

err_remove_cells:
	nvmem_device_remove_all_cells(nvmem);
err_teardown_compat:
	if (config->compat)
		nvmem_sysfs_remove_compat(nvmem, config);
err_device_del:
	device_del(&nvmem->dev);
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
	device_unregister(&nvmem->dev);
}

/**
 * nvmem_unregister() - Unregister previously registered nvmem device
 *
 * @nvmem: Pointer to previously registered nvmem device.
 */
void nvmem_unregister(struct nvmem_device *nvmem)
{
	kref_put(&nvmem->refcnt, nvmem_device_release);
}
EXPORT_SYMBOL_GPL(nvmem_unregister);

static void devm_nvmem_release(struct device *dev, void *res)
{
	nvmem_unregister(*(struct nvmem_device **)res);
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
	struct nvmem_device **ptr, *nvmem;

	ptr = devres_alloc(devm_nvmem_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	nvmem = nvmem_register(config);

	if (!IS_ERR(nvmem)) {
		*ptr = nvmem;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return nvmem;
}
EXPORT_SYMBOL_GPL(devm_nvmem_register);

static int devm_nvmem_match(struct device *dev, void *res, void *data)
{
	struct nvmem_device **r = res;

	return *r == data;
}

/**
 * devm_nvmem_unregister() - Unregister previously registered managed nvmem
 * device.
 *
 * @dev: Device that uses the nvmem device.
 * @nvmem: Pointer to previously registered nvmem device.
 *
 * Return: Will be negative on error or zero on success.
 */
int devm_nvmem_unregister(struct device *dev, struct nvmem_device *nvmem)
{
	return devres_release(dev, devm_nvmem_release, devm_nvmem_match, nvmem);
}
EXPORT_SYMBOL(devm_nvmem_unregister);

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
 * devm_nvmem_device_put() - put alredy got nvmem device
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
 * @dev: Device that requests the nvmem device.
 * @id: name id for the requested nvmem device.
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

static struct nvmem_cell *nvmem_create_cell(struct nvmem_cell_entry *entry, const char *id)
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
				cell = nvmem_create_cell(cell_entry, con_id);
				if (IS_ERR(cell))
					__nvmem_device_put(nvmem);
			}
			break;
		}
	}

	mutex_unlock(&nvmem_lookup_mutex);
	return cell;
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
	int index = 0;

	/* if cell name exists, find index to the name */
	if (id)
		index = of_property_match_string(np, "nvmem-cell-names", id);

	cell_np = of_parse_phandle(np, "nvmem-cells", index);
	if (!cell_np)
		return ERR_PTR(-ENOENT);

	nvmem_np = of_get_next_parent(cell_np);
	if (!nvmem_np)
		return ERR_PTR(-EINVAL);

	nvmem = __nvmem_device_get(nvmem_np, device_match_of_node);
	of_node_put(nvmem_np);
	if (IS_ERR(nvmem))
		return ERR_CAST(nvmem);

	cell_entry = nvmem_find_cell_entry_by_node(nvmem, cell_np);
	if (!cell_entry) {
		__nvmem_device_put(nvmem);
		return ERR_PTR(-ENOENT);
	}

	cell = nvmem_create_cell(cell_entry, id);
	if (IS_ERR(cell))
		__nvmem_device_put(nvmem);

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
}
EXPORT_SYMBOL_GPL(nvmem_cell_put);

static void nvmem_shift_read_buffer_in_place(struct nvmem_cell_entry *cell, void *buf)
{
	u8 *p, *b;
	int i, extra, bit_offset = cell->bit_offset;

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
		      void *buf, size_t *len, const char *id)
{
	int rc;

	rc = nvmem_reg_read(nvmem, cell->offset, buf, cell->bytes);

	if (rc)
		return rc;

	/* shift bits in-place */
	if (cell->bit_offset || cell->nbits)
		nvmem_shift_read_buffer_in_place(cell, buf);

	if (nvmem->cell_post_process) {
		rc = nvmem->cell_post_process(nvmem->priv, id,
					      cell->offset, buf, cell->bytes);
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
	struct nvmem_device *nvmem = cell->entry->nvmem;
	u8 *buf;
	int rc;

	if (!nvmem)
		return ERR_PTR(-EINVAL);

	buf = kzalloc(cell->entry->bytes, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	rc = __nvmem_cell_read(nvmem, cell->entry, buf, len, cell->id);
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

	if (cell->bit_offset || cell->nbits) {
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

	rc = __nvmem_cell_read(nvmem, &cell, buf, &len, NULL);
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
 * nvmem_add_cell_table() - register a table of cell info entries
 *
 * @table: table of cell info entries
 */
void nvmem_add_cell_table(struct nvmem_cell_table *table)
{
	mutex_lock(&nvmem_cell_mutex);
	list_add_tail(&table->node, &nvmem_cell_tables);
	mutex_unlock(&nvmem_cell_mutex);
}
EXPORT_SYMBOL_GPL(nvmem_add_cell_table);

/**
 * nvmem_del_cell_table() - remove a previously registered cell info table
 *
 * @table: table of cell info entries
 */
void nvmem_del_cell_table(struct nvmem_cell_table *table)
{
	mutex_lock(&nvmem_cell_mutex);
	list_del(&table->node);
	mutex_unlock(&nvmem_cell_mutex);
}
EXPORT_SYMBOL_GPL(nvmem_del_cell_table);

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
