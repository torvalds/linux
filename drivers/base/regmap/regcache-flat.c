// SPDX-License-Identifier: GPL-2.0
//
// Register cache access API - flat caching support
//
// Copyright 2012 Wolfson Microelectronics plc
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/limits.h>
#include <linux/overflow.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "internal.h"

static inline unsigned int regcache_flat_get_index(const struct regmap *map,
						   unsigned int reg)
{
	return regcache_get_index_by_order(map, reg);
}

struct regcache_flat_data {
	unsigned long *valid;
	unsigned int data[];
};

static int regcache_flat_init(struct regmap *map)
{
	unsigned int cache_size;
	struct regcache_flat_data *cache;

	if (!map || map->reg_stride_order < 0 || !map->max_register_is_set)
		return -EINVAL;

	cache_size = regcache_flat_get_index(map, map->max_register) + 1;
	cache = kzalloc(struct_size(cache, data, cache_size), map->alloc_flags);
	if (!cache)
		return -ENOMEM;

	cache->valid = bitmap_zalloc(cache_size, map->alloc_flags);
	if (!cache->valid)
		goto err_free;

	map->cache = cache;

	return 0;

err_free:
	kfree(cache);
	return -ENOMEM;
}

static int regcache_flat_exit(struct regmap *map)
{
	struct regcache_flat_data *cache = map->cache;

	if (cache)
		bitmap_free(cache->valid);

	kfree(cache);
	map->cache = NULL;

	return 0;
}

static int regcache_flat_populate(struct regmap *map)
{
	struct regcache_flat_data *cache = map->cache;
	unsigned int i;

	for (i = 0; i < map->num_reg_defaults; i++) {
		unsigned int reg = map->reg_defaults[i].reg;
		unsigned int index = regcache_flat_get_index(map, reg);

		cache->data[index] = map->reg_defaults[i].def;
		__set_bit(index, cache->valid);
	}

	return 0;
}

static int regcache_flat_read(struct regmap *map,
			      unsigned int reg, unsigned int *value)
{
	struct regcache_flat_data *cache = map->cache;
	unsigned int index = regcache_flat_get_index(map, reg);

	/* legacy behavior: ignore validity, but warn the user */
	if (unlikely(!test_bit(index, cache->valid)))
		dev_warn_once(map->dev,
			"using zero-initialized flat cache, this may cause unexpected behavior");

	*value = cache->data[index];

	return 0;
}

static int regcache_flat_sparse_read(struct regmap *map,
				     unsigned int reg, unsigned int *value)
{
	struct regcache_flat_data *cache = map->cache;
	unsigned int index = regcache_flat_get_index(map, reg);

	if (unlikely(!test_bit(index, cache->valid)))
		return -ENOENT;

	*value = cache->data[index];

	return 0;
}

static int regcache_flat_write(struct regmap *map, unsigned int reg,
			       unsigned int value)
{
	struct regcache_flat_data *cache = map->cache;
	unsigned int index = regcache_flat_get_index(map, reg);

	cache->data[index] = value;
	__set_bit(index, cache->valid);

	return 0;
}

static int regcache_flat_drop(struct regmap *map, unsigned int min,
			      unsigned int max)
{
	struct regcache_flat_data *cache = map->cache;
	unsigned int bitmap_min = regcache_flat_get_index(map, min);
	unsigned int bitmap_max = regcache_flat_get_index(map, max);

	bitmap_clear(cache->valid, bitmap_min, bitmap_max + 1 - bitmap_min);

	return 0;
}

struct regcache_ops regcache_flat_ops = {
	.type = REGCACHE_FLAT,
	.name = "flat",
	.init = regcache_flat_init,
	.exit = regcache_flat_exit,
	.populate = regcache_flat_populate,
	.read = regcache_flat_read,
	.write = regcache_flat_write,
};

struct regcache_ops regcache_flat_sparse_ops = {
	.type = REGCACHE_FLAT_S,
	.name = "flat-sparse",
	.init = regcache_flat_init,
	.exit = regcache_flat_exit,
	.populate = regcache_flat_populate,
	.read = regcache_flat_sparse_read,
	.write = regcache_flat_write,
	.drop = regcache_flat_drop,
};
