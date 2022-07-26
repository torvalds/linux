// SPDX-License-Identifier: GPL-2.0
//
// Register map access API
//
// Copyright 2011 Wolfson Microelectronics plc
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/property.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/hwspinlock.h>
#include <asm/unaligned.h>

#define CREATE_TRACE_POINTS
#include "trace.h"

#include "internal.h"

/*
 * Sometimes for failures during very early init the trace
 * infrastructure isn't available early enough to be used.  For this
 * sort of problem defining LOG_DEVICE will add printks for basic
 * register I/O on a specific device.
 */
#undef LOG_DEVICE

#ifdef LOG_DEVICE
static inline bool regmap_should_log(struct regmap *map)
{
	return (map->dev && strcmp(dev_name(map->dev), LOG_DEVICE) == 0);
}
#else
static inline bool regmap_should_log(struct regmap *map) { return false; }
#endif


static int _regmap_update_bits(struct regmap *map, unsigned int reg,
			       unsigned int mask, unsigned int val,
			       bool *change, bool force_write);

static int _regmap_bus_reg_read(void *context, unsigned int reg,
				unsigned int *val);
static int _regmap_bus_read(void *context, unsigned int reg,
			    unsigned int *val);
static int _regmap_bus_formatted_write(void *context, unsigned int reg,
				       unsigned int val);
static int _regmap_bus_reg_write(void *context, unsigned int reg,
				 unsigned int val);
static int _regmap_bus_raw_write(void *context, unsigned int reg,
				 unsigned int val);

bool regmap_reg_in_ranges(unsigned int reg,
			  const struct regmap_range *ranges,
			  unsigned int nranges)
{
	const struct regmap_range *r;
	int i;

	for (i = 0, r = ranges; i < nranges; i++, r++)
		if (regmap_reg_in_range(reg, r))
			return true;
	return false;
}
EXPORT_SYMBOL_GPL(regmap_reg_in_ranges);

bool regmap_check_range_table(struct regmap *map, unsigned int reg,
			      const struct regmap_access_table *table)
{
	/* Check "no ranges" first */
	if (regmap_reg_in_ranges(reg, table->no_ranges, table->n_no_ranges))
		return false;

	/* In case zero "yes ranges" are supplied, any reg is OK */
	if (!table->n_yes_ranges)
		return true;

	return regmap_reg_in_ranges(reg, table->yes_ranges,
				    table->n_yes_ranges);
}
EXPORT_SYMBOL_GPL(regmap_check_range_table);

bool regmap_writeable(struct regmap *map, unsigned int reg)
{
	if (map->max_register && reg > map->max_register)
		return false;

	if (map->writeable_reg)
		return map->writeable_reg(map->dev, reg);

	if (map->wr_table)
		return regmap_check_range_table(map, reg, map->wr_table);

	return true;
}

bool regmap_cached(struct regmap *map, unsigned int reg)
{
	int ret;
	unsigned int val;

	if (map->cache_type == REGCACHE_NONE)
		return false;

	if (!map->cache_ops)
		return false;

	if (map->max_register && reg > map->max_register)
		return false;

	map->lock(map->lock_arg);
	ret = regcache_read(map, reg, &val);
	map->unlock(map->lock_arg);
	if (ret)
		return false;

	return true;
}

bool regmap_readable(struct regmap *map, unsigned int reg)
{
	if (!map->reg_read)
		return false;

	if (map->max_register && reg > map->max_register)
		return false;

	if (map->format.format_write)
		return false;

	if (map->readable_reg)
		return map->readable_reg(map->dev, reg);

	if (map->rd_table)
		return regmap_check_range_table(map, reg, map->rd_table);

	return true;
}

bool regmap_volatile(struct regmap *map, unsigned int reg)
{
	if (!map->format.format_write && !regmap_readable(map, reg))
		return false;

	if (map->volatile_reg)
		return map->volatile_reg(map->dev, reg);

	if (map->volatile_table)
		return regmap_check_range_table(map, reg, map->volatile_table);

	if (map->cache_ops)
		return false;
	else
		return true;
}

bool regmap_precious(struct regmap *map, unsigned int reg)
{
	if (!regmap_readable(map, reg))
		return false;

	if (map->precious_reg)
		return map->precious_reg(map->dev, reg);

	if (map->precious_table)
		return regmap_check_range_table(map, reg, map->precious_table);

	return false;
}

bool regmap_writeable_noinc(struct regmap *map, unsigned int reg)
{
	if (map->writeable_noinc_reg)
		return map->writeable_noinc_reg(map->dev, reg);

	if (map->wr_noinc_table)
		return regmap_check_range_table(map, reg, map->wr_noinc_table);

	return true;
}

bool regmap_readable_noinc(struct regmap *map, unsigned int reg)
{
	if (map->readable_noinc_reg)
		return map->readable_noinc_reg(map->dev, reg);

	if (map->rd_noinc_table)
		return regmap_check_range_table(map, reg, map->rd_noinc_table);

	return true;
}

static bool regmap_volatile_range(struct regmap *map, unsigned int reg,
	size_t num)
{
	unsigned int i;

	for (i = 0; i < num; i++)
		if (!regmap_volatile(map, reg + regmap_get_offset(map, i)))
			return false;

	return true;
}

static void regmap_format_12_20_write(struct regmap *map,
				     unsigned int reg, unsigned int val)
{
	u8 *out = map->work_buf;

	out[0] = reg >> 4;
	out[1] = (reg << 4) | (val >> 16);
	out[2] = val >> 8;
	out[3] = val;
}


static void regmap_format_2_6_write(struct regmap *map,
				     unsigned int reg, unsigned int val)
{
	u8 *out = map->work_buf;

	*out = (reg << 6) | val;
}

static void regmap_format_4_12_write(struct regmap *map,
				     unsigned int reg, unsigned int val)
{
	__be16 *out = map->work_buf;
	*out = cpu_to_be16((reg << 12) | val);
}

static void regmap_format_7_9_write(struct regmap *map,
				    unsigned int reg, unsigned int val)
{
	__be16 *out = map->work_buf;
	*out = cpu_to_be16((reg << 9) | val);
}

static void regmap_format_7_17_write(struct regmap *map,
				    unsigned int reg, unsigned int val)
{
	u8 *out = map->work_buf;

	out[2] = val;
	out[1] = val >> 8;
	out[0] = (val >> 16) | (reg << 1);
}

static void regmap_format_10_14_write(struct regmap *map,
				    unsigned int reg, unsigned int val)
{
	u8 *out = map->work_buf;

	out[2] = val;
	out[1] = (val >> 8) | (reg << 6);
	out[0] = reg >> 2;
}

static void regmap_format_8(void *buf, unsigned int val, unsigned int shift)
{
	u8 *b = buf;

	b[0] = val << shift;
}

static void regmap_format_16_be(void *buf, unsigned int val, unsigned int shift)
{
	put_unaligned_be16(val << shift, buf);
}

static void regmap_format_16_le(void *buf, unsigned int val, unsigned int shift)
{
	put_unaligned_le16(val << shift, buf);
}

static void regmap_format_16_native(void *buf, unsigned int val,
				    unsigned int shift)
{
	u16 v = val << shift;

	memcpy(buf, &v, sizeof(v));
}

static void regmap_format_24_be(void *buf, unsigned int val, unsigned int shift)
{
	put_unaligned_be24(val << shift, buf);
}

static void regmap_format_32_be(void *buf, unsigned int val, unsigned int shift)
{
	put_unaligned_be32(val << shift, buf);
}

static void regmap_format_32_le(void *buf, unsigned int val, unsigned int shift)
{
	put_unaligned_le32(val << shift, buf);
}

static void regmap_format_32_native(void *buf, unsigned int val,
				    unsigned int shift)
{
	u32 v = val << shift;

	memcpy(buf, &v, sizeof(v));
}

#ifdef CONFIG_64BIT
static void regmap_format_64_be(void *buf, unsigned int val, unsigned int shift)
{
	put_unaligned_be64((u64) val << shift, buf);
}

static void regmap_format_64_le(void *buf, unsigned int val, unsigned int shift)
{
	put_unaligned_le64((u64) val << shift, buf);
}

static void regmap_format_64_native(void *buf, unsigned int val,
				    unsigned int shift)
{
	u64 v = (u64) val << shift;

	memcpy(buf, &v, sizeof(v));
}
#endif

static void regmap_parse_inplace_noop(void *buf)
{
}

static unsigned int regmap_parse_8(const void *buf)
{
	const u8 *b = buf;

	return b[0];
}

static unsigned int regmap_parse_16_be(const void *buf)
{
	return get_unaligned_be16(buf);
}

static unsigned int regmap_parse_16_le(const void *buf)
{
	return get_unaligned_le16(buf);
}

static void regmap_parse_16_be_inplace(void *buf)
{
	u16 v = get_unaligned_be16(buf);

	memcpy(buf, &v, sizeof(v));
}

static void regmap_parse_16_le_inplace(void *buf)
{
	u16 v = get_unaligned_le16(buf);

	memcpy(buf, &v, sizeof(v));
}

static unsigned int regmap_parse_16_native(const void *buf)
{
	u16 v;

	memcpy(&v, buf, sizeof(v));
	return v;
}

static unsigned int regmap_parse_24_be(const void *buf)
{
	return get_unaligned_be24(buf);
}

static unsigned int regmap_parse_32_be(const void *buf)
{
	return get_unaligned_be32(buf);
}

static unsigned int regmap_parse_32_le(const void *buf)
{
	return get_unaligned_le32(buf);
}

static void regmap_parse_32_be_inplace(void *buf)
{
	u32 v = get_unaligned_be32(buf);

	memcpy(buf, &v, sizeof(v));
}

static void regmap_parse_32_le_inplace(void *buf)
{
	u32 v = get_unaligned_le32(buf);

	memcpy(buf, &v, sizeof(v));
}

static unsigned int regmap_parse_32_native(const void *buf)
{
	u32 v;

	memcpy(&v, buf, sizeof(v));
	return v;
}

#ifdef CONFIG_64BIT
static unsigned int regmap_parse_64_be(const void *buf)
{
	return get_unaligned_be64(buf);
}

static unsigned int regmap_parse_64_le(const void *buf)
{
	return get_unaligned_le64(buf);
}

static void regmap_parse_64_be_inplace(void *buf)
{
	u64 v =  get_unaligned_be64(buf);

	memcpy(buf, &v, sizeof(v));
}

static void regmap_parse_64_le_inplace(void *buf)
{
	u64 v = get_unaligned_le64(buf);

	memcpy(buf, &v, sizeof(v));
}

static unsigned int regmap_parse_64_native(const void *buf)
{
	u64 v;

	memcpy(&v, buf, sizeof(v));
	return v;
}
#endif

static void regmap_lock_hwlock(void *__map)
{
	struct regmap *map = __map;

	hwspin_lock_timeout(map->hwlock, UINT_MAX);
}

static void regmap_lock_hwlock_irq(void *__map)
{
	struct regmap *map = __map;

	hwspin_lock_timeout_irq(map->hwlock, UINT_MAX);
}

static void regmap_lock_hwlock_irqsave(void *__map)
{
	struct regmap *map = __map;

	hwspin_lock_timeout_irqsave(map->hwlock, UINT_MAX,
				    &map->spinlock_flags);
}

static void regmap_unlock_hwlock(void *__map)
{
	struct regmap *map = __map;

	hwspin_unlock(map->hwlock);
}

static void regmap_unlock_hwlock_irq(void *__map)
{
	struct regmap *map = __map;

	hwspin_unlock_irq(map->hwlock);
}

static void regmap_unlock_hwlock_irqrestore(void *__map)
{
	struct regmap *map = __map;

	hwspin_unlock_irqrestore(map->hwlock, &map->spinlock_flags);
}

static void regmap_lock_unlock_none(void *__map)
{

}

static void regmap_lock_mutex(void *__map)
{
	struct regmap *map = __map;
	mutex_lock(&map->mutex);
}

static void regmap_unlock_mutex(void *__map)
{
	struct regmap *map = __map;
	mutex_unlock(&map->mutex);
}

static void regmap_lock_spinlock(void *__map)
__acquires(&map->spinlock)
{
	struct regmap *map = __map;
	unsigned long flags;

	spin_lock_irqsave(&map->spinlock, flags);
	map->spinlock_flags = flags;
}

static void regmap_unlock_spinlock(void *__map)
__releases(&map->spinlock)
{
	struct regmap *map = __map;
	spin_unlock_irqrestore(&map->spinlock, map->spinlock_flags);
}

static void regmap_lock_raw_spinlock(void *__map)
__acquires(&map->raw_spinlock)
{
	struct regmap *map = __map;
	unsigned long flags;

	raw_spin_lock_irqsave(&map->raw_spinlock, flags);
	map->raw_spinlock_flags = flags;
}

static void regmap_unlock_raw_spinlock(void *__map)
__releases(&map->raw_spinlock)
{
	struct regmap *map = __map;
	raw_spin_unlock_irqrestore(&map->raw_spinlock, map->raw_spinlock_flags);
}

static void dev_get_regmap_release(struct device *dev, void *res)
{
	/*
	 * We don't actually have anything to do here; the goal here
	 * is not to manage the regmap but to provide a simple way to
	 * get the regmap back given a struct device.
	 */
}

static bool _regmap_range_add(struct regmap *map,
			      struct regmap_range_node *data)
{
	struct rb_root *root = &map->range_tree;
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	while (*new) {
		struct regmap_range_node *this =
			rb_entry(*new, struct regmap_range_node, node);

		parent = *new;
		if (data->range_max < this->range_min)
			new = &((*new)->rb_left);
		else if (data->range_min > this->range_max)
			new = &((*new)->rb_right);
		else
			return false;
	}

	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);

	return true;
}

static struct regmap_range_node *_regmap_range_lookup(struct regmap *map,
						      unsigned int reg)
{
	struct rb_node *node = map->range_tree.rb_node;

	while (node) {
		struct regmap_range_node *this =
			rb_entry(node, struct regmap_range_node, node);

		if (reg < this->range_min)
			node = node->rb_left;
		else if (reg > this->range_max)
			node = node->rb_right;
		else
			return this;
	}

	return NULL;
}

static void regmap_range_exit(struct regmap *map)
{
	struct rb_node *next;
	struct regmap_range_node *range_node;

	next = rb_first(&map->range_tree);
	while (next) {
		range_node = rb_entry(next, struct regmap_range_node, node);
		next = rb_next(&range_node->node);
		rb_erase(&range_node->node, &map->range_tree);
		kfree(range_node);
	}

	kfree(map->selector_work_buf);
}

static int regmap_set_name(struct regmap *map, const struct regmap_config *config)
{
	if (config->name) {
		const char *name = kstrdup_const(config->name, GFP_KERNEL);

		if (!name)
			return -ENOMEM;

		kfree_const(map->name);
		map->name = name;
	}

	return 0;
}

int regmap_attach_dev(struct device *dev, struct regmap *map,
		      const struct regmap_config *config)
{
	struct regmap **m;
	int ret;

	map->dev = dev;

	ret = regmap_set_name(map, config);
	if (ret)
		return ret;

	regmap_debugfs_exit(map);
	regmap_debugfs_init(map);

	/* Add a devres resource for dev_get_regmap() */
	m = devres_alloc(dev_get_regmap_release, sizeof(*m), GFP_KERNEL);
	if (!m) {
		regmap_debugfs_exit(map);
		return -ENOMEM;
	}
	*m = map;
	devres_add(dev, m);

	return 0;
}
EXPORT_SYMBOL_GPL(regmap_attach_dev);

static enum regmap_endian regmap_get_reg_endian(const struct regmap_bus *bus,
					const struct regmap_config *config)
{
	enum regmap_endian endian;

	/* Retrieve the endianness specification from the regmap config */
	endian = config->reg_format_endian;

	/* If the regmap config specified a non-default value, use that */
	if (endian != REGMAP_ENDIAN_DEFAULT)
		return endian;

	/* Retrieve the endianness specification from the bus config */
	if (bus && bus->reg_format_endian_default)
		endian = bus->reg_format_endian_default;

	/* If the bus specified a non-default value, use that */
	if (endian != REGMAP_ENDIAN_DEFAULT)
		return endian;

	/* Use this if no other value was found */
	return REGMAP_ENDIAN_BIG;
}

enum regmap_endian regmap_get_val_endian(struct device *dev,
					 const struct regmap_bus *bus,
					 const struct regmap_config *config)
{
	struct fwnode_handle *fwnode = dev ? dev_fwnode(dev) : NULL;
	enum regmap_endian endian;

	/* Retrieve the endianness specification from the regmap config */
	endian = config->val_format_endian;

	/* If the regmap config specified a non-default value, use that */
	if (endian != REGMAP_ENDIAN_DEFAULT)
		return endian;

	/* If the firmware node exist try to get endianness from it */
	if (fwnode_property_read_bool(fwnode, "big-endian"))
		endian = REGMAP_ENDIAN_BIG;
	else if (fwnode_property_read_bool(fwnode, "little-endian"))
		endian = REGMAP_ENDIAN_LITTLE;
	else if (fwnode_property_read_bool(fwnode, "native-endian"))
		endian = REGMAP_ENDIAN_NATIVE;

	/* If the endianness was specified in fwnode, use that */
	if (endian != REGMAP_ENDIAN_DEFAULT)
		return endian;

	/* Retrieve the endianness specification from the bus config */
	if (bus && bus->val_format_endian_default)
		endian = bus->val_format_endian_default;

	/* If the bus specified a non-default value, use that */
	if (endian != REGMAP_ENDIAN_DEFAULT)
		return endian;

	/* Use this if no other value was found */
	return REGMAP_ENDIAN_BIG;
}
EXPORT_SYMBOL_GPL(regmap_get_val_endian);

struct regmap *__regmap_init(struct device *dev,
			     const struct regmap_bus *bus,
			     void *bus_context,
			     const struct regmap_config *config,
			     struct lock_class_key *lock_key,
			     const char *lock_name)
{
	struct regmap *map;
	int ret = -EINVAL;
	enum regmap_endian reg_endian, val_endian;
	int i, j;

	if (!config)
		goto err;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	ret = regmap_set_name(map, config);
	if (ret)
		goto err_map;

	ret = -EINVAL; /* Later error paths rely on this */

	if (config->disable_locking) {
		map->lock = map->unlock = regmap_lock_unlock_none;
		map->can_sleep = config->can_sleep;
		regmap_debugfs_disable(map);
	} else if (config->lock && config->unlock) {
		map->lock = config->lock;
		map->unlock = config->unlock;
		map->lock_arg = config->lock_arg;
		map->can_sleep = config->can_sleep;
	} else if (config->use_hwlock) {
		map->hwlock = hwspin_lock_request_specific(config->hwlock_id);
		if (!map->hwlock) {
			ret = -ENXIO;
			goto err_name;
		}

		switch (config->hwlock_mode) {
		case HWLOCK_IRQSTATE:
			map->lock = regmap_lock_hwlock_irqsave;
			map->unlock = regmap_unlock_hwlock_irqrestore;
			break;
		case HWLOCK_IRQ:
			map->lock = regmap_lock_hwlock_irq;
			map->unlock = regmap_unlock_hwlock_irq;
			break;
		default:
			map->lock = regmap_lock_hwlock;
			map->unlock = regmap_unlock_hwlock;
			break;
		}

		map->lock_arg = map;
	} else {
		if ((bus && bus->fast_io) ||
		    config->fast_io) {
			if (config->use_raw_spinlock) {
				raw_spin_lock_init(&map->raw_spinlock);
				map->lock = regmap_lock_raw_spinlock;
				map->unlock = regmap_unlock_raw_spinlock;
				lockdep_set_class_and_name(&map->raw_spinlock,
							   lock_key, lock_name);
			} else {
				spin_lock_init(&map->spinlock);
				map->lock = regmap_lock_spinlock;
				map->unlock = regmap_unlock_spinlock;
				lockdep_set_class_and_name(&map->spinlock,
							   lock_key, lock_name);
			}
		} else {
			mutex_init(&map->mutex);
			map->lock = regmap_lock_mutex;
			map->unlock = regmap_unlock_mutex;
			map->can_sleep = true;
			lockdep_set_class_and_name(&map->mutex,
						   lock_key, lock_name);
		}
		map->lock_arg = map;
	}

	/*
	 * When we write in fast-paths with regmap_bulk_write() don't allocate
	 * scratch buffers with sleeping allocations.
	 */
	if ((bus && bus->fast_io) || config->fast_io)
		map->alloc_flags = GFP_ATOMIC;
	else
		map->alloc_flags = GFP_KERNEL;

	map->reg_base = config->reg_base;

	map->format.reg_bytes = DIV_ROUND_UP(config->reg_bits, 8);
	map->format.pad_bytes = config->pad_bits / 8;
	map->format.reg_downshift = config->reg_downshift;
	map->format.val_bytes = DIV_ROUND_UP(config->val_bits, 8);
	map->format.buf_size = DIV_ROUND_UP(config->reg_bits +
			config->val_bits + config->pad_bits, 8);
	map->reg_shift = config->pad_bits % 8;
	if (config->reg_stride)
		map->reg_stride = config->reg_stride;
	else
		map->reg_stride = 1;
	if (is_power_of_2(map->reg_stride))
		map->reg_stride_order = ilog2(map->reg_stride);
	else
		map->reg_stride_order = -1;
	map->use_single_read = config->use_single_read || !(config->read || (bus && bus->read));
	map->use_single_write = config->use_single_write || !(config->write || (bus && bus->write));
	map->can_multi_write = config->can_multi_write && (config->write || (bus && bus->write));
	if (bus) {
		map->max_raw_read = bus->max_raw_read;
		map->max_raw_write = bus->max_raw_write;
	} else if (config->max_raw_read && config->max_raw_write) {
		map->max_raw_read = config->max_raw_read;
		map->max_raw_write = config->max_raw_write;
	}
	map->dev = dev;
	map->bus = bus;
	map->bus_context = bus_context;
	map->max_register = config->max_register;
	map->wr_table = config->wr_table;
	map->rd_table = config->rd_table;
	map->volatile_table = config->volatile_table;
	map->precious_table = config->precious_table;
	map->wr_noinc_table = config->wr_noinc_table;
	map->rd_noinc_table = config->rd_noinc_table;
	map->writeable_reg = config->writeable_reg;
	map->readable_reg = config->readable_reg;
	map->volatile_reg = config->volatile_reg;
	map->precious_reg = config->precious_reg;
	map->writeable_noinc_reg = config->writeable_noinc_reg;
	map->readable_noinc_reg = config->readable_noinc_reg;
	map->cache_type = config->cache_type;

	spin_lock_init(&map->async_lock);
	INIT_LIST_HEAD(&map->async_list);
	INIT_LIST_HEAD(&map->async_free);
	init_waitqueue_head(&map->async_waitq);

	if (config->read_flag_mask ||
	    config->write_flag_mask ||
	    config->zero_flag_mask) {
		map->read_flag_mask = config->read_flag_mask;
		map->write_flag_mask = config->write_flag_mask;
	} else if (bus) {
		map->read_flag_mask = bus->read_flag_mask;
	}

	if (config && config->read && config->write) {
		map->reg_read  = _regmap_bus_read;
		if (config->reg_update_bits)
			map->reg_update_bits = config->reg_update_bits;

		/* Bulk read/write */
		map->read = config->read;
		map->write = config->write;

		reg_endian = REGMAP_ENDIAN_NATIVE;
		val_endian = REGMAP_ENDIAN_NATIVE;
	} else if (!bus) {
		map->reg_read  = config->reg_read;
		map->reg_write = config->reg_write;
		map->reg_update_bits = config->reg_update_bits;

		map->defer_caching = false;
		goto skip_format_initialization;
	} else if (!bus->read || !bus->write) {
		map->reg_read = _regmap_bus_reg_read;
		map->reg_write = _regmap_bus_reg_write;
		map->reg_update_bits = bus->reg_update_bits;

		map->defer_caching = false;
		goto skip_format_initialization;
	} else {
		map->reg_read  = _regmap_bus_read;
		map->reg_update_bits = bus->reg_update_bits;
		/* Bulk read/write */
		map->read = bus->read;
		map->write = bus->write;

		reg_endian = regmap_get_reg_endian(bus, config);
		val_endian = regmap_get_val_endian(dev, bus, config);
	}

	switch (config->reg_bits + map->reg_shift) {
	case 2:
		switch (config->val_bits) {
		case 6:
			map->format.format_write = regmap_format_2_6_write;
			break;
		default:
			goto err_hwlock;
		}
		break;

	case 4:
		switch (config->val_bits) {
		case 12:
			map->format.format_write = regmap_format_4_12_write;
			break;
		default:
			goto err_hwlock;
		}
		break;

	case 7:
		switch (config->val_bits) {
		case 9:
			map->format.format_write = regmap_format_7_9_write;
			break;
		case 17:
			map->format.format_write = regmap_format_7_17_write;
			break;
		default:
			goto err_hwlock;
		}
		break;

	case 10:
		switch (config->val_bits) {
		case 14:
			map->format.format_write = regmap_format_10_14_write;
			break;
		default:
			goto err_hwlock;
		}
		break;

	case 12:
		switch (config->val_bits) {
		case 20:
			map->format.format_write = regmap_format_12_20_write;
			break;
		default:
			goto err_hwlock;
		}
		break;

	case 8:
		map->format.format_reg = regmap_format_8;
		break;

	case 16:
		switch (reg_endian) {
		case REGMAP_ENDIAN_BIG:
			map->format.format_reg = regmap_format_16_be;
			break;
		case REGMAP_ENDIAN_LITTLE:
			map->format.format_reg = regmap_format_16_le;
			break;
		case REGMAP_ENDIAN_NATIVE:
			map->format.format_reg = regmap_format_16_native;
			break;
		default:
			goto err_hwlock;
		}
		break;

	case 24:
		switch (reg_endian) {
		case REGMAP_ENDIAN_BIG:
			map->format.format_reg = regmap_format_24_be;
			break;
		default:
			goto err_hwlock;
		}
		break;

	case 32:
		switch (reg_endian) {
		case REGMAP_ENDIAN_BIG:
			map->format.format_reg = regmap_format_32_be;
			break;
		case REGMAP_ENDIAN_LITTLE:
			map->format.format_reg = regmap_format_32_le;
			break;
		case REGMAP_ENDIAN_NATIVE:
			map->format.format_reg = regmap_format_32_native;
			break;
		default:
			goto err_hwlock;
		}
		break;

#ifdef CONFIG_64BIT
	case 64:
		switch (reg_endian) {
		case REGMAP_ENDIAN_BIG:
			map->format.format_reg = regmap_format_64_be;
			break;
		case REGMAP_ENDIAN_LITTLE:
			map->format.format_reg = regmap_format_64_le;
			break;
		case REGMAP_ENDIAN_NATIVE:
			map->format.format_reg = regmap_format_64_native;
			break;
		default:
			goto err_hwlock;
		}
		break;
#endif

	default:
		goto err_hwlock;
	}

	if (val_endian == REGMAP_ENDIAN_NATIVE)
		map->format.parse_inplace = regmap_parse_inplace_noop;

	switch (config->val_bits) {
	case 8:
		map->format.format_val = regmap_format_8;
		map->format.parse_val = regmap_parse_8;
		map->format.parse_inplace = regmap_parse_inplace_noop;
		break;
	case 16:
		switch (val_endian) {
		case REGMAP_ENDIAN_BIG:
			map->format.format_val = regmap_format_16_be;
			map->format.parse_val = regmap_parse_16_be;
			map->format.parse_inplace = regmap_parse_16_be_inplace;
			break;
		case REGMAP_ENDIAN_LITTLE:
			map->format.format_val = regmap_format_16_le;
			map->format.parse_val = regmap_parse_16_le;
			map->format.parse_inplace = regmap_parse_16_le_inplace;
			break;
		case REGMAP_ENDIAN_NATIVE:
			map->format.format_val = regmap_format_16_native;
			map->format.parse_val = regmap_parse_16_native;
			break;
		default:
			goto err_hwlock;
		}
		break;
	case 24:
		switch (val_endian) {
		case REGMAP_ENDIAN_BIG:
			map->format.format_val = regmap_format_24_be;
			map->format.parse_val = regmap_parse_24_be;
			break;
		default:
			goto err_hwlock;
		}
		break;
	case 32:
		switch (val_endian) {
		case REGMAP_ENDIAN_BIG:
			map->format.format_val = regmap_format_32_be;
			map->format.parse_val = regmap_parse_32_be;
			map->format.parse_inplace = regmap_parse_32_be_inplace;
			break;
		case REGMAP_ENDIAN_LITTLE:
			map->format.format_val = regmap_format_32_le;
			map->format.parse_val = regmap_parse_32_le;
			map->format.parse_inplace = regmap_parse_32_le_inplace;
			break;
		case REGMAP_ENDIAN_NATIVE:
			map->format.format_val = regmap_format_32_native;
			map->format.parse_val = regmap_parse_32_native;
			break;
		default:
			goto err_hwlock;
		}
		break;
#ifdef CONFIG_64BIT
	case 64:
		switch (val_endian) {
		case REGMAP_ENDIAN_BIG:
			map->format.format_val = regmap_format_64_be;
			map->format.parse_val = regmap_parse_64_be;
			map->format.parse_inplace = regmap_parse_64_be_inplace;
			break;
		case REGMAP_ENDIAN_LITTLE:
			map->format.format_val = regmap_format_64_le;
			map->format.parse_val = regmap_parse_64_le;
			map->format.parse_inplace = regmap_parse_64_le_inplace;
			break;
		case REGMAP_ENDIAN_NATIVE:
			map->format.format_val = regmap_format_64_native;
			map->format.parse_val = regmap_parse_64_native;
			break;
		default:
			goto err_hwlock;
		}
		break;
#endif
	}

	if (map->format.format_write) {
		if ((reg_endian != REGMAP_ENDIAN_BIG) ||
		    (val_endian != REGMAP_ENDIAN_BIG))
			goto err_hwlock;
		map->use_single_write = true;
	}

	if (!map->format.format_write &&
	    !(map->format.format_reg && map->format.format_val))
		goto err_hwlock;

	map->work_buf = kzalloc(map->format.buf_size, GFP_KERNEL);
	if (map->work_buf == NULL) {
		ret = -ENOMEM;
		goto err_hwlock;
	}

	if (map->format.format_write) {
		map->defer_caching = false;
		map->reg_write = _regmap_bus_formatted_write;
	} else if (map->format.format_val) {
		map->defer_caching = true;
		map->reg_write = _regmap_bus_raw_write;
	}

skip_format_initialization:

	map->range_tree = RB_ROOT;
	for (i = 0; i < config->num_ranges; i++) {
		const struct regmap_range_cfg *range_cfg = &config->ranges[i];
		struct regmap_range_node *new;

		/* Sanity check */
		if (range_cfg->range_max < range_cfg->range_min) {
			dev_err(map->dev, "Invalid range %d: %d < %d\n", i,
				range_cfg->range_max, range_cfg->range_min);
			goto err_range;
		}

		if (range_cfg->range_max > map->max_register) {
			dev_err(map->dev, "Invalid range %d: %d > %d\n", i,
				range_cfg->range_max, map->max_register);
			goto err_range;
		}

		if (range_cfg->selector_reg > map->max_register) {
			dev_err(map->dev,
				"Invalid range %d: selector out of map\n", i);
			goto err_range;
		}

		if (range_cfg->window_len == 0) {
			dev_err(map->dev, "Invalid range %d: window_len 0\n",
				i);
			goto err_range;
		}

		/* Make sure, that this register range has no selector
		   or data window within its boundary */
		for (j = 0; j < config->num_ranges; j++) {
			unsigned int sel_reg = config->ranges[j].selector_reg;
			unsigned int win_min = config->ranges[j].window_start;
			unsigned int win_max = win_min +
					       config->ranges[j].window_len - 1;

			/* Allow data window inside its own virtual range */
			if (j == i)
				continue;

			if (range_cfg->range_min <= sel_reg &&
			    sel_reg <= range_cfg->range_max) {
				dev_err(map->dev,
					"Range %d: selector for %d in window\n",
					i, j);
				goto err_range;
			}

			if (!(win_max < range_cfg->range_min ||
			      win_min > range_cfg->range_max)) {
				dev_err(map->dev,
					"Range %d: window for %d in window\n",
					i, j);
				goto err_range;
			}
		}

		new = kzalloc(sizeof(*new), GFP_KERNEL);
		if (new == NULL) {
			ret = -ENOMEM;
			goto err_range;
		}

		new->map = map;
		new->name = range_cfg->name;
		new->range_min = range_cfg->range_min;
		new->range_max = range_cfg->range_max;
		new->selector_reg = range_cfg->selector_reg;
		new->selector_mask = range_cfg->selector_mask;
		new->selector_shift = range_cfg->selector_shift;
		new->window_start = range_cfg->window_start;
		new->window_len = range_cfg->window_len;

		if (!_regmap_range_add(map, new)) {
			dev_err(map->dev, "Failed to add range %d\n", i);
			kfree(new);
			goto err_range;
		}

		if (map->selector_work_buf == NULL) {
			map->selector_work_buf =
				kzalloc(map->format.buf_size, GFP_KERNEL);
			if (map->selector_work_buf == NULL) {
				ret = -ENOMEM;
				goto err_range;
			}
		}
	}

	ret = regcache_init(map, config);
	if (ret != 0)
		goto err_range;

	if (dev) {
		ret = regmap_attach_dev(dev, map, config);
		if (ret != 0)
			goto err_regcache;
	} else {
		regmap_debugfs_init(map);
	}

	return map;

err_regcache:
	regcache_exit(map);
err_range:
	regmap_range_exit(map);
	kfree(map->work_buf);
err_hwlock:
	if (map->hwlock)
		hwspin_lock_free(map->hwlock);
err_name:
	kfree_const(map->name);
err_map:
	kfree(map);
err:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(__regmap_init);

static void devm_regmap_release(struct device *dev, void *res)
{
	regmap_exit(*(struct regmap **)res);
}

struct regmap *__devm_regmap_init(struct device *dev,
				  const struct regmap_bus *bus,
				  void *bus_context,
				  const struct regmap_config *config,
				  struct lock_class_key *lock_key,
				  const char *lock_name)
{
	struct regmap **ptr, *regmap;

	ptr = devres_alloc(devm_regmap_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	regmap = __regmap_init(dev, bus, bus_context, config,
			       lock_key, lock_name);
	if (!IS_ERR(regmap)) {
		*ptr = regmap;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return regmap;
}
EXPORT_SYMBOL_GPL(__devm_regmap_init);

static void regmap_field_init(struct regmap_field *rm_field,
	struct regmap *regmap, struct reg_field reg_field)
{
	rm_field->regmap = regmap;
	rm_field->reg = reg_field.reg;
	rm_field->shift = reg_field.lsb;
	rm_field->mask = GENMASK(reg_field.msb, reg_field.lsb);

	WARN_ONCE(rm_field->mask == 0, "invalid empty mask defined\n");

	rm_field->id_size = reg_field.id_size;
	rm_field->id_offset = reg_field.id_offset;
}

/**
 * devm_regmap_field_alloc() - Allocate and initialise a register field.
 *
 * @dev: Device that will be interacted with
 * @regmap: regmap bank in which this register field is located.
 * @reg_field: Register field with in the bank.
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap_field. The regmap_field will be automatically freed
 * by the device management code.
 */
struct regmap_field *devm_regmap_field_alloc(struct device *dev,
		struct regmap *regmap, struct reg_field reg_field)
{
	struct regmap_field *rm_field = devm_kzalloc(dev,
					sizeof(*rm_field), GFP_KERNEL);
	if (!rm_field)
		return ERR_PTR(-ENOMEM);

	regmap_field_init(rm_field, regmap, reg_field);

	return rm_field;

}
EXPORT_SYMBOL_GPL(devm_regmap_field_alloc);


/**
 * regmap_field_bulk_alloc() - Allocate and initialise a bulk register field.
 *
 * @regmap: regmap bank in which this register field is located.
 * @rm_field: regmap register fields within the bank.
 * @reg_field: Register fields within the bank.
 * @num_fields: Number of register fields.
 *
 * The return value will be an -ENOMEM on error or zero for success.
 * Newly allocated regmap_fields should be freed by calling
 * regmap_field_bulk_free()
 */
int regmap_field_bulk_alloc(struct regmap *regmap,
			    struct regmap_field **rm_field,
			    const struct reg_field *reg_field,
			    int num_fields)
{
	struct regmap_field *rf;
	int i;

	rf = kcalloc(num_fields, sizeof(*rf), GFP_KERNEL);
	if (!rf)
		return -ENOMEM;

	for (i = 0; i < num_fields; i++) {
		regmap_field_init(&rf[i], regmap, reg_field[i]);
		rm_field[i] = &rf[i];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(regmap_field_bulk_alloc);

/**
 * devm_regmap_field_bulk_alloc() - Allocate and initialise a bulk register
 * fields.
 *
 * @dev: Device that will be interacted with
 * @regmap: regmap bank in which this register field is located.
 * @rm_field: regmap register fields within the bank.
 * @reg_field: Register fields within the bank.
 * @num_fields: Number of register fields.
 *
 * The return value will be an -ENOMEM on error or zero for success.
 * Newly allocated regmap_fields will be automatically freed by the
 * device management code.
 */
int devm_regmap_field_bulk_alloc(struct device *dev,
				 struct regmap *regmap,
				 struct regmap_field **rm_field,
				 const struct reg_field *reg_field,
				 int num_fields)
{
	struct regmap_field *rf;
	int i;

	rf = devm_kcalloc(dev, num_fields, sizeof(*rf), GFP_KERNEL);
	if (!rf)
		return -ENOMEM;

	for (i = 0; i < num_fields; i++) {
		regmap_field_init(&rf[i], regmap, reg_field[i]);
		rm_field[i] = &rf[i];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(devm_regmap_field_bulk_alloc);

/**
 * regmap_field_bulk_free() - Free register field allocated using
 *                       regmap_field_bulk_alloc.
 *
 * @field: regmap fields which should be freed.
 */
void regmap_field_bulk_free(struct regmap_field *field)
{
	kfree(field);
}
EXPORT_SYMBOL_GPL(regmap_field_bulk_free);

/**
 * devm_regmap_field_bulk_free() - Free a bulk register field allocated using
 *                            devm_regmap_field_bulk_alloc.
 *
 * @dev: Device that will be interacted with
 * @field: regmap field which should be freed.
 *
 * Free register field allocated using devm_regmap_field_bulk_alloc(). Usually
 * drivers need not call this function, as the memory allocated via devm
 * will be freed as per device-driver life-cycle.
 */
void devm_regmap_field_bulk_free(struct device *dev,
				 struct regmap_field *field)
{
	devm_kfree(dev, field);
}
EXPORT_SYMBOL_GPL(devm_regmap_field_bulk_free);

/**
 * devm_regmap_field_free() - Free a register field allocated using
 *                            devm_regmap_field_alloc.
 *
 * @dev: Device that will be interacted with
 * @field: regmap field which should be freed.
 *
 * Free register field allocated using devm_regmap_field_alloc(). Usually
 * drivers need not call this function, as the memory allocated via devm
 * will be freed as per device-driver life-cyle.
 */
void devm_regmap_field_free(struct device *dev,
	struct regmap_field *field)
{
	devm_kfree(dev, field);
}
EXPORT_SYMBOL_GPL(devm_regmap_field_free);

/**
 * regmap_field_alloc() - Allocate and initialise a register field.
 *
 * @regmap: regmap bank in which this register field is located.
 * @reg_field: Register field with in the bank.
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap_field. The regmap_field should be freed by the
 * user once its finished working with it using regmap_field_free().
 */
struct regmap_field *regmap_field_alloc(struct regmap *regmap,
		struct reg_field reg_field)
{
	struct regmap_field *rm_field = kzalloc(sizeof(*rm_field), GFP_KERNEL);

	if (!rm_field)
		return ERR_PTR(-ENOMEM);

	regmap_field_init(rm_field, regmap, reg_field);

	return rm_field;
}
EXPORT_SYMBOL_GPL(regmap_field_alloc);

/**
 * regmap_field_free() - Free register field allocated using
 *                       regmap_field_alloc.
 *
 * @field: regmap field which should be freed.
 */
void regmap_field_free(struct regmap_field *field)
{
	kfree(field);
}
EXPORT_SYMBOL_GPL(regmap_field_free);

/**
 * regmap_reinit_cache() - Reinitialise the current register cache
 *
 * @map: Register map to operate on.
 * @config: New configuration.  Only the cache data will be used.
 *
 * Discard any existing register cache for the map and initialize a
 * new cache.  This can be used to restore the cache to defaults or to
 * update the cache configuration to reflect runtime discovery of the
 * hardware.
 *
 * No explicit locking is done here, the user needs to ensure that
 * this function will not race with other calls to regmap.
 */
int regmap_reinit_cache(struct regmap *map, const struct regmap_config *config)
{
	int ret;

	regcache_exit(map);
	regmap_debugfs_exit(map);

	map->max_register = config->max_register;
	map->writeable_reg = config->writeable_reg;
	map->readable_reg = config->readable_reg;
	map->volatile_reg = config->volatile_reg;
	map->precious_reg = config->precious_reg;
	map->writeable_noinc_reg = config->writeable_noinc_reg;
	map->readable_noinc_reg = config->readable_noinc_reg;
	map->cache_type = config->cache_type;

	ret = regmap_set_name(map, config);
	if (ret)
		return ret;

	regmap_debugfs_init(map);

	map->cache_bypass = false;
	map->cache_only = false;

	return regcache_init(map, config);
}
EXPORT_SYMBOL_GPL(regmap_reinit_cache);

/**
 * regmap_exit() - Free a previously allocated register map
 *
 * @map: Register map to operate on.
 */
void regmap_exit(struct regmap *map)
{
	struct regmap_async *async;

	regcache_exit(map);
	regmap_debugfs_exit(map);
	regmap_range_exit(map);
	if (map->bus && map->bus->free_context)
		map->bus->free_context(map->bus_context);
	kfree(map->work_buf);
	while (!list_empty(&map->async_free)) {
		async = list_first_entry_or_null(&map->async_free,
						 struct regmap_async,
						 list);
		list_del(&async->list);
		kfree(async->work_buf);
		kfree(async);
	}
	if (map->hwlock)
		hwspin_lock_free(map->hwlock);
	if (map->lock == regmap_lock_mutex)
		mutex_destroy(&map->mutex);
	kfree_const(map->name);
	kfree(map->patch);
	if (map->bus && map->bus->free_on_exit)
		kfree(map->bus);
	kfree(map);
}
EXPORT_SYMBOL_GPL(regmap_exit);

static int dev_get_regmap_match(struct device *dev, void *res, void *data)
{
	struct regmap **r = res;
	if (!r || !*r) {
		WARN_ON(!r || !*r);
		return 0;
	}

	/* If the user didn't specify a name match any */
	if (data)
		return !strcmp((*r)->name, data);
	else
		return 1;
}

/**
 * dev_get_regmap() - Obtain the regmap (if any) for a device
 *
 * @dev: Device to retrieve the map for
 * @name: Optional name for the register map, usually NULL.
 *
 * Returns the regmap for the device if one is present, or NULL.  If
 * name is specified then it must match the name specified when
 * registering the device, if it is NULL then the first regmap found
 * will be used.  Devices with multiple register maps are very rare,
 * generic code should normally not need to specify a name.
 */
struct regmap *dev_get_regmap(struct device *dev, const char *name)
{
	struct regmap **r = devres_find(dev, dev_get_regmap_release,
					dev_get_regmap_match, (void *)name);

	if (!r)
		return NULL;
	return *r;
}
EXPORT_SYMBOL_GPL(dev_get_regmap);

/**
 * regmap_get_device() - Obtain the device from a regmap
 *
 * @map: Register map to operate on.
 *
 * Returns the underlying device that the regmap has been created for.
 */
struct device *regmap_get_device(struct regmap *map)
{
	return map->dev;
}
EXPORT_SYMBOL_GPL(regmap_get_device);

static int _regmap_select_page(struct regmap *map, unsigned int *reg,
			       struct regmap_range_node *range,
			       unsigned int val_num)
{
	void *orig_work_buf;
	unsigned int win_offset;
	unsigned int win_page;
	bool page_chg;
	int ret;

	win_offset = (*reg - range->range_min) % range->window_len;
	win_page = (*reg - range->range_min) / range->window_len;

	if (val_num > 1) {
		/* Bulk write shouldn't cross range boundary */
		if (*reg + val_num - 1 > range->range_max)
			return -EINVAL;

		/* ... or single page boundary */
		if (val_num > range->window_len - win_offset)
			return -EINVAL;
	}

	/* It is possible to have selector register inside data window.
	   In that case, selector register is located on every page and
	   it needs no page switching, when accessed alone. */
	if (val_num > 1 ||
	    range->window_start + win_offset != range->selector_reg) {
		/* Use separate work_buf during page switching */
		orig_work_buf = map->work_buf;
		map->work_buf = map->selector_work_buf;

		ret = _regmap_update_bits(map, range->selector_reg,
					  range->selector_mask,
					  win_page << range->selector_shift,
					  &page_chg, false);

		map->work_buf = orig_work_buf;

		if (ret != 0)
			return ret;
	}

	*reg = range->window_start + win_offset;

	return 0;
}

static void regmap_set_work_buf_flag_mask(struct regmap *map, int max_bytes,
					  unsigned long mask)
{
	u8 *buf;
	int i;

	if (!mask || !map->work_buf)
		return;

	buf = map->work_buf;

	for (i = 0; i < max_bytes; i++)
		buf[i] |= (mask >> (8 * i)) & 0xff;
}

static int _regmap_raw_write_impl(struct regmap *map, unsigned int reg,
				  const void *val, size_t val_len, bool noinc)
{
	struct regmap_range_node *range;
	unsigned long flags;
	void *work_val = map->work_buf + map->format.reg_bytes +
		map->format.pad_bytes;
	void *buf;
	int ret = -ENOTSUPP;
	size_t len;
	int i;

	/* Check for unwritable or noinc registers in range
	 * before we start
	 */
	if (!regmap_writeable_noinc(map, reg)) {
		for (i = 0; i < val_len / map->format.val_bytes; i++) {
			unsigned int element =
				reg + regmap_get_offset(map, i);
			if (!regmap_writeable(map, element) ||
				regmap_writeable_noinc(map, element))
				return -EINVAL;
		}
	}

	if (!map->cache_bypass && map->format.parse_val) {
		unsigned int ival;
		int val_bytes = map->format.val_bytes;
		for (i = 0; i < val_len / val_bytes; i++) {
			ival = map->format.parse_val(val + (i * val_bytes));
			ret = regcache_write(map,
					     reg + regmap_get_offset(map, i),
					     ival);
			if (ret) {
				dev_err(map->dev,
					"Error in caching of register: %x ret: %d\n",
					reg + regmap_get_offset(map, i), ret);
				return ret;
			}
		}
		if (map->cache_only) {
			map->cache_dirty = true;
			return 0;
		}
	}

	range = _regmap_range_lookup(map, reg);
	if (range) {
		int val_num = val_len / map->format.val_bytes;
		int win_offset = (reg - range->range_min) % range->window_len;
		int win_residue = range->window_len - win_offset;

		/* If the write goes beyond the end of the window split it */
		while (val_num > win_residue) {
			dev_dbg(map->dev, "Writing window %d/%zu\n",
				win_residue, val_len / map->format.val_bytes);
			ret = _regmap_raw_write_impl(map, reg, val,
						     win_residue *
						     map->format.val_bytes, noinc);
			if (ret != 0)
				return ret;

			reg += win_residue;
			val_num -= win_residue;
			val += win_residue * map->format.val_bytes;
			val_len -= win_residue * map->format.val_bytes;

			win_offset = (reg - range->range_min) %
				range->window_len;
			win_residue = range->window_len - win_offset;
		}

		ret = _regmap_select_page(map, &reg, range, noinc ? 1 : val_num);
		if (ret != 0)
			return ret;
	}

	reg += map->reg_base;
	reg >>= map->format.reg_downshift;
	map->format.format_reg(map->work_buf, reg, map->reg_shift);
	regmap_set_work_buf_flag_mask(map, map->format.reg_bytes,
				      map->write_flag_mask);

	/*
	 * Essentially all I/O mechanisms will be faster with a single
	 * buffer to write.  Since register syncs often generate raw
	 * writes of single registers optimise that case.
	 */
	if (val != work_val && val_len == map->format.val_bytes) {
		memcpy(work_val, val, map->format.val_bytes);
		val = work_val;
	}

	if (map->async && map->bus && map->bus->async_write) {
		struct regmap_async *async;

		trace_regmap_async_write_start(map, reg, val_len);

		spin_lock_irqsave(&map->async_lock, flags);
		async = list_first_entry_or_null(&map->async_free,
						 struct regmap_async,
						 list);
		if (async)
			list_del(&async->list);
		spin_unlock_irqrestore(&map->async_lock, flags);

		if (!async) {
			async = map->bus->async_alloc();
			if (!async)
				return -ENOMEM;

			async->work_buf = kzalloc(map->format.buf_size,
						  GFP_KERNEL | GFP_DMA);
			if (!async->work_buf) {
				kfree(async);
				return -ENOMEM;
			}
		}

		async->map = map;

		/* If the caller supplied the value we can use it safely. */
		memcpy(async->work_buf, map->work_buf, map->format.pad_bytes +
		       map->format.reg_bytes + map->format.val_bytes);

		spin_lock_irqsave(&map->async_lock, flags);
		list_add_tail(&async->list, &map->async_list);
		spin_unlock_irqrestore(&map->async_lock, flags);

		if (val != work_val)
			ret = map->bus->async_write(map->bus_context,
						    async->work_buf,
						    map->format.reg_bytes +
						    map->format.pad_bytes,
						    val, val_len, async);
		else
			ret = map->bus->async_write(map->bus_context,
						    async->work_buf,
						    map->format.reg_bytes +
						    map->format.pad_bytes +
						    val_len, NULL, 0, async);

		if (ret != 0) {
			dev_err(map->dev, "Failed to schedule write: %d\n",
				ret);

			spin_lock_irqsave(&map->async_lock, flags);
			list_move(&async->list, &map->async_free);
			spin_unlock_irqrestore(&map->async_lock, flags);
		}

		return ret;
	}

	trace_regmap_hw_write_start(map, reg, val_len / map->format.val_bytes);

	/* If we're doing a single register write we can probably just
	 * send the work_buf directly, otherwise try to do a gather
	 * write.
	 */
	if (val == work_val)
		ret = map->write(map->bus_context, map->work_buf,
				 map->format.reg_bytes +
				 map->format.pad_bytes +
				 val_len);
	else if (map->bus && map->bus->gather_write)
		ret = map->bus->gather_write(map->bus_context, map->work_buf,
					     map->format.reg_bytes +
					     map->format.pad_bytes,
					     val, val_len);
	else
		ret = -ENOTSUPP;

	/* If that didn't work fall back on linearising by hand. */
	if (ret == -ENOTSUPP) {
		len = map->format.reg_bytes + map->format.pad_bytes + val_len;
		buf = kzalloc(len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		memcpy(buf, map->work_buf, map->format.reg_bytes);
		memcpy(buf + map->format.reg_bytes + map->format.pad_bytes,
		       val, val_len);
		ret = map->write(map->bus_context, buf, len);

		kfree(buf);
	} else if (ret != 0 && !map->cache_bypass && map->format.parse_val) {
		/* regcache_drop_region() takes lock that we already have,
		 * thus call map->cache_ops->drop() directly
		 */
		if (map->cache_ops && map->cache_ops->drop)
			map->cache_ops->drop(map, reg, reg + 1);
	}

	trace_regmap_hw_write_done(map, reg, val_len / map->format.val_bytes);

	return ret;
}

/**
 * regmap_can_raw_write - Test if regmap_raw_write() is supported
 *
 * @map: Map to check.
 */
bool regmap_can_raw_write(struct regmap *map)
{
	return map->write && map->format.format_val && map->format.format_reg;
}
EXPORT_SYMBOL_GPL(regmap_can_raw_write);

/**
 * regmap_get_raw_read_max - Get the maximum size we can read
 *
 * @map: Map to check.
 */
size_t regmap_get_raw_read_max(struct regmap *map)
{
	return map->max_raw_read;
}
EXPORT_SYMBOL_GPL(regmap_get_raw_read_max);

/**
 * regmap_get_raw_write_max - Get the maximum size we can read
 *
 * @map: Map to check.
 */
size_t regmap_get_raw_write_max(struct regmap *map)
{
	return map->max_raw_write;
}
EXPORT_SYMBOL_GPL(regmap_get_raw_write_max);

static int _regmap_bus_formatted_write(void *context, unsigned int reg,
				       unsigned int val)
{
	int ret;
	struct regmap_range_node *range;
	struct regmap *map = context;

	WARN_ON(!map->format.format_write);

	range = _regmap_range_lookup(map, reg);
	if (range) {
		ret = _regmap_select_page(map, &reg, range, 1);
		if (ret != 0)
			return ret;
	}

	reg += map->reg_base;
	reg >>= map->format.reg_downshift;
	map->format.format_write(map, reg, val);

	trace_regmap_hw_write_start(map, reg, 1);

	ret = map->write(map->bus_context, map->work_buf, map->format.buf_size);

	trace_regmap_hw_write_done(map, reg, 1);

	return ret;
}

static int _regmap_bus_reg_write(void *context, unsigned int reg,
				 unsigned int val)
{
	struct regmap *map = context;

	return map->bus->reg_write(map->bus_context, reg, val);
}

static int _regmap_bus_raw_write(void *context, unsigned int reg,
				 unsigned int val)
{
	struct regmap *map = context;

	WARN_ON(!map->format.format_val);

	map->format.format_val(map->work_buf + map->format.reg_bytes
			       + map->format.pad_bytes, val, 0);
	return _regmap_raw_write_impl(map, reg,
				      map->work_buf +
				      map->format.reg_bytes +
				      map->format.pad_bytes,
				      map->format.val_bytes,
				      false);
}

static inline void *_regmap_map_get_context(struct regmap *map)
{
	return (map->bus || (!map->bus && map->read)) ? map : map->bus_context;
}

int _regmap_write(struct regmap *map, unsigned int reg,
		  unsigned int val)
{
	int ret;
	void *context = _regmap_map_get_context(map);

	if (!regmap_writeable(map, reg))
		return -EIO;

	if (!map->cache_bypass && !map->defer_caching) {
		ret = regcache_write(map, reg, val);
		if (ret != 0)
			return ret;
		if (map->cache_only) {
			map->cache_dirty = true;
			return 0;
		}
	}

	ret = map->reg_write(context, reg, val);
	if (ret == 0) {
		if (regmap_should_log(map))
			dev_info(map->dev, "%x <= %x\n", reg, val);

		trace_regmap_reg_write(map, reg, val);
	}

	return ret;
}

/**
 * regmap_write() - Write a value to a single register
 *
 * @map: Register map to write to
 * @reg: Register to write to
 * @val: Value to be written
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val)
{
	int ret;

	if (!IS_ALIGNED(reg, map->reg_stride))
		return -EINVAL;

	map->lock(map->lock_arg);

	ret = _regmap_write(map, reg, val);

	map->unlock(map->lock_arg);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_write);

/**
 * regmap_write_async() - Write a value to a single register asynchronously
 *
 * @map: Register map to write to
 * @reg: Register to write to
 * @val: Value to be written
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_write_async(struct regmap *map, unsigned int reg, unsigned int val)
{
	int ret;

	if (!IS_ALIGNED(reg, map->reg_stride))
		return -EINVAL;

	map->lock(map->lock_arg);

	map->async = true;

	ret = _regmap_write(map, reg, val);

	map->async = false;

	map->unlock(map->lock_arg);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_write_async);

int _regmap_raw_write(struct regmap *map, unsigned int reg,
		      const void *val, size_t val_len, bool noinc)
{
	size_t val_bytes = map->format.val_bytes;
	size_t val_count = val_len / val_bytes;
	size_t chunk_count, chunk_bytes;
	size_t chunk_regs = val_count;
	int ret, i;

	if (!val_count)
		return -EINVAL;

	if (map->use_single_write)
		chunk_regs = 1;
	else if (map->max_raw_write && val_len > map->max_raw_write)
		chunk_regs = map->max_raw_write / val_bytes;

	chunk_count = val_count / chunk_regs;
	chunk_bytes = chunk_regs * val_bytes;

	/* Write as many bytes as possible with chunk_size */
	for (i = 0; i < chunk_count; i++) {
		ret = _regmap_raw_write_impl(map, reg, val, chunk_bytes, noinc);
		if (ret)
			return ret;

		reg += regmap_get_offset(map, chunk_regs);
		val += chunk_bytes;
		val_len -= chunk_bytes;
	}

	/* Write remaining bytes */
	if (val_len)
		ret = _regmap_raw_write_impl(map, reg, val, val_len, noinc);

	return ret;
}

/**
 * regmap_raw_write() - Write raw values to one or more registers
 *
 * @map: Register map to write to
 * @reg: Initial register to write to
 * @val: Block of data to be written, laid out for direct transmission to the
 *       device
 * @val_len: Length of data pointed to by val.
 *
 * This function is intended to be used for things like firmware
 * download where a large block of data needs to be transferred to the
 * device.  No formatting will be done on the data provided.
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_raw_write(struct regmap *map, unsigned int reg,
		     const void *val, size_t val_len)
{
	int ret;

	if (!regmap_can_raw_write(map))
		return -EINVAL;
	if (val_len % map->format.val_bytes)
		return -EINVAL;

	map->lock(map->lock_arg);

	ret = _regmap_raw_write(map, reg, val, val_len, false);

	map->unlock(map->lock_arg);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_raw_write);

/**
 * regmap_noinc_write(): Write data from a register without incrementing the
 *			register number
 *
 * @map: Register map to write to
 * @reg: Register to write to
 * @val: Pointer to data buffer
 * @val_len: Length of output buffer in bytes.
 *
 * The regmap API usually assumes that bulk bus write operations will write a
 * range of registers. Some devices have certain registers for which a write
 * operation can write to an internal FIFO.
 *
 * The target register must be volatile but registers after it can be
 * completely unrelated cacheable registers.
 *
 * This will attempt multiple writes as required to write val_len bytes.
 *
 * A value of zero will be returned on success, a negative errno will be
 * returned in error cases.
 */
int regmap_noinc_write(struct regmap *map, unsigned int reg,
		      const void *val, size_t val_len)
{
	size_t write_len;
	int ret;

	if (!map->write)
		return -ENOTSUPP;

	if (val_len % map->format.val_bytes)
		return -EINVAL;
	if (!IS_ALIGNED(reg, map->reg_stride))
		return -EINVAL;
	if (val_len == 0)
		return -EINVAL;

	map->lock(map->lock_arg);

	if (!regmap_volatile(map, reg) || !regmap_writeable_noinc(map, reg)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	while (val_len) {
		if (map->max_raw_write && map->max_raw_write < val_len)
			write_len = map->max_raw_write;
		else
			write_len = val_len;
		ret = _regmap_raw_write(map, reg, val, write_len, true);
		if (ret)
			goto out_unlock;
		val = ((u8 *)val) + write_len;
		val_len -= write_len;
	}

out_unlock:
	map->unlock(map->lock_arg);
	return ret;
}
EXPORT_SYMBOL_GPL(regmap_noinc_write);

/**
 * regmap_field_update_bits_base() - Perform a read/modify/write cycle a
 *                                   register field.
 *
 * @field: Register field to write to
 * @mask: Bitmask to change
 * @val: Value to be written
 * @change: Boolean indicating if a write was done
 * @async: Boolean indicating asynchronously
 * @force: Boolean indicating use force update
 *
 * Perform a read/modify/write cycle on the register field with change,
 * async, force option.
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_field_update_bits_base(struct regmap_field *field,
				  unsigned int mask, unsigned int val,
				  bool *change, bool async, bool force)
{
	mask = (mask << field->shift) & field->mask;

	return regmap_update_bits_base(field->regmap, field->reg,
				       mask, val << field->shift,
				       change, async, force);
}
EXPORT_SYMBOL_GPL(regmap_field_update_bits_base);

/**
 * regmap_field_test_bits() - Check if all specified bits are set in a
 *                            register field.
 *
 * @field: Register field to operate on
 * @bits: Bits to test
 *
 * Returns -1 if the underlying regmap_field_read() fails, 0 if at least one of the
 * tested bits is not set and 1 if all tested bits are set.
 */
int regmap_field_test_bits(struct regmap_field *field, unsigned int bits)
{
	unsigned int val, ret;

	ret = regmap_field_read(field, &val);
	if (ret)
		return ret;

	return (val & bits) == bits;
}
EXPORT_SYMBOL_GPL(regmap_field_test_bits);

/**
 * regmap_fields_update_bits_base() - Perform a read/modify/write cycle a
 *                                    register field with port ID
 *
 * @field: Register field to write to
 * @id: port ID
 * @mask: Bitmask to change
 * @val: Value to be written
 * @change: Boolean indicating if a write was done
 * @async: Boolean indicating asynchronously
 * @force: Boolean indicating use force update
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_fields_update_bits_base(struct regmap_field *field, unsigned int id,
				   unsigned int mask, unsigned int val,
				   bool *change, bool async, bool force)
{
	if (id >= field->id_size)
		return -EINVAL;

	mask = (mask << field->shift) & field->mask;

	return regmap_update_bits_base(field->regmap,
				       field->reg + (field->id_offset * id),
				       mask, val << field->shift,
				       change, async, force);
}
EXPORT_SYMBOL_GPL(regmap_fields_update_bits_base);

/**
 * regmap_bulk_write() - Write multiple registers to the device
 *
 * @map: Register map to write to
 * @reg: First register to be write from
 * @val: Block of data to be written, in native register size for device
 * @val_count: Number of registers to write
 *
 * This function is intended to be used for writing a large block of
 * data to the device either in single transfer or multiple transfer.
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_bulk_write(struct regmap *map, unsigned int reg, const void *val,
		     size_t val_count)
{
	int ret = 0, i;
	size_t val_bytes = map->format.val_bytes;

	if (!IS_ALIGNED(reg, map->reg_stride))
		return -EINVAL;

	/*
	 * Some devices don't support bulk write, for them we have a series of
	 * single write operations.
	 */
	if (!map->write || !map->format.parse_inplace) {
		map->lock(map->lock_arg);
		for (i = 0; i < val_count; i++) {
			unsigned int ival;

			switch (val_bytes) {
			case 1:
				ival = *(u8 *)(val + (i * val_bytes));
				break;
			case 2:
				ival = *(u16 *)(val + (i * val_bytes));
				break;
			case 4:
				ival = *(u32 *)(val + (i * val_bytes));
				break;
#ifdef CONFIG_64BIT
			case 8:
				ival = *(u64 *)(val + (i * val_bytes));
				break;
#endif
			default:
				ret = -EINVAL;
				goto out;
			}

			ret = _regmap_write(map,
					    reg + regmap_get_offset(map, i),
					    ival);
			if (ret != 0)
				goto out;
		}
out:
		map->unlock(map->lock_arg);
	} else {
		void *wval;

		wval = kmemdup(val, val_count * val_bytes, map->alloc_flags);
		if (!wval)
			return -ENOMEM;

		for (i = 0; i < val_count * val_bytes; i += val_bytes)
			map->format.parse_inplace(wval + i);

		ret = regmap_raw_write(map, reg, wval, val_bytes * val_count);

		kfree(wval);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(regmap_bulk_write);

/*
 * _regmap_raw_multi_reg_write()
 *
 * the (register,newvalue) pairs in regs have not been formatted, but
 * they are all in the same page and have been changed to being page
 * relative. The page register has been written if that was necessary.
 */
static int _regmap_raw_multi_reg_write(struct regmap *map,
				       const struct reg_sequence *regs,
				       size_t num_regs)
{
	int ret;
	void *buf;
	int i;
	u8 *u8;
	size_t val_bytes = map->format.val_bytes;
	size_t reg_bytes = map->format.reg_bytes;
	size_t pad_bytes = map->format.pad_bytes;
	size_t pair_size = reg_bytes + pad_bytes + val_bytes;
	size_t len = pair_size * num_regs;

	if (!len)
		return -EINVAL;

	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* We have to linearise by hand. */

	u8 = buf;

	for (i = 0; i < num_regs; i++) {
		unsigned int reg = regs[i].reg;
		unsigned int val = regs[i].def;
		trace_regmap_hw_write_start(map, reg, 1);
		reg += map->reg_base;
		reg >>= map->format.reg_downshift;
		map->format.format_reg(u8, reg, map->reg_shift);
		u8 += reg_bytes + pad_bytes;
		map->format.format_val(u8, val, 0);
		u8 += val_bytes;
	}
	u8 = buf;
	*u8 |= map->write_flag_mask;

	ret = map->write(map->bus_context, buf, len);

	kfree(buf);

	for (i = 0; i < num_regs; i++) {
		int reg = regs[i].reg;
		trace_regmap_hw_write_done(map, reg, 1);
	}
	return ret;
}

static unsigned int _regmap_register_page(struct regmap *map,
					  unsigned int reg,
					  struct regmap_range_node *range)
{
	unsigned int win_page = (reg - range->range_min) / range->window_len;

	return win_page;
}

static int _regmap_range_multi_paged_reg_write(struct regmap *map,
					       struct reg_sequence *regs,
					       size_t num_regs)
{
	int ret;
	int i, n;
	struct reg_sequence *base;
	unsigned int this_page = 0;
	unsigned int page_change = 0;
	/*
	 * the set of registers are not neccessarily in order, but
	 * since the order of write must be preserved this algorithm
	 * chops the set each time the page changes. This also applies
	 * if there is a delay required at any point in the sequence.
	 */
	base = regs;
	for (i = 0, n = 0; i < num_regs; i++, n++) {
		unsigned int reg = regs[i].reg;
		struct regmap_range_node *range;

		range = _regmap_range_lookup(map, reg);
		if (range) {
			unsigned int win_page = _regmap_register_page(map, reg,
								      range);

			if (i == 0)
				this_page = win_page;
			if (win_page != this_page) {
				this_page = win_page;
				page_change = 1;
			}
		}

		/* If we have both a page change and a delay make sure to
		 * write the regs and apply the delay before we change the
		 * page.
		 */

		if (page_change || regs[i].delay_us) {

				/* For situations where the first write requires
				 * a delay we need to make sure we don't call
				 * raw_multi_reg_write with n=0
				 * This can't occur with page breaks as we
				 * never write on the first iteration
				 */
				if (regs[i].delay_us && i == 0)
					n = 1;

				ret = _regmap_raw_multi_reg_write(map, base, n);
				if (ret != 0)
					return ret;

				if (regs[i].delay_us) {
					if (map->can_sleep)
						fsleep(regs[i].delay_us);
					else
						udelay(regs[i].delay_us);
				}

				base += n;
				n = 0;

				if (page_change) {
					ret = _regmap_select_page(map,
								  &base[n].reg,
								  range, 1);
					if (ret != 0)
						return ret;

					page_change = 0;
				}

		}

	}
	if (n > 0)
		return _regmap_raw_multi_reg_write(map, base, n);
	return 0;
}

static int _regmap_multi_reg_write(struct regmap *map,
				   const struct reg_sequence *regs,
				   size_t num_regs)
{
	int i;
	int ret;

	if (!map->can_multi_write) {
		for (i = 0; i < num_regs; i++) {
			ret = _regmap_write(map, regs[i].reg, regs[i].def);
			if (ret != 0)
				return ret;

			if (regs[i].delay_us) {
				if (map->can_sleep)
					fsleep(regs[i].delay_us);
				else
					udelay(regs[i].delay_us);
			}
		}
		return 0;
	}

	if (!map->format.parse_inplace)
		return -EINVAL;

	if (map->writeable_reg)
		for (i = 0; i < num_regs; i++) {
			int reg = regs[i].reg;
			if (!map->writeable_reg(map->dev, reg))
				return -EINVAL;
			if (!IS_ALIGNED(reg, map->reg_stride))
				return -EINVAL;
		}

	if (!map->cache_bypass) {
		for (i = 0; i < num_regs; i++) {
			unsigned int val = regs[i].def;
			unsigned int reg = regs[i].reg;
			ret = regcache_write(map, reg, val);
			if (ret) {
				dev_err(map->dev,
				"Error in caching of register: %x ret: %d\n",
								reg, ret);
				return ret;
			}
		}
		if (map->cache_only) {
			map->cache_dirty = true;
			return 0;
		}
	}

	WARN_ON(!map->bus);

	for (i = 0; i < num_regs; i++) {
		unsigned int reg = regs[i].reg;
		struct regmap_range_node *range;

		/* Coalesce all the writes between a page break or a delay
		 * in a sequence
		 */
		range = _regmap_range_lookup(map, reg);
		if (range || regs[i].delay_us) {
			size_t len = sizeof(struct reg_sequence)*num_regs;
			struct reg_sequence *base = kmemdup(regs, len,
							   GFP_KERNEL);
			if (!base)
				return -ENOMEM;
			ret = _regmap_range_multi_paged_reg_write(map, base,
								  num_regs);
			kfree(base);

			return ret;
		}
	}
	return _regmap_raw_multi_reg_write(map, regs, num_regs);
}

/**
 * regmap_multi_reg_write() - Write multiple registers to the device
 *
 * @map: Register map to write to
 * @regs: Array of structures containing register,value to be written
 * @num_regs: Number of registers to write
 *
 * Write multiple registers to the device where the set of register, value
 * pairs are supplied in any order, possibly not all in a single range.
 *
 * The 'normal' block write mode will send ultimately send data on the
 * target bus as R,V1,V2,V3,..,Vn where successively higher registers are
 * addressed. However, this alternative block multi write mode will send
 * the data as R1,V1,R2,V2,..,Rn,Vn on the target bus. The target device
 * must of course support the mode.
 *
 * A value of zero will be returned on success, a negative errno will be
 * returned in error cases.
 */
int regmap_multi_reg_write(struct regmap *map, const struct reg_sequence *regs,
			   int num_regs)
{
	int ret;

	map->lock(map->lock_arg);

	ret = _regmap_multi_reg_write(map, regs, num_regs);

	map->unlock(map->lock_arg);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_multi_reg_write);

/**
 * regmap_multi_reg_write_bypassed() - Write multiple registers to the
 *                                     device but not the cache
 *
 * @map: Register map to write to
 * @regs: Array of structures containing register,value to be written
 * @num_regs: Number of registers to write
 *
 * Write multiple registers to the device but not the cache where the set
 * of register are supplied in any order.
 *
 * This function is intended to be used for writing a large block of data
 * atomically to the device in single transfer for those I2C client devices
 * that implement this alternative block write mode.
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_multi_reg_write_bypassed(struct regmap *map,
				    const struct reg_sequence *regs,
				    int num_regs)
{
	int ret;
	bool bypass;

	map->lock(map->lock_arg);

	bypass = map->cache_bypass;
	map->cache_bypass = true;

	ret = _regmap_multi_reg_write(map, regs, num_regs);

	map->cache_bypass = bypass;

	map->unlock(map->lock_arg);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_multi_reg_write_bypassed);

/**
 * regmap_raw_write_async() - Write raw values to one or more registers
 *                            asynchronously
 *
 * @map: Register map to write to
 * @reg: Initial register to write to
 * @val: Block of data to be written, laid out for direct transmission to the
 *       device.  Must be valid until regmap_async_complete() is called.
 * @val_len: Length of data pointed to by val.
 *
 * This function is intended to be used for things like firmware
 * download where a large block of data needs to be transferred to the
 * device.  No formatting will be done on the data provided.
 *
 * If supported by the underlying bus the write will be scheduled
 * asynchronously, helping maximise I/O speed on higher speed buses
 * like SPI.  regmap_async_complete() can be called to ensure that all
 * asynchrnous writes have been completed.
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_raw_write_async(struct regmap *map, unsigned int reg,
			   const void *val, size_t val_len)
{
	int ret;

	if (val_len % map->format.val_bytes)
		return -EINVAL;
	if (!IS_ALIGNED(reg, map->reg_stride))
		return -EINVAL;

	map->lock(map->lock_arg);

	map->async = true;

	ret = _regmap_raw_write(map, reg, val, val_len, false);

	map->async = false;

	map->unlock(map->lock_arg);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_raw_write_async);

static int _regmap_raw_read(struct regmap *map, unsigned int reg, void *val,
			    unsigned int val_len, bool noinc)
{
	struct regmap_range_node *range;
	int ret;

	if (!map->read)
		return -EINVAL;

	range = _regmap_range_lookup(map, reg);
	if (range) {
		ret = _regmap_select_page(map, &reg, range,
					  noinc ? 1 : val_len / map->format.val_bytes);
		if (ret != 0)
			return ret;
	}

	reg += map->reg_base;
	reg >>= map->format.reg_downshift;
	map->format.format_reg(map->work_buf, reg, map->reg_shift);
	regmap_set_work_buf_flag_mask(map, map->format.reg_bytes,
				      map->read_flag_mask);
	trace_regmap_hw_read_start(map, reg, val_len / map->format.val_bytes);

	ret = map->read(map->bus_context, map->work_buf,
			map->format.reg_bytes + map->format.pad_bytes,
			val, val_len);

	trace_regmap_hw_read_done(map, reg, val_len / map->format.val_bytes);

	return ret;
}

static int _regmap_bus_reg_read(void *context, unsigned int reg,
				unsigned int *val)
{
	struct regmap *map = context;

	return map->bus->reg_read(map->bus_context, reg, val);
}

static int _regmap_bus_read(void *context, unsigned int reg,
			    unsigned int *val)
{
	int ret;
	struct regmap *map = context;
	void *work_val = map->work_buf + map->format.reg_bytes +
		map->format.pad_bytes;

	if (!map->format.parse_val)
		return -EINVAL;

	ret = _regmap_raw_read(map, reg, work_val, map->format.val_bytes, false);
	if (ret == 0)
		*val = map->format.parse_val(work_val);

	return ret;
}

static int _regmap_read(struct regmap *map, unsigned int reg,
			unsigned int *val)
{
	int ret;
	void *context = _regmap_map_get_context(map);

	if (!map->cache_bypass) {
		ret = regcache_read(map, reg, val);
		if (ret == 0)
			return 0;
	}

	if (map->cache_only)
		return -EBUSY;

	if (!regmap_readable(map, reg))
		return -EIO;

	ret = map->reg_read(context, reg, val);
	if (ret == 0) {
		if (regmap_should_log(map))
			dev_info(map->dev, "%x => %x\n", reg, *val);

		trace_regmap_reg_read(map, reg, *val);

		if (!map->cache_bypass)
			regcache_write(map, reg, *val);
	}

	return ret;
}

/**
 * regmap_read() - Read a value from a single register
 *
 * @map: Register map to read from
 * @reg: Register to be read from
 * @val: Pointer to store read value
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
	int ret;

	if (!IS_ALIGNED(reg, map->reg_stride))
		return -EINVAL;

	map->lock(map->lock_arg);

	ret = _regmap_read(map, reg, val);

	map->unlock(map->lock_arg);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_read);

/**
 * regmap_raw_read() - Read raw data from the device
 *
 * @map: Register map to read from
 * @reg: First register to be read from
 * @val: Pointer to store read value
 * @val_len: Size of data to read
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_raw_read(struct regmap *map, unsigned int reg, void *val,
		    size_t val_len)
{
	size_t val_bytes = map->format.val_bytes;
	size_t val_count = val_len / val_bytes;
	unsigned int v;
	int ret, i;

	if (val_len % map->format.val_bytes)
		return -EINVAL;
	if (!IS_ALIGNED(reg, map->reg_stride))
		return -EINVAL;
	if (val_count == 0)
		return -EINVAL;

	map->lock(map->lock_arg);

	if (regmap_volatile_range(map, reg, val_count) || map->cache_bypass ||
	    map->cache_type == REGCACHE_NONE) {
		size_t chunk_count, chunk_bytes;
		size_t chunk_regs = val_count;

		if (!map->read) {
			ret = -ENOTSUPP;
			goto out;
		}

		if (map->use_single_read)
			chunk_regs = 1;
		else if (map->max_raw_read && val_len > map->max_raw_read)
			chunk_regs = map->max_raw_read / val_bytes;

		chunk_count = val_count / chunk_regs;
		chunk_bytes = chunk_regs * val_bytes;

		/* Read bytes that fit into whole chunks */
		for (i = 0; i < chunk_count; i++) {
			ret = _regmap_raw_read(map, reg, val, chunk_bytes, false);
			if (ret != 0)
				goto out;

			reg += regmap_get_offset(map, chunk_regs);
			val += chunk_bytes;
			val_len -= chunk_bytes;
		}

		/* Read remaining bytes */
		if (val_len) {
			ret = _regmap_raw_read(map, reg, val, val_len, false);
			if (ret != 0)
				goto out;
		}
	} else {
		/* Otherwise go word by word for the cache; should be low
		 * cost as we expect to hit the cache.
		 */
		for (i = 0; i < val_count; i++) {
			ret = _regmap_read(map, reg + regmap_get_offset(map, i),
					   &v);
			if (ret != 0)
				goto out;

			map->format.format_val(val + (i * val_bytes), v, 0);
		}
	}

 out:
	map->unlock(map->lock_arg);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_raw_read);

/**
 * regmap_noinc_read(): Read data from a register without incrementing the
 *			register number
 *
 * @map: Register map to read from
 * @reg: Register to read from
 * @val: Pointer to data buffer
 * @val_len: Length of output buffer in bytes.
 *
 * The regmap API usually assumes that bulk read operations will read a
 * range of registers. Some devices have certain registers for which a read
 * operation read will read from an internal FIFO.
 *
 * The target register must be volatile but registers after it can be
 * completely unrelated cacheable registers.
 *
 * This will attempt multiple reads as required to read val_len bytes.
 *
 * A value of zero will be returned on success, a negative errno will be
 * returned in error cases.
 */
int regmap_noinc_read(struct regmap *map, unsigned int reg,
		      void *val, size_t val_len)
{
	size_t read_len;
	int ret;

	if (!map->read)
		return -ENOTSUPP;

	if (val_len % map->format.val_bytes)
		return -EINVAL;
	if (!IS_ALIGNED(reg, map->reg_stride))
		return -EINVAL;
	if (val_len == 0)
		return -EINVAL;

	map->lock(map->lock_arg);

	if (!regmap_volatile(map, reg) || !regmap_readable_noinc(map, reg)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	while (val_len) {
		if (map->max_raw_read && map->max_raw_read < val_len)
			read_len = map->max_raw_read;
		else
			read_len = val_len;
		ret = _regmap_raw_read(map, reg, val, read_len, true);
		if (ret)
			goto out_unlock;
		val = ((u8 *)val) + read_len;
		val_len -= read_len;
	}

out_unlock:
	map->unlock(map->lock_arg);
	return ret;
}
EXPORT_SYMBOL_GPL(regmap_noinc_read);

/**
 * regmap_field_read(): Read a value to a single register field
 *
 * @field: Register field to read from
 * @val: Pointer to store read value
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_field_read(struct regmap_field *field, unsigned int *val)
{
	int ret;
	unsigned int reg_val;
	ret = regmap_read(field->regmap, field->reg, &reg_val);
	if (ret != 0)
		return ret;

	reg_val &= field->mask;
	reg_val >>= field->shift;
	*val = reg_val;

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_field_read);

/**
 * regmap_fields_read() - Read a value to a single register field with port ID
 *
 * @field: Register field to read from
 * @id: port ID
 * @val: Pointer to store read value
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_fields_read(struct regmap_field *field, unsigned int id,
		       unsigned int *val)
{
	int ret;
	unsigned int reg_val;

	if (id >= field->id_size)
		return -EINVAL;

	ret = regmap_read(field->regmap,
			  field->reg + (field->id_offset * id),
			  &reg_val);
	if (ret != 0)
		return ret;

	reg_val &= field->mask;
	reg_val >>= field->shift;
	*val = reg_val;

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_fields_read);

/**
 * regmap_bulk_read() - Read multiple registers from the device
 *
 * @map: Register map to read from
 * @reg: First register to be read from
 * @val: Pointer to store read value, in native register size for device
 * @val_count: Number of registers to read
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_bulk_read(struct regmap *map, unsigned int reg, void *val,
		     size_t val_count)
{
	int ret, i;
	size_t val_bytes = map->format.val_bytes;
	bool vol = regmap_volatile_range(map, reg, val_count);

	if (!IS_ALIGNED(reg, map->reg_stride))
		return -EINVAL;
	if (val_count == 0)
		return -EINVAL;

	if (map->read && map->format.parse_inplace && (vol || map->cache_type == REGCACHE_NONE)) {
		ret = regmap_raw_read(map, reg, val, val_bytes * val_count);
		if (ret != 0)
			return ret;

		for (i = 0; i < val_count * val_bytes; i += val_bytes)
			map->format.parse_inplace(val + i);
	} else {
#ifdef CONFIG_64BIT
		u64 *u64 = val;
#endif
		u32 *u32 = val;
		u16 *u16 = val;
		u8 *u8 = val;

		map->lock(map->lock_arg);

		for (i = 0; i < val_count; i++) {
			unsigned int ival;

			ret = _regmap_read(map, reg + regmap_get_offset(map, i),
					   &ival);
			if (ret != 0)
				goto out;

			switch (map->format.val_bytes) {
#ifdef CONFIG_64BIT
			case 8:
				u64[i] = ival;
				break;
#endif
			case 4:
				u32[i] = ival;
				break;
			case 2:
				u16[i] = ival;
				break;
			case 1:
				u8[i] = ival;
				break;
			default:
				ret = -EINVAL;
				goto out;
			}
		}

out:
		map->unlock(map->lock_arg);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_bulk_read);

static int _regmap_update_bits(struct regmap *map, unsigned int reg,
			       unsigned int mask, unsigned int val,
			       bool *change, bool force_write)
{
	int ret;
	unsigned int tmp, orig;

	if (change)
		*change = false;

	if (regmap_volatile(map, reg) && map->reg_update_bits) {
		ret = map->reg_update_bits(map->bus_context, reg, mask, val);
		if (ret == 0 && change)
			*change = true;
	} else {
		ret = _regmap_read(map, reg, &orig);
		if (ret != 0)
			return ret;

		tmp = orig & ~mask;
		tmp |= val & mask;

		if (force_write || (tmp != orig)) {
			ret = _regmap_write(map, reg, tmp);
			if (ret == 0 && change)
				*change = true;
		}
	}

	return ret;
}

/**
 * regmap_update_bits_base() - Perform a read/modify/write cycle on a register
 *
 * @map: Register map to update
 * @reg: Register to update
 * @mask: Bitmask to change
 * @val: New value for bitmask
 * @change: Boolean indicating if a write was done
 * @async: Boolean indicating asynchronously
 * @force: Boolean indicating use force update
 *
 * Perform a read/modify/write cycle on a register map with change, async, force
 * options.
 *
 * If async is true:
 *
 * With most buses the read must be done synchronously so this is most useful
 * for devices with a cache which do not need to interact with the hardware to
 * determine the current register value.
 *
 * Returns zero for success, a negative number on error.
 */
int regmap_update_bits_base(struct regmap *map, unsigned int reg,
			    unsigned int mask, unsigned int val,
			    bool *change, bool async, bool force)
{
	int ret;

	map->lock(map->lock_arg);

	map->async = async;

	ret = _regmap_update_bits(map, reg, mask, val, change, force);

	map->async = false;

	map->unlock(map->lock_arg);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_update_bits_base);

/**
 * regmap_test_bits() - Check if all specified bits are set in a register.
 *
 * @map: Register map to operate on
 * @reg: Register to read from
 * @bits: Bits to test
 *
 * Returns 0 if at least one of the tested bits is not set, 1 if all tested
 * bits are set and a negative error number if the underlying regmap_read()
 * fails.
 */
int regmap_test_bits(struct regmap *map, unsigned int reg, unsigned int bits)
{
	unsigned int val, ret;

	ret = regmap_read(map, reg, &val);
	if (ret)
		return ret;

	return (val & bits) == bits;
}
EXPORT_SYMBOL_GPL(regmap_test_bits);

void regmap_async_complete_cb(struct regmap_async *async, int ret)
{
	struct regmap *map = async->map;
	bool wake;

	trace_regmap_async_io_complete(map);

	spin_lock(&map->async_lock);
	list_move(&async->list, &map->async_free);
	wake = list_empty(&map->async_list);

	if (ret != 0)
		map->async_ret = ret;

	spin_unlock(&map->async_lock);

	if (wake)
		wake_up(&map->async_waitq);
}
EXPORT_SYMBOL_GPL(regmap_async_complete_cb);

static int regmap_async_is_done(struct regmap *map)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&map->async_lock, flags);
	ret = list_empty(&map->async_list);
	spin_unlock_irqrestore(&map->async_lock, flags);

	return ret;
}

/**
 * regmap_async_complete - Ensure all asynchronous I/O has completed.
 *
 * @map: Map to operate on.
 *
 * Blocks until any pending asynchronous I/O has completed.  Returns
 * an error code for any failed I/O operations.
 */
int regmap_async_complete(struct regmap *map)
{
	unsigned long flags;
	int ret;

	/* Nothing to do with no async support */
	if (!map->bus || !map->bus->async_write)
		return 0;

	trace_regmap_async_complete_start(map);

	wait_event(map->async_waitq, regmap_async_is_done(map));

	spin_lock_irqsave(&map->async_lock, flags);
	ret = map->async_ret;
	map->async_ret = 0;
	spin_unlock_irqrestore(&map->async_lock, flags);

	trace_regmap_async_complete_done(map);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_async_complete);

/**
 * regmap_register_patch - Register and apply register updates to be applied
 *                         on device initialistion
 *
 * @map: Register map to apply updates to.
 * @regs: Values to update.
 * @num_regs: Number of entries in regs.
 *
 * Register a set of register updates to be applied to the device
 * whenever the device registers are synchronised with the cache and
 * apply them immediately.  Typically this is used to apply
 * corrections to be applied to the device defaults on startup, such
 * as the updates some vendors provide to undocumented registers.
 *
 * The caller must ensure that this function cannot be called
 * concurrently with either itself or regcache_sync().
 */
int regmap_register_patch(struct regmap *map, const struct reg_sequence *regs,
			  int num_regs)
{
	struct reg_sequence *p;
	int ret;
	bool bypass;

	if (WARN_ONCE(num_regs <= 0, "invalid registers number (%d)\n",
	    num_regs))
		return 0;

	p = krealloc(map->patch,
		     sizeof(struct reg_sequence) * (map->patch_regs + num_regs),
		     GFP_KERNEL);
	if (p) {
		memcpy(p + map->patch_regs, regs, num_regs * sizeof(*regs));
		map->patch = p;
		map->patch_regs += num_regs;
	} else {
		return -ENOMEM;
	}

	map->lock(map->lock_arg);

	bypass = map->cache_bypass;

	map->cache_bypass = true;
	map->async = true;

	ret = _regmap_multi_reg_write(map, regs, num_regs);

	map->async = false;
	map->cache_bypass = bypass;

	map->unlock(map->lock_arg);

	regmap_async_complete(map);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_register_patch);

/**
 * regmap_get_val_bytes() - Report the size of a register value
 *
 * @map: Register map to operate on.
 *
 * Report the size of a register value, mainly intended to for use by
 * generic infrastructure built on top of regmap.
 */
int regmap_get_val_bytes(struct regmap *map)
{
	if (map->format.format_write)
		return -EINVAL;

	return map->format.val_bytes;
}
EXPORT_SYMBOL_GPL(regmap_get_val_bytes);

/**
 * regmap_get_max_register() - Report the max register value
 *
 * @map: Register map to operate on.
 *
 * Report the max register value, mainly intended to for use by
 * generic infrastructure built on top of regmap.
 */
int regmap_get_max_register(struct regmap *map)
{
	return map->max_register ? map->max_register : -EINVAL;
}
EXPORT_SYMBOL_GPL(regmap_get_max_register);

/**
 * regmap_get_reg_stride() - Report the register address stride
 *
 * @map: Register map to operate on.
 *
 * Report the register address stride, mainly intended to for use by
 * generic infrastructure built on top of regmap.
 */
int regmap_get_reg_stride(struct regmap *map)
{
	return map->reg_stride;
}
EXPORT_SYMBOL_GPL(regmap_get_reg_stride);

int regmap_parse_val(struct regmap *map, const void *buf,
			unsigned int *val)
{
	if (!map->format.parse_val)
		return -EINVAL;

	*val = map->format.parse_val(buf);

	return 0;
}
EXPORT_SYMBOL_GPL(regmap_parse_val);

static int __init regmap_initcall(void)
{
	regmap_debugfs_initcall();

	return 0;
}
postcore_initcall(regmap_initcall);
