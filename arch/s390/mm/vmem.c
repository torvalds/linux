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
	if (slab_is_available())
		return (void *)__get_free_pages(GFP_KERNEL, order);
	return alloc_bootmem_pages((1 << order) * PAGE_SIZE);
}

static inline pud_t *vmem_pud_alloc(void)
{
	pud_t *pud = NULL;

#ifdef CONFIG_64BIT
	pud = vmem_alloc_pages(2);
	if (!pud)
		return NULL;
	clear_table((unsigned long *) pud, _REGION3_ENTRY_EMPTY, PAGE_SIZE * 4);
#endif
	return pud;
}

static inline pmd_t *vmem_pmd_alloc(void)
{
	pmd_t *pmd = NULL;

#ifdef CONFIG_64BIT
	pmd = vmem_alloc_pages(2);
	if (!pmd)
		return NULL;
	clear_table((unsigned long *) pmd, _SEGMENT_ENTRY_EMPTY, PAGE_SIZE * 4);
#endif
	return pmd;
}

static pte_t __ref *vmem_pte_alloc(unsigned long address)
{
	pte_t *pte;

	if (slab_is_available())
		pte = (pte_t *) page_table_alloc(&init_mm, address);
	else
		pte = alloc_bootmem(PTRS_PER_PTE * sizeof(pte_t));
	if (!pte)
		return NULL;
	clear_table((unsigned long *) pte, _PAGE_TYPE_EMPTY,
		    PTRS_PER_PTE * sizeof(pte_t));
	return pte;
}

/*
 * Add a physical memory range to the 1:1 mapping.
 */
static int vmem_add_mem(unsigned long start, unsigned long size, int ro)
{
	unsigned long address;
	pgd_t *pg_dir;
	pud_t *pu_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;
	pte_t  pte;
	int ret = -ENOMEM;

	for (address = start; address < start + size; address += PAGE_SIZE) {
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

		pte = mk_pte_phys(address, __pgprot(ro ? _PAGE_RO : 0));
		pm_dir = pmd_offset(pu_dir, address);

#ifdef CONFIG_64BIT
		if (MACHINE_HAS_HPAGE && !(address & ~HPAGE_MASK) &&
		    (address + HPAGE_SIZE <= start + size) &&
		    (address >= HPAGE_SIZE)) {
			pte_val(pte) |= _SEGMENT_ENTRY_LARGE;
			pmd_val(*pm_dir) = pte_val(pte);
			address += HPAGE_SIZE - PAGE_SIZE;
			continue;
		}
#endif
		if (pmd_none(*pm_dir)) {
			pt_dir = vmem_pte_alloc(address);
			if (!pt_dir)
				goto out;
			pmd_populate(&init_mm, pm_dir, pt_dir);
		}

		pt_dir = pte_offset_kernel(pm_dir, address);
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
	pud_t *pu_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;
	pte_t  pte;

	pte_val(pte) = _PAGE_TYPE_EMPTY;
	for (address = start; address < start + size; address += PAGE_SIZE) {
		pg_dir = pgd_offset_k(address);
		pu_dir = pud_offset(pg_dir, address);
		if (pud_none(*pu_dir))
			continue;
		pm_dir = pmd_offset(pu_dir, address);
		if (pmd_none(*pm_dir))
			continue;

		if (pmd_huge(*pm_dir)) {
			pmd_clear(pm_dir);
			address += HPAGE_SIZE - PAGE_SIZE;
			continue;
		}

		pt_dir = pte_offset_kernel(pm_dir, address);
		*pt_dir = pte;
	}
	flush_tlb_kernel_range(start, start + size);
}

/*
 * Add a backed mem_map array to the virtual mem_map array.
 */
int __meminit vmemmap_populate(struct page *start, unsigned long nr, int node)
{
	unsigned long address, start_addr, end_addr;
	pgd_t *pg_dir;
	pud_t *pu_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;
	pte_t  pte;
	int ret = -ENOMEM;

	start_addr = (unsigned long) start;
	end_addr = (unsigned long) (start + nr);

	for (address = start_addr; address < end_addr; address += PAGE_SIZE) {
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
			pt_dir = vmem_pte_alloc(address);
			if (!pt_dir)
				goto out;
			pmd_populate(&init_mm, pm_dir, pt_dir);
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
	memset(start, 0, nr * sizeof(struct page));
	ret = 0;
out:
	flush_tlb_kernel_range(start_addr, end_addr);
	return ret;
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

	ret = vmem_add_mem(start, size, 0);
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
	unsigned long ro_start, ro_end;
	unsigned long start, end;
	int i;

	ro_start = ((unsigned long)&_stext) & PAGE_MASK;
	ro_end = PFN_ALIGN((unsigned long)&_eshared);
	for (i = 0; i < MEMORY_CHUNKS && memory_chunk[i].size > 0; i++) {
		if (memory_chunk[i].type == CHUNK_CRASHK ||
		    memory_chunk[i].type == CHUNK_OLDMEM)
			continue;
		start = memory_chunk[i].addr;
		end = memory_chunk[i].addr + memory_chunk[i].size;
		if (start >= ro_end || end <= ro_start)
			vmem_add_mem(start, end - start, 0);
		else if (start >= ro_start && end <= ro_end)
			vmem_add_mem(start, end - start, 1);
		else if (start >= ro_start) {
			vmem_add_mem(start, ro_end - start, 1);
			vmem_add_mem(ro_end, end - ro_end, 0);
		} else if (end < ro_end) {
			vmem_add_mem(start, ro_start - start, 0);
			vmem_add_mem(ro_start, end - ro_start, 1);
		} else {
			vmem_add_mem(start, ro_start - start, 0);
			vmem_add_mem(ro_start, ro_end - ro_start, 1);
			vmem_add_mem(ro_end, end - ro_end, 0);
		}
	}
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
	for (i = 0; i < MEMORY_CHUNKS; i++) {
		if (!memory_chunk[i].size)
			continue;
		if (memory_chunk[i].type == CHUNK_CRASHK ||
		    memory_chunk[i].type == CHUNK_OLDMEM)
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
