/*
 * Register cache access API - indexed caching support
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>

#include "internal.h"

static int regcache_indexed_read(struct regmap *map, unsigned int reg,
				 unsigned int *value)
{
	int ret;

	ret = regcache_lookup_reg(map, reg);
	if (ret >= 0)
		*value = map->reg_defaults[ret].def;

	return ret;
}

static int regcache_indexed_write(struct regmap *map, unsigned int reg,
				  unsigned int value)
{
	int ret;

	ret = regcache_lookup_reg(map, reg);
	if (ret < 0)
		return regcache_insert_reg(map, reg, value);
	map->reg_defaults[ret].def = value;
	return 0;
}

static int regcache_indexed_sync(struct regmap *map)
{
	unsigned int i;
	int ret;

	for (i = 0; i < map->num_reg_defaults; i++) {
		ret = _regmap_write(map, map->reg_defaults[i].reg,
				    map->reg_defaults[i].def);
		if (ret < 0)
			return ret;
		dev_dbg(map->dev, "Synced register %#x, value %#x\n",
			map->reg_defaults[i].reg,
			map->reg_defaults[i].def);
	}
	return 0;
}

struct regcache_ops regcache_indexed_ops = {
	.type = REGCACHE_INDEXED,
	.name = "indexed",
	.read = regcache_indexed_read,
	.write = regcache_indexed_write,
	.sync = regcache_indexed_sync
};
