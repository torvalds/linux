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
/* Ctypen, bits[3(n - 1) + 2 : 3(n - 1)], for n = 1 to 7 */
#define CLIDR_CTYPE_SHIFT(level)	(3 * (level - 1))
#define CLIDR_CTYPE_MASK(level)		(7 << CLIDR_CTYPE_SHIFT(level))
#define CLIDR_CTYPE(clidr, level)	\
	(((clidr) & CLIDR_CTYPE_MASK(level)) >> CLIDR_CTYPE_SHIFT(level))

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

static int __init_cache_level(unsigned int cpu)
{
	unsigned int ctype, level, leaves, fw_level;
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);

	for (level = 1, leaves = 0; level <= MAX_CACHE_LEVEL; level++) {
		ctype = get_cache_type(level);
		if (ctype == CACHE_TYPE_NOCACHE) {
			level--;
			break;
		}
		/* Separate instruction and data caches */
		leaves += (ctype == CACHE_TYPE_SEPARATE) ? 2 : 1;
	}

	if (acpi_disabled)
		fw_level = of_find_last_cache_level(cpu);
	else
		fw_level = acpi_find_last_cache_level(cpu);

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

static int __populate_cache_leaves(unsigned int cpu)
{
	unsigned int level, idx;
	enum cache_type type;
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *this_leaf = this_cpu_ci->info_list;

	for (idx = 0, level = 1; level <= this_cpu_ci->num_levels &&
	     idx < this_cpu_ci->num_leaves; idx++, level++) {
		type = get_cache_type(level);
		if (type == CACHE_TYPE_SEPARATE) {
			ci_leaf_init(this_leaf++, CACHE_TYPE_DATA, level);
			ci_leaf_init(this_leaf++, CACHE_TYPE_INST, level);
		} else {
			ci_leaf_init(this_leaf++, type, level);
		}
	}
	return 0;
}

DEFINE_SMP_CALL_CACHE_FUNCTION(init_cache_level)
DEFINE_SMP_CALL_CACHE_FUNCTION(populate_cache_leaves)
