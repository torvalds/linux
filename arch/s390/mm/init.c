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
#include <linux/percpu.h>
#include <asm/processor.h>
#include <linux/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/ctlreg.h>
#include <asm/kfence.h>
#include <asm/dma.h>
#include <asm/abs_lowcore.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/sclp.h>
#include <asm/set_memory.h>
#include <asm/kasan.h>
#include <asm/dma-mapping.h>
#include <asm/uv.h>
#include <linux/virtio_anchor.h>
#include <linux/virtio_config.h>
#include <linux/execmem.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD] __section(".bss..swapper_pg_dir");
pgd_t invalid_pg_dir[PTRS_PER_PGD] __section(".bss..invalid_pg_dir");

struct ctlreg __bootdata_preserved(s390_invalid_asce);

unsigned long empty_zero_page, zero_page_mask;
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(zero_page_mask);

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

	vmem_map_init();
	sparse_init();
	zone_dma_bits = 31;
	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] = virt_to_pfn(MAX_DMA_ADDRESS);
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
	free_area_init(max_zone_pfns);
}

void mark_rodata_ro(void)
{
	unsigned long size = __end_ro_after_init - __start_ro_after_init;

	__set_memory_ro(__start_ro_after_init, __end_ro_after_init);
	pr_info("Write protected read-only-after-init data: %luk\n", size >> 10);
}

int set_memory_encrypted(unsigned long vaddr, int numpages)
{
	int i;

	/* make specified pages unshared, (swiotlb, dma_free) */
	for (i = 0; i < numpages; ++i) {
		uv_remove_shared(virt_to_phys((void *)vaddr));
		vaddr += PAGE_SIZE;
	}
	return 0;
}

int set_memory_decrypted(unsigned long vaddr, int numpages)
{
	int i;
	/* make specified pages shared (swiotlb, dma_alloca) */
	for (i = 0; i < numpages; ++i) {
		uv_set_shared(virt_to_phys((void *)vaddr));
		vaddr += PAGE_SIZE;
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

	virtio_set_mem_acc_cb(virtio_require_restricted_mem_acc);

	/* make sure bounce buffers are shared */
	swiotlb_init(true, SWIOTLB_FORCE | SWIOTLB_VERBOSE);
	swiotlb_update_mem_attributes();
}

void __init mem_init(void)
{
	cpumask_set_cpu(0, &init_mm.context.cpu_attach_mask);
	cpumask_set_cpu(0, mm_cpumask(&init_mm));

	set_max_mapnr(max_low_pfn);
        high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

	pv_init();
	kfence_split_mapping();

	/* this will put all low memory onto the freelists */
	memblock_free_all();
	setup_zero_pages();	/* Setup zeroed pages. */
}

void free_initmem(void)
{
	set_memory_rwnx((unsigned long)_sinittext,
			(unsigned long)(_einittext - _sinittext) >> PAGE_SHIFT);
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

unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

static int __init pcpu_cpu_distance(unsigned int from, unsigned int to)
{
	return LOCAL_DISTANCE;
}

static int __init pcpu_cpu_to_node(int cpu)
{
	return 0;
}

void __init setup_per_cpu_areas(void)
{
	unsigned long delta;
	unsigned int cpu;
	int rc;

	/*
	 * Always reserve area for module percpu variables.  That's
	 * what the legacy allocator did.
	 */
	rc = pcpu_embed_first_chunk(PERCPU_MODULE_RESERVE,
				    PERCPU_DYNAMIC_RESERVE, PAGE_SIZE,
				    pcpu_cpu_distance,
				    pcpu_cpu_to_node);
	if (rc < 0)
		panic("Failed to initialize percpu areas.");

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset[cpu] = delta + pcpu_unit_offsets[cpu];
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

	if (WARN_ON_ONCE(params->pgprot.pgprot != PAGE_KERNEL.pgprot))
		return -EINVAL;

	VM_BUG_ON(!mhp_range_allowed(start, size, true));
	rc = vmem_add_mapping(start, size);
	if (rc)
		return rc;

	rc = __add_pages(nid, start_pfn, size_pages, params);
	if (rc)
		vmem_remove_mapping(start, size);
	return rc;
}

void arch_remove_memory(u64 start, u64 size, struct vmem_altmap *altmap)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;

	__remove_pages(start_pfn, nr_pages, altmap);
	vmem_remove_mapping(start, size);
}
#endif /* CONFIG_MEMORY_HOTPLUG */

#ifdef CONFIG_EXECMEM
static struct execmem_info execmem_info __ro_after_init;

struct execmem_info __init *execmem_arch_setup(void)
{
	unsigned long module_load_offset = 0;
	unsigned long start;

	if (kaslr_enabled())
		module_load_offset = get_random_u32_inclusive(1, 1024) * PAGE_SIZE;

	start = MODULES_VADDR + module_load_offset;

	execmem_info = (struct execmem_info){
		.ranges = {
			[EXECMEM_DEFAULT] = {
				.flags	= EXECMEM_KASAN_SHADOW,
				.start	= start,
				.end	= MODULES_END,
				.pgprot	= PAGE_KERNEL,
				.alignment = MODULE_ALIGN,
			},
		},
	};

	return &execmem_info;
}
#endif /* CONFIG_EXECMEM */
