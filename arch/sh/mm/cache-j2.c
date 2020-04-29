// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/sh/mm/cache-j2.c
 *
 * Copyright (C) 2015-2016 Smart Energy Instruments, Inc.
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/cpumask.h>

#include <asm/cache.h>
#include <asm/addrspace.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/io.h>

#define ICACHE_ENABLE	0x1
#define DCACHE_ENABLE	0x2
#define CACHE_ENABLE	(ICACHE_ENABLE | DCACHE_ENABLE)
#define ICACHE_FLUSH	0x100
#define DCACHE_FLUSH	0x200
#define CACHE_FLUSH	(ICACHE_FLUSH | DCACHE_FLUSH)

u32 __iomem *j2_ccr_base;

static void j2_flush_icache(void *args)
{
	unsigned cpu;
	for_each_possible_cpu(cpu)
		__raw_writel(CACHE_ENABLE | ICACHE_FLUSH, j2_ccr_base + cpu);
}

static void j2_flush_dcache(void *args)
{
	unsigned cpu;
	for_each_possible_cpu(cpu)
		__raw_writel(CACHE_ENABLE | DCACHE_FLUSH, j2_ccr_base + cpu);
}

static void j2_flush_both(void *args)
{
	unsigned cpu;
	for_each_possible_cpu(cpu)
		__raw_writel(CACHE_ENABLE | CACHE_FLUSH, j2_ccr_base + cpu);
}

void __init j2_cache_init(void)
{
	if (!j2_ccr_base)
		return;

	local_flush_cache_all = j2_flush_both;
	local_flush_cache_mm = j2_flush_both;
	local_flush_cache_dup_mm = j2_flush_both;
	local_flush_cache_page = j2_flush_both;
	local_flush_cache_range = j2_flush_both;
	local_flush_dcache_page = j2_flush_dcache;
	local_flush_icache_range = j2_flush_icache;
	local_flush_icache_page = j2_flush_icache;
	local_flush_cache_sigtramp = j2_flush_icache;

	pr_info("Initial J2 CCR is %.8x\n", __raw_readl(j2_ccr_base));
}
