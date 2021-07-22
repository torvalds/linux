// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 SiFive
 */

#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <asm/cacheinfo.h>

static struct riscv_cacheinfo_ops *rv_cache_ops;

void riscv_set_cacheinfo_ops(struct riscv_cacheinfo_ops *ops)
{
	rv_cache_ops = ops;
}
EXPORT_SYMBOL_GPL(riscv_set_cacheinfo_ops);

const struct attribute_group *
cache_get_priv_group(struct cacheinfo *this_leaf)
{
	if (rv_cache_ops && rv_cache_ops->get_priv_group)
		return rv_cache_ops->get_priv_group(this_leaf);
	return NULL;
}

static struct cacheinfo *get_cacheinfo(u32 level, enum cache_type type)
{
	/*
	 * Using raw_smp_processor_id() elides a preemptability check, but this
	 * is really indicative of a larger problem: the cacheinfo UABI assumes
	 * that cores have a homonogenous view of the cache hierarchy.  That
	 * happens to be the case for the current set of RISC-V systems, but
	 * likely won't be true in general.  Since there's no way to provide
	 * correct information for these systems via the current UABI we're
	 * just eliding the check for now.
	 */
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(raw_smp_processor_id());
	struct cacheinfo *this_leaf;
	int index;

	for (index = 0; index < this_cpu_ci->num_leaves; index++) {
		this_leaf = this_cpu_ci->info_list + index;
		if (this_leaf->level == level && this_leaf->type == type)
			return this_leaf;
	}

	return NULL;
}

uintptr_t get_cache_size(u32 level, enum cache_type type)
{
	struct cacheinfo *this_leaf = get_cacheinfo(level, type);

	return this_leaf ? this_leaf->size : 0;
}

uintptr_t get_cache_geometry(u32 level, enum cache_type type)
{
	struct cacheinfo *this_leaf = get_cacheinfo(level, type);

	return this_leaf ? (this_leaf->ways_of_associativity << 16 |
			    this_leaf->coherency_line_size) :
			   0;
}

static void ci_leaf_init(struct cacheinfo *this_leaf, enum cache_type type,
			 unsigned int level, unsigned int size,
			 unsigned int sets, unsigned int line_size)
{
	this_leaf->level = level;
	this_leaf->type = type;
	this_leaf->size = size;
	this_leaf->number_of_sets = sets;
	this_leaf->coherency_line_size = line_size;

	/*
	 * If the cache is fully associative, there is no need to
	 * check the other properties.
	 */
	if (sets == 1)
		return;

	/*
	 * Set the ways number for n-ways associative, make sure
	 * all properties are big than zero.
	 */
	if (sets > 0 && size > 0 && line_size > 0)
		this_leaf->ways_of_associativity = (size / sets) / line_size;
}

static void fill_cacheinfo(struct cacheinfo **this_leaf,
			   struct device_node *node, unsigned int level)
{
	unsigned int size, sets, line_size;

	if (!of_property_read_u32(node, "cache-size", &size) &&
	    !of_property_read_u32(node, "cache-block-size", &line_size) &&
	    !of_property_read_u32(node, "cache-sets", &sets)) {
		ci_leaf_init((*this_leaf)++, CACHE_TYPE_UNIFIED, level, size, sets, line_size);
	}

	if (!of_property_read_u32(node, "i-cache-size", &size) &&
	    !of_property_read_u32(node, "i-cache-sets", &sets) &&
	    !of_property_read_u32(node, "i-cache-block-size", &line_size)) {
		ci_leaf_init((*this_leaf)++, CACHE_TYPE_INST, level, size, sets, line_size);
	}

	if (!of_property_read_u32(node, "d-cache-size", &size) &&
	    !of_property_read_u32(node, "d-cache-sets", &sets) &&
	    !of_property_read_u32(node, "d-cache-block-size", &line_size)) {
		ci_leaf_init((*this_leaf)++, CACHE_TYPE_DATA, level, size, sets, line_size);
	}
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

	/* Level 1 caches in cpu node */
	fill_cacheinfo(&this_leaf, np, level);

	/* Next level caches in cache nodes */
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

		fill_cacheinfo(&this_leaf, np, level);

		levels = level;
	}
	of_node_put(np);

	return 0;
}

DEFINE_SMP_CALL_CACHE_FUNCTION(init_cache_level)
DEFINE_SMP_CALL_CACHE_FUNCTION(populate_cache_leaves)
