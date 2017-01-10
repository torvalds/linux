/*
 *    Copyright IBM Corp. 2006
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/bootmem.h>
#include <linux/pfn.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <asm/cacheflush.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>

static DEFINE_MUTEX(vmem_mutex);

struct memory_segment {
	struct list_head list;
	unsigned long start;
	unsigned long size;
};

static LIST_HEAD(mem_segs);

static void __ref *vmem_alloc_pages(unsigned int order)
{
	unsigned long size = PAGE_SIZE << order;

	if (slab_is_available())
		return (void *)__get_free_pages(GFP_KERNEL, order);
	return (void *) memblock_alloc(size, size);
}

static inline pud_t *vmem_pud_alloc(void)
{
	pud_t *pud = NULL;

	pud = vmem_alloc_pages(2);
	if (!pud)
		return NULL;
	clear_table((unsigned long *) pud, _REGION3_ENTRY_EMPTY, PAGE_SIZE * 4);
	return pud;
}

pmd_t *vmem_pmd_alloc(void)
{
	pmd_t *pmd = NULL;

	pmd = vmem_alloc_pages(2);
	if (!pmd)
		return NULL;
	clear_table((unsigned long *) pmd, _SEGMENT_ENTRY_EMPTY, PAGE_SIZE * 4);
	return pmd;
}

pte_t __ref *vmem_pte_alloc(void)
{
	unsigned long size = PTRS_PER_PTE * sizeof(pte_t);
	pte_t *pte;

	if (slab_is_available())
		pte = (pte_t *) page_table_alloc(&init_mm);
	else
		pte = (pte_t *) memblock_alloc(size, size);
	if (!pte)
		return NULL;
	clear_table((unsigned long *) pte, _PAGE_INVALID, size);
	return pte;
}

/*
 * Add a physical memory range to the 1:1 mapping.
 */
static int vmem_add_mem(unsigned long start, unsigned long size)
{
	unsigned long pages4k, pages1m, pages2g;
	unsigned long end = start + size;
	unsigned long address = start;
	pgd_t *pg_dir;
	pud_t *pu_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;
	int ret = -ENOMEM;

	pages4k = pages1m = pages2g = 0;
	while (address < end) {
		pg_dir = pgd_offset_k(address);
		if (pgd_none(*pg_dir)) {
			pu_dir = vmem_pud_alloc();
			if (!pu_dir)
				goto out;
			pgd_populate(&init_mm, pg_dir, pu_dir);
		}
		pu_dir = pud_offset(pg_dir, address);
		if (MACHINE_HAS_EDAT2 && pud_none(*pu_dir) && address &&
		    !(address & ~PUD_MASK) && (address + PUD_SIZE <= end) &&
		     !debug_pagealloc_enabled()) {
			pud_val(*pu_dir) = address | pgprot_val(REGION3_KERNEL);
			address += PUD_SIZE;
			pages2g++;
			continue;
		}
		if (pud_none(*pu_dir)) {
			pm_dir = vmem_pmd_alloc();
			if (!pm_dir)
				goto out;
			pud_populate(&init_mm, pu_dir, pm_dir);
		}
		pm_dir = pmd_offset(pu_dir, address);
		if (MACHINE_HAS_EDAT1 && pmd_none(*pm_dir) && address &&
		    !(address & ~PMD_MASK) && (address + PMD_SIZE <= end) &&
		    !debug_pagealloc_enabled()) {
			pmd_val(*pm_dir) = address | pgprot_val(SEGMENT_KERNEL);
			address += PMD_SIZE;
			pages1m++;
			continue;
		}
		if (pmd_none(*pm_dir)) {
			pt_dir = vmem_pte_alloc();
			if (!pt_dir)
				goto out;
			pmd_populate(&init_mm, pm_dir, pt_dir);
		}

		pt_dir = pte_offset_kernel(pm_dir, address);
		pte_val(*pt_dir) = address |  pgprot_val(PAGE_KERNEL);
		address += PAGE_SIZE;
		pages4k++;
	}
	ret = 0;
out:
	update_page_count(PG_DIRECT_MAP_4K, pages4k);
	update_page_count(PG_DIRECT_MAP_1M, pages1m);
	update_page_count(PG_DIRECT_MAP_2G, pages2g);
	return ret;
}

/*
 * Remove a physical memory range from the 1:1 mapping.
 * Currently only invalidates page table entries.
 */
static void vmem_remove_range(unsigned long start, unsigned long size)
{
	unsigned long pages4k, pages1m, pages2g;
	unsigned long end = start + size;
	unsigned long address = start;
	pgd_t *pg_dir;
	pud_t *pu_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;

	pages4k = pages1m = pages2g = 0;
	while (address < end) {
		pg_dir = pgd_offset_k(address);
		if (pgd_none(*pg_dir)) {
			address += PGDIR_SIZE;
			continue;
		}
		pu_dir = pud_offset(pg_dir, address);
		if (pud_none(*pu_dir)) {
			address += PUD_SIZE;
			continue;
		}
		if (pud_large(*pu_dir)) {
			pud_clear(pu_dir);
			address += PUD_SIZE;
			pages2g++;
			continue;
		}
		pm_dir = pmd_offset(pu_dir, address);
		if (pmd_none(*pm_dir)) {
			address += PMD_SIZE;
			continue;
		}
		if (pmd_large(*pm_dir)) {
			pmd_clear(pm_dir);
			address += PMD_SIZE;
			pages1m++;
			continue;
		}
		pt_dir = pte_offset_kernel(pm_dir, address);
		pte_clear(&init_mm, address, pt_dir);
		address += PAGE_SIZE;
		pages4k++;
	}
	flush_tlb_kernel_range(start, end);
	update_page_count(PG_DIRECT_MAP_4K, -pages4k);
	update_page_count(PG_DIRECT_MAP_1M, -pages1m);
	update_page_count(PG_DIRECT_MAP_2G, -pages2g);
}

/*
 * Add a backed mem_map array to the virtual mem_map array.
 */
int __meminit vmemmap_populate(unsigned long start, unsigned long end, int node)
{
	unsigned long address = start;
	pgd_t *pg_dir;
	pud_t *pu_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;
	int ret = -ENOMEM;

	for (address = start; address < end;) {
		pg_dir = pgd_offset_k(address);
		if (pgd_none(*pg_dir)) {
			pu_dir = vmem_pud_alloc();
			if (!pu_dir)
				goto out;
			pgd_populate(&init_mm, pg_dir, pu_dir);
		}

		pu_dir = pud_offset(pg_dir, address);
		if (pud_none(*pu_dir)) {
			pm_dir = vmem_pmd_alloc();
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
				pmd_val(*pm_dir) = __pa(new_page) |
					_SEGMENT_ENTRY | _SEGMENT_ENTRY_LARGE;
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
			pte_val(*pt_dir) =
				__pa(new_page) | pgprot_val(PAGE_KERNEL);
		}
		address += PAGE_SIZE;
	}
	ret = 0;
out:
	return ret;
}

void vmemmap_free(unsigned long start, unsigned long end)
{
}

/*
 * Add memory segment to the segment list if it doesn't overlap with
 * an already present segment.
 */
static int insert_memory_segment(struct memory_segment *seg)
{
	struct memory_segment *tmp;

	if (seg->start + seg->size > VMEM_MAX_PHYS ||
	    seg->start + seg->size < seg->start)
		return -ERANGE;

	list_for_each_entry(tmp, &mem_segs, list) {
		if (seg->start >= tmp->start + tmp->size)
			continue;
		if (seg->start + seg->size <= tmp->start)
			continue;
		return -ENOSPC;
	}
	list_add(&seg->list, &mem_segs);
	return 0;
}

/*
 * Remove memory segment from the segment list.
 */
static void remove_memory_segment(struct memory_segment *seg)
{
	list_del(&seg->list);
}

static void __remove_shared_memory(struct memory_segment *seg)
{
	remove_memory_segment(seg);
	vmem_remove_range(seg->start, seg->size);
}

int vmem_remove_mapping(unsigned long start, unsigned long size)
{
	struct memory_segment *seg;
	int ret;

	mutex_lock(&vmem_mutex);

	ret = -ENOENT;
	list_for_each_entry(seg, &mem_segs, list) {
		if (seg->start == start && seg->size == size)
			break;
	}

	if (seg->start != start || seg->size != size)
		goto out;

	ret = 0;
	__remove_shared_memory(seg);
	kfree(seg);
out:
	mutex_unlock(&vmem_mutex);
	return ret;
}

int vmem_add_mapping(unsigned long start, unsigned long size)
{
	struct memory_segment *seg;
	int ret;

	mutex_lock(&vmem_mutex);
	ret = -ENOMEM;
	seg = kzalloc(sizeof(*seg), GFP_KERNEL);
	if (!seg)
		goto out;
	seg->start = start;
	seg->size = size;

	ret = insert_memory_segment(seg);
	if (ret)
		goto out_free;

	ret = vmem_add_mem(start, size);
	if (ret)
		goto out_remove;
	goto out;

out_remove:
	__remove_shared_memory(seg);
out_free:
	kfree(seg);
out:
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
	unsigned long size = _eshared - _stext;
	struct memblock_region *reg;

	for_each_memblock(memory, reg)
		vmem_add_mem(reg->base, reg->size);
	set_memory_ro((unsigned long)_stext, size >> PAGE_SHIFT);
	pr_info("Write protected kernel read-only data: %luk\n", size >> 10);
}

/*
 * Convert memblock.memory  to a memory segment list so there is a single
 * list that contains all memory segments.
 */
static int __init vmem_convert_memory_chunk(void)
{
	struct memblock_region *reg;
	struct memory_segment *seg;

	mutex_lock(&vmem_mutex);
	for_each_memblock(memory, reg) {
		seg = kzalloc(sizeof(*seg), GFP_KERNEL);
		if (!seg)
			panic("Out of memory...\n");
		seg->start = reg->base;
		seg->size = reg->size;
		insert_memory_segment(seg);
	}
	mutex_unlock(&vmem_mutex);
	return 0;
}

core_initcall(vmem_convert_memory_chunk);
