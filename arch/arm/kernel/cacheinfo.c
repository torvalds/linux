// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ARM cacheinfo support
 *
 *  Copyright (C) 2023 Linaro Ltd.
 *  Copyright (C) 2015 ARM Ltd.
 *  All Rights Reserved
 */

#include <linux/bitfield.h>
#include <linux/cacheinfo.h>
#include <linux/of.h>

#include <asm/cachetype.h>
#include <asm/cputype.h>
#include <asm/system_info.h>

/* Ctypen, bits[3(n - 1) + 2 : 3(n - 1)], for n = 1 to 7 */
#define CLIDR_CTYPE_SHIFT(level)	(3 * (level - 1))
#define CLIDR_CTYPE_MASK(level)		(7 << CLIDR_CTYPE_SHIFT(level))
#define CLIDR_CTYPE(clidr, level)	\
	(((clidr) & CLIDR_CTYPE_MASK(level)) >> CLIDR_CTYPE_SHIFT(level))

#define MAX_CACHE_LEVEL			7	/* Max 7 level supported */

#define CTR_FORMAT_MASK	GENMASK(31, 29)
#define CTR_FORMAT_ARMV6 0
#define CTR_FORMAT_ARMV7 4
#define CTR_CWG_MASK	GENMASK(27, 24)
#define CTR_DSIZE_LEN_MASK GENMASK(13, 12)
#define CTR_ISIZE_LEN_MASK GENMASK(1, 0)

/* Also valid for v7m */
static inline int cache_line_size_cp15(void)
{
	u32 ctr = read_cpuid_cachetype();
	u32 format = FIELD_GET(CTR_FORMAT_MASK, ctr);

	if (format == CTR_FORMAT_ARMV7) {
		u32 cwg = FIELD_GET(CTR_CWG_MASK, ctr);

		return cwg ? 4 << cwg : ARCH_DMA_MINALIGN;
	} else if (WARN_ON_ONCE(format != CTR_FORMAT_ARMV6)) {
		return ARCH_DMA_MINALIGN;
	}

	return 8 << max(FIELD_GET(CTR_ISIZE_LEN_MASK, ctr),
			FIELD_GET(CTR_DSIZE_LEN_MASK, ctr));
}

int cache_line_size(void)
{
	if (coherency_max_size != 0)
		return coherency_max_size;

	/* CP15 is optional / implementation defined before ARMv6 */
	if (cpu_architecture() < CPU_ARCH_ARMv6)
		return ARCH_DMA_MINALIGN;

	return cache_line_size_cp15();
}
EXPORT_SYMBOL_GPL(cache_line_size);

static inline enum cache_type get_cache_type(int level)
{
	u32 clidr;

	if (level > MAX_CACHE_LEVEL)
		return CACHE_TYPE_NOCACHE;

	clidr = read_clidr();

	return CLIDR_CTYPE(clidr, level);
}

static void ci_leaf_init(struct cacheinfo *this_leaf,
			 enum cache_type type, unsigned int level)
{
	this_leaf->level = level;
	this_leaf->type = type;
}

static int detect_cache_level(unsigned int *level_p, unsigned int *leaves_p)
{
	unsigned int ctype, level, leaves;
	u32 ctr, format;

	/* CLIDR is not present before ARMv7/v7m */
	if (cpu_architecture() < CPU_ARCH_ARMv7)
		return -EOPNOTSUPP;

	/* Don't try reading CLIDR if CTR declares old format */
	ctr = read_cpuid_cachetype();
	format = FIELD_GET(CTR_FORMAT_MASK, ctr);
	if (format != CTR_FORMAT_ARMV7)
		return -EOPNOTSUPP;

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

	return 0;
}

int early_cache_level(unsigned int cpu)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);

	return detect_cache_level(&this_cpu_ci->num_levels, &this_cpu_ci->num_leaves);
}

int init_cache_level(unsigned int cpu)
{
	unsigned int level, leaves;
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	int fw_level;
	int ret;

	ret = detect_cache_level(&level, &leaves);
	if (ret)
		return ret;

	fw_level = of_find_last_cache_level(cpu);

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
	struct cacheinfo *this_leaf = this_cpu_ci->info_list;
	unsigned int arch = cpu_architecture();

	/* CLIDR is not present before ARMv7/v7m */
	if (arch < CPU_ARCH_ARMv7)
		return -EOPNOTSUPP;

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
