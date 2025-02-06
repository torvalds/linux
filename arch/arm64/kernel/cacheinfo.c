// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ARM64 cacheinfo support
 *
 *  Copyright (C) 2015 ARM Ltd.
 *  All Rights Reserved
 */

#include <linux/acpi.h>
#include <linux/cacheinfo.h>
#include <linux/of.h>

#define MAX_CACHE_LEVEL			7	/* Max 7 level supported */

int cache_line_size(void)
{
	if (coherency_max_size != 0)
		return coherency_max_size;

	return cache_line_size_of_cpu();
}
EXPORT_SYMBOL_GPL(cache_line_size);

static inline enum cache_type get_cache_type(int level)
{
	u64 clidr;

	if (level > MAX_CACHE_LEVEL)
		return CACHE_TYPE_NOCACHE;
	clidr = read_sysreg(clidr_el1);
	return CLIDR_CTYPE(clidr, level);
}

static void ci_leaf_init(struct cacheinfo *this_leaf,
			 enum cache_type type, unsigned int level)
{
	this_leaf->level = level;
	this_leaf->type = type;
}

static void detect_cache_level(unsigned int *level_p, unsigned int *leaves_p)
{
	unsigned int ctype, level, leaves;

	for (level = 1, leaves = 0; level <= MAX_CACHE_LEVEL; level++) {
		ctype = get_cache_type(level);
		if (ctype == CACHE_TYPE_NOCACHE) {
			level--;
			break;
		}
		/* Separate instruction and data caches */
		leaves += (ctype == CACHE_TYPE_SEPARATE) ? 2 : 1;
	}

	*level_p = level;
	*leaves_p = leaves;
}

int early_cache_level(unsigned int cpu)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);

	detect_cache_level(&this_cpu_ci->num_levels, &this_cpu_ci->num_leaves);

	return 0;
}

int init_cache_level(unsigned int cpu)
{
	unsigned int level, leaves;
	int fw_level, ret;
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);

	detect_cache_level(&level, &leaves);

	if (acpi_disabled) {
		fw_level = of_find_last_cache_level(cpu);
	} else {
		ret = acpi_get_cache_info(cpu, &fw_level, NULL);
		if (ret < 0)
			fw_level = 0;
	}

	if (level < fw_level) {
		/*
		 * some external caches not specified in CLIDR_EL1
		 * the information may be available in the device tree
		 * only unified external caches are considered here
		 */
		leaves += (fw_level - level);
		level = fw_level;
	}

	this_cpu_ci->num_levels = level;
	this_cpu_ci->num_leaves = leaves;
	return 0;
}

int populate_cache_leaves(unsigned int cpu)
{
	unsigned int level, idx;
	enum cache_type type;
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *infos = this_cpu_ci->info_list;

	for (idx = 0, level = 1; level <= this_cpu_ci->num_levels &&
	     idx < this_cpu_ci->num_leaves; level++) {
		type = get_cache_type(level);
		if (type == CACHE_TYPE_SEPARATE) {
			if (idx + 1 >= this_cpu_ci->num_leaves)
				break;
			ci_leaf_init(&infos[idx++], CACHE_TYPE_DATA, level);
			ci_leaf_init(&infos[idx++], CACHE_TYPE_INST, level);
		} else {
			ci_leaf_init(&infos[idx++], type, level);
		}
	}
	return 0;
}
