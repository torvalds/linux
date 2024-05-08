// SPDX-License-Identifier: GPL-2.0
//
// Register cache access API - maple tree based cache
//
// Copyright 2023 Arm, Ltd
//
// Author: Mark Brown <broonie@kernel.org>

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/maple_tree.h>
#include <linux/slab.h>

#include "internal.h"

static int regcache_maple_read(struct regmap *map,
			       unsigned int reg, unsigned int *value)
{
	struct maple_tree *mt = map->cache;
	MA_STATE(mas, mt, reg, reg);
	unsigned long *entry;

	rcu_read_lock();

	entry = mas_walk(&mas);
	if (!entry) {
		rcu_read_unlock();
		return -ENOENT;
	}

	*value = entry[reg - mas.index];

	rcu_read_unlock();

	return 0;
}

static int regcache_maple_write(struct regmap *map, unsigned int reg,
				unsigned int val)
{
	struct maple_tree *mt = map->cache;
	MA_STATE(mas, mt, reg, reg);
	unsigned long *entry, *upper, *lower;
	unsigned long index, last;
	size_t lower_sz, upper_sz;
	int ret;

	rcu_read_lock();

	entry = mas_walk(&mas);
	if (entry) {
		entry[reg - mas.index] = val;
		rcu_read_unlock();
		return 0;
	}

	/* Any adjacent entries to extend/merge? */
	mas_set_range(&mas, reg - 1, reg + 1);
	index = reg;
	last = reg;

	lower = mas_find(&mas, reg - 1);
	if (lower) {
		index = mas.index;
		lower_sz = (mas.last - mas.index + 1) * sizeof(unsigned long);
	}

	upper = mas_find(&mas, reg + 1);
	if (upper) {
		last = mas.last;
		upper_sz = (mas.last - mas.index + 1) * sizeof(unsigned long);
	}

	rcu_read_unlock();

	entry = kmalloc((last - index + 1) * sizeof(unsigned long),
			map->alloc_flags);
	if (!entry)
		return -ENOMEM;

	if (lower)
		memcpy(entry, lower, lower_sz);
	entry[reg - index] = val;
	if (upper)
		memcpy(&entry[reg - index + 1], upper, upper_sz);

	/*
	 * This is safe because the regmap lock means the Maple lock
	 * is redundant, but we need to take it due to lockdep asserts
	 * in the maple tree code.
	 */
	mas_lock(&mas);

	mas_set_range(&mas, index, last);
	ret = mas_store_gfp(&mas, entry, map->alloc_flags);

	mas_unlock(&mas);

	if (ret == 0) {
		kfree(lower);
		kfree(upper);
	}
	
	return ret;
}

static int regcache_maple_drop(struct regmap *map, unsigned int min,
			       unsigned int max)
{
	struct maple_tree *mt = map->cache;
	MA_STATE(mas, mt, min, max);
	unsigned long *entry, *lower, *upper;
	unsigned long lower_index, lower_last;
	unsigned long upper_index, upper_last;
	int ret = 0;

	lower = NULL;
	upper = NULL;

	mas_lock(&mas);

	mas_for_each(&mas, entry, max) {
		/*
		 * This is safe because the regmap lock means the
		 * Maple lock is redundant, but we need to take it due
		 * to lockdep asserts in the maple tree code.
		 */
		mas_unlock(&mas);

		/* Do we need to save any of this entry? */
		if (mas.index < min) {
			lower_index = mas.index;
			lower_last = min -1;

			lower = kmemdup(entry, ((min - mas.index) *
						sizeof(unsigned long)),
					map->alloc_flags);
			if (!lower) {
				ret = -ENOMEM;
				goto out_unlocked;
			}
		}

		if (mas.last > max) {
			upper_index = max + 1;
			upper_last = mas.last;

			upper = kmemdup(&entry[max - mas.index + 1],
					((mas.last - max) *
					 sizeof(unsigned long)),
					map->alloc_flags);
			if (!upper) {
				ret = -ENOMEM;
				goto out_unlocked;
			}
		}

		kfree(entry);
		mas_lock(&mas);
		mas_erase(&mas);

		/* Insert new nodes with the saved data */
		if (lower) {
			mas_set_range(&mas, lower_index, lower_last);
			ret = mas_store_gfp(&mas, lower, map->alloc_flags);
			if (ret != 0)
				goto out;
			lower = NULL;
		}

		if (upper) {
			mas_set_range(&mas, upper_index, upper_last);
			ret = mas_store_gfp(&mas, upper, map->alloc_flags);
			if (ret != 0)
				goto out;
			upper = NULL;
		}
	}

out:
	mas_unlock(&mas);
out_unlocked:
	kfree(lower);
	kfree(upper);

	return ret;
}

static int regcache_maple_sync_block(struct regmap *map, unsigned long *entry,
				     struct ma_state *mas,
				     unsigned int min, unsigned int max)
{
	void *buf;
	unsigned long r;
	size_t val_bytes = map->format.val_bytes;
	int ret = 0;

	mas_pause(mas);
	rcu_read_unlock();

	/*
	 * Use a raw write if writing more than one register to a
	 * device that supports raw writes to reduce transaction
	 * overheads.
	 */
	if (max - min > 1 && regmap_can_raw_write(map)) {
		buf = kmalloc(val_bytes * (max - min), map->alloc_flags);
		if (!buf) {
			ret = -ENOMEM;
			goto out;
		}

		/* Render the data for a raw write */
		for (r = min; r < max; r++) {
			regcache_set_val(map, buf, r - min,
					 entry[r - mas->index]);
		}

		ret = _regmap_raw_write(map, min, buf, (max - min) * val_bytes,
					false);

		kfree(buf);
	} else {
		for (r = min; r < max; r++) {
			ret = _regmap_write(map, r,
					    entry[r - mas->index]);
			if (ret != 0)
				goto out;
		}
	}

out:
	rcu_read_lock();

	return ret;
}

static int regcache_maple_sync(struct regmap *map, unsigned int min,
			       unsigned int max)
{
	struct maple_tree *mt = map->cache;
	unsigned long *entry;
	MA_STATE(mas, mt, min, max);
	unsigned long lmin = min;
	unsigned long lmax = max;
	unsigned int r, v, sync_start;
	int ret = 0;
	bool sync_needed = false;

	map->cache_bypass = true;

	rcu_read_lock();

	mas_for_each(&mas, entry, max) {
		for (r = max(mas.index, lmin); r <= min(mas.last, lmax); r++) {
			v = entry[r - mas.index];

			if (regcache_reg_needs_sync(map, r, v)) {
				if (!sync_needed) {
					sync_start = r;
					sync_needed = true;
				}
				continue;
			}

			if (!sync_needed)
				continue;

			ret = regcache_maple_sync_block(map, entry, &mas,
							sync_start, r);
			if (ret != 0)
				goto out;
			sync_needed = false;
		}

		if (sync_needed) {
			ret = regcache_maple_sync_block(map, entry, &mas,
							sync_start, r);
			if (ret != 0)
				goto out;
			sync_needed = false;
		}
	}

out:
	rcu_read_unlock();

	map->cache_bypass = false;

	return ret;
}

static int regcache_maple_exit(struct regmap *map)
{
	struct maple_tree *mt = map->cache;
	MA_STATE(mas, mt, 0, UINT_MAX);
	unsigned int *entry;;

	/* if we've already been called then just return */
	if (!mt)
		return 0;

	mas_lock(&mas);
	mas_for_each(&mas, entry, UINT_MAX)
		kfree(entry);
	__mt_destroy(mt);
	mas_unlock(&mas);

	kfree(mt);
	map->cache = NULL;

	return 0;
}

static int regcache_maple_insert_block(struct regmap *map, int first,
					int last)
{
	struct maple_tree *mt = map->cache;
	MA_STATE(mas, mt, first, last);
	unsigned long *entry;
	int i, ret;

	entry = kcalloc(last - first + 1, sizeof(unsigned long), map->alloc_flags);
	if (!entry)
		return -ENOMEM;

	for (i = 0; i < last - first + 1; i++)
		entry[i] = map->reg_defaults[first + i].def;

	mas_lock(&mas);

	mas_set_range(&mas, map->reg_defaults[first].reg,
		      map->reg_defaults[last].reg);
	ret = mas_store_gfp(&mas, entry, map->alloc_flags);

	mas_unlock(&mas);

	if (ret)
		kfree(entry);

	return ret;
}

static int regcache_maple_init(struct regmap *map)
{
	struct maple_tree *mt;
	int i;
	int ret;
	int range_start;

	mt = kmalloc(sizeof(*mt), GFP_KERNEL);
	if (!mt)
		return -ENOMEM;
	map->cache = mt;

	mt_init(mt);

	if (!map->num_reg_defaults)
		return 0;

	range_start = 0;

	/* Scan for ranges of contiguous registers */
	for (i = 1; i < map->num_reg_defaults; i++) {
		if (map->reg_defaults[i].reg !=
		    map->reg_defaults[i - 1].reg + 1) {
			ret = regcache_maple_insert_block(map, range_start,
							  i - 1);
			if (ret != 0)
				goto err;

			range_start = i;
		}
	}

	/* Add the last block */
	ret = regcache_maple_insert_block(map, range_start,
					  map->num_reg_defaults - 1);
	if (ret != 0)
		goto err;

	return 0;

err:
	regcache_maple_exit(map);
	return ret;
}

struct regcache_ops regcache_maple_ops = {
	.type = REGCACHE_MAPLE,
	.name = "maple",
	.init = regcache_maple_init,
	.exit = regcache_maple_exit,
	.read = regcache_maple_read,
	.write = regcache_maple_write,
	.drop = regcache_maple_drop,
	.sync = regcache_maple_sync,
};
