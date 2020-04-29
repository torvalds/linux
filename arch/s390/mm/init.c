// SPDX-License-Identifier: GPL-2.0
/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1995  Linus Torvalds
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
#include <linux/swap.h>
#include <linux/swiotlb.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/memblock.h>
#include <linux/memory.h>
#include <linux/pfn.h>
#include <linux/poison.h>
#include <linux/initrd.h>
#include <linux/export.h>
#include <linux/cma.h>
#include <linux/gfp.h>
#include <linux/dma-direct.h>
#include <asm/processor.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/lowcore.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/ctl_reg.h>
#include <asm/sclp.h>
#include <asm/set_memory.h>
#include <asm/kasan.h>
#include <asm/dma-mapping.h>
#include <asm/uv.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD] __section(.bss..swapper_pg_dir);

unsigned long empty_zero_page, zero_page_mask;
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(zero_page_mask);

bool initmem_freed;

static void __init setup_zero_pages(void)
{
	unsigned int order;
	struct page *page;
	int i;

	/* Latest machines require a mapping granularity of 512KB */
	order = 7;

	/* Limit number of empty zero pages for small memory sizes */
	while (order > 2 && (totalram_pages() >> 10) < (1UL << order))
		order--;

	empty_zero_page = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!empty_zero_page)
		panic("Out of memory in setup_zero_pages");

	page = virt_to_page((void *) empty_zero_page);
	split_page(page, order);
	for (i = 1 << order; i > 0; i--) {
		mark_page_reserved(page);
		page++;
	}

	zero_page_mask = ((PAGE_SIZE << order) - 1) & PAGE_MASK;
}

/*
 * paging_init() sets up the page tables
 */
void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	unsigned long pgd_type, asce_bits;
	psw_t psw;

	init_mm.pgd = swapper_pg_dir;
	if (VMALLOC_END > _REGION2_SIZE) {
		asce_bits = _ASCE_TYPE_REGION2 | _ASCE_TABLE_LENGTH;
		pgd_type = _REGION2_ENTRY_EMPTY;
	} else {
		asce_bits = _ASCE_TYPE_REGION3 | _ASCE_TABLE_LENGTH;
		pgd_type = _REGION3_ENTRY_EMPTY;
	}
	init_mm.context.asce = (__pa(init_mm.pgd) & PAGE_MASK) | asce_bits;
	S390_lowcore.kernel_asce = init_mm.context.asce;
	S390_lowcore.user_asce = S390_lowcore.kernel_asce;
	crst_table_init((unsigned long *) init_mm.pgd, pgd_type);
	vmem_map_init();
	kasan_copy_shadow(init_mm.pgd);

	/* enable virtual mapping in kernel mode */
	__ctl_load(S390_lowcore.kernel_asce, 1, 1);
	__ctl_load(S390_lowcore.kernel_asce, 7, 7);
	__ctl_load(S390_lowcore.kernel_asce, 13, 13);
	psw.mask = __extract_psw();
	psw_bits(psw).dat = 1;
	psw_bits(psw).as = PSW_BITS_AS_HOME;
	__load_psw_mask(psw.mask);
	kasan_free_early_identity();

	sparse_memory_present_with_active_regions(MAX_NUMNODES);
	sparse_init();
	zone_dma_bits = 31;
	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] = PFN_DOWN(MAX_DMA_ADDRESS);
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
	free_area_init_nodes(max_zone_pfns);
}

void mark_rodata_ro(void)
{
	unsigned long size = __end_ro_after_init - __start_ro_after_init;

	set_memory_ro((unsigned long)__start_ro_after_init, size >> PAGE_SHIFT);
	pr_info("Write protected read-only-after-init data: %luk\n", size >> 10);
}

int set_memory_encrypted(unsigned long addr, int numpages)
{
	int i;

	/* make specified pages unshared, (swiotlb, dma_free) */
	for (i = 0; i < numpages; ++i) {
		uv_remove_shared(addr);
		addr += PAGE_SIZE;
	}
	return 0;
}

int set_memory_decrypted(unsigned long addr, int numpages)
{
	int i;
	/* make specified pages shared (swiotlb, dma_alloca) */
	for (i = 0; i < numpages; ++i) {
		uv_set_shared(addr);
		addr += PAGE_SIZE;
	}
	return 0;
}

/* are we a protected virtualization guest? */
bool force_dma_unencrypted(struct device *dev)
{
	return is_prot_virt_guest();
}

/* protected virtualization */
static void pv_init(void)
{
	if (!is_prot_virt_guest())
		return;

	/* make sure bounce buffers are shared */
	swiotlb_init(1);
	swiotlb_update_mem_attributes();
	swiotlb_force = SWIOTLB_FORCE;
}

void __init mem_init(void)
{
	cpumask_set_cpu(0, &init_mm.context.cpu_attach_mask);
	cpumask_set_cpu(0, mm_cpumask(&init_mm));

	set_max_mapnr(max_low_pfn);
        high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

	pv_init();

	/* Setup guest page hinting */
	cmma_init();

	/* this will put all low memory onto the freelists */
	memblock_free_all();
	setup_zero_pages();	/* Setup zeroed pages. */

	cmma_init_nodat();

	mem_init_print_info(NULL);
}

void free_initmem(void)
{
	initmem_freed = true;
	__set_memory((unsigned long)_sinittext,
		     (unsigned long)(_einittext - _sinittext) >> PAGE_SHIFT,
		     SET_MEMORY_RW | SET_MEMORY_NX);
	free_initmem_default(POISON_FREE_INITMEM);
}

unsigned long memory_block_size_bytes(void)
{
	/*
	 * Make sure the memory block size is always greater
	 * or equal than the memory increment size.
	 */
	return max_t(unsigned long, MIN_MEMORY_BLOCK_SIZE, sclp.rzm);
}

#ifdef CONFIG_MEMORY_HOTPLUG

#ifdef CONFIG_CMA

/* Prevent memory blocks which contain cma regions from going offline */

struct s390_cma_mem_data {
	unsigned long start;
	unsigned long end;
};

static int s390_cma_check_range(struct cma *cma, void *data)
{
	struct s390_cma_mem_data *mem_data;
	unsigned long start, end;

	mem_data = data;
	start = cma_get_base(cma);
	end = start + cma_get_size(cma);
	if (end < mem_data->start)
		return 0;
	if (start >= mem_data->end)
		return 0;
	return -EBUSY;
}

static int s390_cma_mem_notifier(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct s390_cma_mem_data mem_data;
	struct memory_notify *arg;
	int rc = 0;

	arg = data;
	mem_data.start = arg->start_pfn << PAGE_SHIFT;
	mem_data.end = mem_data.start + (arg->nr_pages << PAGE_SHIFT);
	if (action == MEM_GOING_OFFLINE)
		rc = cma_for_each_area(s390_cma_check_range, &mem_data);
	return notifier_from_errno(rc);
}

static struct notifier_block s390_cma_mem_nb = {
	.notifier_call = s390_cma_mem_notifier,
};

static int __init s390_cma_mem_init(void)
{
	return register_memory_notifier(&s390_cma_mem_nb);
}
device_initcall(s390_cma_mem_init);

#endif /* CONFIG_CMA */

int arch_add_memory(int nid, u64 start, u64 size,
		    struct mhp_params *params)
{
	unsigned long start_pfn = PFN_DOWN(start);
	unsigned long size_pages = PFN_DOWN(size);
	int rc;

	if (WARN_ON_ONCE(params->altmap))
		return -EINVAL;

	if (WARN_ON_ONCE(params->pgprot.pgprot != PAGE_KERNEL.pgprot))
		return -EINVAL;

	rc = vmem_add_mapping(start, size);
	if (rc)
		return rc;

	rc = __add_pages(nid, start_pfn, size_pages, params);
	if (rc)
		vmem_remove_mapping(start, size);
	return rc;
}

void arch_remove_memory(int nid, u64 start, u64 size,
			struct vmem_altmap *altmap)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;

	__remove_pages(start_pfn, nr_pages, altmap);
	vmem_remove_mapping(start, size);
}
#endif /* CONFIG_MEMORY_HOTPLUG */
