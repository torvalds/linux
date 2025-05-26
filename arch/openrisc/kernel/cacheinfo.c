// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenRISC cacheinfo support
 *
 * Based on work done for MIPS and LoongArch. All original copyrights
 * apply as per the original source declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2025 Sahil Siddiq <sahilcdq@proton.me>
 */

#include <linux/cacheinfo.h>
#include <asm/cpuinfo.h>
#include <asm/spr.h>
#include <asm/spr_defs.h>

static inline void ci_leaf_init(struct cacheinfo *this_leaf, enum cache_type type,
				unsigned int level, struct cache_desc *cache, int cpu)
{
	this_leaf->type = type;
	this_leaf->level = level;
	this_leaf->coherency_line_size = cache->block_size;
	this_leaf->number_of_sets = cache->sets;
	this_leaf->ways_of_associativity = cache->ways;
	this_leaf->size = cache->size;
	cpumask_set_cpu(cpu, &this_leaf->shared_cpu_map);
}

int init_cache_level(unsigned int cpu)
{
	struct cpuinfo_or1k *cpuinfo = &cpuinfo_or1k[smp_processor_id()];
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	int leaves = 0, levels = 0;
	unsigned long upr = mfspr(SPR_UPR);
	unsigned long iccfgr, dccfgr;

	if (!(upr & SPR_UPR_UP)) {
		printk(KERN_INFO
		       "-- no UPR register... unable to detect configuration\n");
		return -ENOENT;
	}

	if (cpu_cache_is_present(SPR_UPR_DCP)) {
		dccfgr = mfspr(SPR_DCCFGR);
		cpuinfo->dcache.ways = 1 << (dccfgr & SPR_DCCFGR_NCW);
		cpuinfo->dcache.sets = 1 << ((dccfgr & SPR_DCCFGR_NCS) >> 3);
		cpuinfo->dcache.block_size = 16 << ((dccfgr & SPR_DCCFGR_CBS) >> 7);
		cpuinfo->dcache.size =
		    cpuinfo->dcache.sets * cpuinfo->dcache.ways * cpuinfo->dcache.block_size;
		leaves += 1;
		printk(KERN_INFO
		       "-- dcache: %d bytes total, %d bytes/line, %d set(s), %d way(s)\n",
		       cpuinfo->dcache.size, cpuinfo->dcache.block_size,
		       cpuinfo->dcache.sets, cpuinfo->dcache.ways);
	} else
		printk(KERN_INFO "-- dcache disabled\n");

	if (cpu_cache_is_present(SPR_UPR_ICP)) {
		iccfgr = mfspr(SPR_ICCFGR);
		cpuinfo->icache.ways = 1 << (iccfgr & SPR_ICCFGR_NCW);
		cpuinfo->icache.sets = 1 << ((iccfgr & SPR_ICCFGR_NCS) >> 3);
		cpuinfo->icache.block_size = 16 << ((iccfgr & SPR_ICCFGR_CBS) >> 7);
		cpuinfo->icache.size =
		    cpuinfo->icache.sets * cpuinfo->icache.ways * cpuinfo->icache.block_size;
		leaves += 1;
		printk(KERN_INFO
		       "-- icache: %d bytes total, %d bytes/line, %d set(s), %d way(s)\n",
		       cpuinfo->icache.size, cpuinfo->icache.block_size,
		       cpuinfo->icache.sets, cpuinfo->icache.ways);
	} else
		printk(KERN_INFO "-- icache disabled\n");

	if (!leaves)
		return -ENOENT;

	levels = 1;

	this_cpu_ci->num_leaves = leaves;
	this_cpu_ci->num_levels = levels;

	return 0;
}

int populate_cache_leaves(unsigned int cpu)
{
	struct cpuinfo_or1k *cpuinfo = &cpuinfo_or1k[smp_processor_id()];
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *this_leaf = this_cpu_ci->info_list;
	int level = 1;

	if (cpu_cache_is_present(SPR_UPR_DCP)) {
		ci_leaf_init(this_leaf, CACHE_TYPE_DATA, level, &cpuinfo->dcache, cpu);
		this_leaf->attributes = ((mfspr(SPR_DCCFGR) & SPR_DCCFGR_CWS) >> 8) ?
					CACHE_WRITE_BACK : CACHE_WRITE_THROUGH;
		this_leaf++;
	}

	if (cpu_cache_is_present(SPR_UPR_ICP))
		ci_leaf_init(this_leaf, CACHE_TYPE_INST, level, &cpuinfo->icache, cpu);

	this_cpu_ci->cpu_map_populated = true;

	return 0;
}
