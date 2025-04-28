// SPDX-License-Identifier: GPL-2.0
//
// Register cache access API
//
// Copyright 2011 Wolfson Microelectronics plc
//
// Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>

#include <linux/bsearch.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/sort.h>

#include "trace.h"
#include "internal.h"

static const struct regcache_ops *cache_types[] = {
	&regcache_rbtree_ops,
	&regcache_maple_ops,
	&regcache_flat_ops,
};

static int regcache_defaults_cmp(const void *a, const void *b)
{
	const struct reg_default *x = a;
	const struct reg_default *y = b;

	if (x->reg > y->reg)
		return 1;
	else if (x->reg < y->reg)
		return -1;
	else
		return 0;
}

void regcache_sort_defaults(struct reg_default *defaults, unsigned int ndefaults)
{
	sort(defaults, ndefaults, sizeof(*defaults),
	     regcache_defaults_cmp, NULL);
}
EXPORT_SYMBOL_GPL(regcache_sort_defaults);

static int regcache_hw_init(struct regmap *map)
{
	int i, j;
	int ret;
	int count;
	unsigned int reg, val;
	void *tmp_buf;

	if (!map->num_reg_defaults_raw)
		return -EINVAL;

	/* calculate the size of reg_defaults */
	for (count = 0, i = 0; i < map->num_reg_defaults_raw; i++)
		if (regmap_readable(map, i * map->reg_stride) &&
		    !regmap_volatile(map, i * map->reg_stride))
			count++;

	/* all registers are unreadable or volatile, so just bypass */
	if (!count) {
		map->cache_bypass = true;
		return 0;
	}

	map->num_reg_defaults = count;
	map->reg_defaults = kmalloc_array(count, sizeof(struct reg_default),
					  GFP_KERNEL);
	if (!map->reg_defaults)
		return -ENOMEM;

	if (!map->reg_defaults_raw) {
		bool cache_bypass = map->cache_bypass;
		dev_warn(map->dev, "No cache defaults, reading back from HW\n");

		/* Bypass the cache access till data read from HW */
		map->cache_bypass = true;
		tmp_buf = kmalloc(map->cache_size_raw, GFP_KERNEL);
		if (!tmp_buf) {
			ret = -ENOMEM;
			goto err_free;
		}
		ret = regmap_raw_read(map, 0, tmp_buf,
				      map->cache_size_raw);
		map->cache_bypass = cache_bypass;
		if (ret == 0) {
			map->reg_defaults_raw = tmp_buf;
			map->cache_free = true;
		} else {
			kfree(tmp_buf);
		}
	}

	/* fill the reg_defaults */
	for (i = 0, j = 0; i < map->num_reg_defaults_raw; i++) {
		reg = i * map->reg_stride;

		if (!regmap_readable(map, reg))
			continue;

		if (regmap_volatile(map, reg))
			continue;

		if (map->reg_defaults_raw) {
			val = regcache_get_val(map, map->reg_defaults_raw, i);
		} else {
			bool cache_bypass = map->cache_bypass;

			map->cache_bypass = true;
			ret = regmap_read(map, reg, &val);
			map->cache_bypass = cache_bypass;
			if (ret != 0) {
				dev_err(map->dev, "Failed to read %d: %d\n",
					reg, ret);
				goto err_free;
			}
		}

		map->reg_defaults[j].reg = reg;
		map->reg_defaults[j].def = val;
		j++;
	}

	return 0;

err_free:
	kfree(map->reg_defaults);

	return ret;
}

int regcache_init(struct regmap *map, const struct regmap_config *config)
{
	int ret;
	int i;
	void *tmp_buf;

	if (map->cache_type == REGCACHE_NONE) {
		if (config->reg_defaults || config->num_reg_defaults_raw)
			dev_warn(map->dev,
				 "No cache used with register defaults set!\n");

		map->cache_bypass = true;
		return 0;
	}

	if (config->reg_defaults && !config->num_reg_defaults) {
		dev_err(map->dev,
			 "Register defaults are set without the number!\n");
		return -EINVAL;
	}

	if (config->num_reg_defaults && !config->reg_defaults) {
		dev_err(map->dev,
			"Register defaults number are set without the reg!\n");
		return -EINVAL;
	}

	for (i = 0; i < config->num_reg_defaults; i++)
		if (config->reg_defaults[i].reg % map->reg_stride)
			return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cache_types); i++)
		if (cache_types[i]->type == map->cache_type)
			break;

	if (i == ARRAY_SIZE(cache_types)) {
		dev_err(map->dev, "Could not match cache type: %d\n",
			map->cache_type);
		return -EINVAL;
	}

	map->num_reg_defaults = config->num_reg_defaults;
	map->num_reg_defaults_raw = config->num_reg_defaults_raw;
	map->reg_defaults_raw = config->reg_defaults_raw;
	map->cache_word_size = BITS_TO_BYTES(config->val_bits);
	map->cache_size_raw = map->cache_word_size * config->num_reg_defaults_raw;

	map->cache = NULL;
	map->cache_ops = cache_types[i];

	if (!map->cache_ops->read ||
	    !map->cache_ops->write ||
	    !map->cache_ops->name)
		return -EINVAL;

	/* We still need to ensure that the reg_defaults
	 * won't vanish from under us.  We'll need to make
	 * a copy of it.
	 */
	if (config->reg_defaults) {
		tmp_buf = kmemdup_array(config->reg_defaults, map->num_reg_defaults,
					sizeof(*map->reg_defaults), GFP_KERNEL);
		if (!tmp_buf)
			return -ENOMEM;
		map->reg_defaults = tmp_buf;
	} else if (map->num_reg_defaults_raw) {
		/* Some devices such as PMICs don't have cache defaults,
		 * we cope with this by reading back the HW registers and
		 * crafting the cache defaults by hand.
		 */
		ret = regcache_hw_init(map);
		if (ret < 0)
			return ret;
		if (map->cache_bypass)
			return 0;
	}

	if (!map->max_register_is_set && map->num_reg_defaults_raw) {
		map->max_register = (map->num_reg_defaults_raw  - 1) * map->reg_stride;
		map->max_register_is_set = true;
	}

	if (map->cache_ops->init) {
		dev_dbg(map->dev, "Initializing %s cache\n",
			map->cache_ops->name);
		map->lock(map->lock_arg);
		ret = map->cache_ops->init(map);
		map->unlock(map->lock_arg);
		if (ret)
			goto err_free;
	}
	return 0;

err_free:
	kfree(map->reg_defaults);
	if (map->cache_free)
		kfree(map->reg_defaults_raw);

	return ret;
}

void regcache_exit(struct regmap *map)
{
	if (map->cache_type == REGCACHE_NONE)
		return;

	BUG_ON(!map->cache_ops);

	kfree(map->reg_defaults);
	if (map->cache_free)
		kfree(map->reg_defaults_raw);

	if (map->cache_ops->exit) {
		dev_dbg(map->dev, "Destroying %s cache\n",
			map->cache_ops->name);
		map->lock(map->lock_arg);
		map->cache_ops->exit(map);
		map->unlock(map->lock_arg);
	}
}

/**
 * regcache_read - Fetch the value of a given register from the cache.
 *
 * @map: map to configure.
 * @reg: The register index.
 * @value: The value to be returned.
 *
 * Return a negative value on failure, 0 on success.
 */
int regcache_read(struct regmap *map,
		  unsigned int reg, unsigned int *value)
{
	int ret;

	if (map->cache_type == REGCACHE_NONE)
		return -EINVAL;

	BUG_ON(!map->cache_ops);

	if (!regmap_volatile(map, reg)) {
		ret = map->cache_ops->read(map, reg, value);

		if (ret == 0)
			trace_regmap_reg_read_cache(map, reg, *value);

		return ret;
	}

	return -EINVAL;
}

/**
 * regcache_write - Set the value of a given register in the cache.
 *
 * @map: map to configure.
 * @reg: The register index.
 * @value: The new register value.
 *
 * Return a negative value on failure, 0 on success.
 */
int regcache_write(struct regmap *map,
		   unsigned int reg, unsigned int value)
{
	if (map->cache_type == REGCACHE_NONE)
		return 0;

	BUG_ON(!map->cache_ops);

	if (!regmap_volatile(map, reg))
		return map->cache_ops->write(map, reg, value);

	return 0;
}

bool regcache_reg_needs_sync(struct regmap *map, unsigned int reg,
			     unsigned int val)
{
	int ret;

	if (!regmap_writeable(map, reg))
		return false;

	/* If we don't know the chip just got reset, then sync everything. */
	if (!map->no_sync_defaults)
		return true;

	/* Is this the hardware default?  If so skip. */
	ret = regcache_lookup_reg(map, reg);
	if (ret >= 0 && val == map->reg_defaults[ret].def)
		return false;
	return true;
}

static int regcache_default_sync(struct regmap *map, unsigned int min,
				 unsigned int max)
{
	unsigned int reg;

	for (reg = min; reg <= max; reg += map->reg_stride) {
		unsigned int val;
		int ret;

		if (regmap_volatile(map, reg) ||
		    !regmap_writeable(map, reg))
			continue;

		ret = regcache_read(map, reg, &val);
		if (ret == -ENOENT)
			continue;
		if (ret)
			return ret;

		if (!regcache_reg_needs_sync(map, reg, val))
			continue;

		map->cache_bypass = true;
		ret = _regmap_write(map, reg, val);
		map->cache_bypass = false;
		if (ret) {
			dev_err(map->dev, "Unable to sync register %#x. %d\n",
				reg, ret);
			return ret;
		}
		dev_dbg(map->dev, "Synced register %#x, value %#x\n", reg, val);
	}

	return 0;
}

static int rbtree_all(const void *key, const struct rb_node *node)
{
	return 0;
}

/**
 * regcache_sync - Sync the register cache with the hardware.
 *
 * @map: map to configure.
 *
 * Any registers that should not be synced should be marked as
 * volatile.  In general drivers can choose not to use the provided
 * syncing functionality if they so require.
 *
 * Return a negative value on failure, 0 on success.
 */
int regcache_sync(struct regmap *map)
{
	int ret = 0;
	unsigned int i;
	const char *name;
	bool bypass;
	struct rb_node *node;

	if (WARN_ON(map->cache_type == REGCACHE_NONE))
		return -EINVAL;

	BUG_ON(!map->cache_ops);

	map->lock(map->lock_arg);
	/* Remember the initial bypass state */
	bypass = map->cache_bypass;
	dev_dbg(map->dev, "Syncing %s cache\n",
		map->cache_ops->name);
	name = map->cache_ops->name;
	trace_regcache_sync(map, name, "start");

	if (!map->cache_dirty)
		goto out;

	/* Apply any patch first */
	map->cache_bypass = true;
	for (i = 0; i < map->patch_regs; i++) {
		ret = _regmap_write(map, map->patch[i].reg, map->patch[i].def);
		if (ret != 0) {
			dev_err(map->dev, "Failed to write %x = %x: %d\n",
				map->patch[i].reg, map->patch[i].def, ret);
			goto out;
		}
	}
	map->cache_bypass = false;

	if (map->cache_ops->sync)
		ret = map->cache_ops->sync(map, 0, map->max_register);
	else
		ret = regcache_default_sync(map, 0, map->max_register);

	if (ret == 0)
		map->cache_dirty = false;

out:
	/* Restore the bypass state */
	map->cache_bypass = bypass;
	map->no_sync_defaults = false;

	/*
	 * If we did any paging with cache bypassed and a cached
	 * paging register then the register and cache state might
	 * have gone out of sync, force writes of all the paging
	 * registers.
	 */
	rb_for_each(node, NULL, &map->range_tree, rbtree_all) {
		struct regmap_range_node *this =
			rb_entry(node, struct regmap_range_node, node);

		/* If there's nothing in the cache there's nothing to sync */
		if (regcache_read(map, this->selector_reg, &i) != 0)
			continue;

		ret = _regmap_write(map, this->selector_reg, i);
		if (ret != 0) {
			dev_err(map->dev, "Failed to write %x = %x: %d\n",
				this->selector_reg, i, ret);
			break;
		}
	}

	map->unlock(map->lock_arg);

	regmap_async_complete(map);

	trace_regcache_sync(map, name, "stop");

	return ret;
}
EXPORT_SYMBOL_GPL(regcache_sync);

/**
 * regcache_sync_region - Sync part  of the register cache with the hardware.
 *
 * @map: map to sync.
 * @min: first register to sync
 * @max: last register to sync
 *
 * Write all non-default register values in the specified region to
 * the hardware.
 *
 * Return a negative value on failure, 0 on success.
 */
int regcache_sync_region(struct regmap *map, unsigned int min,
			 unsigned int max)
{
	int ret = 0;
	const char *name;
	bool bypass;

	if (WARN_ON(map->cache_type == REGCACHE_NONE))
		return -EINVAL;

	BUG_ON(!map->cache_ops);

	map->lock(map->lock_arg);

	/* Remember the initial bypass state */
	bypass = map->cache_bypass;

	name = map->cache_ops->name;
	dev_dbg(map->dev, "Syncing %s cache from %d-%d\n", name, min, max);

	trace_regcache_sync(map, name, "start region");

	if (!map->cache_dirty)
		goto out;

	map->async = true;

	if (map->cache_ops->sync)
		ret = map->cache_ops->sync(map, min, max);
	else
		ret = regcache_default_sync(map, min, max);

out:
	/* Restore the bypass state */
	map->cache_bypass = bypass;
	map->async = false;
	map->no_sync_defaults = false;
	map->unlock(map->lock_arg);

	regmap_async_complete(map);

	trace_regcache_sync(map, name, "stop region");

	return ret;
}
EXPORT_SYMBOL_GPL(regcache_sync_region);

/**
 * regcache_drop_region - Discard part of the register cache
 *
 * @map: map to operate on
 * @min: first register to discard
 * @max: last register to discard
 *
 * Discard part of the register cache.
 *
 * Return a negative value on failure, 0 on success.
 */
int regcache_drop_region(struct regmap *map, unsigned int min,
			 unsigned int max)
{
	int ret = 0;

	if (!map->cache_ops || !map->cache_ops->drop)
		return -EINVAL;

	map->lock(map->lock_arg);

	trace_regcache_drop_region(map, min, max);

	ret = map->cache_ops->drop(map, min, max);

	map->unlock(map->lock_arg);

	return ret;
}
EXPORT_SYMBOL_GPL(regcache_drop_region);

/**
 * regcache_cache_only - Put a register map into cache only mode
 *
 * @map: map to configure
 * @enable: flag if changes should be written to the hardware
 *
 * When a register map is marked as cache only writes to the register
 * map API will only update the register cache, they will not cause
 * any hardware changes.  This is useful for allowing portions of
 * drivers to act as though the device were functioning as normal when
 * it is disabled for power saving reasons.
 */
void regcache_cache_only(struct regmap *map, bool enable)
{
	map->lock(map->lock_arg);
	WARN_ON(map->cache_type != REGCACHE_NONE &&
		map->cache_bypass && enable);
	map->cache_only = enable;
	trace_regmap_cache_only(map, enable);
	map->unlock(map->lock_arg);
}
EXPORT_SYMBOL_GPL(regcache_cache_only);

/**
 * regcache_mark_dirty - Indicate that HW registers were reset to default values
 *
 * @map: map to mark
 *
 * Inform regcache that the device has been powered down or reset, so that
 * on resume, regcache_sync() knows to write out all non-default values
 * stored in the cache.
 *
 * If this function is not called, regcache_sync() will assume that
 * the hardware state still matches the cache state, modulo any writes that
 * happened when cache_only was true.
 */
void regcache_mark_dirty(struct regmap *map)
{
	map->lock(map->lock_arg);
	map->cache_dirty = true;
	map->no_sync_defaults = true;
	map->unlock(map->lock_arg);
}
EXPORT_SYMBOL_GPL(regcache_mark_dirty);

/**
 * regcache_cache_bypass - Put a register map into cache bypass mode
 *
 * @map: map to configure
 * @enable: flag if changes should not be written to the cache
 *
 * When a register map is marked with the cache bypass option, writes
 * to the register map API will only update the hardware and not
 * the cache directly.  This is useful when syncing the cache back to
 * the hardware.
 */
void regcache_cache_bypass(struct regmap *map, bool enable)
{
	map->lock(map->lock_arg);
	WARN_ON(map->cache_only && enable);
	map->cache_bypass = enable;
	trace_regmap_cache_bypass(map, enable);
	map->unlock(map->lock_arg);
}
EXPORT_SYMBOL_GPL(regcache_cache_bypass);

/**
 * regcache_reg_cached - Check if a register is cached
 *
 * @map: map to check
 * @reg: register to check
 *
 * Reports if a register is cached.
 */
bool regcache_reg_cached(struct regmap *map, unsigned int reg)
{
	unsigned int val;
	int ret;

	map->lock(map->lock_arg);

	ret = regcache_read(map, reg, &val);

	map->unlock(map->lock_arg);

	return ret == 0;
}
EXPORT_SYMBOL_GPL(regcache_reg_cached);

void regcache_set_val(struct regmap *map, void *base, unsigned int idx,
		      unsigned int val)
{
	/* Use device native format if possible */
	if (map->format.format_val) {
		map->format.format_val(base + (map->cache_word_size * idx),
				       val, 0);
		return;
	}

	switch (map->cache_word_size) {
	case 1: {
		u8 *cache = base;

		cache[idx] = val;
		break;
	}
	case 2: {
		u16 *cache = base;

		cache[idx] = val;
		break;
	}
	case 4: {
		u32 *cache = base;

		cache[idx] = val;
		break;
	}
	default:
		BUG();
	}
}

unsigned int regcache_get_val(struct regmap *map, const void *base,
			      unsigned int idx)
{
	if (!base)
		return -EINVAL;

	/* Use device native format if possible */
	if (map->format.parse_val)
		return map->format.parse_val(regcache_get_val_addr(map, base,
								   idx));

	switch (map->cache_word_size) {
	case 1: {
		const u8 *cache = base;

		return cache[idx];
	}
	case 2: {
		const u16 *cache = base;

		return cache[idx];
	}
	case 4: {
		const u32 *cache = base;

		return cache[idx];
	}
	default:
		BUG();
	}
	/* unreachable */
	return -1;
}

static int regcache_default_cmp(const void *a, const void *b)
{
	const struct reg_default *_a = a;
	const struct reg_default *_b = b;

	return _a->reg - _b->reg;
}

int regcache_lookup_reg(struct regmap *map, unsigned int reg)
{
	struct reg_default key;
	struct reg_default *r;

	key.reg = reg;
	key.def = 0;

	r = bsearch(&key, map->reg_defaults, map->num_reg_defaults,
		    sizeof(struct reg_default), regcache_default_cmp);

	if (r)
		return r - map->reg_defaults;
	else
		return -ENOENT;
}

static bool regcache_reg_present(unsigned long *cache_present, unsigned int idx)
{
	if (!cache_present)
		return true;

	return test_bit(idx, cache_present);
}

int regcache_sync_val(struct regmap *map, unsigned int reg, unsigned int val)
{
	int ret;

	if (!regcache_reg_needs_sync(map, reg, val))
		return 0;

	map->cache_bypass = true;

	ret = _regmap_write(map, reg, val);

	map->cache_bypass = false;

	if (ret != 0) {
		dev_err(map->dev, "Unable to sync register %#x. %d\n",
			reg, ret);
		return ret;
	}
	dev_dbg(map->dev, "Synced register %#x, value %#x\n",
		reg, val);

	return 0;
}

static int regcache_sync_block_single(struct regmap *map, void *block,
				      unsigned long *cache_present,
				      unsigned int block_base,
				      unsigned int start, unsigned int end)
{
	unsigned int i, regtmp, val;
	int ret;

	for (i = start; i < end; i++) {
		regtmp = block_base + (i * map->reg_stride);

		if (!regcache_reg_present(cache_present, i) ||
		    !regmap_writeable(map, regtmp))
			continue;

		val = regcache_get_val(map, block, i);
		ret = regcache_sync_val(map, regtmp, val);
		if (ret != 0)
			return ret;
	}

	return 0;
}

static int regcache_sync_block_raw_flush(struct regmap *map, const void **data,
					 unsigned int base, unsigned int cur)
{
	size_t val_bytes = map->format.val_bytes;
	int ret, count;

	if (*data == NULL)
		return 0;

	count = (cur - base) / map->reg_stride;

	dev_dbg(map->dev, "Writing %zu bytes for %d registers from 0x%x-0x%x\n",
		count * val_bytes, count, base, cur - map->reg_stride);

	map->cache_bypass = true;

	ret = _regmap_raw_write(map, base, *data, count * val_bytes, false);
	if (ret)
		dev_err(map->dev, "Unable to sync registers %#x-%#x. %d\n",
			base, cur - map->reg_stride, ret);

	map->cache_bypass = false;

	*data = NULL;

	return ret;
}

static int regcache_sync_block_raw(struct regmap *map, void *block,
			    unsigned long *cache_present,
			    unsigned int block_base, unsigned int start,
			    unsigned int end)
{
	unsigned int i, val;
	unsigned int regtmp = 0;
	unsigned int base = 0;
	const void *data = NULL;
	int ret;

	for (i = start; i < end; i++) {
		regtmp = block_base + (i * map->reg_stride);

		if (!regcache_reg_present(cache_present, i) ||
		    !regmap_writeable(map, regtmp)) {
			ret = regcache_sync_block_raw_flush(map, &data,
							    base, regtmp);
			if (ret != 0)
				return ret;
			continue;
		}

		val = regcache_get_val(map, block, i);
		if (!regcache_reg_needs_sync(map, regtmp, val)) {
			ret = regcache_sync_block_raw_flush(map, &data,
							    base, regtmp);
			if (ret != 0)
				return ret;
			continue;
		}

		if (!data) {
			data = regcache_get_val_addr(map, block, i);
			base = regtmp;
		}
	}

	return regcache_sync_block_raw_flush(map, &data, base, regtmp +
			map->reg_stride);
}

int regcache_sync_block(struct regmap *map, void *block,
			unsigned long *cache_present,
			unsigned int block_base, unsigned int start,
			unsigned int end)
{
	if (regmap_can_raw_write(map) && !map->use_single_write)
		return regcache_sync_block_raw(map, block, cache_present,
					       block_base, start, end);
	else
		return regcache_sync_block_single(map, block, cache_present,
						  block_base, start, end);
}
