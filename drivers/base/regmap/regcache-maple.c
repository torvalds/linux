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
			GFP_KERNEL);
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
	ret = mas_store_gfp(&mas, entry, GFP_KERNEL);

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
	int ret;

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
					GFP_KERNEL);
			if (!lower) {
				ret = -ENOMEM;
				goto out_unlocked;
			}
		}

		if (mas.last > max) {
			upper_index = max + 1;
			upper_last = mas.last;

			upper = kmemdup(&entry[max + 1],
					((mas.last - max) *
					 sizeof(unsigned long)),
					GFP_KERNEL);
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
			ret = mas_store_gfp(&mas, lower, GFP_KERNEL);
			if (ret != 0)
				goto out;
			lower = NULL;
		}

		if (upper) {
			mas_set_range(&mas, upper_index, upper_last);
			ret = mas_store_gfp(&mas, upper, GFP_KERNEL);
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

static int regcache_maple_sync(struct regmap *map, unsigned int min,
			       unsigned int max)
{
	struct maple_tree *mt = map->cache;
	unsigned long *entry;
	MA_STATE(mas, mt, min, max);
	unsigned long lmin = min;
	unsigned long lmax = max;
	unsigned int r;
	int ret;

	map->cache_bypass = true;

	rcu_read_lock();

	mas_for_each(&mas, entry, max) {
		for (r = max(mas.index, lmin); r <= min(mas.last, lmax); r++) {
			ret = regcache_sync_val(map, r, entry[r - mas.index]);
			if (ret != 0)
				goto out;
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

static int regcache_maple_init(struct regmap *map)
{
	struct maple_tree *mt;
	int i;
	int ret;

	mt = kmalloc(sizeof(*mt), GFP_KERNEL);
	if (!mt)
		return -ENOMEM;
	map->cache = mt;

	mt_init(mt);

	for (i = 0; i < map->num_reg_defaults; i++) {
		ret = regcache_maple_write(map,
					   map->reg_defaults[i].reg,
					   map->reg_defaults[i].def);
		if (ret)
			goto err;
	}

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
