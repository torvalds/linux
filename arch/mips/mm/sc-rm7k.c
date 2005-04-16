/*
 * sc-rm7k.c: RM7000 cache management functions.
 *
 * Copyright (C) 1997, 2001, 2003, 2004 Ralf Baechle (ralf@linux-mips.org)
 */

#undef DEBUG

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/addrspace.h>
#include <asm/bcache.h>
#include <asm/cacheops.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>

/* Primary cache parameters. */
#define sc_lsize	32
#define tc_pagesize	(32*128)

/* Secondary cache parameters. */
#define scache_size	(256*1024)	/* Fixed to 256KiB on RM7000 */

extern unsigned long icache_way_size, dcache_way_size;

#include <asm/r4kcache.h>

int rm7k_tcache_enabled;

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

	a = addr & ~(sc_lsize - 1);
	end = (addr + size - 1) & ~(sc_lsize - 1);
	while (1) {
		flush_scache_line(a);	/* Hit_Writeback_Inv_SD */
		if (a == end)
			break;
		a += sc_lsize;
	}

	if (!rm7k_tcache_enabled)
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

	a = addr & ~(sc_lsize - 1);
	end = (addr + size - 1) & ~(sc_lsize - 1);
	while (1) {
		invalidate_scache_line(a);	/* Hit_Invalidate_SD */
		if (a == end)
			break;
		a += sc_lsize;
	}

	if (!rm7k_tcache_enabled)
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

/*
 * This function is executed in the uncached segment CKSEG1.
 * It must not touch the stack, because the stack pointer still points
 * into CKSEG0.
 *
 * Three options:
 *	- Write it in assembly and guarantee that we don't use the stack.
 *	- Disable caching for CKSEG0 before calling it.
 *	- Pray that GCC doesn't randomly start using the stack.
 *
 * This being Linux, we obviously take the least sane of those options -
 * following DaveM's lead in c-r4k.c
 *
 * It seems we get our kicks from relying on unguaranteed behaviour in GCC
 */
static __init void __rm7k_sc_enable(void)
{
	int i;

	set_c0_config(1 << 3);				/* CONF_SE */

	write_c0_taglo(0);
	write_c0_taghi(0);

	for (i = 0; i < scache_size; i += sc_lsize) {
		__asm__ __volatile__ (
		      ".set noreorder\n\t"
		      ".set mips3\n\t"
		      "cache %1, (%0)\n\t"
		      ".set mips0\n\t"
		      ".set reorder"
		      :
		      : "r" (KSEG0ADDR(i)), "i" (Index_Store_Tag_SD));
	}
}

static __init void rm7k_sc_enable(void)
{
	void (*func)(void) = (void *) KSEG1ADDR(&__rm7k_sc_enable);

	if (read_c0_config() & 0x08)			/* CONF_SE */
		return;

	printk(KERN_INFO "Enabling secondary cache...");
	func();
}

static void rm7k_sc_disable(void)
{
	clear_c0_config(1<<3);				/* CONF_SE */
}

struct bcache_ops rm7k_sc_ops = {
	.bc_enable = rm7k_sc_enable,
	.bc_disable = rm7k_sc_disable,
	.bc_wback_inv = rm7k_sc_wback_inv,
	.bc_inv = rm7k_sc_inv
};

void __init rm7k_sc_init(void)
{
	unsigned int config = read_c0_config();

	if ((config >> 31) & 1)		/* Bit 31 set -> no S-Cache */
		return;

	printk(KERN_INFO "Secondary cache size %dK, linesize %d bytes.\n",
	       (scache_size >> 10), sc_lsize);

	if (!((config >> 3) & 1))	/* CONF_SE */
		rm7k_sc_enable();

	/*
	 * While we're at it let's deal with the tertiary cache.
	 */
	if (!((config >> 17) & 1)) {

		/*
		 * We can't enable the L3 cache yet. There may be board-specific
		 * magic necessary to turn it on, and blindly asking the CPU to
		 * start using it would may give cache errors.
		 *
		 * Also, board-specific knowledge may allow us to use the
		 * CACHE Flash_Invalidate_T instruction if the tag RAM supports
		 * it, and may specify the size of the L3 cache so we don't have
		 * to probe it.
		 */
		printk(KERN_INFO "Tertiary cache present, %s enabled\n",
		       config&(1<<12) ? "already" : "not (yet)");

		if ((config >> 12) & 1)
			rm7k_tcache_enabled = 1;
	}

	bcops = &rm7k_sc_ops;
}
