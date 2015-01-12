/*
 * Register cache access API - flat caching support
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "internal.h"

static int regcache_flat_init(struct regmap *map)
{
	int i;
	unsigned int *cache;

	map->cache = kzalloc(sizeof(unsigned int) * (map->max_register + 1),
			     GFP_KERNEL);
	if (!map->cache)
		return -ENOMEM;

	cache = map->cache;

	for (i = 0; i < map->num_reg_defaults; i++)
		cache[map->reg_defaults[i].reg] = map->reg_defaults[i].def;

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

	*value = cache[reg];

	return 0;
}

static int regcache_flat_write(struct regmap *map, unsigned int reg,
			       unsigned int value)
{
	unsigned int *cache = map->cache;

	cache[reg] = value;

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
