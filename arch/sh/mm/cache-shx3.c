/*
 * arch/sh/mm/cache-shx3.c - SH-X3 optimized cache ops
 *
 * Copyright (C) 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>

#define CCR_CACHE_SNM	0x40000		/* Hardware-assisted synonym avoidance */
#define CCR_CACHE_IBE	0x1000000	/* ICBI broadcast */

void __init shx3_cache_init(void)
{
	unsigned int ccr;

	ccr = __raw_readl(SH_CCR);

	/*
	 * If we've got cache aliases, resolve them in hardware.
	 */
	if (boot_cpu_data.dcache.n_aliases || boot_cpu_data.icache.n_aliases) {
		ccr |= CCR_CACHE_SNM;

		boot_cpu_data.icache.n_aliases = 0;
		boot_cpu_data.dcache.n_aliases = 0;

		pr_info("Enabling hardware synonym avoidance\n");
	}

#ifdef CONFIG_SMP
	/*
	 * Broadcast I-cache block invalidations by default.
	 */
	ccr |= CCR_CACHE_IBE;
#endif

	writel_uncached(ccr, SH_CCR);
}
