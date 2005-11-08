/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Dave Engebretsen <engebret@us.ibm.com>
 *      Rework for PPC64 port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/idr.h>
#include <linux/nodemask.h>
#include <linux/module.h>

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/lmb.h>
#include <asm/rtas.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/uaccess.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/tlb.h>
#include <asm/eeh.h>
#include <asm/processor.h>
#include <asm/mmzone.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/iommu.h>
#include <asm/abs_addr.h>
#include <asm/vdso.h>
#include <asm/imalloc.h>

#if PGTABLE_RANGE > USER_VSID_RANGE
#warning Limited user VSID range means pagetable space is wasted
#endif

#if (TASK_SIZE_USER64 < PGTABLE_RANGE) && (TASK_SIZE_USER64 < USER_VSID_RANGE)
#warning TASK_SIZE is smaller than it needs to be.
#endif

unsigned long klimit = (unsigned long)_end;

/* max amount of RAM to use */
unsigned long __max_memory;

/* info on what we think the IO hole is */
unsigned long 	io_hole_start;
unsigned long	io_hole_size;

/*
 * Do very early mm setup.
 */
void __init mm_init_ppc64(void)
{
#ifndef CONFIG_PPC_ISERIES
	unsigned long i;
#endif

	ppc64_boot_msg(0x100, "MM Init");

	/* This is the story of the IO hole... please, keep seated,
	 * unfortunately, we are out of oxygen masks at the moment.
	 * So we need some rough way to tell where your big IO hole
	 * is. On pmac, it's between 2G and 4G, on POWER3, it's around
	 * that area as well, on POWER4 we don't have one, etc...
	 * We need that as a "hint" when sizing the TCE table on POWER3
	 * So far, the simplest way that seem work well enough for us it
	 * to just assume that the first discontinuity in our physical
	 * RAM layout is the IO hole. That may not be correct in the future
	 * (and isn't on iSeries but then we don't care ;)
	 */

#ifndef CONFIG_PPC_ISERIES
	for (i = 1; i < lmb.memory.cnt; i++) {
		unsigned long base, prevbase, prevsize;

		prevbase = lmb.memory.region[i-1].base;
		prevsize = lmb.memory.region[i-1].size;
		base = lmb.memory.region[i].base;
		if (base > (prevbase + prevsize)) {
			io_hole_start = prevbase + prevsize;
			io_hole_size = base  - (prevbase + prevsize);
			break;
		}
	}
#endif /* CONFIG_PPC_ISERIES */
	if (io_hole_start)
		printk("IO Hole assumed to be %lx -> %lx\n",
		       io_hole_start, io_hole_start + io_hole_size - 1);

	ppc64_boot_msg(0x100, "MM Init Done");
}

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)__init_begin;
	for (; addr < (unsigned long)__init_end; addr += PAGE_SIZE) {
		memset((void *)addr, 0xcc, PAGE_SIZE);
		ClearPageReserved(virt_to_page(addr));
		set_page_count(virt_to_page(addr), 1);
		free_page(addr);
		totalram_pages++;
	}
	printk ("Freeing unused kernel memory: %luk freed\n",
		((unsigned long)__init_end - (unsigned long)__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
#endif

static struct kcore_list kcore_vmem;

static int __init setup_kcore(void)
{
	int i;

	for (i=0; i < lmb.memory.cnt; i++) {
		unsigned long base, size;
		struct kcore_list *kcore_mem;

		base = lmb.memory.region[i].base;
		size = lmb.memory.region[i].size;

		/* GFP_ATOMIC to avoid might_sleep warnings during boot */
		kcore_mem = kmalloc(sizeof(struct kcore_list), GFP_ATOMIC);
		if (!kcore_mem)
			panic("mem_init: kmalloc failed\n");

		kclist_add(kcore_mem, __va(base), size);
	}

	kclist_add(&kcore_vmem, (void *)VMALLOC_START, VMALLOC_END-VMALLOC_START);

	return 0;
}
module_init(setup_kcore);

static void zero_ctor(void *addr, kmem_cache_t *cache, unsigned long flags)
{
	memset(addr, 0, kmem_cache_size(cache));
}

#ifdef CONFIG_PPC_64K_PAGES
static const int pgtable_cache_size[2] = {
	PTE_TABLE_SIZE, PGD_TABLE_SIZE
};
static const char *pgtable_cache_name[ARRAY_SIZE(pgtable_cache_size)] = {
	"pte_pmd_cache", "pgd_cache",
};
#else
static const int pgtable_cache_size[2] = {
	PTE_TABLE_SIZE, PMD_TABLE_SIZE
};
static const char *pgtable_cache_name[ARRAY_SIZE(pgtable_cache_size)] = {
	"pgd_pte_cache", "pud_pmd_cache",
};
#endif /* CONFIG_PPC_64K_PAGES */

kmem_cache_t *pgtable_cache[ARRAY_SIZE(pgtable_cache_size)];

void pgtable_cache_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pgtable_cache_size); i++) {
		int size = pgtable_cache_size[i];
		const char *name = pgtable_cache_name[i];

		pgtable_cache[i] = kmem_cache_create(name,
						     size, size,
						     SLAB_HWCACHE_ALIGN |
						     SLAB_MUST_HWCACHE_ALIGN,
						     zero_ctor,
						     NULL);
		if (! pgtable_cache[i])
			panic("pgtable_cache_init(): could not create %s!\n",
			      name);
	}
}
