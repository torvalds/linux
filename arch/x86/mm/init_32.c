// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/pci.h>
#include <linux/pfn.h>
#include <linux/poison.h>
#include <linux/memblock.h>
#include <linux/proc_fs.h>
#include <linux/memory_hotplug.h>
#include <linux/initrd.h>
#include <linux/cpumask.h>
#include <linux/gfp.h>

#include <asm/asm.h>
#include <asm/bios_ebda.h>
#include <asm/processor.h>
#include <linux/uaccess.h>
#include <asm/dma.h>
#include <asm/fixmap.h>
#include <asm/e820/api.h>
#include <asm/apic.h>
#include <asm/bugs.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/olpc_ofw.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/paravirt.h>
#include <asm/setup.h>
#include <asm/set_memory.h>
#include <asm/page_types.h>
#include <asm/cpu_entry_area.h>
#include <asm/init.h>
#include <asm/pgtable_areas.h>
#include <asm/numa.h>

#include "mm_internal.h"

unsigned long highstart_pfn, highend_pfn;

bool __read_mostly __vmalloc_start_set = false;

/*
 * Creates a middle page table and puts a pointer to it in the
 * given global directory entry. This only returns the gd entry
 * in non-PAE compilation mode, since the middle layer is folded.
 */
static pmd_t * __init one_md_table_init(pgd_t *pgd)
{
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd_table;

#ifdef CONFIG_X86_PAE
	if (!(pgd_val(*pgd) & _PAGE_PRESENT)) {
		pmd_table = (pmd_t *)alloc_low_page();
		paravirt_alloc_pmd(&init_mm, __pa(pmd_table) >> PAGE_SHIFT);
		set_pgd(pgd, __pgd(__pa(pmd_table) | _PAGE_PRESENT));
		p4d = p4d_offset(pgd, 0);
		pud = pud_offset(p4d, 0);
		BUG_ON(pmd_table != pmd_offset(pud, 0));

		return pmd_table;
	}
#endif
	p4d = p4d_offset(pgd, 0);
	pud = pud_offset(p4d, 0);
	pmd_table = pmd_offset(pud, 0);

	return pmd_table;
}

/*
 * Create a page table and place a pointer to it in a middle page
 * directory entry:
 */
static pte_t * __init one_page_table_init(pmd_t *pmd)
{
	if (!(pmd_val(*pmd) & _PAGE_PRESENT)) {
		pte_t *page_table = (pte_t *)alloc_low_page();

		paravirt_alloc_pte(&init_mm, __pa(page_table) >> PAGE_SHIFT);
		set_pmd(pmd, __pmd(__pa(page_table) | _PAGE_TABLE));
		BUG_ON(page_table != pte_offset_kernel(pmd, 0));
	}

	return pte_offset_kernel(pmd, 0);
}

pmd_t * __init populate_extra_pmd(unsigned long vaddr)
{
	int pgd_idx = pgd_index(vaddr);
	int pmd_idx = pmd_index(vaddr);

	return one_md_table_init(swapper_pg_dir + pgd_idx) + pmd_idx;
}

pte_t * __init populate_extra_pte(unsigned long vaddr)
{
	int pte_idx = pte_index(vaddr);
	pmd_t *pmd;

	pmd = populate_extra_pmd(vaddr);
	return one_page_table_init(pmd) + pte_idx;
}

static unsigned long __init
page_table_range_init_count(unsigned long start, unsigned long end)
{
	unsigned long count = 0;
#ifdef CONFIG_HIGHMEM
	int pmd_idx_kmap_begin = fix_to_virt(FIX_KMAP_END) >> PMD_SHIFT;
	int pmd_idx_kmap_end = fix_to_virt(FIX_KMAP_BEGIN) >> PMD_SHIFT;
	int pgd_idx, pmd_idx;
	unsigned long vaddr;

	if (pmd_idx_kmap_begin == pmd_idx_kmap_end)
		return 0;

	vaddr = start;
	pgd_idx = pgd_index(vaddr);
	pmd_idx = pmd_index(vaddr);

	for ( ; (pgd_idx < PTRS_PER_PGD) && (vaddr != end); pgd_idx++) {
		for (; (pmd_idx < PTRS_PER_PMD) && (vaddr != end);
							pmd_idx++) {
			if ((vaddr >> PMD_SHIFT) >= pmd_idx_kmap_begin &&
			    (vaddr >> PMD_SHIFT) <= pmd_idx_kmap_end)
				count++;
			vaddr += PMD_SIZE;
		}
		pmd_idx = 0;
	}
#endif
	return count;
}

static pte_t *__init page_table_kmap_check(pte_t *pte, pmd_t *pmd,
					   unsigned long vaddr, pte_t *lastpte,
					   void **adr)
{
#ifdef CONFIG_HIGHMEM
	/*
	 * Something (early fixmap) may already have put a pte
	 * page here, which causes the page table allocation
	 * to become nonlinear. Attempt to fix it, and if it
	 * is still nonlinear then we have to bug.
	 */
	int pmd_idx_kmap_begin = fix_to_virt(FIX_KMAP_END) >> PMD_SHIFT;
	int pmd_idx_kmap_end = fix_to_virt(FIX_KMAP_BEGIN) >> PMD_SHIFT;

	if (pmd_idx_kmap_begin != pmd_idx_kmap_end
	    && (vaddr >> PMD_SHIFT) >= pmd_idx_kmap_begin
	    && (vaddr >> PMD_SHIFT) <= pmd_idx_kmap_end) {
		pte_t *newpte;
		int i;

		BUG_ON(after_bootmem);
		newpte = *adr;
		for (i = 0; i < PTRS_PER_PTE; i++)
			set_pte(newpte + i, pte[i]);
		*adr = (void *)(((unsigned long)(*adr)) + PAGE_SIZE);

		paravirt_alloc_pte(&init_mm, __pa(newpte) >> PAGE_SHIFT);
		set_pmd(pmd, __pmd(__pa(newpte)|_PAGE_TABLE));
		BUG_ON(newpte != pte_offset_kernel(pmd, 0));
		__flush_tlb_all();

		paravirt_release_pte(__pa(pte) >> PAGE_SHIFT);
		pte = newpte;
	}
	BUG_ON(vaddr < fix_to_virt(FIX_KMAP_BEGIN - 1)
	       && vaddr > fix_to_virt(FIX_KMAP_END)
	       && lastpte && lastpte + PTRS_PER_PTE != pte);
#endif
	return pte;
}

/*
 * This function initializes a certain range of kernel virtual memory
 * with new bootmem page tables, everywhere page tables are missing in
 * the given range.
 *
 * NOTE: The pagetables are allocated contiguous on the physical space
 * so we can cache the place of the first one and move around without
 * checking the pgd every time.
 */
static void __init
page_table_range_init(unsigned long start, unsigned long end, pgd_t *pgd_base)
{
	int pgd_idx, pmd_idx;
	unsigned long vaddr;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte = NULL;
	unsigned long count = page_table_range_init_count(start, end);
	void *adr = NULL;

	if (count)
		adr = alloc_low_pages(count);

	vaddr = start;
	pgd_idx = pgd_index(vaddr);
	pmd_idx = pmd_index(vaddr);
	pgd = pgd_base + pgd_idx;

	for ( ; (pgd_idx < PTRS_PER_PGD) && (vaddr != end); pgd++, pgd_idx++) {
		pmd = one_md_table_init(pgd);
		pmd = pmd + pmd_index(vaddr);
		for (; (pmd_idx < PTRS_PER_PMD) && (vaddr != end);
							pmd++, pmd_idx++) {
			pte = page_table_kmap_check(one_page_table_init(pmd),
						    pmd, vaddr, pte, &adr);

			vaddr += PMD_SIZE;
		}
		pmd_idx = 0;
	}
}

/*
 * The <linux/kallsyms.h> already defines is_kernel_text,
 * using '__' prefix not to get in conflict.
 */
static inline int __is_kernel_text(unsigned long addr)
{
	if (addr >= (unsigned long)_text && addr <= (unsigned long)__init_end)
		return 1;
	return 0;
}

/*
 * This maps the physical memory to kernel virtual address space, a total
 * of max_low_pfn pages, by creating page tables starting from address
 * PAGE_OFFSET:
 */
unsigned long __init
kernel_physical_mapping_init(unsigned long start,
			     unsigned long end,
			     unsigned long page_size_mask,
			     pgprot_t prot)
{
	int use_pse = page_size_mask == (1<<PG_LEVEL_2M);
	unsigned long last_map_addr = end;
	unsigned long start_pfn, end_pfn;
	pgd_t *pgd_base = swapper_pg_dir;
	int pgd_idx, pmd_idx, pte_ofs;
	unsigned long pfn;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned pages_2m, pages_4k;
	int mapping_iter;

	start_pfn = start >> PAGE_SHIFT;
	end_pfn = end >> PAGE_SHIFT;

	/*
	 * First iteration will setup identity mapping using large/small pages
	 * based on use_pse, with other attributes same as set by
	 * the early code in head_32.S
	 *
	 * Second iteration will setup the appropriate attributes (NX, GLOBAL..)
	 * as desired for the kernel identity mapping.
	 *
	 * This two pass mechanism conforms to the TLB app note which says:
	 *
	 *     "Software should not write to a paging-structure entry in a way
	 *      that would change, for any linear address, both the page size
	 *      and either the page frame or attributes."
	 */
	mapping_iter = 1;

	if (!boot_cpu_has(X86_FEATURE_PSE))
		use_pse = 0;

repeat:
	pages_2m = pages_4k = 0;
	pfn = start_pfn;
	pgd_idx = pgd_index((pfn<<PAGE_SHIFT) + PAGE_OFFSET);
	pgd = pgd_base + pgd_idx;
	for (; pgd_idx < PTRS_PER_PGD; pgd++, pgd_idx++) {
		pmd = one_md_table_init(pgd);

		if (pfn >= end_pfn)
			continue;
#ifdef CONFIG_X86_PAE
		pmd_idx = pmd_index((pfn<<PAGE_SHIFT) + PAGE_OFFSET);
		pmd += pmd_idx;
#else
		pmd_idx = 0;
#endif
		for (; pmd_idx < PTRS_PER_PMD && pfn < end_pfn;
		     pmd++, pmd_idx++) {
			unsigned int addr = pfn * PAGE_SIZE + PAGE_OFFSET;

			/*
			 * Map with big pages if possible, otherwise
			 * create normal page tables:
			 */
			if (use_pse) {
				unsigned int addr2;
				pgprot_t prot = PAGE_KERNEL_LARGE;
				/*
				 * first pass will use the same initial
				 * identity mapping attribute + _PAGE_PSE.
				 */
				pgprot_t init_prot =
					__pgprot(PTE_IDENT_ATTR |
						 _PAGE_PSE);

				pfn &= PMD_MASK >> PAGE_SHIFT;
				addr2 = (pfn + PTRS_PER_PTE-1) * PAGE_SIZE +
					PAGE_OFFSET + PAGE_SIZE-1;

				if (__is_kernel_text(addr) ||
				    __is_kernel_text(addr2))
					prot = PAGE_KERNEL_LARGE_EXEC;

				pages_2m++;
				if (mapping_iter == 1)
					set_pmd(pmd, pfn_pmd(pfn, init_prot));
				else
					set_pmd(pmd, pfn_pmd(pfn, prot));

				pfn += PTRS_PER_PTE;
				continue;
			}
			pte = one_page_table_init(pmd);

			pte_ofs = pte_index((pfn<<PAGE_SHIFT) + PAGE_OFFSET);
			pte += pte_ofs;
			for (; pte_ofs < PTRS_PER_PTE && pfn < end_pfn;
			     pte++, pfn++, pte_ofs++, addr += PAGE_SIZE) {
				pgprot_t prot = PAGE_KERNEL;
				/*
				 * first pass will use the same initial
				 * identity mapping attribute.
				 */
				pgprot_t init_prot = __pgprot(PTE_IDENT_ATTR);

				if (__is_kernel_text(addr))
					prot = PAGE_KERNEL_EXEC;

				pages_4k++;
				if (mapping_iter == 1) {
					set_pte(pte, pfn_pte(pfn, init_prot));
					last_map_addr = (pfn << PAGE_SHIFT) + PAGE_SIZE;
				} else
					set_pte(pte, pfn_pte(pfn, prot));
			}
		}
	}
	if (mapping_iter == 1) {
		/*
		 * update direct mapping page count only in the first
		 * iteration.
		 */
		update_page_count(PG_LEVEL_2M, pages_2m);
		update_page_count(PG_LEVEL_4K, pages_4k);

		/*
		 * local global flush tlb, which will flush the previous
		 * mappings present in both small and large page TLB's.
		 */
		__flush_tlb_all();

		/*
		 * Second iteration will set the actual desired PTE attributes.
		 */
		mapping_iter = 2;
		goto repeat;
	}
	return last_map_addr;
}

#ifdef CONFIG_HIGHMEM
static void __init permanent_kmaps_init(pgd_t *pgd_base)
{
	unsigned long vaddr = PKMAP_BASE;

	page_table_range_init(vaddr, vaddr + PAGE_SIZE*LAST_PKMAP, pgd_base);

	pkmap_page_table = virt_to_kpte(vaddr);
}

void __init add_highpages_with_active_regions(int nid,
			 unsigned long start_pfn, unsigned long end_pfn)
{
	phys_addr_t start, end;
	u64 i;

	for_each_free_mem_range(i, nid, MEMBLOCK_NONE, &start, &end, NULL) {
		unsigned long pfn = clamp_t(unsigned long, PFN_UP(start),
					    start_pfn, end_pfn);
		unsigned long e_pfn = clamp_t(unsigned long, PFN_DOWN(end),
					      start_pfn, end_pfn);
		for ( ; pfn < e_pfn; pfn++)
			if (pfn_valid(pfn))
				free_highmem_page(pfn_to_page(pfn));
	}
}
#else
static inline void permanent_kmaps_init(pgd_t *pgd_base)
{
}
#endif /* CONFIG_HIGHMEM */

void __init sync_initial_page_table(void)
{
	clone_pgd_range(initial_page_table + KERNEL_PGD_BOUNDARY,
			swapper_pg_dir     + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	/*
	 * sync back low identity map too.  It is used for example
	 * in the 32-bit EFI stub.
	 */
	clone_pgd_range(initial_page_table,
			swapper_pg_dir     + KERNEL_PGD_BOUNDARY,
			min(KERNEL_PGD_PTRS, KERNEL_PGD_BOUNDARY));
}

void __init native_pagetable_init(void)
{
	unsigned long pfn, va;
	pgd_t *pgd, *base = swapper_pg_dir;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	/*
	 * Remove any mappings which extend past the end of physical
	 * memory from the boot time page table.
	 * In virtual address space, we should have at least two pages
	 * from VMALLOC_END to pkmap or fixmap according to VMALLOC_END
	 * definition. And max_low_pfn is set to VMALLOC_END physical
	 * address. If initial memory mapping is doing right job, we
	 * should have pte used near max_low_pfn or one pmd is not present.
	 */
	for (pfn = max_low_pfn; pfn < 1<<(32-PAGE_SHIFT); pfn++) {
		va = PAGE_OFFSET + (pfn<<PAGE_SHIFT);
		pgd = base + pgd_index(va);
		if (!pgd_present(*pgd))
			break;

		p4d = p4d_offset(pgd, va);
		pud = pud_offset(p4d, va);
		pmd = pmd_offset(pud, va);
		if (!pmd_present(*pmd))
			break;

		/* should not be large page here */
		if (pmd_large(*pmd)) {
			pr_warn("try to clear pte for ram above max_low_pfn: pfn: %lx pmd: %p pmd phys: %lx, but pmd is big page and is not using pte !\n",
				pfn, pmd, __pa(pmd));
			BUG_ON(1);
		}

		pte = pte_offset_kernel(pmd, va);
		if (!pte_present(*pte))
			break;

		printk(KERN_DEBUG "clearing pte for ram above max_low_pfn: pfn: %lx pmd: %p pmd phys: %lx pte: %p pte phys: %lx\n",
				pfn, pmd, __pa(pmd), pte, __pa(pte));
		pte_clear(NULL, va, pte);
	}
	paravirt_alloc_pmd(&init_mm, __pa(base) >> PAGE_SHIFT);
	paging_init();
}

/*
 * Build a proper pagetable for the kernel mappings.  Up until this
 * point, we've been running on some set of pagetables constructed by
 * the boot process.
 *
 * If we're booting on native hardware, this will be a pagetable
 * constructed in arch/x86/kernel/head_32.S.  The root of the
 * pagetable will be swapper_pg_dir.
 *
 * If we're booting paravirtualized under a hypervisor, then there are
 * more options: we may already be running PAE, and the pagetable may
 * or may not be based in swapper_pg_dir.  In any case,
 * paravirt_pagetable_init() will set up swapper_pg_dir
 * appropriately for the rest of the initialization to work.
 *
 * In general, pagetable_init() assumes that the pagetable may already
 * be partially populated, and so it avoids stomping on any existing
 * mappings.
 */
void __init early_ioremap_page_table_range_init(void)
{
	pgd_t *pgd_base = swapper_pg_dir;
	unsigned long vaddr, end;

	/*
	 * Fixed mappings, only the page table structure has to be
	 * created - mappings will be set by set_fixmap():
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	end = (FIXADDR_TOP + PMD_SIZE - 1) & PMD_MASK;
	page_table_range_init(vaddr, end, pgd_base);
	early_ioremap_reset();
}

static void __init pagetable_init(void)
{
	pgd_t *pgd_base = swapper_pg_dir;

	permanent_kmaps_init(pgd_base);
}

#define DEFAULT_PTE_MASK ~(_PAGE_NX | _PAGE_GLOBAL)
/* Bits supported by the hardware: */
pteval_t __supported_pte_mask __read_mostly = DEFAULT_PTE_MASK;
/* Bits allowed in normal kernel mappings: */
pteval_t __default_kernel_pte_mask __read_mostly = DEFAULT_PTE_MASK;
EXPORT_SYMBOL_GPL(__supported_pte_mask);
/* Used in PAGE_KERNEL_* macros which are reasonably used out-of-tree: */
EXPORT_SYMBOL(__default_kernel_pte_mask);

/* user-defined highmem size */
static unsigned int highmem_pages = -1;

/*
 * highmem=size forces highmem to be exactly 'size' bytes.
 * This works even on boxes that have no highmem otherwise.
 * This also works to reduce highmem size on bigger boxes.
 */
static int __init parse_highmem(char *arg)
{
	if (!arg)
		return -EINVAL;

	highmem_pages = memparse(arg, &arg) >> PAGE_SHIFT;
	return 0;
}
early_param("highmem", parse_highmem);

#define MSG_HIGHMEM_TOO_BIG \
	"highmem size (%luMB) is bigger than pages available (%luMB)!\n"

#define MSG_LOWMEM_TOO_SMALL \
	"highmem size (%luMB) results in <64MB lowmem, ignoring it!\n"
/*
 * All of RAM fits into lowmem - but if user wants highmem
 * artificially via the highmem=x boot parameter then create
 * it:
 */
static void __init lowmem_pfn_init(void)
{
	/* max_low_pfn is 0, we already have early_res support */
	max_low_pfn = max_pfn;

	if (highmem_pages == -1)
		highmem_pages = 0;
#ifdef CONFIG_HIGHMEM
	if (highmem_pages >= max_pfn) {
		printk(KERN_ERR MSG_HIGHMEM_TOO_BIG,
			pages_to_mb(highmem_pages), pages_to_mb(max_pfn));
		highmem_pages = 0;
	}
	if (highmem_pages) {
		if (max_low_pfn - highmem_pages < 64*1024*1024/PAGE_SIZE) {
			printk(KERN_ERR MSG_LOWMEM_TOO_SMALL,
				pages_to_mb(highmem_pages));
			highmem_pages = 0;
		}
		max_low_pfn -= highmem_pages;
	}
#else
	if (highmem_pages)
		printk(KERN_ERR "ignoring highmem size on non-highmem kernel!\n");
#endif
}

#define MSG_HIGHMEM_TOO_SMALL \
	"only %luMB highmem pages available, ignoring highmem size of %luMB!\n"

#define MSG_HIGHMEM_TRIMMED \
	"Warning: only 4GB will be used. Use a HIGHMEM64G enabled kernel!\n"
/*
 * We have more RAM than fits into lowmem - we try to put it into
 * highmem, also taking the highmem=x boot parameter into account:
 */
static void __init highmem_pfn_init(void)
{
	max_low_pfn = MAXMEM_PFN;

	if (highmem_pages == -1)
		highmem_pages = max_pfn - MAXMEM_PFN;

	if (highmem_pages + MAXMEM_PFN < max_pfn)
		max_pfn = MAXMEM_PFN + highmem_pages;

	if (highmem_pages + MAXMEM_PFN > max_pfn) {
		printk(KERN_WARNING MSG_HIGHMEM_TOO_SMALL,
			pages_to_mb(max_pfn - MAXMEM_PFN),
			pages_to_mb(highmem_pages));
		highmem_pages = 0;
	}
#ifndef CONFIG_HIGHMEM
	/* Maximum memory usable is what is directly addressable */
	printk(KERN_WARNING "Warning only %ldMB will be used.\n", MAXMEM>>20);
	if (max_pfn > MAX_NONPAE_PFN)
		printk(KERN_WARNING "Use a HIGHMEM64G enabled kernel.\n");
	else
		printk(KERN_WARNING "Use a HIGHMEM enabled kernel.\n");
	max_pfn = MAXMEM_PFN;
#else /* !CONFIG_HIGHMEM */
#ifndef CONFIG_HIGHMEM64G
	if (max_pfn > MAX_NONPAE_PFN) {
		max_pfn = MAX_NONPAE_PFN;
		printk(KERN_WARNING MSG_HIGHMEM_TRIMMED);
	}
#endif /* !CONFIG_HIGHMEM64G */
#endif /* !CONFIG_HIGHMEM */
}

/*
 * Determine low and high memory ranges:
 */
void __init find_low_pfn_range(void)
{
	/* it could update max_pfn */

	if (max_pfn <= MAXMEM_PFN)
		lowmem_pfn_init();
	else
		highmem_pfn_init();
}

#ifndef CONFIG_NEED_MULTIPLE_NODES
void __init initmem_init(void)
{
#ifdef CONFIG_HIGHMEM
	highstart_pfn = highend_pfn = max_pfn;
	if (max_pfn > max_low_pfn)
		highstart_pfn = max_low_pfn;
	printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
		pages_to_mb(highend_pfn - highstart_pfn));
	high_memory = (void *) __va(highstart_pfn * PAGE_SIZE - 1) + 1;
#else
	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE - 1) + 1;
#endif

	memblock_set_node(0, PHYS_ADDR_MAX, &memblock.memory, 0);

#ifdef CONFIG_FLATMEM
	max_mapnr = IS_ENABLED(CONFIG_HIGHMEM) ? highend_pfn : max_low_pfn;
#endif
	__vmalloc_start_set = true;

	printk(KERN_NOTICE "%ldMB LOWMEM available.\n",
			pages_to_mb(max_low_pfn));

	setup_bootmem_allocator();
}
#endif /* !CONFIG_NEED_MULTIPLE_NODES */

void __init setup_bootmem_allocator(void)
{
	printk(KERN_INFO "  mapped low ram: 0 - %08lx\n",
		 max_pfn_mapped<<PAGE_SHIFT);
	printk(KERN_INFO "  low ram: 0 - %08lx\n", max_low_pfn<<PAGE_SHIFT);
}

/*
 * paging_init() sets up the page tables - note that the first 8MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
void __init paging_init(void)
{
	pagetable_init();

	__flush_tlb_all();

	/*
	 * NOTE: at this point the bootmem allocator is fully available.
	 */
	olpc_dt_build_devicetree();
	sparse_init();
	zone_sizes_init();
}

/*
 * Test if the WP bit works in supervisor mode. It isn't supported on 386's
 * and also on some strange 486's. All 586+'s are OK. This used to involve
 * black magic jumps to work around some nasty CPU bugs, but fortunately the
 * switch to using exceptions got rid of all that.
 */
static void __init test_wp_bit(void)
{
	char z = 0;

	printk(KERN_INFO "Checking if this processor honours the WP bit even in supervisor mode...");

	__set_fixmap(FIX_WP_TEST, __pa_symbol(empty_zero_page), PAGE_KERNEL_RO);

	if (copy_to_kernel_nofault((char *)fix_to_virt(FIX_WP_TEST), &z, 1)) {
		clear_fixmap(FIX_WP_TEST);
		printk(KERN_CONT "Ok.\n");
		return;
	}

	printk(KERN_CONT "No.\n");
	panic("Linux doesn't support CPUs with broken WP.");
}

void __init mem_init(void)
{
	pci_iommu_alloc();

#ifdef CONFIG_FLATMEM
	BUG_ON(!mem_map);
#endif
	/*
	 * With CONFIG_DEBUG_PAGEALLOC initialization of highmem pages has to
	 * be done before memblock_free_all(). Memblock use free low memory for
	 * temporary data (see find_range_array()) and for this purpose can use
	 * pages that was already passed to the buddy allocator, hence marked as
	 * not accessible in the page tables when compiled with
	 * CONFIG_DEBUG_PAGEALLOC. Otherwise order of initialization is not
	 * important here.
	 */
	set_highmem_pages_init();

	/* this will put all low memory onto the freelists */
	memblock_free_all();

	after_bootmem = 1;
	x86_init.hyper.init_after_bootmem();

	mem_init_print_info(NULL);

	/*
	 * Check boundaries twice: Some fundamental inconsistencies can
	 * be detected at build time already.
	 */
#define __FIXADDR_TOP (-PAGE_SIZE)
#ifdef CONFIG_HIGHMEM
	BUILD_BUG_ON(PKMAP_BASE + LAST_PKMAP*PAGE_SIZE	> FIXADDR_START);
	BUILD_BUG_ON(VMALLOC_END			> PKMAP_BASE);
#endif
#define high_memory (-128UL << 20)
	BUILD_BUG_ON(VMALLOC_START			>= VMALLOC_END);
#undef high_memory
#undef __FIXADDR_TOP

#ifdef CONFIG_HIGHMEM
	BUG_ON(PKMAP_BASE + LAST_PKMAP*PAGE_SIZE	> FIXADDR_START);
	BUG_ON(VMALLOC_END				> PKMAP_BASE);
#endif
	BUG_ON(VMALLOC_START				>= VMALLOC_END);
	BUG_ON((unsigned long)high_memory		> VMALLOC_START);

	test_wp_bit();
}

#ifdef CONFIG_MEMORY_HOTPLUG
int arch_add_memory(int nid, u64 start, u64 size,
		    struct mhp_params *params)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int ret;

	/*
	 * The page tables were already mapped at boot so if the caller
	 * requests a different mapping type then we must change all the
	 * pages with __set_memory_prot().
	 */
	if (params->pgprot.pgprot != PAGE_KERNEL.pgprot) {
		ret = __set_memory_prot(start, nr_pages, params->pgprot);
		if (ret)
			return ret;
	}

	return __add_pages(nid, start_pfn, nr_pages, params);
}

void arch_remove_memory(int nid, u64 start, u64 size,
			struct vmem_altmap *altmap)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;

	__remove_pages(start_pfn, nr_pages, altmap);
}
#endif

int kernel_set_to_readonly __read_mostly;

static void mark_nxdata_nx(void)
{
	/*
	 * When this called, init has already been executed and released,
	 * so everything past _etext should be NX.
	 */
	unsigned long start = PFN_ALIGN(_etext);
	/*
	 * This comes from __is_kernel_text upper limit. Also HPAGE where used:
	 */
	unsigned long size = (((unsigned long)__init_end + HPAGE_SIZE) & HPAGE_MASK) - start;

	if (__supported_pte_mask & _PAGE_NX)
		printk(KERN_INFO "NX-protecting the kernel data: %luk\n", size >> 10);
	set_memory_nx(start, size >> PAGE_SHIFT);
}

void mark_rodata_ro(void)
{
	unsigned long start = PFN_ALIGN(_text);
	unsigned long size = (unsigned long)__end_rodata - start;

	set_pages_ro(virt_to_page(start), size >> PAGE_SHIFT);
	pr_info("Write protecting kernel text and read-only data: %luk\n",
		size >> 10);

	kernel_set_to_readonly = 1;

#ifdef CONFIG_CPA_DEBUG
	pr_info("Testing CPA: Reverting %lx-%lx\n", start, start + size);
	set_pages_rw(virt_to_page(start), size >> PAGE_SHIFT);

	pr_info("Testing CPA: write protecting again\n");
	set_pages_ro(virt_to_page(start), size >> PAGE_SHIFT);
#endif
	mark_nxdata_nx();
	if (__supported_pte_mask & _PAGE_NX)
		debug_checkwx();
}
