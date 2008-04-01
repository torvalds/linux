/*
 * Copyright (C) 1997, 2001 Ralf Baechle (ralf@gnu.org),
 * derived from r4xx0.c by David S. Miller (dm@engr.sgi.com).
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/mipsregs.h>
#include <asm/bcache.h>
#include <asm/cacheops.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/mmu_context.h>
#include <asm/r4kcache.h>

/* Secondary cache size in bytes, if present.  */
static unsigned long scache_size;

#define SC_LINE 32
#define SC_PAGE (128*SC_LINE)

static inline void blast_r5000_scache(void)
{
	unsigned long start = INDEX_BASE;
	unsigned long end = start + scache_size;

	while(start < end) {
		cache_op(R5K_Page_Invalidate_S, start);
		start += SC_PAGE;
	}
}

static void r5k_dma_cache_inv_sc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	/* Catch bad driver code */
	BUG_ON(size == 0);

	if (size >= scache_size) {
		blast_r5000_scache();
		return;
	}

	/* On the R5000 secondary cache we cannot
	 * invalidate less than a page at a time.
	 * The secondary cache is physically indexed, write-through.
	 */
	a = addr & ~(SC_PAGE - 1);
	end = (addr + size - 1) & ~(SC_PAGE - 1);
	while (a <= end) {
		cache_op(R5K_Page_Invalidate_S, a);
		a += SC_PAGE;
	}
}

static void r5k_sc_enable(void)
{
        unsigned long flags;

	local_irq_save(flags);
	set_c0_config(R5K_CONF_SE);
	blast_r5000_scache();
	local_irq_restore(flags);
}

static void r5k_sc_disable(void)
{
        unsigned long flags;

	local_irq_save(flags);
	blast_r5000_scache();
	clear_c0_config(R5K_CONF_SE);
	local_irq_restore(flags);
}

static inline int __init r5k_sc_probe(void)
{
	unsigned long config = read_c0_config();

	if (config & CONF_SC)
		return(0);

	scache_size = (512 * 1024) << ((config & R5K_CONF_SS) >> 20);

	printk("R5000 SCACHE size %ldkB, linesize 32 bytes.\n",
			scache_size >> 10);

	return 1;
}

static struct bcache_ops r5k_sc_ops = {
	.bc_enable = r5k_sc_enable,
	.bc_disable = r5k_sc_disable,
	.bc_wback_inv = r5k_dma_cache_inv_sc,
	.bc_inv = r5k_dma_cache_inv_sc
};

void __cpuinit r5k_sc_init(void)
{
	if (r5k_sc_probe()) {
		r5k_sc_enable();
		bcops = &r5k_sc_ops;
	}
}
