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

#undef DEBUG

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
#include <linux/memblock.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/prom.h>
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
#include <asm/iommu.h>
#include <asm/vdso.h>

#include "mmu_decl.h"

#ifdef CONFIG_PPC_STD_MMU_64
#if PGTABLE_RANGE > USER_VSID_RANGE
#warning Limited user VSID range means pagetable space is wasted
#endif

#if (TASK_SIZE_USER64 < PGTABLE_RANGE) && (TASK_SIZE_USER64 < USER_VSID_RANGE)
#warning TASK_SIZE is smaller than it needs to be.
#endif
#endif /* CONFIG_PPC_STD_MMU_64 */

phys_addr_t memstart_addr = ~0;
EXPORT_SYMBOL_GPL(memstart_addr);
phys_addr_t kernstart_addr;
EXPORT_SYMBOL_GPL(kernstart_addr);

static void pgd_ctor(void *addr)
{
	memset(addr, 0, PGD_TABLE_SIZE);
}

static void pmd_ctor(void *addr)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	memset(addr, 0, PMD_TABLE_SIZE * 2);
#else
	memset(addr, 0, PMD_TABLE_SIZE);
#endif
}

struct kmem_cache *pgtable_cache[MAX_PGTABLE_INDEX_SIZE];

/*
 * Create a kmem_cache() for pagetables.  This is not used for PTE
 * pages - they're linked to struct page, come from the normal free
 * pages pool and have a different entry size (see real_pte_t) to
 * everything else.  Caches created by this function are used for all
 * the higher level pagetables, and for hugepage pagetables.
 */
void pgtable_cache_add(unsigned shift, void (*ctor)(void *))
{
	char *name;
	unsigned long table_size = sizeof(void *) << shift;
	unsigned long align = table_size;

	/* When batching pgtable pointers for RCU freeing, we store
	 * the index size in the low bits.  Table alignment must be
	 * big enough to fit it.
	 *
	 * Likewise, hugeapge pagetable pointers contain a (different)
	 * shift value in the low bits.  All tables must be aligned so
	 * as to leave enough 0 bits in the address to contain it. */
	unsigned long minalign = max(MAX_PGTABLE_INDEX_SIZE + 1,
				     HUGEPD_SHIFT_MASK + 1);
	struct kmem_cache *new;

	/* It would be nice if this was a BUILD_BUG_ON(), but at the
	 * moment, gcc doesn't seem to recognize is_power_of_2 as a
	 * constant expression, so so much for that. */
	BUG_ON(!is_power_of_2(minalign));
	BUG_ON((shift < 1) || (shift > MAX_PGTABLE_INDEX_SIZE));

	if (PGT_CACHE(shift))
		return; /* Already have a cache of this size */

	align = max_t(unsigned long, align, minalign);
	name = kasprintf(GFP_KERNEL, "pgtable-2^%d", shift);
	new = kmem_cache_create(name, table_size, align, 0, ctor);
	pgtable_cache[shift - 1] = new;
	pr_debug("Allocated pgtable cache for order %d\n", shift);
}


void pgtable_cache_init(void)
{
	pgtable_cache_add(PGD_INDEX_SIZE, pgd_ctor);
	pgtable_cache_add(PMD_CACHE_INDEX, pmd_ctor);
	if (!PGT_CACHE(PGD_INDEX_SIZE) || !PGT_CACHE(PMD_CACHE_INDEX))
		panic("Couldn't allocate pgtable caches");
	/* In all current configs, when the PUD index exists it's the
	 * same size as either the pgd or pmd index.  Verify that the
	 * initialization above has also created a PUD cache.  This
	 * will need re-examiniation if we add new possibilities for
	 * the pagetable layout. */
	BUG_ON(PUD_INDEX_SIZE && !PGT_CACHE(PUD_INDEX_SIZE));
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/*
 * Given an address within the vmemmap, determine the pfn of the page that
 * represents the start of the section it is within.  Note that we have to
 * do this by hand as the proffered address may not be correctly aligned.
 * Subtraction of non-aligned pointers produces undefined results.
 */
static unsigned long __meminit vmemmap_section_start(unsigned long page)
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
static int __meminit vmemmap_populated(unsigned long start, int page_size)
{
	unsigned long end = start + page_size;

	for (; start < end; start += (PAGES_PER_SECTION * sizeof(struct page)))
		if (pfn_valid(vmemmap_section_start(start)))
			return 1;

	return 0;
}

/* On hash-based CPUs, the vmemmap is bolted in the hash table.
 *
 * On Book3E CPUs, the vmemmap is currently mapped in the top half of
 * the vmalloc space using normal page tables, though the size of
 * pages encoded in the PTEs can be different
 */

#ifdef CONFIG_PPC_BOOK3E
static void __meminit vmemmap_create_mapping(unsigned long start,
					     unsigned long page_size,
					     unsigned long phys)
{
	/* Create a PTE encoding without page size */
	unsigned long i, flags = _PAGE_PRESENT | _PAGE_ACCESSED |
		_PAGE_KERNEL_RW;

	/* PTEs only contain page size encodings up to 32M */
	BUG_ON(mmu_psize_defs[mmu_vmemmap_psize].enc > 0xf);

	/* Encode the size in the PTE */
	flags |= mmu_psize_defs[mmu_vmemmap_psize].enc << 8;

	/* For each PTE for that area, map things. Note that we don't
	 * increment phys because all PTEs are of the large size and
	 * thus must have the low bits clear
	 */
	for (i = 0; i < page_size; i += PAGE_SIZE)
		BUG_ON(map_kernel_page(start + i, phys, flags));
}
#else /* CONFIG_PPC_BOOK3E */
static void __meminit vmemmap_create_mapping(unsigned long start,
					     unsigned long page_size,
					     unsigned long phys)
{
	int  mapped = htab_bolt_mapping(start, start + page_size, phys,
					pgprot_val(PAGE_KERNEL),
					mmu_vmemmap_psize,
					mmu_kernel_ssize);
	BUG_ON(mapped < 0);
}
#endif /* CONFIG_PPC_BOOK3E */

struct vmemmap_backing *vmemmap_list;

static __meminit struct vmemmap_backing * vmemmap_list_alloc(int node)
{
	static struct vmemmap_backing *next;
	static int num_left;

	/* allocate a page when required and hand out chunks */
	if (!next || !num_left) {
		next = vmemmap_alloc_block(PAGE_SIZE, node);
		if (unlikely(!next)) {
			WARN_ON(1);
			return NULL;
		}
		num_left = PAGE_SIZE / sizeof(struct vmemmap_backing);
	}

	num_left--;

	return next++;
}

static __meminit void vmemmap_list_populate(unsigned long phys,
					    unsigned long start,
					    int node)
{
	struct vmemmap_backing *vmem_back;

	vmem_back = vmemmap_list_alloc(node);
	if (unlikely(!vmem_back)) {
		WARN_ON(1);
		return;
	}

	vmem_back->phys = phys;
	vmem_back->virt_addr = start;
	vmem_back->list = vmemmap_list;

	vmemmap_list = vmem_back;
}

int __meminit vmemmap_populate(unsigned long start, unsigned long end, int node)
{
	unsigned long page_size = 1 << mmu_psize_defs[mmu_vmemmap_psize].shift;

	/* Align to the page size of the linear mapping. */
	start = _ALIGN_DOWN(start, page_size);

	pr_debug("vmemmap_populate %lx..%lx, node %d\n", start, end, node);

	for (; start < end; start += page_size) {
		void *p;

		if (vmemmap_populated(start, page_size))
			continue;

		p = vmemmap_alloc_block(page_size, node);
		if (!p)
			return -ENOMEM;

		vmemmap_list_populate(__pa(p), start, node);

		pr_debug("      * %016lx..%016lx allocated at %p\n",
			 start, start + page_size, p);

		vmemmap_create_mapping(start, page_size, __pa(p));
	}

	return 0;
}

void vmemmap_free(unsigned long start, unsigned long end)
{
}

void register_page_bootmem_memmap(unsigned long section_nr,
				  struct page *start_page, unsigned long size)
{
}

/*
 * We do not have access to the sparsemem vmemmap, so we fallback to
 * walking the list of sparsemem blocks which we already maintain for
 * the sake of crashdump. In the long run, we might want to maintain
 * a tree if performance of that linear walk becomes a problem.
 *
 * realmode_pfn_to_page functions can fail due to:
 * 1) As real sparsemem blocks do not lay in RAM continously (they
 * are in virtual address space which is not available in the real mode),
 * the requested page struct can be split between blocks so get_page/put_page
 * may fail.
 * 2) When huge pages are used, the get_page/put_page API will fail
 * in real mode as the linked addresses in the page struct are virtual
 * too.
 */
struct page *realmode_pfn_to_page(unsigned long pfn)
{
	struct vmemmap_backing *vmem_back;
	struct page *page;
	unsigned long page_size = 1 << mmu_psize_defs[mmu_vmemmap_psize].shift;
	unsigned long pg_va = (unsigned long) pfn_to_page(pfn);

	for (vmem_back = vmemmap_list; vmem_back; vmem_back = vmem_back->list) {
		if (pg_va < vmem_back->virt_addr)
			continue;

		/* Check that page struct is not split between real pages */
		if ((pg_va + sizeof(struct page)) >
				(vmem_back->virt_addr + page_size))
			return NULL;

		page = (struct page *) (vmem_back->phys + pg_va -
				vmem_back->virt_addr);
		return page;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(realmode_pfn_to_page);

#elif defined(CONFIG_FLATMEM)

struct page *realmode_pfn_to_page(unsigned long pfn)
{
	struct page *page = pfn_to_page(pfn);
	return page;
}
EXPORT_SYMBOL_GPL(realmode_pfn_to_page);

#endif /* CONFIG_SPARSEMEM_VMEMMAP/CONFIG_FLATMEM */
