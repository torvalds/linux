/*
 * arch/xtensa/mm/init.c
 *
 * Derived from MIPS, PPC.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Chris Zankel	<chris@zankel.net>
 * Joe Taylor	<joe@tensilica.com, joetylr@yahoo.com>
 * Marc Gauthier
 * Kevin Chea
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/bootmem.h>
#include <linux/swap.h>

#include <asm/pgtable.h>
#include <asm/bootparam.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>


#define DEBUG 0

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);
//static DEFINE_SPINLOCK(tlb_lock);

/*
 * This flag is used to indicate that the page was mapped and modified in
 * kernel space, so the cache is probably dirty at that address.
 * If cache aliasing is enabled and the page color mismatches, update_mmu_cache
 * synchronizes the caches if this bit is set.
 */

#define PG_cache_clean PG_arch_1

/* References to section boundaries */

extern char _ftext, _etext, _fdata, _edata, _rodata_end;
extern char __init_begin, __init_end;

/*
 * mem_reserve(start, end, must_exist)
 *
 * Reserve some memory from the memory pool.
 *
 * Parameters:
 *  start	Start of region,
 *  end		End of region,
 *  must_exist	Must exist in memory pool.
 *
 * Returns:
 *  0 (memory area couldn't be mapped)
 * -1 (success)
 */

int __init mem_reserve(unsigned long start, unsigned long end, int must_exist)
{
	int i;

	if (start == end)
		return 0;

	start = start & PAGE_MASK;
	end = PAGE_ALIGN(end);

	for (i = 0; i < sysmem.nr_banks; i++)
		if (start < sysmem.bank[i].end
		    && end >= sysmem.bank[i].start)
			break;

	if (i == sysmem.nr_banks) {
		if (must_exist)
			printk (KERN_WARNING "mem_reserve: [0x%0lx, 0x%0lx) "
				"not in any region!\n", start, end);
		return 0;
	}

	if (start > sysmem.bank[i].start) {
		if (end < sysmem.bank[i].end) {
			/* split entry */
			if (sysmem.nr_banks >= SYSMEM_BANKS_MAX)
				panic("meminfo overflow\n");
			sysmem.bank[sysmem.nr_banks].start = end;
			sysmem.bank[sysmem.nr_banks].end = sysmem.bank[i].end;
			sysmem.nr_banks++;
		}
		sysmem.bank[i].end = start;
	} else {
		if (end < sysmem.bank[i].end)
			sysmem.bank[i].start = end;
		else {
			/* remove entry */
			sysmem.nr_banks--;
			sysmem.bank[i].start = sysmem.bank[sysmem.nr_banks].start;
			sysmem.bank[i].end   = sysmem.bank[sysmem.nr_banks].end;
		}
	}
	return -1;
}


/*
 * Initialize the bootmem system and give it all the memory we have available.
 */

void __init bootmem_init(void)
{
	unsigned long pfn;
	unsigned long bootmap_start, bootmap_size;
	int i;

	max_low_pfn = max_pfn = 0;
	min_low_pfn = ~0;

	for (i=0; i < sysmem.nr_banks; i++) {
		pfn = PAGE_ALIGN(sysmem.bank[i].start) >> PAGE_SHIFT;
		if (pfn < min_low_pfn)
			min_low_pfn = pfn;
		pfn = PAGE_ALIGN(sysmem.bank[i].end - 1) >> PAGE_SHIFT;
		if (pfn > max_pfn)
			max_pfn = pfn;
	}

	if (min_low_pfn > max_pfn)
		panic("No memory found!\n");

	max_low_pfn = max_pfn < MAX_LOW_MEMORY >> PAGE_SHIFT ?
		max_pfn : MAX_LOW_MEMORY >> PAGE_SHIFT;

	/* Find an area to use for the bootmem bitmap. */

	bootmap_size = bootmem_bootmap_pages(max_low_pfn) << PAGE_SHIFT;
	bootmap_start = ~0;

	for (i=0; i<sysmem.nr_banks; i++)
		if (sysmem.bank[i].end - sysmem.bank[i].start >= bootmap_size) {
			bootmap_start = sysmem.bank[i].start;
			break;
		}

	if (bootmap_start == ~0UL)
		panic("Cannot find %ld bytes for bootmap\n", bootmap_size);

	/* Reserve the bootmem bitmap area */

	mem_reserve(bootmap_start, bootmap_start + bootmap_size, 1);
	bootmap_size = init_bootmem_node(NODE_DATA(0), min_low_pfn,
					 bootmap_start >> PAGE_SHIFT,
					 max_low_pfn);

	/* Add all remaining memory pieces into the bootmem map */

	for (i=0; i<sysmem.nr_banks; i++)
		free_bootmem(sysmem.bank[i].start,
			     sysmem.bank[i].end - sysmem.bank[i].start);

}


void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];
	int i;

	/* All pages are DMA-able, so we put them all in the DMA zone. */

	zones_size[ZONE_DMA] = max_low_pfn;
	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

#ifdef CONFIG_HIGHMEM
	zones_size[ZONE_HIGHMEM] = max_pfn - max_low_pfn;
#endif

	/* Initialize the kernel's page tables. */

	memset(swapper_pg_dir, 0, PAGE_SIZE);

	free_area_init(zones_size);
}

/*
 * Flush the mmu and reset associated register to default values.
 */

void __init init_mmu (void)
{
	/* Writing zeros to the <t>TLBCFG special registers ensure
	 * that valid values exist in the register.  For existing
	 * PGSZID<w> fields, zero selects the first element of the
	 * page-size array.  For nonexistant PGSZID<w> fields, zero is
	 * the best value to write.  Also, when changing PGSZID<w>
	 * fields, the corresponding TLB must be flushed.
	 */
	set_itlbcfg_register (0);
	set_dtlbcfg_register (0);
	flush_tlb_all ();

	/* Set rasid register to a known value. */

	set_rasid_register (ASID_ALL_RESERVED);

	/* Set PTEVADDR special register to the start of the page
	 * table, which is in kernel mappable space (ie. not
	 * statically mapped).  This register's value is undefined on
	 * reset.
	 */
	set_ptevaddr_register (PGTABLE_START);
}

/*
 * Initialize memory pages.
 */

void __init mem_init(void)
{
	unsigned long codesize, reservedpages, datasize, initsize;
	unsigned long highmemsize, tmp, ram;

	max_mapnr = num_physpages = max_low_pfn;
	high_memory = (void *) __va(max_mapnr << PAGE_SHIFT);
	highmemsize = 0;

#ifdef CONFIG_HIGHMEM
#error HIGHGMEM not implemented in init.c
#endif

	totalram_pages += free_all_bootmem();

	reservedpages = ram = 0;
	for (tmp = 0; tmp < max_low_pfn; tmp++) {
		ram++;
		if (PageReserved(mem_map+tmp))
			reservedpages++;
	}

	codesize =  (unsigned long) &_etext - (unsigned long) &_ftext;
	datasize =  (unsigned long) &_edata - (unsigned long) &_fdata;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk("Memory: %luk/%luk available (%ldk kernel code, %ldk reserved, "
	       "%ldk data, %ldk init %ldk highmem)\n",
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
	       ram << (PAGE_SHIFT-10),
	       codesize >> 10,
	       reservedpages << (PAGE_SHIFT-10),
	       datasize >> 10,
	       initsize >> 10,
	       highmemsize >> 10);
}

void
free_reserved_mem(void *start, void *end)
{
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page((unsigned long)start);
		totalram_pages++;
	}
}

#ifdef CONFIG_BLK_DEV_INITRD
extern int initrd_is_mapped;

void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (initrd_is_mapped) {
		free_reserved_mem((void*)start, (void*)end);
		printk ("Freeing initrd memory: %ldk freed\n",(end-start)>>10);
	}
}
#endif

void free_initmem(void)
{
	free_reserved_mem(&__init_begin, &__init_end);
	printk("Freeing unused kernel memory: %dk freed\n",
	       (&__init_end - &__init_begin) >> 10);
}

void show_mem(void)
{
	int i, free = 0, total = 0, reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!page_count(mem_map + i))
			free++;
		else
			shared += page_count(mem_map + i) - 1;
	}
	printk("%d pages of RAM\n", total);
	printk("%d reserved pages\n", reserved);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n",cached);
	printk("%d free pages\n", free);
}

/* ------------------------------------------------------------------------- */

#if (DCACHE_WAY_SIZE > PAGE_SIZE)

/*
 * With cache aliasing, the page color of the page in kernel space and user
 * space might mismatch. We temporarily map the page to a different virtual
 * address with the same color and clear the page there.
 */

void clear_user_page(void *kaddr, unsigned long vaddr, struct page* page)
{

  	/*  There shouldn't be any entries for this page. */

	__flush_invalidate_dcache_page_phys(__pa(page_address(page)));

	if (!PAGE_COLOR_EQ(vaddr, kaddr)) {
		unsigned long v, p;

		/* Temporarily map page to DTLB_WAY_DCACHE_ALIAS0. */

		spin_lock(&tlb_lock);

		p = (unsigned long)pte_val((mk_pte(page,PAGE_KERNEL)));
		kaddr = (void*)PAGE_COLOR_MAP0(vaddr);
		v = (unsigned long)kaddr | DTLB_WAY_DCACHE_ALIAS0;
		__asm__ __volatile__("wdtlb %0,%1; dsync" : :"a" (p), "a" (v));

		clear_page(kaddr);

		spin_unlock(&tlb_lock);
	} else {
		clear_page(kaddr);
	}

	/* We need to make sure that i$ and d$ are coherent. */

	clear_bit(PG_cache_clean, &page->flags);
}

/*
 * With cache aliasing, we have to make sure that the page color of the page
 * in kernel space matches that of the virtual user address before we read
 * the page. If the page color differ, we create a temporary DTLB entry with
 * the corrent page color and use this 'temporary' address as the source.
 * We then use the same approach as in clear_user_page and copy the data
 * to the kernel space and clear the PG_cache_clean bit to synchronize caches
 * later.
 *
 * Note:
 * Instead of using another 'way' for the temporary DTLB entry, we could
 * probably use the same entry that points to the kernel address (after
 * saving the original value and restoring it when we are done).
 */

void copy_user_page(void* to, void* from, unsigned long vaddr,
    		    struct page* to_page)
{
	/* There shouldn't be any entries for the new page. */

	__flush_invalidate_dcache_page_phys(__pa(page_address(to_page)));

	spin_lock(&tlb_lock);

	if (!PAGE_COLOR_EQ(vaddr, from)) {
		unsigned long v, p, t;

		__asm__ __volatile__ ("pdtlb %1,%2; rdtlb1 %0,%1"
				      : "=a"(p), "=a"(t) : "a"(from));
		from = (void*)PAGE_COLOR_MAP0(vaddr);
		v = (unsigned long)from | DTLB_WAY_DCACHE_ALIAS0;
		__asm__ __volatile__ ("wdtlb %0,%1; dsync" ::"a" (p), "a" (v));
	}

	if (!PAGE_COLOR_EQ(vaddr, to)) {
		unsigned long v, p;

		p = (unsigned long)pte_val((mk_pte(to_page,PAGE_KERNEL)));
		to = (void*)PAGE_COLOR_MAP1(vaddr);
		v = (unsigned long)to | DTLB_WAY_DCACHE_ALIAS1;
		__asm__ __volatile__ ("wdtlb %0,%1; dsync" ::"a" (p), "a" (v));
	}
	copy_page(to, from);

	spin_unlock(&tlb_lock);

	/* We need to make sure that i$ and d$ are coherent. */

	clear_bit(PG_cache_clean, &to_page->flags);
}



/*
 * Any time the kernel writes to a user page cache page, or it is about to
 * read from a page cache page this routine is called.
 *
 * Note:
 * The kernel currently only provides one architecture bit in the page
 * flags that we use for I$/D$ coherency. Maybe, in future, we can
 * use a sepearte bit for deferred dcache aliasing:
 * If the page is not mapped yet, we only need to set a flag,
 * if mapped, we need to invalidate the page.
 */
// FIXME: we probably need this for WB caches not only for Page Coloring..

void flush_dcache_page(struct page *page)
{
	unsigned long addr = __pa(page_address(page));
	struct address_space *mapping = page_mapping(page);

	__flush_invalidate_dcache_page_phys(addr);

	if (!test_bit(PG_cache_clean, &page->flags))
		return;

	/* If this page hasn't been mapped, yet, handle I$/D$ coherency later.*/
#if 0
	if (mapping && !mapping_mapped(mapping))
		clear_bit(PG_cache_clean, &page->flags);
	else
#endif
		__invalidate_icache_page_phys(addr);
}

void flush_cache_range(struct vm_area_struct* vma, unsigned long s,
		       unsigned long e)
{
	__flush_invalidate_cache_all();
}

void flush_cache_page(struct vm_area_struct* vma, unsigned long address,
    		      unsigned long pfn)
{
	struct page *page = pfn_to_page(pfn);

	/* Remove any entry for the old mapping. */

	if (current->active_mm == vma->vm_mm) {
		unsigned long addr = __pa(page_address(page));
		__flush_invalidate_dcache_page_phys(addr);
		if ((vma->vm_flags & VM_EXEC) != 0)
			__invalidate_icache_page_phys(addr);
	} else {
		BUG();
	}
}

#endif	/* (DCACHE_WAY_SIZE > PAGE_SIZE) */


pte_t* pte_alloc_one_kernel (struct mm_struct* mm, unsigned long addr)
{
	pte_t* pte = (pte_t*)__get_free_pages(GFP_KERNEL|__GFP_REPEAT, 0);
	if (likely(pte)) {
	       	pte_t* ptep = (pte_t*)(pte_val(*pte) + PAGE_OFFSET);
		int i;
		for (i = 0; i < 1024; i++, ptep++)
			pte_clear(mm, addr, ptep);
	}
	return pte;
}

struct page* pte_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	struct page *page;

	page = alloc_pages(GFP_KERNEL | __GFP_REPEAT, 0);

	if (likely(page)) {
		pte_t* ptep = kmap_atomic(page, KM_USER0);
		int i;

		for (i = 0; i < 1024; i++, ptep++)
			pte_clear(mm, addr, ptep);

		kunmap_atomic(ptep, KM_USER0);
	}
	return page;
}


/*
 * Handle D$/I$ coherency.
 *
 * Note:
 * We only have one architecture bit for the page flags, so we cannot handle
 * cache aliasing, yet.
 */

void
update_mmu_cache(struct vm_area_struct * vma, unsigned long addr, pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);
	struct page *page;
	unsigned long vaddr = addr & PAGE_MASK;

	if (!pfn_valid(pfn))
		return;

	page = pfn_to_page(pfn);

	invalidate_itlb_mapping(addr);
	invalidate_dtlb_mapping(addr);

	/* We have a new mapping. Use it. */

	write_dtlb_entry(pte, dtlb_probe(addr));

	/* If the processor can execute from this page, synchronize D$/I$. */

	if ((vma->vm_flags & VM_EXEC) != 0) {

		write_itlb_entry(pte, itlb_probe(addr));

		/* Synchronize caches, if not clean. */

		if (!test_and_set_bit(PG_cache_clean, &page->flags)) {
			__flush_dcache_page(vaddr);
			__invalidate_icache_page(vaddr);
		}
	}
}

