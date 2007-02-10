/*
 *  arch/s390/mm/vmem.c
 *
 *    Copyright IBM Corp. 2006
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/bootmem.h>
#include <linux/pfn.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/list.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>

unsigned long vmalloc_end;
EXPORT_SYMBOL(vmalloc_end);

static struct page *vmem_map;
static DEFINE_MUTEX(vmem_mutex);

struct memory_segment {
	struct list_head list;
	unsigned long start;
	unsigned long size;
};

static LIST_HEAD(mem_segs);

void memmap_init(unsigned long size, int nid, unsigned long zone,
		 unsigned long start_pfn)
{
	struct page *start, *end;
	struct page *map_start, *map_end;
	int i;

	start = pfn_to_page(start_pfn);
	end = start + size;

	for (i = 0; i < MEMORY_CHUNKS && memory_chunk[i].size > 0; i++) {
		unsigned long cstart, cend;

		cstart = PFN_DOWN(memory_chunk[i].addr);
		cend = cstart + PFN_DOWN(memory_chunk[i].size);

		map_start = mem_map + cstart;
		map_end = mem_map + cend;

		if (map_start < start)
			map_start = start;
		if (map_end > end)
			map_end = end;

		map_start -= ((unsigned long) map_start & (PAGE_SIZE - 1))
			/ sizeof(struct page);
		map_end += ((PFN_ALIGN((unsigned long) map_end)
			     - (unsigned long) map_end)
			    / sizeof(struct page));

		if (map_start < map_end)
			memmap_init_zone((unsigned long)(map_end - map_start),
					 nid, zone, page_to_pfn(map_start),
					 MEMMAP_EARLY);
	}
}

static inline void *vmem_alloc_pages(unsigned int order)
{
	if (slab_is_available())
		return (void *)__get_free_pages(GFP_KERNEL, order);
	return alloc_bootmem_pages((1 << order) * PAGE_SIZE);
}

static inline pmd_t *vmem_pmd_alloc(void)
{
	pmd_t *pmd;
	int i;

	pmd = vmem_alloc_pages(PMD_ALLOC_ORDER);
	if (!pmd)
		return NULL;
	for (i = 0; i < PTRS_PER_PMD; i++)
		pmd_clear_kernel(pmd + i);
	return pmd;
}

static inline pte_t *vmem_pte_alloc(void)
{
	pte_t *pte;
	pte_t empty_pte;
	int i;

	pte = vmem_alloc_pages(PTE_ALLOC_ORDER);
	if (!pte)
		return NULL;
	pte_val(empty_pte) = _PAGE_TYPE_EMPTY;
	for (i = 0; i < PTRS_PER_PTE; i++)
		pte[i] = empty_pte;
	return pte;
}

/*
 * Add a physical memory range to the 1:1 mapping.
 */
static int vmem_add_range(unsigned long start, unsigned long size)
{
	unsigned long address;
	pgd_t *pg_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;
	pte_t  pte;
	int ret = -ENOMEM;

	for (address = start; address < start + size; address += PAGE_SIZE) {
		pg_dir = pgd_offset_k(address);
		if (pgd_none(*pg_dir)) {
			pm_dir = vmem_pmd_alloc();
			if (!pm_dir)
				goto out;
			pgd_populate_kernel(&init_mm, pg_dir, pm_dir);
		}

		pm_dir = pmd_offset(pg_dir, address);
		if (pmd_none(*pm_dir)) {
			pt_dir = vmem_pte_alloc();
			if (!pt_dir)
				goto out;
			pmd_populate_kernel(&init_mm, pm_dir, pt_dir);
		}

		pt_dir = pte_offset_kernel(pm_dir, address);
		pte = pfn_pte(address >> PAGE_SHIFT, PAGE_KERNEL);
		*pt_dir = pte;
	}
	ret = 0;
out:
	flush_tlb_kernel_range(start, start + size);
	return ret;
}

/*
 * Remove a physical memory range from the 1:1 mapping.
 * Currently only invalidates page table entries.
 */
static void vmem_remove_range(unsigned long start, unsigned long size)
{
	unsigned long address;
	pgd_t *pg_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;
	pte_t  pte;

	pte_val(pte) = _PAGE_TYPE_EMPTY;
	for (address = start; address < start + size; address += PAGE_SIZE) {
		pg_dir = pgd_offset_k(address);
		if (pgd_none(*pg_dir))
			continue;
		pm_dir = pmd_offset(pg_dir, address);
		if (pmd_none(*pm_dir))
			continue;
		pt_dir = pte_offset_kernel(pm_dir, address);
		*pt_dir = pte;
	}
	flush_tlb_kernel_range(start, start + size);
}

/*
 * Add a backed mem_map array to the virtual mem_map array.
 */
static int vmem_add_mem_map(unsigned long start, unsigned long size)
{
	unsigned long address, start_addr, end_addr;
	struct page *map_start, *map_end;
	pgd_t *pg_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;
	pte_t  pte;
	int ret = -ENOMEM;

	map_start = vmem_map + PFN_DOWN(start);
	map_end	= vmem_map + PFN_DOWN(start + size);

	start_addr = (unsigned long) map_start & PAGE_MASK;
	end_addr = PFN_ALIGN((unsigned long) map_end);

	for (address = start_addr; address < end_addr; address += PAGE_SIZE) {
		pg_dir = pgd_offset_k(address);
		if (pgd_none(*pg_dir)) {
			pm_dir = vmem_pmd_alloc();
			if (!pm_dir)
				goto out;
			pgd_populate_kernel(&init_mm, pg_dir, pm_dir);
		}

		pm_dir = pmd_offset(pg_dir, address);
		if (pmd_none(*pm_dir)) {
			pt_dir = vmem_pte_alloc();
			if (!pt_dir)
				goto out;
			pmd_populate_kernel(&init_mm, pm_dir, pt_dir);
		}

		pt_dir = pte_offset_kernel(pm_dir, address);
		if (pte_none(*pt_dir)) {
			unsigned long new_page;

			new_page =__pa(vmem_alloc_pages(0));
			if (!new_page)
				goto out;
			pte = pfn_pte(new_page >> PAGE_SHIFT, PAGE_KERNEL);
			*pt_dir = pte;
		}
	}
	ret = 0;
out:
	flush_tlb_kernel_range(start_addr, end_addr);
	return ret;
}

static int vmem_add_mem(unsigned long start, unsigned long size)
{
	int ret;

	ret = vmem_add_range(start, size);
	if (ret)
		return ret;
	return vmem_add_mem_map(start, size);
}

/*
 * Add memory segment to the segment list if it doesn't overlap with
 * an already present segment.
 */
static int insert_memory_segment(struct memory_segment *seg)
{
	struct memory_segment *tmp;

	if (PFN_DOWN(seg->start + seg->size) > max_pfn ||
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

int remove_shared_memory(unsigned long start, unsigned long size)
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

int add_shared_memory(unsigned long start, unsigned long size)
{
	struct memory_segment *seg;
	struct page *page;
	unsigned long pfn, num_pfn, end_pfn;
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

	pfn = PFN_DOWN(start);
	num_pfn = PFN_DOWN(size);
	end_pfn = pfn + num_pfn;

	page = pfn_to_page(pfn);
	memset(page, 0, num_pfn * sizeof(struct page));

	for (; pfn < end_pfn; pfn++) {
		page = pfn_to_page(pfn);
		init_page_count(page);
		reset_page_mapcount(page);
		SetPageReserved(page);
		INIT_LIST_HEAD(&page->lru);
	}
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
 */
void __init vmem_map_init(void)
{
	unsigned long map_size;
	int i;

	map_size = ALIGN(max_low_pfn, MAX_ORDER_NR_PAGES) * sizeof(struct page);
	vmalloc_end = PFN_ALIGN(VMALLOC_END_INIT) - PFN_ALIGN(map_size);
	vmem_map = (struct page *) vmalloc_end;
	NODE_DATA(0)->node_mem_map = vmem_map;

	for (i = 0; i < MEMORY_CHUNKS && memory_chunk[i].size > 0; i++)
		vmem_add_mem(memory_chunk[i].addr, memory_chunk[i].size);
}

/*
 * Convert memory chunk array to a memory segment list so there is a single
 * list that contains both r/w memory and shared memory segments.
 */
static int __init vmem_convert_memory_chunk(void)
{
	struct memory_segment *seg;
	int i;

	mutex_lock(&vmem_mutex);
	for (i = 0; i < MEMORY_CHUNKS && memory_chunk[i].size > 0; i++) {
		if (!memory_chunk[i].size)
			continue;
		seg = kzalloc(sizeof(*seg), GFP_KERNEL);
		if (!seg)
			panic("Out of memory...\n");
		seg->start = memory_chunk[i].addr;
		seg->size = memory_chunk[i].size;
		insert_memory_segment(seg);
	}
	mutex_unlock(&vmem_mutex);
	return 0;
}

core_initcall(vmem_convert_memory_chunk);
