// SPDX-License-Identifier: GPL-2.0
/*
 * sc-rm7k.c: RM7000 cache management functions.
 *
 * Copyright (C) 1997, 2001, 2003, 2004 Ralf Baechle (ralf@linux-mips.org)
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bitops.h>

#include <asm/addrspace.h>
#include <asm/bcache.h>
#include <asm/cacheops.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/cacheflush.h> /* for run_uncached() */

/* Primary cache parameters. */
#define sc_lsize	32
#define tc_pagesize	(32*128)

/* Secondary cache parameters. */
#define scache_size	(256*1024)	/* Fixed to 256KiB on RM7000 */

/* Tertiary cache parameters */
#define tc_lsize	32

extern unsigned long icache_way_size, dcache_way_size;
static unsigned long tcache_size;

#include <asm/r4kcache.h>

static int rm7k_tcache_init;

/*
 * Writeback and invalidate the primary cache dcache before DMA.
 * (XXX These need to be fixed ...)
 */
static void rm7k_sc_wback_inv(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	pr_debug("rm7k_sc_wback_inv[%08lx,%08lx]", addr, size);

	/* Catch bad driver code */
	BUG_ON(size == 0);

	blast_scache_range(addr, addr + size);

	if (!rm7k_tcache_init)
		return;

	a = addr & ~(tc_pagesize - 1);
	end = (addr + size - 1) & ~(tc_pagesize - 1);
	while(1) {
		invalidate_tcache_page(a);	/* Page_Invalidate_T */
		if (a == end)
			break;
		a += tc_pagesize;
	}
}

static void rm7k_sc_inv(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	pr_debug("rm7k_sc_inv[%08lx,%08lx]", addr, size);

	/* Catch bad driver code */
	BUG_ON(size == 0);

	blast_inv_scache_range(addr, addr + size);

	if (!rm7k_tcache_init)
		return;

	a = addr & ~(tc_pagesize - 1);
	end = (addr + size - 1) & ~(tc_pagesize - 1);
	while(1) {
		invalidate_tcache_page(a);	/* Page_Invalidate_T */
		if (a == end)
			break;
		a += tc_pagesize;
	}
}

static void blast_rm7k_tcache(void)
{
	unsigned long start = CKSEG0ADDR(0);
	unsigned long end = start + tcache_size;

	write_c0_taglo(0);

	while (start < end) {
		cache_op(Page_Invalidate_T, start);
		start += tc_pagesize;
	}
}

/*
 * This function is executed in uncached address space.
 */
static void __rm7k_tc_enable(void)
{
	int i;

	set_c0_config(RM7K_CONF_TE);

	write_c0_taglo(0);
	write_c0_taghi(0);

	for (i = 0; i < tcache_size; i += tc_lsize)
		cache_op(Index_Store_Tag_T, CKSEG0ADDR(i));
}

static void rm7k_tc_enable(void)
{
	if (read_c0_config() & RM7K_CONF_TE)
		return;

	BUG_ON(tcache_size == 0);

	run_uncached(__rm7k_tc_enable);
}

/*
 * This function is executed in uncached address space.
 */
static void __rm7k_sc_enable(void)
{
	int i;

	set_c0_config(RM7K_CONF_SE);

	write_c0_taglo(0);
	write_c0_taghi(0);

	for (i = 0; i < scache_size; i += sc_lsize)
		cache_op(Index_Store_Tag_SD, CKSEG0ADDR(i));
}

static void rm7k_sc_enable(void)
{
	if (read_c0_config() & RM7K_CONF_SE)
		return;

	pr_info("Enabling secondary cache...\n");
	run_uncached(__rm7k_sc_enable);

	if (rm7k_tcache_init)
		rm7k_tc_enable();
}

static void rm7k_tc_disable(void)
{
	unsigned long flags;

	local_irq_save(flags);
	blast_rm7k_tcache();
	clear_c0_config(RM7K_CONF_TE);
	local_irq_restore(flags);
}

static void rm7k_sc_disable(void)
{
	clear_c0_config(RM7K_CONF_SE);

	if (rm7k_tcache_init)
		rm7k_tc_disable();
}

static struct bcache_ops rm7k_sc_ops = {
	.bc_enable = rm7k_sc_enable,
	.bc_disable = rm7k_sc_disable,
	.bc_wback_inv = rm7k_sc_wback_inv,
	.bc_inv = rm7k_sc_inv
};

/*
 * This is a probing function like the one found in c-r4k.c, we look for the
 * wrap around point with different addresses.
 */
static void __probe_tcache(void)
{
	unsigned long flags, addr, begin, end, pow2;

	begin = (unsigned long) &_stext;
	begin  &= ~((8 * 1024 * 1024) - 1);
	end = begin + (8 * 1024 * 1024);

	local_irq_save(flags);

	set_c0_config(RM7K_CONF_TE);

	/* Fill size-multiple lines with a valid tag */
	pow2 = (256 * 1024);
	for (addr = begin; addr <= end; addr = (begin + pow2)) {
		unsigned long *p = (unsigned long *) addr;
		__asm__ __volatile__("nop" : : "r" (*p));
		pow2 <<= 1;
	}

	/* Load first line with a 0 tag, to check after */
	write_c0_taglo(0);
	write_c0_taghi(0);
	cache_op(Index_Store_Tag_T, begin);

	/* Look for the wrap-around */
	pow2 = (512 * 1024);
	for (addr = begin + (512 * 1024); addr <= end; addr = begin + pow2) {
		cache_op(Index_Load_Tag_T, addr);
		if (!read_c0_taglo())
			break;
		pow2 <<= 1;
	}

	addr -= begin;
	tcache_size = addr;

	clear_c0_config(RM7K_CONF_TE);

	local_irq_restore(flags);
}

void rm7k_sc_init(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int config = read_c0_config();

	if ((config & RM7K_CONF_SC))
		return;

	c->scache.linesz = sc_lsize;
	c->scache.ways = 4;
	c->scache.waybit= __ffs(scache_size / c->scache.ways);
	c->scache.waysize = scache_size / c->scache.ways;
	c->scache.sets = scache_size / (c->scache.linesz * c->scache.ways);
	printk(KERN_INFO "Secondary cache size %dK, linesize %d bytes.\n",
	       (scache_size >> 10), sc_lsize);

	if (!(config & RM7K_CONF_SE))
		rm7k_sc_enable();

	bcops = &rm7k_sc_ops;

	/*
	 * While we're at it let's deal with the tertiary cache.
	 */

	rm7k_tcache_init = 0;
	tcache_size = 0;

	if (config & RM7K_CONF_TC)
		return;

	/*
	 * No efficient way to ask the hardware for the size of the tcache,
	 * so must probe for it.
	 */
	run_uncached(__probe_tcache);
	rm7k_tc_enable();
	rm7k_tcache_init = 1;
	c->tcache.linesz = tc_lsize;
	c->tcache.ways = 1;
	pr_info("Tertiary cache size %ldK.\n", (tcache_size >> 10));
}
