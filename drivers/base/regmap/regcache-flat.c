// SPDX-License-Identifier: GPL-2.0
//
// Register cache access API - flat caching support
//
// Copyright 2012 Wolfson Microelectronics plc
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "internal.h"

static inline unsigned int regcache_flat_get_index(const struct regmap *map,
						   unsigned int reg)
{
	return regcache_get_index_by_order(map, reg);
}

static int regcache_flat_init(struct regmap *map)
{
	int i;
	unsigned int *cache;

	if (!map || map->reg_stride_order < 0 || !map->max_register_is_set)
		return -EINVAL;

	map->cache = kcalloc(regcache_flat_get_index(map, map->max_register)
			     + 1, sizeof(unsigned int), map->alloc_flags);
	if (!map->cache)
		return -ENOMEM;

	cache = map->cache;

	for (i = 0; i < map->num_reg_defaults; i++) {
		unsigned int reg = map->reg_defaults[i].reg;
		unsigned int index = regcache_flat_get_index(map, reg);

		cache[index] = map->reg_defaults[i].def;
	}

	return 0;
}

static int regcache_flat_exit(struct regmap *map)
{
	kfree(map->cache);
	map->cache = NULL;

	return 0;
}

static int regcache_flat_read(struct regmap *map,
			      unsigned int reg, unsigned int *value)
{
	unsigned int *cache = map->cache;
	unsigned int index = regcache_flat_get_index(map, reg);

	*value = cache[index];

	return 0;
}

static int regcache_flat_write(struct regmap *map, unsigned int reg,
			       unsigned int value)
{
	unsigned int *cache = map->cache;
	unsigned int index = regcache_flat_get_index(map, reg);

	cache[index] = value;

	return 0;
}

struct regcache_ops regcache_flat_ops = {
	.type = REGCACHE_FLAT,
	.name = "flat",
	.init = regcache_flat_init,
	.exit = regcache_flat_exit,
	.read = regcache_flat_read,
	.write = regcache_flat_write,
};
