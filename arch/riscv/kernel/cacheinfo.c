/*
 * Copyright (C) 2017 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_device.h>

static void ci_leaf_init(struct cacheinfo *this_leaf,
			 struct device_node *node,
			 enum cache_type type, unsigned int level)
{
	this_leaf->level = level;
	this_leaf->type = type;
}

static int __init_cache_level(unsigned int cpu)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct device_node *np = of_cpu_device_node_get(cpu);
	struct device_node *prev = NULL;
	int levels = 0, leaves = 0, level;

	if (of_property_read_bool(np, "cache-size"))
		++leaves;
	if (of_property_read_bool(np, "i-cache-size"))
		++leaves;
	if (of_property_read_bool(np, "d-cache-size"))
		++leaves;
	if (leaves > 0)
		levels = 1;

	prev = np;
	while ((np = of_find_next_cache_node(np))) {
		of_node_put(prev);
		prev = np;
		if (!of_device_is_compatible(np, "cache"))
			break;
		if (of_property_read_u32(np, "cache-level", &level))
			break;
		if (level <= levels)
			break;
		if (of_property_read_bool(np, "cache-size"))
			++leaves;
		if (of_property_read_bool(np, "i-cache-size"))
			++leaves;
		if (of_property_read_bool(np, "d-cache-size"))
			++leaves;
		levels = level;
	}

	of_node_put(np);
	this_cpu_ci->num_levels = levels;
	this_cpu_ci->num_leaves = leaves;

	return 0;
}

static int __populate_cache_leaves(unsigned int cpu)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *this_leaf = this_cpu_ci->info_list;
	struct device_node *np = of_cpu_device_node_get(cpu);
	struct device_node *prev = NULL;
	int levels = 1, level = 1;

	if (of_property_read_bool(np, "cache-size"))
		ci_leaf_init(this_leaf++, np, CACHE_TYPE_UNIFIED, level);
	if (of_property_read_bool(np, "i-cache-size"))
		ci_leaf_init(this_leaf++, np, CACHE_TYPE_INST, level);
	if (of_property_read_bool(np, "d-cache-size"))
		ci_leaf_init(this_leaf++, np, CACHE_TYPE_DATA, level);

	prev = np;
	while ((np = of_find_next_cache_node(np))) {
		of_node_put(prev);
		prev = np;
		if (!of_device_is_compatible(np, "cache"))
			break;
		if (of_property_read_u32(np, "cache-level", &level))
			break;
		if (level <= levels)
			break;
		if (of_property_read_bool(np, "cache-size"))
			ci_leaf_init(this_leaf++, np, CACHE_TYPE_UNIFIED, level);
		if (of_property_read_bool(np, "i-cache-size"))
			ci_leaf_init(this_leaf++, np, CACHE_TYPE_INST, level);
		if (of_property_read_bool(np, "d-cache-size"))
			ci_leaf_init(this_leaf++, np, CACHE_TYPE_DATA, level);
		levels = level;
	}
	of_node_put(np);

	return 0;
}

DEFINE_SMP_CALL_CACHE_FUNCTION(init_cache_level)
DEFINE_SMP_CALL_CACHE_FUNCTION(populate_cache_leaves)
