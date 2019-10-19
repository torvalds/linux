// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/bitops.h>
#include <linux/cacheinfo.h>
#include <linux/cpu.h>

static void ci_leaf_init(struct cacheinfo *this_leaf,
			 enum cache_type type, unsigned int level)
{
	char cache_type = (type & CACHE_TYPE_INST ? ICACHE : DCACHE);

	this_leaf->level = level;
	this_leaf->type = type;
	this_leaf->coherency_line_size = CACHE_LINE_SIZE(cache_type);
	this_leaf->number_of_sets = CACHE_SET(cache_type);
	this_leaf->ways_of_associativity = CACHE_WAY(cache_type);
	this_leaf->size = this_leaf->number_of_sets *
	    this_leaf->coherency_line_size * this_leaf->ways_of_associativity;
#if defined(CONFIG_CPU_DCACHE_WRITETHROUGH)
	this_leaf->attributes = CACHE_WRITE_THROUGH;
#else
	this_leaf->attributes = CACHE_WRITE_BACK;
#endif
}

int init_cache_level(unsigned int cpu)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);

	/* Only 1 level and I/D cache seperate. */
	this_cpu_ci->num_levels = 1;
	this_cpu_ci->num_leaves = 2;
	return 0;
}

int populate_cache_leaves(unsigned int cpu)
{
	unsigned int level, idx;
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *this_leaf = this_cpu_ci->info_list;

	for (idx = 0, level = 1; level <= this_cpu_ci->num_levels &&
	     idx < this_cpu_ci->num_leaves; idx++, level++) {
		ci_leaf_init(this_leaf++, CACHE_TYPE_DATA, level);
		ci_leaf_init(this_leaf++, CACHE_TYPE_INST, level);
	}
	return 0;
}
