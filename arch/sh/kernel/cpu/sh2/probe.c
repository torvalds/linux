// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/cpu/sh2/probe.c
 *
 * CPU Subtype Probing for SH-2.
 *
 * Copyright (C) 2002 Paul Mundt
 */
#include <linux/init.h>
#include <linux/of_fdt.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <asm/processor.h>
#include <asm/cache.h>

#if defined(CONFIG_CPU_J2)
extern u32 __iomem *j2_ccr_base;
static int __init scan_cache(unsigned long node, const char *uname,
			     int depth, void *data)
{
	if (!of_flat_dt_is_compatible(node, "jcore,cache"))
		return 0;

	j2_ccr_base = (u32 __iomem *)of_flat_dt_translate_address(node);

	return 1;
}
#endif

void __ref cpu_probe(void)
{
#if defined(CONFIG_CPU_SUBTYPE_SH7619)
	boot_cpu_data.type			= CPU_SH7619;
	boot_cpu_data.dcache.ways		= 4;
	boot_cpu_data.dcache.way_incr	= (1<<12);
	boot_cpu_data.dcache.sets		= 256;
	boot_cpu_data.dcache.entry_shift	= 4;
	boot_cpu_data.dcache.linesz		= L1_CACHE_BYTES;
	boot_cpu_data.dcache.flags		= 0;
#endif

#if defined(CONFIG_CPU_J2)
#if defined(CONFIG_SMP)
	unsigned cpu = hard_smp_processor_id();
#else
	unsigned cpu = 0;
#endif
	if (cpu == 0) of_scan_flat_dt(scan_cache, NULL);
	if (j2_ccr_base) __raw_writel(0x80000303, j2_ccr_base + 4*cpu);
	if (cpu != 0) return;
	boot_cpu_data.type			= CPU_J2;

	/* These defaults are appropriate for the original/current
	 * J2 cache. Once there is a proper framework for getting cache
	 * info from device tree, we should switch to that. */
	boot_cpu_data.dcache.ways		= 1;
	boot_cpu_data.dcache.sets		= 256;
	boot_cpu_data.dcache.entry_shift	= 5;
	boot_cpu_data.dcache.linesz		= 32;
	boot_cpu_data.dcache.flags		= 0;

	boot_cpu_data.flags |= CPU_HAS_CAS_L;
#else
	/*
	 * SH-2 doesn't have separate caches
	 */
	boot_cpu_data.dcache.flags |= SH_CACHE_COMBINED;
#endif
	boot_cpu_data.icache = boot_cpu_data.dcache;
	boot_cpu_data.family = CPU_FAMILY_SH2;
}
