// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2006
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/memblock.h>
#include <linux/pfn.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/pgalloc.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/set_memory.h>

static DEFINE_MUTEX(vmem_mutex);

static void __ref *vmem_alloc_pages(unsigned int order)
{
	unsigned long size = PAGE_SIZE << order;

	if (slab_is_available())
		return (void *)__get_free_pages(GFP_KERNEL, order);
	return (void *) memblock_phys_alloc(size, size);
}

static void vmem_free_pages(unsigned long addr, int order)
{
	/* We don't expect boot memory to be removed ever. */
	if (!slab_is_available() ||
	    WARN_ON_ONCE(PageReserved(phys_to_page(addr))))
		return;
	free_pages(addr, order);
}

void *vmem_crst_alloc(unsigned long val)
{
	unsigned long *table;

	table = vmem_alloc_pages(CRST_ALLOC_ORDER);
	if (table)
		crst_table_init(table, val);
	return table;
}

pte_t __ref *vmem_pte_alloc(void)
{
	unsigned long size = PTRS_PER_PTE * sizeof(pte_t);
	pte_t *pte;

	if (slab_is_available())
		pte = (pte_t *) page_table_alloc(&init_mm);
	else
		pte = (pte_t *) memblock_phys_alloc(size, size);
	if (!pte)
		return NULL;
	memset64((u64 *)pte, _PAGE_INVALID, PTRS_PER_PTE);
	return pte;
}

static void vmem_pte_free(unsigned long *table)
{
	/* We don't expect boot memory to be removed ever. */
	if (!slab_is_available() ||
	    WARN_ON_ONCE(PageReserved(virt_to_page(table))))
		return;
	page_table_free(&init_mm, table);
}

#define PAGE_UNUSED 0xFD

/*
 * The unused vmemmap range, which was not yet memset(PAGE_UNUSED) ranges
 * from unused_pmd_start to next PMD_SIZE boundary.
 */
static unsigned long unused_pmd_start;

static void vmemmap_flush_unused_pmd(void)
{
	if (!unused_pmd_start)
		return;
	memset(__va(unused_pmd_start), PAGE_UNUSED,
	       ALIGN(unused_pmd_start, PMD_SIZE) - unused_pmd_start);
	unused_pmd_start = 0;
}

static void __vmemmap_use_sub_pmd(unsigned long start, unsigned long end)
{
	/*
	 * As we expect to add in the same granularity as we remove, it's
	 * sufficient to mark only some piece used to block the memmap page from
	 * getting removed (just in case the memmap never gets initialized,
	 * e.g., because the memory block never gets onlined).
	 */
	memset(__va(start), 0, sizeof(struct page));
}

static void vmemmap_use_sub_pmd(unsigned long start, unsigned long end)
{
	/*
	 * We only optimize if the new used range directly follows the
	 * previously unused range (esp., when populating consecutive sections).
	 */
	if (unused_pmd_start == start) {
		unused_pmd_start = end;
		if (likely(IS_ALIGNED(unused_pmd_start, PMD_SIZE)))
			unused_pmd_start = 0;
		return;
	}
	vmemmap_flush_unused_pmd();
	__vmemmap_use_sub_pmd(start, end);
}

static void vmemmap_use_new_sub_pmd(unsigned long start, unsigned long end)
{
	void *page = __va(ALIGN_DOWN(start, PMD_SIZE));

	vmemmap_flush_unused_pmd();

	/* Could be our memmap page is filled with PAGE_UNUSED already ... */
	__vmemmap_use_sub_pmd(start, end);

	/* Mark the unused parts of the new memmap page PAGE_UNUSED. */
	if (!IS_ALIGNED(start, PMD_SIZE))
		memset(page, PAGE_UNUSED, start - __pa(page));
	/*
	 * We want to avoid memset(PAGE_UNUSED) when populating the vmemmap of
	 * consecutive sections. Remember for the last added PMD the last
	 * unused range in the populated PMD.
	 */
	if (!IS_ALIGNED(end, PMD_SIZE))
		unused_pmd_start = end;
}

/* Returns true if the PMD is completely unused and can be freed. */
static bool vmemmap_unuse_sub_pmd(unsigned long start, unsigned long end)
{
	void *page = __va(ALIGN_DOWN(start, PMD_SIZE));

	vmemmap_flush_unused_pmd();
	memset(__va(start), PAGE_UNUSED, end - start);
	return !memchr_inv(page, PAGE_UNUSED, PMD_SIZE);
}

/* __ref: we'll only call vmemmap_alloc_block() via vmemmap_populate() */
static int __ref modify_pte_table(pmd_t *pmd, unsigned long addr,
				  unsigned long end, bool add, bool direct)
{
	unsigned long prot, pages = 0;
	int ret = -ENOMEM;
	pte_t *pte;

	prot = pgprot_val(PAGE_KERNEL);
	if (!MACHINE_HAS_NX)
		prot &= ~_PAGE_NOEXEC;

	pte = pte_offset_kernel(pmd, addr);
	for (; addr < end; addr += PAGE_SIZE, pte++) {
		if (!add) {
			if (pte_none(*pte))
				continue;
			if (!direct)
				vmem_free_pages(pfn_to_phys(pte_pfn(*pte)), 0);
			pte_clear(&init_mm, addr, pte);
		} else if (pte_none(*pte)) {
			if (!direct) {
				void *new_page = vmemmap_alloc_block(PAGE_SIZE, NUMA_NO_NODE);

				if (!new_page)
					goto out;
				pte_val(*pte) = __pa(new_page) | prot;
			} else {
				pte_val(*pte) = addr | prot;
			}
		} else {
			continue;
		}
		pages++;
	}
	ret = 0;
out:
	if (direct)
		update_page_count(PG_DIRECT_MAP_4K, add ? pages : -pages);
	return ret;
}

static void try_free_pte_table(pmd_t *pmd, unsigned long start)
{
	pte_t *pte;
	int i;

	/* We can safely assume this is fully in 1:1 mapping & vmemmap area */
	pte = pte_offset_kernel(pmd, start);
	for (i = 0; i < PTRS_PER_PTE; i++, pte++) {
		if (!pte_none(*pte))
			return;
	}
	vmem_pte_free(__va(pmd_deref(*pmd)));
	pmd_clear(pmd);
}

/* __ref: we'll only call vmemmap_alloc_block() via vmemmap_populate() */
static int __ref modify_pmd_table(pud_t *pud, unsigned long addr,
				  unsigned long end, bool add, bool direct)
{
	unsigned long next, prot, pages = 0;
	int ret = -ENOMEM;
	pmd_t *pmd;
	pte_t *pte;

	prot = pgprot_val(SEGMENT_KERNEL);
	if (!MACHINE_HAS_NX)
		prot &= ~_SEGMENT_ENTRY_NOEXEC;

	pmd = pmd_offset(pud, addr);
	for (; addr < end; addr = next, pmd++) {
		next = pmd_addr_end(addr, end);
		if (!add) {
			if (pmd_none(*pmd))
				continue;
			if (pmd_large(*pmd) && !add) {
				if (IS_ALIGNED(addr, PMD_SIZE) &&
				    IS_ALIGNED(next, PMD_SIZE)) {
					if (!direct)
						vmem_free_pages(pmd_deref(*pmd), get_order(PMD_SIZE));
					pmd_clear(pmd);
					pages++;
				} else if (!direct && vmemmap_unuse_sub_pmd(addr, next)) {
					vmem_free_pages(pmd_deref(*pmd), get_order(PMD_SIZE));
					pmd_clear(pmd);
				}
				continue;
			}
		} else if (pmd_none(*pmd)) {
			if (IS_ALIGNED(addr, PMD_SIZE) &&
			    IS_ALIGNED(next, PMD_SIZE) &&
			    MACHINE_HAS_EDAT1 && addr && direct &&
			    !debug_pagealloc_enabled()) {
				pmd_val(*pmd) = addr | prot;
				pages++;
				continue;
			} else if (!direct && MACHINE_HAS_EDAT1) {
				void *new_page;

				/*
				 * Use 1MB frames for vmemmap if available. We
				 * always use large frames even if they are only
				 * partially used. Otherwise we would have also
				 * page tables since vmemmap_populate gets
				 * called for each section separately.
				 */
				new_page = vmemmap_alloc_block(PMD_SIZE, NUMA_NO_NODE);
				if (new_page) {
					pmd_val(*pmd) = __pa(new_page) | prot;
					if (!IS_ALIGNED(addr, PMD_SIZE) ||
					    !IS_ALIGNED(next, PMD_SIZE)) {
						vmemmap_use_new_sub_pmd(addr, next);
					}
					continue;
				}
			}
			pte = vmem_pte_alloc();
			if (!pte)
				goto out;
			pmd_populate(&init_mm, pmd, pte);
		} else if (pmd_large(*pmd)) {
			if (!direct)
				vmemmap_use_sub_pmd(addr, next);
			continue;
		}
		ret = modify_pte_table(pmd, addr, next, add, direct);
		if (ret)
			goto out;
		if (!add)
			try_free_pte_table(pmd, addr & PMD_MASK);
	}
	ret = 0;
out:
	if (direct)
		update_page_count(PG_DIRECT_MAP_1M, add ? pages : -pages);
	return ret;
}

static void try_free_pmd_table(pud_t *pud, unsigned long start)
{
	const unsigned long end = start + PUD_SIZE;
	pmd_t *pmd;
	int i;

	/* Don't mess with any tables not fully in 1:1 mapping & vmemmap area */
	if (end > VMALLOC_START)
		return;
#ifdef CONFIG_KASAN
	if (start < KASAN_SHADOW_END && KASAN_SHADOW_START > end)
		return;
#endif
	pmd = pmd_offset(pud, start);
	for (i = 0; i < PTRS_PER_PMD; i++, pmd++)
		if (!pmd_none(*pmd))
			return;
	vmem_free_pages(pud_deref(*pud), CRST_ALLOC_ORDER);
	pud_clear(pud);
}

static int modify_pud_table(p4d_t *p4d, unsigned long addr, unsigned long end,
			    bool add, bool direct)
{
	unsigned long next, prot, pages = 0;
	int ret = -ENOMEM;
	pud_t *pud;
	pmd_t *pmd;

	prot = pgprot_val(REGION3_KERNEL);
	if (!MACHINE_HAS_NX)
		prot &= ~_REGION_ENTRY_NOEXEC;
	pud = pud_offset(p4d, addr);
	for (; addr < end; addr = next, pud++) {
		next = pud_addr_end(addr, end);
		if (!add) {
			if (pud_none(*pud))
				continue;
			if (pud_large(*pud)) {
				if (IS_ALIGNED(addr, PUD_SIZE) &&
				    IS_ALIGNED(next, PUD_SIZE)) {
					pud_clear(pud);
					pages++;
				}
				continue;
			}
		} else if (pud_none(*pud)) {
			if (IS_ALIGNED(addr, PUD_SIZE) &&
			    IS_ALIGNED(next, PUD_SIZE) &&
			    MACHINE_HAS_EDAT2 && addr && direct &&
			    !debug_pagealloc_enabled()) {
				pud_val(*pud) = addr | prot;
				pages++;
				continue;
			}
			pmd = vmem_crst_alloc(_SEGMENT_ENTRY_EMPTY);
			if (!pmd)
				goto out;
			pud_populate(&init_mm, pud, pmd);
		} else if (pud_large(*pud)) {
			continue;
		}
		ret = modify_pmd_table(pud, addr, next, add, direct);
		if (ret)
			goto out;
		if (!add)
			try_free_pmd_table(pud, addr & PUD_MASK);
	}
	ret = 0;
out:
	if (direct)
		update_page_count(PG_DIRECT_MAP_2G, add ? pages : -pages);
	return ret;
}

static void try_free_pud_table(p4d_t *p4d, unsigned long start)
{
	const unsigned long end = start + P4D_SIZE;
	pud_t *pud;
	int i;

	/* Don't mess with any tables not fully in 1:1 mapping & vmemmap area */
	if (end > VMALLOC_START)
		return;
#ifdef CONFIG_KASAN
	if (start < KASAN_SHADOW_END && KASAN_SHADOW_START > end)
		return;
#endif

	pud = pud_offset(p4d, start);
	for (i = 0; i < PTRS_PER_PUD; i++, pud++) {
		if (!pud_none(*pud))
			return;
	}
	vmem_free_pages(p4d_deref(*p4d), CRST_ALLOC_ORDER);
	p4d_clear(p4d);
}

static int modify_p4d_table(pgd_t *pgd, unsigned long addr, unsigned long end,
			    bool add, bool direct)
{
	unsigned long next;
	int ret = -ENOMEM;
	p4d_t *p4d;
	pud_t *pud;

	p4d = p4d_offset(pgd, addr);
	for (; addr < end; addr = next, p4d++) {
		next = p4d_addr_end(addr, end);
		if (!add) {
			if (p4d_none(*p4d))
				continue;
		} else if (p4d_none(*p4d)) {
			pud = vmem_crst_alloc(_REGION3_ENTRY_EMPTY);
			if (!pud)
				goto out;
			p4d_populate(&init_mm, p4d, pud);
		}
		ret = modify_pud_table(p4d, addr, next, add, direct);
		if (ret)
			goto out;
		if (!add)
			try_free_pud_table(p4d, addr & P4D_MASK);
	}
	ret = 0;
out:
	return ret;
}

static void try_free_p4d_table(pgd_t *pgd, unsigned long start)
{
	const unsigned long end = start + PGDIR_SIZE;
	p4d_t *p4d;
	int i;

	/* Don't mess with any tables not fully in 1:1 mapping & vmemmap area */
	if (end > VMALLOC_START)
		return;
#ifdef CONFIG_KASAN
	if (start < KASAN_SHADOW_END && KASAN_SHADOW_START > end)
		return;
#endif

	p4d = p4d_offset(pgd, start);
	for (i = 0; i < PTRS_PER_P4D; i++, p4d++) {
		if (!p4d_none(*p4d))
			return;
	}
	vmem_free_pages(pgd_deref(*pgd), CRST_ALLOC_ORDER);
	pgd_clear(pgd);
}

static int modify_pagetable(unsigned long start, unsigned long end, bool add,
			    bool direct)
{
	unsigned long addr, next;
	int ret = -ENOMEM;
	pgd_t *pgd;
	p4d_t *p4d;

	if (WARN_ON_ONCE(!PAGE_ALIGNED(start | end)))
		return -EINVAL;
	for (addr = start; addr < end; addr = next) {
		next = pgd_addr_end(addr, end);
		pgd = pgd_offset_k(addr);

		if (!add) {
			if (pgd_none(*pgd))
				continue;
		} else if (pgd_none(*pgd)) {
			p4d = vmem_crst_alloc(_REGION2_ENTRY_EMPTY);
			if (!p4d)
				goto out;
			pgd_populate(&init_mm, pgd, p4d);
		}
		ret = modify_p4d_table(pgd, addr, next, add, direct);
		if (ret)
			goto out;
		if (!add)
			try_free_p4d_table(pgd, addr & PGDIR_MASK);
	}
	ret = 0;
out:
	if (!add)
		flush_tlb_kernel_range(start, end);
	return ret;
}

static int add_pagetable(unsigned long start, unsigned long end, bool direct)
{
	return modify_pagetable(start, end, true, direct);
}

static int remove_pagetable(unsigned long start, unsigned long end, bool direct)
{
	return modify_pagetable(start, end, false, direct);
}

/*
 * Add a physical memory range to the 1:1 mapping.
 */
static int vmem_add_range(unsigned long start, unsigned long size)
{
	return add_pagetable(start, start + size, true);
}

/*
 * Remove a physical memory range from the 1:1 mapping.
 */
static void vmem_remove_range(unsigned long start, unsigned long size)
{
	remove_pagetable(start, start + size, true);
}

/*
 * Add a backed mem_map array to the virtual mem_map array.
 */
int __meminit vmemmap_populate(unsigned long start, unsigned long end, int node,
			       struct vmem_altmap *altmap)
{
	int ret;

	mutex_lock(&vmem_mutex);
	/* We don't care about the node, just use NUMA_NO_NODE on allocations */
	ret = add_pagetable(start, end, false);
	if (ret)
		remove_pagetable(start, end, false);
	mutex_unlock(&vmem_mutex);
	return ret;
}

void vmemmap_free(unsigned long start, unsigned long end,
		  struct vmem_altmap *altmap)
{
	mutex_lock(&vmem_mutex);
	remove_pagetable(start, end, false);
	mutex_unlock(&vmem_mutex);
}

void vmem_remove_mapping(unsigned long start, unsigned long size)
{
	mutex_lock(&vmem_mutex);
	vmem_remove_range(start, size);
	mutex_unlock(&vmem_mutex);
}

int vmem_add_mapping(unsigned long start, unsigned long size)
{
	int ret;

	if (start + size > VMEM_MAX_PHYS ||
	    start + size < start)
		return -ERANGE;

	mutex_lock(&vmem_mutex);
	ret = vmem_add_range(start, size);
	if (ret)
		vmem_remove_range(start, size);
	mutex_unlock(&vmem_mutex);
	return ret;
}

/*
 * map whole physical memory to virtual memory (identity mapping)
 * we reserve enough space in the vmalloc area for vmemmap to hotplug
 * additional memory segments.
 */
void __init vmem_map_init(void)
{
	struct memblock_region *reg;

	for_each_memblock(memory, reg)
		vmem_add_range(reg->base, reg->size);
	__set_memory((unsigned long)_stext,
		     (unsigned long)(_etext - _stext) >> PAGE_SHIFT,
		     SET_MEMORY_RO | SET_MEMORY_X);
	__set_memory((unsigned long)_etext,
		     (unsigned long)(__end_rodata - _etext) >> PAGE_SHIFT,
		     SET_MEMORY_RO);
	__set_memory((unsigned long)_sinittext,
		     (unsigned long)(_einittext - _sinittext) >> PAGE_SHIFT,
		     SET_MEMORY_RO | SET_MEMORY_X);
	__set_memory(__stext_dma, (__etext_dma - __stext_dma) >> PAGE_SHIFT,
		     SET_MEMORY_RO | SET_MEMORY_X);

	/* we need lowcore executable for our LPSWE instructions */
	set_memory_x(0, 1);

	pr_info("Write protected kernel read-only data: %luk\n",
		(unsigned long)(__end_rodata - _stext) >> 10);
}
