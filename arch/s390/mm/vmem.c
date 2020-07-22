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

static void modify_pte_table(pmd_t *pmd, unsigned long addr, unsigned long end,
			    bool add)
{
	unsigned long prot, pages = 0;
	pte_t *pte;

	prot = pgprot_val(PAGE_KERNEL);
	if (!MACHINE_HAS_NX)
		prot &= ~_PAGE_NOEXEC;

	pte = pte_offset_kernel(pmd, addr);
	for (; addr < end; addr += PAGE_SIZE, pte++) {
		if (!add) {
			if (pte_none(*pte))
				continue;
			pte_clear(&init_mm, addr, pte);
		} else if (pte_none(*pte)) {
			pte_val(*pte) = addr | prot;
		} else
			continue;

		pages++;
	}

	update_page_count(PG_DIRECT_MAP_4K, add ? pages : -pages);
}

static int modify_pmd_table(pud_t *pud, unsigned long addr, unsigned long end,
			    bool add)
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
					pmd_clear(pmd);
					pages++;
				}
				continue;
			}
		} else if (pmd_none(*pmd)) {
			if (IS_ALIGNED(addr, PMD_SIZE) &&
			    IS_ALIGNED(next, PMD_SIZE) &&
			    MACHINE_HAS_EDAT1 && addr &&
			    !debug_pagealloc_enabled()) {
				pmd_val(*pmd) = addr | prot;
				pages++;
				continue;
			}
			pte = vmem_pte_alloc();
			if (!pte)
				goto out;
			pmd_populate(&init_mm, pmd, pte);
		} else if (pmd_large(*pmd))
			continue;

		modify_pte_table(pmd, addr, next, add);
	}
	ret = 0;
out:
	update_page_count(PG_DIRECT_MAP_1M, add ? pages : -pages);
	return ret;
}

static int modify_pud_table(p4d_t *p4d, unsigned long addr, unsigned long end,
			    bool add)
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
			    MACHINE_HAS_EDAT2 && addr &&
			    !debug_pagealloc_enabled()) {
				pud_val(*pud) = addr | prot;
				pages++;
				continue;
			}
			pmd = vmem_crst_alloc(_SEGMENT_ENTRY_EMPTY);
			if (!pmd)
				goto out;
			pud_populate(&init_mm, pud, pmd);
		} else if (pud_large(*pud))
			continue;

		ret = modify_pmd_table(pud, addr, next, add);
		if (ret)
			goto out;
	}
	ret = 0;
out:
	update_page_count(PG_DIRECT_MAP_2G, add ? pages : -pages);
	return ret;
}

static int modify_p4d_table(pgd_t *pgd, unsigned long addr, unsigned long end,
			    bool add)
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
		}

		ret = modify_pud_table(p4d, addr, next, add);
		if (ret)
			goto out;
	}
	ret = 0;
out:
	return ret;
}

static int modify_pagetable(unsigned long start, unsigned long end, bool add)
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

		ret = modify_p4d_table(pgd, addr, next, add);
		if (ret)
			goto out;
	}
	ret = 0;
out:
	if (!add)
		flush_tlb_kernel_range(start, end);
	return ret;
}

static int add_pagetable(unsigned long start, unsigned long end)
{
	return modify_pagetable(start, end, true);
}

static int remove_pagetable(unsigned long start, unsigned long end)
{
	return modify_pagetable(start, end, false);
}

/*
 * Add a physical memory range to the 1:1 mapping.
 */
static int vmem_add_range(unsigned long start, unsigned long size)
{
	return add_pagetable(start, start + size);
}

/*
 * Remove a physical memory range from the 1:1 mapping.
 * Currently only invalidates page table entries.
 */
static void vmem_remove_range(unsigned long start, unsigned long size)
{
	remove_pagetable(start, start + size);
}

/*
 * Add a backed mem_map array to the virtual mem_map array.
 */
int __meminit vmemmap_populate(unsigned long start, unsigned long end, int node,
		struct vmem_altmap *altmap)
{
	unsigned long pgt_prot, sgt_prot;
	unsigned long address = start;
	pgd_t *pg_dir;
	p4d_t *p4_dir;
	pud_t *pu_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;
	int ret = -ENOMEM;

	pgt_prot = pgprot_val(PAGE_KERNEL);
	sgt_prot = pgprot_val(SEGMENT_KERNEL);
	if (!MACHINE_HAS_NX) {
		pgt_prot &= ~_PAGE_NOEXEC;
		sgt_prot &= ~_SEGMENT_ENTRY_NOEXEC;
	}
	for (address = start; address < end;) {
		pg_dir = pgd_offset_k(address);
		if (pgd_none(*pg_dir)) {
			p4_dir = vmem_crst_alloc(_REGION2_ENTRY_EMPTY);
			if (!p4_dir)
				goto out;
			pgd_populate(&init_mm, pg_dir, p4_dir);
		}

		p4_dir = p4d_offset(pg_dir, address);
		if (p4d_none(*p4_dir)) {
			pu_dir = vmem_crst_alloc(_REGION3_ENTRY_EMPTY);
			if (!pu_dir)
				goto out;
			p4d_populate(&init_mm, p4_dir, pu_dir);
		}

		pu_dir = pud_offset(p4_dir, address);
		if (pud_none(*pu_dir)) {
			pm_dir = vmem_crst_alloc(_SEGMENT_ENTRY_EMPTY);
			if (!pm_dir)
				goto out;
			pud_populate(&init_mm, pu_dir, pm_dir);
		}

		pm_dir = pmd_offset(pu_dir, address);
		if (pmd_none(*pm_dir)) {
			/* Use 1MB frames for vmemmap if available. We always
			 * use large frames even if they are only partially
			 * used.
			 * Otherwise we would have also page tables since
			 * vmemmap_populate gets called for each section
			 * separately. */
			if (MACHINE_HAS_EDAT1) {
				void *new_page;

				new_page = vmemmap_alloc_block(PMD_SIZE, node);
				if (!new_page)
					goto out;
				pmd_val(*pm_dir) = __pa(new_page) | sgt_prot;
				address = (address + PMD_SIZE) & PMD_MASK;
				continue;
			}
			pt_dir = vmem_pte_alloc();
			if (!pt_dir)
				goto out;
			pmd_populate(&init_mm, pm_dir, pt_dir);
		} else if (pmd_large(*pm_dir)) {
			address = (address + PMD_SIZE) & PMD_MASK;
			continue;
		}

		pt_dir = pte_offset_kernel(pm_dir, address);
		if (pte_none(*pt_dir)) {
			void *new_page;

			new_page = vmemmap_alloc_block(PAGE_SIZE, node);
			if (!new_page)
				goto out;
			pte_val(*pt_dir) = __pa(new_page) | pgt_prot;
		}
		address += PAGE_SIZE;
	}
	ret = 0;
out:
	return ret;
}

void vmemmap_free(unsigned long start, unsigned long end,
		struct vmem_altmap *altmap)
{
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
