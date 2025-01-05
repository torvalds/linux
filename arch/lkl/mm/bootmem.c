// SPDX-License-Identifier: GPL-2.0
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <asm/host_ops.h>

unsigned long memory_start, memory_end;
static void *_memory_start;
static unsigned long mem_size;

void *empty_zero_page;

void __init bootmem_init(unsigned long mem_sz)
{
	unsigned long zones_max_pfn[MAX_NR_ZONES] = {0, };

	mem_size = mem_sz;

#ifdef CONFIG_MMU
	mem_size = PAGE_ALIGN(mem_size);
	lkl_ops->shmem_init(mem_size);
	void *lkl_va_base = (void *)CONFIG_LKL_MEMORY_START;
	enum lkl_prot shmem_prot = LKL_PROT_READ | LKL_PROT_WRITE;

	_memory_start = lkl_ops->shmem_mmap(lkl_va_base, 0, mem_size, shmem_prot);
#else
	if (lkl_ops->page_alloc) {
		mem_size = PAGE_ALIGN(mem_size);
		_memory_start = lkl_ops->page_alloc(mem_size);
	} else {
		_memory_start = lkl_ops->mem_alloc(mem_size);
	}
#endif
	memory_start = (unsigned long)_memory_start;
	BUG_ON(!memory_start);
	memory_end = memory_start + mem_size;

	if (PAGE_ALIGN(memory_start) != memory_start) {
		mem_size -= PAGE_ALIGN(memory_start) - memory_start;
		memory_start = PAGE_ALIGN(memory_start);
		mem_size = (mem_size / PAGE_SIZE) * PAGE_SIZE;
	}
	pr_info("memblock address range: 0x%lx - 0x%lx\n", memory_start,
		memory_start+mem_size);
	/*
	 * Give all the memory to the bootmap allocator, tell it to put the
	 * boot mem_map at the start of memory.
	 */
	max_low_pfn = virt_to_pfn((void *)memory_end);
	min_low_pfn = virt_to_pfn((void *)memory_start);
	memblock_add(__pa(memory_start), mem_size);

	empty_zero_page = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	memset(empty_zero_page, 0, PAGE_SIZE);

	zones_max_pfn[ZONE_NORMAL] = max_low_pfn;
	free_area_init(zones_max_pfn);
}

void __init mem_init(void)
{
	memblock_free_all();
	max_mapnr = totalram_pages();
	max_low_pfn = max_mapnr + ARCH_PFN_OFFSET;
	max_pfn = max_mapnr + ARCH_PFN_OFFSET;
}

/*
 * In our case __init memory is not part of the page allocator so there is
 * nothing to free.
 */
void free_initmem(void)
{
}

void free_mem(void)
{
	if (lkl_ops->page_free)
		lkl_ops->page_free(_memory_start, mem_size);
	else
		lkl_ops->mem_free(_memory_start);
}
