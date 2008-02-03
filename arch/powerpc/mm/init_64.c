/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
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
#include <linux/poison.h>

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

#include "mmu_decl.h"

#if PGTABLE_RANGE > USER_VSID_RANGE
#warning Limited user VSID range means pagetable space is wasted
#endif

#if (TASK_SIZE_USER64 < PGTABLE_RANGE) && (TASK_SIZE_USER64 < USER_VSID_RANGE)
#warning TASK_SIZE is smaller than it needs to be.
#endif

/* max amount of RAM to use */
unsigned long __max_memory;

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)__init_begin;
	for (; addr < (unsigned long)__init_end; addr += PAGE_SIZE) {
		memset((void *)addr, POISON_FREE_INITMEM, PAGE_SIZE);
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
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
		init_page_count(virt_to_page(start));
		free_page(start);
		totalram_pages++;
	}
}
#endif

#ifdef CONFIG_PROC_KCORE
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
			panic("%s: kmalloc failed\n", __FUNCTION__);

		kclist_add(kcore_mem, __va(base), size);
	}

	kclist_add(&kcore_vmem, (void *)VMALLOC_START, VMALLOC_END-VMALLOC_START);

	return 0;
}
module_init(setup_kcore);
#endif

static void zero_ctor(struct kmem_cache *cache, void *addr)
{
	memset(addr, 0, kmem_cache_size(cache));
}

static const unsigned int pgtable_cache_size[2] = {
	PGD_TABLE_SIZE, PMD_TABLE_SIZE
};
static const char *pgtable_cache_name[ARRAY_SIZE(pgtable_cache_size)] = {
#ifdef CONFIG_PPC_64K_PAGES
	"pgd_cache", "pmd_cache",
#else
	"pgd_cache", "pud_pmd_cache",
#endif /* CONFIG_PPC_64K_PAGES */
};

#ifdef CONFIG_HUGETLB_PAGE
/* Hugepages need one extra cache, initialized in hugetlbpage.c.  We
 * can't put into the tables above, because HPAGE_SHIFT is not compile
 * time constant. */
struct kmem_cache *pgtable_cache[ARRAY_SIZE(pgtable_cache_size)+1];
#else
struct kmem_cache *pgtable_cache[ARRAY_SIZE(pgtable_cache_size)];
#endif

void pgtable_cache_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pgtable_cache_size); i++) {
		int size = pgtable_cache_size[i];
		const char *name = pgtable_cache_name[i];

		pr_debug("Allocating page table cache %s (#%d) "
			"for size: %08x...\n", name, i, size);
		pgtable_cache[i] = kmem_cache_create(name,
						     size, size,
						     SLAB_PANIC,
						     zero_ctor);
	}
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/*
 * Given an address within the vmemmap, determine the pfn of the page that
 * represents the start of the section it is within.  Note that we have to
 * do this by hand as the proffered address may not be correctly aligned.
 * Subtraction of non-aligned pointers produces undefined results.
 */
unsigned long __meminit vmemmap_section_start(unsigned long page)
{
	unsigned long offset = page - ((unsigned long)(vmemmap));

	/* Return the pfn of the start of the section. */
	return (offset / sizeof(struct page)) & PAGE_SECTION_MASK;
}

/*
 * Check if this vmemmap page is already initialised.  If any section
 * which overlaps this vmemmap page is initialised then this page is
 * initialised already.
 */
int __meminit vmemmap_populated(unsigned long start, int page_size)
{
	unsigned long end = start + page_size;

	for (; start < end; start += (PAGES_PER_SECTION * sizeof(struct page)))
		if (pfn_valid(vmemmap_section_start(start)))
			return 1;

	return 0;
}

int __meminit vmemmap_populate(struct page *start_page,
					unsigned long nr_pages, int node)
{
	unsigned long mode_rw;
	unsigned long start = (unsigned long)start_page;
	unsigned long end = (unsigned long)(start_page + nr_pages);
	unsigned long page_size = 1 << mmu_psize_defs[mmu_linear_psize].shift;

	mode_rw = _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_COHERENT | PP_RWXX;

	/* Align to the page size of the linear mapping. */
	start = _ALIGN_DOWN(start, page_size);

	for (; start < end; start += page_size) {
		int mapped;
		void *p;

		if (vmemmap_populated(start, page_size))
			continue;

		p = vmemmap_alloc_block(page_size, node);
		if (!p)
			return -ENOMEM;

		pr_debug("vmemmap %08lx allocated at %p, physical %08lx.\n",
			start, p, __pa(p));

		mapped = htab_bolt_mapping(start, start + page_size,
					__pa(p), mode_rw, mmu_linear_psize,
					mmu_kernel_ssize);
		BUG_ON(mapped < 0);
	}

	return 0;
}
#endif
