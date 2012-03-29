/*
 * Copyright (C) 1995  Linus Torvalds
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/module.h>
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
#include <linux/poison.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/efi.h>
#include <linux/memory_hotplug.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/fixmap.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/homecache.h>
#include <hv/hypervisor.h>
#include <arch/chip.h>

#include "migrate.h"

#define clear_pgd(pmdptr) (*(pmdptr) = hv_pte(0))

#ifndef __tilegx__
unsigned long VMALLOC_RESERVE = CONFIG_VMALLOC_RESERVE;
EXPORT_SYMBOL(VMALLOC_RESERVE);
#endif

/* Create an L2 page table */
static pte_t * __init alloc_pte(void)
{
	return __alloc_bootmem(L2_KERNEL_PGTABLE_SIZE, HV_PAGE_TABLE_ALIGN, 0);
}

/*
 * L2 page tables per controller.  We allocate these all at once from
 * the bootmem allocator and store them here.  This saves on kernel L2
 * page table memory, compared to allocating a full 64K page per L2
 * page table, and also means that in cases where we use huge pages,
 * we are guaranteed to later be able to shatter those huge pages and
 * switch to using these page tables instead, without requiring
 * further allocation.  Each l2_ptes[] entry points to the first page
 * table for the first hugepage-size piece of memory on the
 * controller; other page tables are just indexed directly, i.e. the
 * L2 page tables are contiguous in memory for each controller.
 */
static pte_t *l2_ptes[MAX_NUMNODES];
static int num_l2_ptes[MAX_NUMNODES];

static void init_prealloc_ptes(int node, int pages)
{
	BUG_ON(pages & (PTRS_PER_PTE - 1));
	if (pages) {
		num_l2_ptes[node] = pages;
		l2_ptes[node] = __alloc_bootmem(pages * sizeof(pte_t),
						HV_PAGE_TABLE_ALIGN, 0);
	}
}

pte_t *get_prealloc_pte(unsigned long pfn)
{
	int node = pfn_to_nid(pfn);
	pfn &= ~(-1UL << (NR_PA_HIGHBIT_SHIFT - PAGE_SHIFT));
	BUG_ON(node >= MAX_NUMNODES);
	BUG_ON(pfn >= num_l2_ptes[node]);
	return &l2_ptes[node][pfn];
}

/*
 * What caching do we expect pages from the heap to have when
 * they are allocated during bootup?  (Once we've installed the
 * "real" swapper_pg_dir.)
 */
static int initial_heap_home(void)
{
#if CHIP_HAS_CBOX_HOME_MAP()
	if (hash_default)
		return PAGE_HOME_HASH;
#endif
	return smp_processor_id();
}

/*
 * Place a pointer to an L2 page table in a middle page
 * directory entry.
 */
static void __init assign_pte(pmd_t *pmd, pte_t *page_table)
{
	phys_addr_t pa = __pa(page_table);
	unsigned long l2_ptfn = pa >> HV_LOG2_PAGE_TABLE_ALIGN;
	pte_t pteval = hv_pte_set_ptfn(__pgprot(_PAGE_TABLE), l2_ptfn);
	BUG_ON((pa & (HV_PAGE_TABLE_ALIGN-1)) != 0);
	pteval = pte_set_home(pteval, initial_heap_home());
	*(pte_t *)pmd = pteval;
	if (page_table != (pte_t *)pmd_page_vaddr(*pmd))
		BUG();
}

#ifdef __tilegx__

static inline pmd_t *alloc_pmd(void)
{
	return __alloc_bootmem(L1_KERNEL_PGTABLE_SIZE, HV_PAGE_TABLE_ALIGN, 0);
}

static inline void assign_pmd(pud_t *pud, pmd_t *pmd)
{
	assign_pte((pmd_t *)pud, (pte_t *)pmd);
}

#endif /* __tilegx__ */

/* Replace the given pmd with a full PTE table. */
void __init shatter_pmd(pmd_t *pmd)
{
	pte_t *pte = get_prealloc_pte(pte_pfn(*(pte_t *)pmd));
	assign_pte(pmd, pte);
}

#ifdef CONFIG_HIGHMEM
/*
 * This function initializes a certain range of kernel virtual memory
 * with new bootmem page tables, everywhere page tables are missing in
 * the given range.
 */

/*
 * NOTE: The pagetables are allocated contiguous on the physical space
 * so we can cache the place of the first one and move around without
 * checking the pgd every time.
 */
static void __init page_table_range_init(unsigned long start,
					 unsigned long end, pgd_t *pgd_base)
{
	pgd_t *pgd;
	int pgd_idx;
	unsigned long vaddr;

	vaddr = start;
	pgd_idx = pgd_index(vaddr);
	pgd = pgd_base + pgd_idx;

	for ( ; (pgd_idx < PTRS_PER_PGD) && (vaddr != end); pgd++, pgd_idx++) {
		pmd_t *pmd = pmd_offset(pud_offset(pgd, vaddr), vaddr);
		if (pmd_none(*pmd))
			assign_pte(pmd, alloc_pte());
		vaddr += PMD_SIZE;
	}
}
#endif /* CONFIG_HIGHMEM */


#if CHIP_HAS_CBOX_HOME_MAP()

static int __initdata ktext_hash = 1;  /* .text pages */
static int __initdata kdata_hash = 1;  /* .data and .bss pages */
int __write_once hash_default = 1;     /* kernel allocator pages */
EXPORT_SYMBOL(hash_default);
int __write_once kstack_hash = 1;      /* if no homecaching, use h4h */
#endif /* CHIP_HAS_CBOX_HOME_MAP */

/*
 * CPUs to use to for striping the pages of kernel data.  If hash-for-home
 * is available, this is only relevant if kcache_hash sets up the
 * .data and .bss to be page-homed, and we don't want the default mode
 * of using the full set of kernel cpus for the striping.
 */
static __initdata struct cpumask kdata_mask;
static __initdata int kdata_arg_seen;

int __write_once kdata_huge;       /* if no homecaching, small pages */


/* Combine a generic pgprot_t with cache home to get a cache-aware pgprot. */
static pgprot_t __init construct_pgprot(pgprot_t prot, int home)
{
	prot = pte_set_home(prot, home);
#if CHIP_HAS_CBOX_HOME_MAP()
	if (home == PAGE_HOME_IMMUTABLE) {
		if (ktext_hash)
			prot = hv_pte_set_mode(prot, HV_PTE_MODE_CACHE_HASH_L3);
		else
			prot = hv_pte_set_mode(prot, HV_PTE_MODE_CACHE_NO_L3);
	}
#endif
	return prot;
}

/*
 * For a given kernel data VA, how should it be cached?
 * We return the complete pgprot_t with caching bits set.
 */
static pgprot_t __init init_pgprot(ulong address)
{
	int cpu;
	unsigned long page;
	enum { CODE_DELTA = MEM_SV_INTRPT - PAGE_OFFSET };

#if CHIP_HAS_CBOX_HOME_MAP()
	/* For kdata=huge, everything is just hash-for-home. */
	if (kdata_huge)
		return construct_pgprot(PAGE_KERNEL, PAGE_HOME_HASH);
#endif

	/* We map the aliased pages of permanent text inaccessible. */
	if (address < (ulong) _sinittext - CODE_DELTA)
		return PAGE_NONE;

	/*
	 * We map read-only data non-coherent for performance.  We could
	 * use neighborhood caching on TILE64, but it's not clear it's a win.
	 */
	if ((address >= (ulong) __start_rodata &&
	     address < (ulong) __end_rodata) ||
	    address == (ulong) empty_zero_page) {
		return construct_pgprot(PAGE_KERNEL_RO, PAGE_HOME_IMMUTABLE);
	}

#ifndef __tilegx__
#if !ATOMIC_LOCKS_FOUND_VIA_TABLE()
	/* Force the atomic_locks[] array page to be hash-for-home. */
	if (address == (ulong) atomic_locks)
		return construct_pgprot(PAGE_KERNEL, PAGE_HOME_HASH);
#endif
#endif

	/*
	 * Everything else that isn't data or bss is heap, so mark it
	 * with the initial heap home (hash-for-home, or this cpu).  This
	 * includes any addresses after the loaded image and any address before
	 * _einitdata, since we already captured the case of text before
	 * _sinittext, and __pa(einittext) is approximately __pa(sinitdata).
	 *
	 * All the LOWMEM pages that we mark this way will get their
	 * struct page homecache properly marked later, in set_page_homes().
	 * The HIGHMEM pages we leave with a default zero for their
	 * homes, but with a zero free_time we don't have to actually
	 * do a flush action the first time we use them, either.
	 */
	if (address >= (ulong) _end || address < (ulong) _einitdata)
		return construct_pgprot(PAGE_KERNEL, initial_heap_home());

#if CHIP_HAS_CBOX_HOME_MAP()
	/* Use hash-for-home if requested for data/bss. */
	if (kdata_hash)
		return construct_pgprot(PAGE_KERNEL, PAGE_HOME_HASH);
#endif

	/*
	 * Make the w1data homed like heap to start with, to avoid
	 * making it part of the page-striped data area when we're just
	 * going to convert it to read-only soon anyway.
	 */
	if (address >= (ulong)__w1data_begin && address < (ulong)__w1data_end)
		return construct_pgprot(PAGE_KERNEL, initial_heap_home());

	/*
	 * Otherwise we just hand out consecutive cpus.  To avoid
	 * requiring this function to hold state, we just walk forward from
	 * _sdata by PAGE_SIZE, skipping the readonly and init data, to reach
	 * the requested address, while walking cpu home around kdata_mask.
	 * This is typically no more than a dozen or so iterations.
	 */
	page = (((ulong)__w1data_end) + PAGE_SIZE - 1) & PAGE_MASK;
	BUG_ON(address < page || address >= (ulong)_end);
	cpu = cpumask_first(&kdata_mask);
	for (; page < address; page += PAGE_SIZE) {
		if (page >= (ulong)&init_thread_union &&
		    page < (ulong)&init_thread_union + THREAD_SIZE)
			continue;
		if (page == (ulong)empty_zero_page)
			continue;
#ifndef __tilegx__
#if !ATOMIC_LOCKS_FOUND_VIA_TABLE()
		if (page == (ulong)atomic_locks)
			continue;
#endif
#endif
		cpu = cpumask_next(cpu, &kdata_mask);
		if (cpu == NR_CPUS)
			cpu = cpumask_first(&kdata_mask);
	}
	return construct_pgprot(PAGE_KERNEL, cpu);
}

/*
 * This function sets up how we cache the kernel text.  If we have
 * hash-for-home support, normally that is used instead (see the
 * kcache_hash boot flag for more information).  But if we end up
 * using a page-based caching technique, this option sets up the
 * details of that.  In addition, the "ktext=nocache" option may
 * always be used to disable local caching of text pages, if desired.
 */

static int __initdata ktext_arg_seen;
static int __initdata ktext_small;
static int __initdata ktext_local;
static int __initdata ktext_all;
static int __initdata ktext_nondataplane;
static int __initdata ktext_nocache;
static struct cpumask __initdata ktext_mask;

static int __init setup_ktext(char *str)
{
	if (str == NULL)
		return -EINVAL;

	/* If you have a leading "nocache", turn off ktext caching */
	if (strncmp(str, "nocache", 7) == 0) {
		ktext_nocache = 1;
		pr_info("ktext: disabling local caching of kernel text\n");
		str += 7;
		if (*str == ',')
			++str;
		if (*str == '\0')
			return 0;
	}

	ktext_arg_seen = 1;

	/* Default setting on Tile64: use a huge page */
	if (strcmp(str, "huge") == 0)
		pr_info("ktext: using one huge locally cached page\n");

	/* Pay TLB cost but get no cache benefit: cache small pages locally */
	else if (strcmp(str, "local") == 0) {
		ktext_small = 1;
		ktext_local = 1;
		pr_info("ktext: using small pages with local caching\n");
	}

	/* Neighborhood cache ktext pages on all cpus. */
	else if (strcmp(str, "all") == 0) {
		ktext_small = 1;
		ktext_all = 1;
		pr_info("ktext: using maximal caching neighborhood\n");
	}


	/* Neighborhood ktext pages on specified mask */
	else if (cpulist_parse(str, &ktext_mask) == 0) {
		char buf[NR_CPUS * 5];
		cpulist_scnprintf(buf, sizeof(buf), &ktext_mask);
		if (cpumask_weight(&ktext_mask) > 1) {
			ktext_small = 1;
			pr_info("ktext: using caching neighborhood %s "
			       "with small pages\n", buf);
		} else {
			pr_info("ktext: caching on cpu %s with one huge page\n",
			       buf);
		}
	}

	else if (*str)
		return -EINVAL;

	return 0;
}

early_param("ktext", setup_ktext);


static inline pgprot_t ktext_set_nocache(pgprot_t prot)
{
	if (!ktext_nocache)
		prot = hv_pte_set_nc(prot);
#if CHIP_HAS_NC_AND_NOALLOC_BITS()
	else
		prot = hv_pte_set_no_alloc_l2(prot);
#endif
	return prot;
}

#ifndef __tilegx__
static pmd_t *__init get_pmd(pgd_t pgtables[], unsigned long va)
{
	return pmd_offset(pud_offset(&pgtables[pgd_index(va)], va), va);
}
#else
static pmd_t *__init get_pmd(pgd_t pgtables[], unsigned long va)
{
	pud_t *pud = pud_offset(&pgtables[pgd_index(va)], va);
	if (pud_none(*pud))
		assign_pmd(pud, alloc_pmd());
	return pmd_offset(pud, va);
}
#endif

/* Temporary page table we use for staging. */
static pgd_t pgtables[PTRS_PER_PGD]
 __attribute__((aligned(HV_PAGE_TABLE_ALIGN)));

/*
 * This maps the physical memory to kernel virtual address space, a total
 * of max_low_pfn pages, by creating page tables starting from address
 * PAGE_OFFSET.
 *
 * This routine transitions us from using a set of compiled-in large
 * pages to using some more precise caching, including removing access
 * to code pages mapped at PAGE_OFFSET (executed only at MEM_SV_START)
 * marking read-only data as locally cacheable, striping the remaining
 * .data and .bss across all the available tiles, and removing access
 * to pages above the top of RAM (thus ensuring a page fault from a bad
 * virtual address rather than a hypervisor shoot down for accessing
 * memory outside the assigned limits).
 */
static void __init kernel_physical_mapping_init(pgd_t *pgd_base)
{
	unsigned long long irqmask;
	unsigned long address, pfn;
	pmd_t *pmd;
	pte_t *pte;
	int pte_ofs;
	const struct cpumask *my_cpu_mask = cpumask_of(smp_processor_id());
	struct cpumask kstripe_mask;
	int rc, i;

#if CHIP_HAS_CBOX_HOME_MAP()
	if (ktext_arg_seen && ktext_hash) {
		pr_warning("warning: \"ktext\" boot argument ignored"
			   " if \"kcache_hash\" sets up text hash-for-home\n");
		ktext_small = 0;
	}

	if (kdata_arg_seen && kdata_hash) {
		pr_warning("warning: \"kdata\" boot argument ignored"
			   " if \"kcache_hash\" sets up data hash-for-home\n");
	}

	if (kdata_huge && !hash_default) {
		pr_warning("warning: disabling \"kdata=huge\"; requires"
			  " kcache_hash=all or =allbutstack\n");
		kdata_huge = 0;
	}
#endif

	/*
	 * Set up a mask for cpus to use for kernel striping.
	 * This is normally all cpus, but minus dataplane cpus if any.
	 * If the dataplane covers the whole chip, we stripe over
	 * the whole chip too.
	 */
	cpumask_copy(&kstripe_mask, cpu_possible_mask);
	if (!kdata_arg_seen)
		kdata_mask = kstripe_mask;

	/* Allocate and fill in L2 page tables */
	for (i = 0; i < MAX_NUMNODES; ++i) {
#ifdef CONFIG_HIGHMEM
		unsigned long end_pfn = node_lowmem_end_pfn[i];
#else
		unsigned long end_pfn = node_end_pfn[i];
#endif
		unsigned long end_huge_pfn = 0;

		/* Pre-shatter the last huge page to allow per-cpu pages. */
		if (kdata_huge)
			end_huge_pfn = end_pfn - (HPAGE_SIZE >> PAGE_SHIFT);

		pfn = node_start_pfn[i];

		/* Allocate enough memory to hold L2 page tables for node. */
		init_prealloc_ptes(i, end_pfn - pfn);

		address = (unsigned long) pfn_to_kaddr(pfn);
		while (pfn < end_pfn) {
			BUG_ON(address & (HPAGE_SIZE-1));
			pmd = get_pmd(pgtables, address);
			pte = get_prealloc_pte(pfn);
			if (pfn < end_huge_pfn) {
				pgprot_t prot = init_pgprot(address);
				*(pte_t *)pmd = pte_mkhuge(pfn_pte(pfn, prot));
				for (pte_ofs = 0; pte_ofs < PTRS_PER_PTE;
				     pfn++, pte_ofs++, address += PAGE_SIZE)
					pte[pte_ofs] = pfn_pte(pfn, prot);
			} else {
				if (kdata_huge)
					printk(KERN_DEBUG "pre-shattered huge"
					       " page at %#lx\n", address);
				for (pte_ofs = 0; pte_ofs < PTRS_PER_PTE;
				     pfn++, pte_ofs++, address += PAGE_SIZE) {
					pgprot_t prot = init_pgprot(address);
					pte[pte_ofs] = pfn_pte(pfn, prot);
				}
				assign_pte(pmd, pte);
			}
		}
	}

	/*
	 * Set or check ktext_map now that we have cpu_possible_mask
	 * and kstripe_mask to work with.
	 */
	if (ktext_all)
		cpumask_copy(&ktext_mask, cpu_possible_mask);
	else if (ktext_nondataplane)
		ktext_mask = kstripe_mask;
	else if (!cpumask_empty(&ktext_mask)) {
		/* Sanity-check any mask that was requested */
		struct cpumask bad;
		cpumask_andnot(&bad, &ktext_mask, cpu_possible_mask);
		cpumask_and(&ktext_mask, &ktext_mask, cpu_possible_mask);
		if (!cpumask_empty(&bad)) {
			char buf[NR_CPUS * 5];
			cpulist_scnprintf(buf, sizeof(buf), &bad);
			pr_info("ktext: not using unavailable cpus %s\n", buf);
		}
		if (cpumask_empty(&ktext_mask)) {
			pr_warning("ktext: no valid cpus; caching on %d.\n",
				   smp_processor_id());
			cpumask_copy(&ktext_mask,
				     cpumask_of(smp_processor_id()));
		}
	}

	address = MEM_SV_INTRPT;
	pmd = get_pmd(pgtables, address);
	pfn = 0;  /* code starts at PA 0 */
	if (ktext_small) {
		/* Allocate an L2 PTE for the kernel text */
		int cpu = 0;
		pgprot_t prot = construct_pgprot(PAGE_KERNEL_EXEC,
						 PAGE_HOME_IMMUTABLE);

		if (ktext_local) {
			if (ktext_nocache)
				prot = hv_pte_set_mode(prot,
						       HV_PTE_MODE_UNCACHED);
			else
				prot = hv_pte_set_mode(prot,
						       HV_PTE_MODE_CACHE_NO_L3);
		} else {
			prot = hv_pte_set_mode(prot,
					       HV_PTE_MODE_CACHE_TILE_L3);
			cpu = cpumask_first(&ktext_mask);

			prot = ktext_set_nocache(prot);
		}

		BUG_ON(address != (unsigned long)_stext);
		pte = NULL;
		for (; address < (unsigned long)_einittext;
		     pfn++, address += PAGE_SIZE) {
			pte_ofs = pte_index(address);
			if (pte_ofs == 0) {
				if (pte)
					assign_pte(pmd++, pte);
				pte = alloc_pte();
			}
			if (!ktext_local) {
				prot = set_remote_cache_cpu(prot, cpu);
				cpu = cpumask_next(cpu, &ktext_mask);
				if (cpu == NR_CPUS)
					cpu = cpumask_first(&ktext_mask);
			}
			pte[pte_ofs] = pfn_pte(pfn, prot);
		}
		if (pte)
			assign_pte(pmd, pte);
	} else {
		pte_t pteval = pfn_pte(0, PAGE_KERNEL_EXEC);
		pteval = pte_mkhuge(pteval);
#if CHIP_HAS_CBOX_HOME_MAP()
		if (ktext_hash) {
			pteval = hv_pte_set_mode(pteval,
						 HV_PTE_MODE_CACHE_HASH_L3);
			pteval = ktext_set_nocache(pteval);
		} else
#endif /* CHIP_HAS_CBOX_HOME_MAP() */
		if (cpumask_weight(&ktext_mask) == 1) {
			pteval = set_remote_cache_cpu(pteval,
					      cpumask_first(&ktext_mask));
			pteval = hv_pte_set_mode(pteval,
						 HV_PTE_MODE_CACHE_TILE_L3);
			pteval = ktext_set_nocache(pteval);
		} else if (ktext_nocache)
			pteval = hv_pte_set_mode(pteval,
						 HV_PTE_MODE_UNCACHED);
		else
			pteval = hv_pte_set_mode(pteval,
						 HV_PTE_MODE_CACHE_NO_L3);
		for (; address < (unsigned long)_einittext;
		     pfn += PFN_DOWN(HPAGE_SIZE), address += HPAGE_SIZE)
			*(pte_t *)(pmd++) = pfn_pte(pfn, pteval);
	}

	/* Set swapper_pgprot here so it is flushed to memory right away. */
	swapper_pgprot = init_pgprot((unsigned long)swapper_pg_dir);

	/*
	 * Since we may be changing the caching of the stack and page
	 * table itself, we invoke an assembly helper to do the
	 * following steps:
	 *
	 *  - flush the cache so we start with an empty slate
	 *  - install pgtables[] as the real page table
	 *  - flush the TLB so the new page table takes effect
	 */
	irqmask = interrupt_mask_save_mask();
	interrupt_mask_set_mask(-1ULL);
	rc = flush_and_install_context(__pa(pgtables),
				       init_pgprot((unsigned long)pgtables),
				       __get_cpu_var(current_asid),
				       cpumask_bits(my_cpu_mask));
	interrupt_mask_restore_mask(irqmask);
	BUG_ON(rc != 0);

	/* Copy the page table back to the normal swapper_pg_dir. */
	memcpy(pgd_base, pgtables, sizeof(pgtables));
	__install_page_table(pgd_base, __get_cpu_var(current_asid),
			     swapper_pgprot);

	/*
	 * We just read swapper_pgprot and thus brought it into the cache,
	 * with its new home & caching mode.  When we start the other CPUs,
	 * they're going to reference swapper_pgprot via their initial fake
	 * VA-is-PA mappings, which cache everything locally.  At that
	 * time, if it's in our cache with a conflicting home, the
	 * simulator's coherence checker will complain.  So, flush it out
	 * of our cache; we're not going to ever use it again anyway.
	 */
	__insn_finv(&swapper_pgprot);
}

/*
 * devmem_is_allowed() checks to see if /dev/mem access to a certain address
 * is valid. The argument is a physical page number.
 *
 * On Tile, the only valid things for which we can just hand out unchecked
 * PTEs are the kernel code and data.  Anything else might change its
 * homing with time, and we wouldn't know to adjust the /dev/mem PTEs.
 * Note that init_thread_union is released to heap soon after boot,
 * so we include it in the init data.
 *
 * For TILE-Gx, we might want to consider allowing access to PA
 * regions corresponding to PCI space, etc.
 */
int devmem_is_allowed(unsigned long pagenr)
{
	return pagenr < kaddr_to_pfn(_end) &&
		!(pagenr >= kaddr_to_pfn(&init_thread_union) ||
		  pagenr < kaddr_to_pfn(_einitdata)) &&
		!(pagenr >= kaddr_to_pfn(_sinittext) ||
		  pagenr <= kaddr_to_pfn(_einittext-1));
}

#ifdef CONFIG_HIGHMEM
static void __init permanent_kmaps_init(pgd_t *pgd_base)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long vaddr;

	vaddr = PKMAP_BASE;
	page_table_range_init(vaddr, vaddr + PAGE_SIZE*LAST_PKMAP, pgd_base);

	pgd = swapper_pg_dir + pgd_index(vaddr);
	pud = pud_offset(pgd, vaddr);
	pmd = pmd_offset(pud, vaddr);
	pte = pte_offset_kernel(pmd, vaddr);
	pkmap_page_table = pte;
}
#endif /* CONFIG_HIGHMEM */


static void __init init_free_pfn_range(unsigned long start, unsigned long end)
{
	unsigned long pfn;
	struct page *page = pfn_to_page(start);

	for (pfn = start; pfn < end; ) {
		/* Optimize by freeing pages in large batches */
		int order = __ffs(pfn);
		int count, i;
		struct page *p;

		if (order >= MAX_ORDER)
			order = MAX_ORDER-1;
		count = 1 << order;
		while (pfn + count > end) {
			count >>= 1;
			--order;
		}
		for (p = page, i = 0; i < count; ++i, ++p) {
			__ClearPageReserved(p);
			/*
			 * Hacky direct set to avoid unnecessary
			 * lock take/release for EVERY page here.
			 */
			p->_count.counter = 0;
			p->_mapcount.counter = -1;
		}
		init_page_count(page);
		__free_pages(page, order);
		totalram_pages += count;

		page += count;
		pfn += count;
	}
}

static void __init set_non_bootmem_pages_init(void)
{
	struct zone *z;
	for_each_zone(z) {
		unsigned long start, end;
		int nid = z->zone_pgdat->node_id;
		int idx = zone_idx(z);

		start = z->zone_start_pfn;
		if (start == 0)
			continue;  /* bootmem */
		end = start + z->spanned_pages;
		if (idx == ZONE_NORMAL) {
			BUG_ON(start != node_start_pfn[nid]);
			start = node_free_pfn[nid];
		}
#ifdef CONFIG_HIGHMEM
		if (idx == ZONE_HIGHMEM)
			totalhigh_pages += z->spanned_pages;
#endif
		if (kdata_huge) {
			unsigned long percpu_pfn = node_percpu_pfn[nid];
			if (start < percpu_pfn && end > percpu_pfn)
				end = percpu_pfn;
		}
#ifdef CONFIG_PCI
		if (start <= pci_reserve_start_pfn &&
		    end > pci_reserve_start_pfn) {
			if (end > pci_reserve_end_pfn)
				init_free_pfn_range(pci_reserve_end_pfn, end);
			end = pci_reserve_start_pfn;
		}
#endif
		init_free_pfn_range(start, end);
	}
}

/*
 * paging_init() sets up the page tables - note that all of lowmem is
 * already mapped by head.S.
 */
void __init paging_init(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long vaddr, end;
#endif
#ifdef __tilegx__
	pud_t *pud;
#endif
	pgd_t *pgd_base = swapper_pg_dir;

	kernel_physical_mapping_init(pgd_base);

#ifdef CONFIG_HIGHMEM
	/*
	 * Fixed mappings, only the page table structure has to be
	 * created - mappings will be set by set_fixmap():
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	end = (FIXADDR_TOP + PMD_SIZE - 1) & PMD_MASK;
	page_table_range_init(vaddr, end, pgd_base);
	permanent_kmaps_init(pgd_base);
#endif

#ifdef __tilegx__
	/*
	 * Since GX allocates just one pmd_t array worth of vmalloc space,
	 * we go ahead and allocate it statically here, then share it
	 * globally.  As a result we don't have to worry about any task
	 * changing init_mm once we get up and running, and there's no
	 * need for e.g. vmalloc_sync_all().
	 */
	BUILD_BUG_ON(pgd_index(VMALLOC_START) != pgd_index(VMALLOC_END - 1));
	pud = pud_offset(pgd_base + pgd_index(VMALLOC_START), VMALLOC_START);
	assign_pmd(pud, alloc_pmd());
#endif
}


/*
 * Walk the kernel page tables and derive the page_home() from
 * the PTEs, so that set_pte() can properly validate the caching
 * of all PTEs it sees.
 */
void __init set_page_homes(void)
{
}

static void __init set_max_mapnr_init(void)
{
#ifdef CONFIG_FLATMEM
	max_mapnr = max_low_pfn;
#endif
}

void __init mem_init(void)
{
	int codesize, datasize, initsize;
	int i;
#ifndef __tilegx__
	void *last;
#endif

#ifdef CONFIG_FLATMEM
	BUG_ON(!mem_map);
#endif

#ifdef CONFIG_HIGHMEM
	/* check that fixmap and pkmap do not overlap */
	if (PKMAP_ADDR(LAST_PKMAP-1) >= FIXADDR_START) {
		pr_err("fixmap and kmap areas overlap"
		       " - this will crash\n");
		pr_err("pkstart: %lxh pkend: %lxh fixstart %lxh\n",
		       PKMAP_BASE, PKMAP_ADDR(LAST_PKMAP-1),
		       FIXADDR_START);
		BUG();
	}
#endif

	set_max_mapnr_init();

	/* this will put all bootmem onto the freelists */
	totalram_pages += free_all_bootmem();

	/* count all remaining LOWMEM and give all HIGHMEM to page allocator */
	set_non_bootmem_pages_init();

	codesize =  (unsigned long)&_etext - (unsigned long)&_text;
	datasize =  (unsigned long)&_end - (unsigned long)&_sdata;
	initsize =  (unsigned long)&_einittext - (unsigned long)&_sinittext;
	initsize += (unsigned long)&_einitdata - (unsigned long)&_sinitdata;

	pr_info("Memory: %luk/%luk available (%dk kernel code, %dk data, %dk init, %ldk highmem)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		num_physpages << (PAGE_SHIFT-10),
		codesize >> 10,
		datasize >> 10,
		initsize >> 10,
		(unsigned long) (totalhigh_pages << (PAGE_SHIFT-10))
	       );

	/*
	 * In debug mode, dump some interesting memory mappings.
	 */
#ifdef CONFIG_HIGHMEM
	printk(KERN_DEBUG "  KMAP    %#lx - %#lx\n",
	       FIXADDR_START, FIXADDR_TOP + PAGE_SIZE - 1);
	printk(KERN_DEBUG "  PKMAP   %#lx - %#lx\n",
	       PKMAP_BASE, PKMAP_ADDR(LAST_PKMAP) - 1);
#endif
#ifdef CONFIG_HUGEVMAP
	printk(KERN_DEBUG "  HUGEMAP %#lx - %#lx\n",
	       HUGE_VMAP_BASE, HUGE_VMAP_END - 1);
#endif
	printk(KERN_DEBUG "  VMALLOC %#lx - %#lx\n",
	       _VMALLOC_START, _VMALLOC_END - 1);
#ifdef __tilegx__
	for (i = MAX_NUMNODES-1; i >= 0; --i) {
		struct pglist_data *node = &node_data[i];
		if (node->node_present_pages) {
			unsigned long start = (unsigned long)
				pfn_to_kaddr(node->node_start_pfn);
			unsigned long end = start +
				(node->node_present_pages << PAGE_SHIFT);
			printk(KERN_DEBUG "  MEM%d    %#lx - %#lx\n",
			       i, start, end - 1);
		}
	}
#else
	last = high_memory;
	for (i = MAX_NUMNODES-1; i >= 0; --i) {
		if ((unsigned long)vbase_map[i] != -1UL) {
			printk(KERN_DEBUG "  LOWMEM%d %#lx - %#lx\n",
			       i, (unsigned long) (vbase_map[i]),
			       (unsigned long) (last-1));
			last = vbase_map[i];
		}
	}
#endif

#ifndef __tilegx__
	/*
	 * Convert from using one lock for all atomic operations to
	 * one per cpu.
	 */
	__init_atomic_per_cpu();
#endif
}

/*
 * this is for the non-NUMA, single node SMP system case.
 * Specifically, in the case of x86, we will always add
 * memory to the highmem for now.
 */
#ifndef CONFIG_NEED_MULTIPLE_NODES
int arch_add_memory(u64 start, u64 size)
{
	struct pglist_data *pgdata = &contig_page_data;
	struct zone *zone = pgdata->node_zones + MAX_NR_ZONES-1;
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;

	return __add_pages(zone, start_pfn, nr_pages);
}

int remove_memory(u64 start, u64 size)
{
	return -EINVAL;
}
#endif

struct kmem_cache *pgd_cache;

void __init pgtable_cache_init(void)
{
	pgd_cache = kmem_cache_create("pgd", SIZEOF_PGD, SIZEOF_PGD, 0, NULL);
	if (!pgd_cache)
		panic("pgtable_cache_init(): Cannot create pgd cache");
}

#if !CHIP_HAS_COHERENT_LOCAL_CACHE()
/*
 * The __w1data area holds data that is only written during initialization,
 * and is read-only and thus freely cacheable thereafter.  Fix the page
 * table entries that cover that region accordingly.
 */
static void mark_w1data_ro(void)
{
	/* Loop over page table entries */
	unsigned long addr = (unsigned long)__w1data_begin;
	BUG_ON((addr & (PAGE_SIZE-1)) != 0);
	for (; addr <= (unsigned long)__w1data_end - 1; addr += PAGE_SIZE) {
		unsigned long pfn = kaddr_to_pfn((void *)addr);
		pte_t *ptep = virt_to_pte(NULL, addr);
		BUG_ON(pte_huge(*ptep));   /* not relevant for kdata_huge */
		set_pte_at(&init_mm, addr, ptep, pfn_pte(pfn, PAGE_KERNEL_RO));
	}
}
#endif

#ifdef CONFIG_DEBUG_PAGEALLOC
static long __write_once initfree;
#else
static long __write_once initfree = 1;
#endif

/* Select whether to free (1) or mark unusable (0) the __init pages. */
static int __init set_initfree(char *str)
{
	long val;
	if (strict_strtol(str, 0, &val) == 0) {
		initfree = val;
		pr_info("initfree: %s free init pages\n",
			initfree ? "will" : "won't");
	}
	return 1;
}
__setup("initfree=", set_initfree);

static void free_init_pages(char *what, unsigned long begin, unsigned long end)
{
	unsigned long addr = (unsigned long) begin;

	if (kdata_huge && !initfree) {
		pr_warning("Warning: ignoring initfree=0:"
			   " incompatible with kdata=huge\n");
		initfree = 1;
	}
	end = (end + PAGE_SIZE - 1) & PAGE_MASK;
	local_flush_tlb_pages(NULL, begin, PAGE_SIZE, end - begin);
	for (addr = begin; addr < end; addr += PAGE_SIZE) {
		/*
		 * Note we just reset the home here directly in the
		 * page table.  We know this is safe because our caller
		 * just flushed the caches on all the other cpus,
		 * and they won't be touching any of these pages.
		 */
		int pfn = kaddr_to_pfn((void *)addr);
		struct page *page = pfn_to_page(pfn);
		pte_t *ptep = virt_to_pte(NULL, addr);
		if (!initfree) {
			/*
			 * If debugging page accesses then do not free
			 * this memory but mark them not present - any
			 * buggy init-section access will create a
			 * kernel page fault:
			 */
			pte_clear(&init_mm, addr, ptep);
			continue;
		}
		__ClearPageReserved(page);
		init_page_count(page);
		if (pte_huge(*ptep))
			BUG_ON(!kdata_huge);
		else
			set_pte_at(&init_mm, addr, ptep,
				   pfn_pte(pfn, PAGE_KERNEL));
		memset((void *)addr, POISON_FREE_INITMEM, PAGE_SIZE);
		free_page(addr);
		totalram_pages++;
	}
	pr_info("Freeing %s: %ldk freed\n", what, (end - begin) >> 10);
}

void free_initmem(void)
{
	const unsigned long text_delta = MEM_SV_INTRPT - PAGE_OFFSET;

	/*
	 * Evict the dirty initdata on the boot cpu, evict the w1data
	 * wherever it's homed, and evict all the init code everywhere.
	 * We are guaranteed that no one will touch the init pages any
	 * more, and although other cpus may be touching the w1data,
	 * we only actually change the caching on tile64, which won't
	 * be keeping local copies in the other tiles' caches anyway.
	 */
	homecache_evict(&cpu_cacheable_map);

	/* Free the data pages that we won't use again after init. */
	free_init_pages("unused kernel data",
			(unsigned long)_sinitdata,
			(unsigned long)_einitdata);

	/*
	 * Free the pages mapped from 0xc0000000 that correspond to code
	 * pages from MEM_SV_INTRPT that we won't use again after init.
	 */
	free_init_pages("unused kernel text",
			(unsigned long)_sinittext - text_delta,
			(unsigned long)_einittext - text_delta);

#if !CHIP_HAS_COHERENT_LOCAL_CACHE()
	/*
	 * Upgrade the .w1data section to globally cached.
	 * We don't do this on tilepro, since the cache architecture
	 * pretty much makes it irrelevant, and in any case we end
	 * up having racing issues with other tiles that may touch
	 * the data after we flush the cache but before we update
	 * the PTEs and flush the TLBs, causing sharer shootdowns
	 * later.  Even though this is to clean data, it seems like
	 * an unnecessary complication.
	 */
	mark_w1data_ro();
#endif

	/* Do a global TLB flush so everyone sees the changes. */
	flush_tlb_all();
}
