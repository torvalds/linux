/*
 *  linux/arch/arm/mm/dma-mapping.c
 *
 *  Copyright (C) 2000-2004 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  DMA uncached mapping support.
 */
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/genalloc.h>
#include <linux/gfp.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <linux/sizes.h>
#include <linux/cma.h>

#include <asm/memory.h>
#include <asm/highmem.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/mach/arch.h>
#include <asm/dma-iommu.h>
#include <asm/mach/map.h>
#include <asm/system_info.h>
#include <asm/dma-contiguous.h>

#include "dma.h"
#include "mm.h"

struct arm_dma_alloc_args {
	struct device *dev;
	size_t size;
	gfp_t gfp;
	pgprot_t prot;
	const void *caller;
	bool want_vaddr;
};

struct arm_dma_free_args {
	struct device *dev;
	size_t size;
	void *cpu_addr;
	struct page *page;
	bool want_vaddr;
};

struct arm_dma_allocator {
	void *(*alloc)(struct arm_dma_alloc_args *args,
		       struct page **ret_page);
	void (*free)(struct arm_dma_free_args *args);
};

struct arm_dma_buffer {
	struct list_head list;
	void *virt;
	struct arm_dma_allocator *allocator;
};

static LIST_HEAD(arm_dma_bufs);
static DEFINE_SPINLOCK(arm_dma_bufs_lock);

static struct arm_dma_buffer *arm_dma_buffer_find(void *virt)
{
	struct arm_dma_buffer *buf, *found = NULL;
	unsigned long flags;

	spin_lock_irqsave(&arm_dma_bufs_lock, flags);
	list_for_each_entry(buf, &arm_dma_bufs, list) {
		if (buf->virt == virt) {
			list_del(&buf->list);
			found = buf;
			break;
		}
	}
	spin_unlock_irqrestore(&arm_dma_bufs_lock, flags);
	return found;
}

/*
 * The DMA API is built upon the notion of "buffer ownership".  A buffer
 * is either exclusively owned by the CPU (and therefore may be accessed
 * by it) or exclusively owned by the DMA device.  These helper functions
 * represent the transitions between these two ownership states.
 *
 * Note, however, that on later ARMs, this notion does not work due to
 * speculative prefetches.  We model our approach on the assumption that
 * the CPU does do speculative prefetches, which means we clean caches
 * before transfers and delay cache invalidation until transfer completion.
 *
 */
static void __dma_page_cpu_to_dev(struct page *, unsigned long,
		size_t, enum dma_data_direction);
static void __dma_page_dev_to_cpu(struct page *, unsigned long,
		size_t, enum dma_data_direction);

/**
 * arm_dma_map_page - map a portion of a page for streaming DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @page: page that buffer resides in
 * @offset: offset into page for start of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Ensure that any data held in the cache is appropriately discarded
 * or written back.
 *
 * The device owns this memory once this call has completed.  The CPU
 * can regain ownership by calling dma_unmap_page().
 */
static dma_addr_t arm_dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size, enum dma_data_direction dir,
	     struct dma_attrs *attrs)
{
	if (!dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		__dma_page_cpu_to_dev(page, offset, size, dir);
	return pfn_to_dma(dev, page_to_pfn(page)) + offset;
}

static dma_addr_t arm_coherent_dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size, enum dma_data_direction dir,
	     struct dma_attrs *attrs)
{
	return pfn_to_dma(dev, page_to_pfn(page)) + offset;
}

/**
 * arm_dma_unmap_page - unmap a buffer previously mapped through dma_map_page()
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @size: size of buffer (same as passed to dma_map_page)
 * @dir: DMA transfer direction (same as passed to dma_map_page)
 *
 * Unmap a page streaming mode DMA translation.  The handle and size
 * must match what was provided in the previous dma_map_page() call.
 * All other usages are undefined.
 *
 * After this call, reads by the CPU to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static void arm_dma_unmap_page(struct device *dev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	if (!dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		__dma_page_dev_to_cpu(pfn_to_page(dma_to_pfn(dev, handle)),
				      handle & ~PAGE_MASK, size, dir);
}

static void arm_dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	unsigned int offset = handle & (PAGE_SIZE - 1);
	struct page *page = pfn_to_page(dma_to_pfn(dev, handle-offset));
	__dma_page_dev_to_cpu(page, offset, size, dir);
}

static void arm_dma_sync_single_for_device(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	unsigned int offset = handle & (PAGE_SIZE - 1);
	struct page *page = pfn_to_page(dma_to_pfn(dev, handle-offset));
	__dma_page_cpu_to_dev(page, offset, size, dir);
}

struct dma_map_ops arm_dma_ops = {
	.alloc			= arm_dma_alloc,
	.free			= arm_dma_free,
	.mmap			= arm_dma_mmap,
	.get_sgtable		= arm_dma_get_sgtable,
	.map_page		= arm_dma_map_page,
	.unmap_page		= arm_dma_unmap_page,
	.map_sg			= arm_dma_map_sg,
	.unmap_sg		= arm_dma_unmap_sg,
	.sync_single_for_cpu	= arm_dma_sync_single_for_cpu,
	.sync_single_for_device	= arm_dma_sync_single_for_device,
	.sync_sg_for_cpu	= arm_dma_sync_sg_for_cpu,
	.sync_sg_for_device	= arm_dma_sync_sg_for_device,
	.set_dma_mask		= arm_dma_set_mask,
};
EXPORT_SYMBOL(arm_dma_ops);

static void *arm_coherent_dma_alloc(struct device *dev, size_t size,
	dma_addr_t *handle, gfp_t gfp, struct dma_attrs *attrs);
static void arm_coherent_dma_free(struct device *dev, size_t size, void *cpu_addr,
				  dma_addr_t handle, struct dma_attrs *attrs);
static int arm_coherent_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		 void *cpu_addr, dma_addr_t dma_addr, size_t size,
		 struct dma_attrs *attrs);

struct dma_map_ops arm_coherent_dma_ops = {
	.alloc			= arm_coherent_dma_alloc,
	.free			= arm_coherent_dma_free,
	.mmap			= arm_coherent_dma_mmap,
	.get_sgtable		= arm_dma_get_sgtable,
	.map_page		= arm_coherent_dma_map_page,
	.map_sg			= arm_dma_map_sg,
	.set_dma_mask		= arm_dma_set_mask,
};
EXPORT_SYMBOL(arm_coherent_dma_ops);

static int __dma_supported(struct device *dev, u64 mask, bool warn)
{
	unsigned long max_dma_pfn;

	/*
	 * If the mask allows for more memory than we can address,
	 * and we actually have that much memory, then we must
	 * indicate that DMA to this device is not supported.
	 */
	if (sizeof(mask) != sizeof(dma_addr_t) &&
	    mask > (dma_addr_t)~0 &&
	    dma_to_pfn(dev, ~0) < max_pfn - 1) {
		if (warn) {
			dev_warn(dev, "Coherent DMA mask %#llx is larger than dma_addr_t allows\n",
				 mask);
			dev_warn(dev, "Driver did not use or check the return value from dma_set_coherent_mask()?\n");
		}
		return 0;
	}

	max_dma_pfn = min(max_pfn, arm_dma_pfn_limit);

	/*
	 * Translate the device's DMA mask to a PFN limit.  This
	 * PFN number includes the page which we can DMA to.
	 */
	if (dma_to_pfn(dev, mask) < max_dma_pfn) {
		if (warn)
			dev_warn(dev, "Coherent DMA mask %#llx (pfn %#lx-%#lx) covers a smaller range of system memory than the DMA zone pfn 0x0-%#lx\n",
				 mask,
				 dma_to_pfn(dev, 0), dma_to_pfn(dev, mask) + 1,
				 max_dma_pfn + 1);
		return 0;
	}

	return 1;
}

static u64 get_coherent_dma_mask(struct device *dev)
{
	u64 mask = (u64)DMA_BIT_MASK(32);

	if (dev) {
		mask = dev->coherent_dma_mask;

		/*
		 * Sanity check the DMA mask - it must be non-zero, and
		 * must be able to be satisfied by a DMA allocation.
		 */
		if (mask == 0) {
			dev_warn(dev, "coherent DMA mask is unset\n");
			return 0;
		}

		if (!__dma_supported(dev, mask, true))
			return 0;
	}

	return mask;
}

static void __dma_clear_buffer(struct page *page, size_t size)
{
	/*
	 * Ensure that the allocated pages are zeroed, and that any data
	 * lurking in the kernel direct-mapped region is invalidated.
	 */
	if (PageHighMem(page)) {
		phys_addr_t base = __pfn_to_phys(page_to_pfn(page));
		phys_addr_t end = base + size;
		while (size > 0) {
			void *ptr = kmap_atomic(page);
			memset(ptr, 0, PAGE_SIZE);
			dmac_flush_range(ptr, ptr + PAGE_SIZE);
			kunmap_atomic(ptr);
			page++;
			size -= PAGE_SIZE;
		}
		outer_flush_range(base, end);
	} else {
		void *ptr = page_address(page);
		memset(ptr, 0, size);
		dmac_flush_range(ptr, ptr + size);
		outer_flush_range(__pa(ptr), __pa(ptr) + size);
	}
}

/*
 * Allocate a DMA buffer for 'dev' of size 'size' using the
 * specified gfp mask.  Note that 'size' must be page aligned.
 */
static struct page *__dma_alloc_buffer(struct device *dev, size_t size, gfp_t gfp)
{
	unsigned long order = get_order(size);
	struct page *page, *p, *e;

	page = alloc_pages(gfp, order);
	if (!page)
		return NULL;

	/*
	 * Now split the huge page and free the excess pages
	 */
	split_page(page, order);
	for (p = page + (size >> PAGE_SHIFT), e = page + (1 << order); p < e; p++)
		__free_page(p);

	__dma_clear_buffer(page, size);

	return page;
}

/*
 * Free a DMA buffer.  'size' must be page aligned.
 */
static void __dma_free_buffer(struct page *page, size_t size)
{
	struct page *e = page + (size >> PAGE_SHIFT);

	while (page < e) {
		__free_page(page);
		page++;
	}
}

#ifdef CONFIG_MMU

static void *__alloc_from_contiguous(struct device *dev, size_t size,
				     pgprot_t prot, struct page **ret_page,
				     const void *caller, bool want_vaddr);

static void *__alloc_remap_buffer(struct device *dev, size_t size, gfp_t gfp,
				 pgprot_t prot, struct page **ret_page,
				 const void *caller, bool want_vaddr);

static void *
__dma_alloc_remap(struct page *page, size_t size, gfp_t gfp, pgprot_t prot,
	const void *caller)
{
	/*
	 * DMA allocation can be mapped to user space, so lets
	 * set VM_USERMAP flags too.
	 */
	return dma_common_contiguous_remap(page, size,
			VM_ARM_DMA_CONSISTENT | VM_USERMAP,
			prot, caller);
}

static void __dma_free_remap(void *cpu_addr, size_t size)
{
	dma_common_free_remap(cpu_addr, size,
			VM_ARM_DMA_CONSISTENT | VM_USERMAP);
}

#define DEFAULT_DMA_COHERENT_POOL_SIZE	SZ_256K
static struct gen_pool *atomic_pool;

static size_t atomic_pool_size = DEFAULT_DMA_COHERENT_POOL_SIZE;

static int __init early_coherent_pool(char *p)
{
	atomic_pool_size = memparse(p, &p);
	return 0;
}
early_param("coherent_pool", early_coherent_pool);

void __init init_dma_coherent_pool_size(unsigned long size)
{
	/*
	 * Catch any attempt to set the pool size too late.
	 */
	BUG_ON(atomic_pool);

	/*
	 * Set architecture specific coherent pool size only if
	 * it has not been changed by kernel command line parameter.
	 */
	if (atomic_pool_size == DEFAULT_DMA_COHERENT_POOL_SIZE)
		atomic_pool_size = size;
}

/*
 * Initialise the coherent pool for atomic allocations.
 */
static int __init atomic_pool_init(void)
{
	pgprot_t prot = pgprot_dmacoherent(PAGE_KERNEL);
	gfp_t gfp = GFP_KERNEL | GFP_DMA;
	struct page *page;
	void *ptr;

	atomic_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!atomic_pool)
		goto out;

	if (dev_get_cma_area(NULL))
		ptr = __alloc_from_contiguous(NULL, atomic_pool_size, prot,
					      &page, atomic_pool_init, true);
	else
		ptr = __alloc_remap_buffer(NULL, atomic_pool_size, gfp, prot,
					   &page, atomic_pool_init, true);
	if (ptr) {
		int ret;

		ret = gen_pool_add_virt(atomic_pool, (unsigned long)ptr,
					page_to_phys(page),
					atomic_pool_size, -1);
		if (ret)
			goto destroy_genpool;

		gen_pool_set_algo(atomic_pool,
				gen_pool_first_fit_order_align,
				(void *)PAGE_SHIFT);
		pr_info("DMA: preallocated %zd KiB pool for atomic coherent allocations\n",
		       atomic_pool_size / 1024);
		return 0;
	}

destroy_genpool:
	gen_pool_destroy(atomic_pool);
	atomic_pool = NULL;
out:
	pr_err("DMA: failed to allocate %zx KiB pool for atomic coherent allocation\n",
	       atomic_pool_size / 1024);
	return -ENOMEM;
}
/*
 * CMA is activated by core_initcall, so we must be called after it.
 */
postcore_initcall(atomic_pool_init);

struct dma_contig_early_reserve {
	phys_addr_t base;
	unsigned long size;
};

static struct dma_contig_early_reserve dma_mmu_remap[MAX_CMA_AREAS] __initdata;

static int dma_mmu_remap_num __initdata;

void __init dma_contiguous_early_fixup(phys_addr_t base, unsigned long size)
{
	dma_mmu_remap[dma_mmu_remap_num].base = base;
	dma_mmu_remap[dma_mmu_remap_num].size = size;
	dma_mmu_remap_num++;
}

void __init dma_contiguous_remap(void)
{
	int i;
	for (i = 0; i < dma_mmu_remap_num; i++) {
		phys_addr_t start = dma_mmu_remap[i].base;
		phys_addr_t end = start + dma_mmu_remap[i].size;
		struct map_desc map;
		unsigned long addr;

		if (end > arm_lowmem_limit)
			end = arm_lowmem_limit;
		if (start >= end)
			continue;

		map.pfn = __phys_to_pfn(start);
		map.virtual = __phys_to_virt(start);
		map.length = end - start;
		map.type = MT_MEMORY_DMA_READY;

		/*
		 * Clear previous low-memory mapping to ensure that the
		 * TLB does not see any conflicting entries, then flush
		 * the TLB of the old entries before creating new mappings.
		 *
		 * This ensures that any speculatively loaded TLB entries
		 * (even though they may be rare) can not cause any problems,
		 * and ensures that this code is architecturally compliant.
		 */
		for (addr = __phys_to_virt(start); addr < __phys_to_virt(end);
		     addr += PMD_SIZE)
			pmd_clear(pmd_off_k(addr));

		flush_tlb_kernel_range(__phys_to_virt(start),
				       __phys_to_virt(end));

		iotable_init(&map, 1);
	}
}

static int __dma_update_pte(pte_t *pte, pgtable_t token, unsigned long addr,
			    void *data)
{
	struct page *page = virt_to_page(addr);
	pgprot_t prot = *(pgprot_t *)data;

	set_pte_ext(pte, mk_pte(page, prot), 0);
	return 0;
}

static void __dma_remap(struct page *page, size_t size, pgprot_t prot)
{
	unsigned long start = (unsigned long) page_address(page);
	unsigned end = start + size;

	apply_to_page_range(&init_mm, start, size, __dma_update_pte, &prot);
	flush_tlb_kernel_range(start, end);
}

static void *__alloc_remap_buffer(struct device *dev, size_t size, gfp_t gfp,
				 pgprot_t prot, struct page **ret_page,
				 const void *caller, bool want_vaddr)
{
	struct page *page;
	void *ptr = NULL;
	page = __dma_alloc_buffer(dev, size, gfp);
	if (!page)
		return NULL;
	if (!want_vaddr)
		goto out;

	ptr = __dma_alloc_remap(page, size, gfp, prot, caller);
	if (!ptr) {
		__dma_free_buffer(page, size);
		return NULL;
	}

 out:
	*ret_page = page;
	return ptr;
}

static void *__alloc_from_pool(size_t size, struct page **ret_page)
{
	unsigned long val;
	void *ptr = NULL;

	if (!atomic_pool) {
		WARN(1, "coherent pool not initialised!\n");
		return NULL;
	}

	val = gen_pool_alloc(atomic_pool, size);
	if (val) {
		phys_addr_t phys = gen_pool_virt_to_phys(atomic_pool, val);

		*ret_page = phys_to_page(phys);
		ptr = (void *)val;
	}

	return ptr;
}

static bool __in_atomic_pool(void *start, size_t size)
{
	return addr_in_gen_pool(atomic_pool, (unsigned long)start, size);
}

static int __free_from_pool(void *start, size_t size)
{
	if (!__in_atomic_pool(start, size))
		return 0;

	gen_pool_free(atomic_pool, (unsigned long)start, size);

	return 1;
}

static void *__alloc_from_contiguous(struct device *dev, size_t size,
				     pgprot_t prot, struct page **ret_page,
				     const void *caller, bool want_vaddr)
{
	unsigned long order = get_order(size);
	size_t count = size >> PAGE_SHIFT;
	struct page *page;
	void *ptr = NULL;

	page = dma_alloc_from_contiguous(dev, count, order);
	if (!page)
		return NULL;

	__dma_clear_buffer(page, size);

	if (!want_vaddr)
		goto out;

	if (PageHighMem(page)) {
		ptr = __dma_alloc_remap(page, size, GFP_KERNEL, prot, caller);
		if (!ptr) {
			dma_release_from_contiguous(dev, page, count);
			return NULL;
		}
	} else {
		__dma_remap(page, size, prot);
		ptr = page_address(page);
	}

 out:
	*ret_page = page;
	return ptr;
}

static void __free_from_contiguous(struct device *dev, struct page *page,
				   void *cpu_addr, size_t size, bool want_vaddr)
{
	if (want_vaddr) {
		if (PageHighMem(page))
			__dma_free_remap(cpu_addr, size);
		else
			__dma_remap(page, size, PAGE_KERNEL);
	}
	dma_release_from_contiguous(dev, page, size >> PAGE_SHIFT);
}

static inline pgprot_t __get_dma_pgprot(struct dma_attrs *attrs, pgprot_t prot)
{
	prot = dma_get_attr(DMA_ATTR_WRITE_COMBINE, attrs) ?
			    pgprot_writecombine(prot) :
			    pgprot_dmacoherent(prot);
	return prot;
}

#define nommu() 0

#else	/* !CONFIG_MMU */

#define nommu() 1

#define __get_dma_pgprot(attrs, prot)				__pgprot(0)
#define __alloc_remap_buffer(dev, size, gfp, prot, ret, c, wv)	NULL
#define __alloc_from_pool(size, ret_page)			NULL
#define __alloc_from_contiguous(dev, size, prot, ret, c, wv)	NULL
#define __free_from_pool(cpu_addr, size)			do { } while (0)
#define __free_from_contiguous(dev, page, cpu_addr, size, wv)	do { } while (0)
#define __dma_free_remap(cpu_addr, size)			do { } while (0)

#endif	/* CONFIG_MMU */

static void *__alloc_simple_buffer(struct device *dev, size_t size, gfp_t gfp,
				   struct page **ret_page)
{
	struct page *page;
	page = __dma_alloc_buffer(dev, size, gfp);
	if (!page)
		return NULL;

	*ret_page = page;
	return page_address(page);
}

static void *simple_allocator_alloc(struct arm_dma_alloc_args *args,
				    struct page **ret_page)
{
	return __alloc_simple_buffer(args->dev, args->size, args->gfp,
				     ret_page);
}

static void simple_allocator_free(struct arm_dma_free_args *args)
{
	__dma_free_buffer(args->page, args->size);
}

static struct arm_dma_allocator simple_allocator = {
	.alloc = simple_allocator_alloc,
	.free = simple_allocator_free,
};

static void *cma_allocator_alloc(struct arm_dma_alloc_args *args,
				 struct page **ret_page)
{
	return __alloc_from_contiguous(args->dev, args->size, args->prot,
				       ret_page, args->caller,
				       args->want_vaddr);
}

static void cma_allocator_free(struct arm_dma_free_args *args)
{
	__free_from_contiguous(args->dev, args->page, args->cpu_addr,
			       args->size, args->want_vaddr);
}

static struct arm_dma_allocator cma_allocator = {
	.alloc = cma_allocator_alloc,
	.free = cma_allocator_free,
};

static void *pool_allocator_alloc(struct arm_dma_alloc_args *args,
				  struct page **ret_page)
{
	return __alloc_from_pool(args->size, ret_page);
}

static void pool_allocator_free(struct arm_dma_free_args *args)
{
	__free_from_pool(args->cpu_addr, args->size);
}

static struct arm_dma_allocator pool_allocator = {
	.alloc = pool_allocator_alloc,
	.free = pool_allocator_free,
};

static void *remap_allocator_alloc(struct arm_dma_alloc_args *args,
				   struct page **ret_page)
{
	return __alloc_remap_buffer(args->dev, args->size, args->gfp,
				    args->prot, ret_page, args->caller,
				    args->want_vaddr);
}

static void remap_allocator_free(struct arm_dma_free_args *args)
{
	if (args->want_vaddr)
		__dma_free_remap(args->cpu_addr, args->size);

	__dma_free_buffer(args->page, args->size);
}

static struct arm_dma_allocator remap_allocator = {
	.alloc = remap_allocator_alloc,
	.free = remap_allocator_free,
};

static void *__dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
			 gfp_t gfp, pgprot_t prot, bool is_coherent,
			 struct dma_attrs *attrs, const void *caller)
{
	u64 mask = get_coherent_dma_mask(dev);
	struct page *page = NULL;
	void *addr;
	bool allowblock, cma;
	struct arm_dma_buffer *buf;
	struct arm_dma_alloc_args args = {
		.dev = dev,
		.size = PAGE_ALIGN(size),
		.gfp = gfp,
		.prot = prot,
		.caller = caller,
		.want_vaddr = !dma_get_attr(DMA_ATTR_NO_KERNEL_MAPPING, attrs),
	};

#ifdef CONFIG_DMA_API_DEBUG
	u64 limit = (mask + 1) & ~mask;
	if (limit && size >= limit) {
		dev_warn(dev, "coherent allocation too big (requested %#x mask %#llx)\n",
			size, mask);
		return NULL;
	}
#endif

	if (!mask)
		return NULL;

	buf = kzalloc(sizeof(*buf),
		      gfp & ~(__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM));
	if (!buf)
		return NULL;

	if (mask < 0xffffffffULL)
		gfp |= GFP_DMA;

	/*
	 * Following is a work-around (a.k.a. hack) to prevent pages
	 * with __GFP_COMP being passed to split_page() which cannot
	 * handle them.  The real problem is that this flag probably
	 * should be 0 on ARM as it is not supported on this
	 * platform; see CONFIG_HUGETLBFS.
	 */
	gfp &= ~(__GFP_COMP);
	args.gfp = gfp;

	*handle = DMA_ERROR_CODE;
	allowblock = gfpflags_allow_blocking(gfp);
	cma = allowblock ? dev_get_cma_area(dev) : false;

	if (cma)
		buf->allocator = &cma_allocator;
	else if (nommu() || is_coherent)
		buf->allocator = &simple_allocator;
	else if (allowblock)
		buf->allocator = &remap_allocator;
	else
		buf->allocator = &pool_allocator;

	addr = buf->allocator->alloc(&args, &page);

	if (page) {
		unsigned long flags;

		*handle = pfn_to_dma(dev, page_to_pfn(page));
		buf->virt = args.want_vaddr ? addr : page;

		spin_lock_irqsave(&arm_dma_bufs_lock, flags);
		list_add(&buf->list, &arm_dma_bufs);
		spin_unlock_irqrestore(&arm_dma_bufs_lock, flags);
	} else {
		kfree(buf);
	}

	return args.want_vaddr ? addr : page;
}

/*
 * Allocate DMA-coherent memory space and return both the kernel remapped
 * virtual and bus address for that space.
 */
void *arm_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
		    gfp_t gfp, struct dma_attrs *attrs)
{
	pgprot_t prot = __get_dma_pgprot(attrs, PAGE_KERNEL);

	return __dma_alloc(dev, size, handle, gfp, prot, false,
			   attrs, __builtin_return_address(0));
}

static void *arm_coherent_dma_alloc(struct device *dev, size_t size,
	dma_addr_t *handle, gfp_t gfp, struct dma_attrs *attrs)
{
	return __dma_alloc(dev, size, handle, gfp, PAGE_KERNEL, true,
			   attrs, __builtin_return_address(0));
}

static int __arm_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		 void *cpu_addr, dma_addr_t dma_addr, size_t size,
		 struct dma_attrs *attrs)
{
	int ret = -ENXIO;
#ifdef CONFIG_MMU
	unsigned long nr_vma_pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	unsigned long nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long pfn = dma_to_pfn(dev, dma_addr);
	unsigned long off = vma->vm_pgoff;

	if (dma_mmap_from_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	if (off < nr_pages && nr_vma_pages <= (nr_pages - off)) {
		ret = remap_pfn_range(vma, vma->vm_start,
				      pfn + off,
				      vma->vm_end - vma->vm_start,
				      vma->vm_page_prot);
	}
#endif	/* CONFIG_MMU */

	return ret;
}

/*
 * Create userspace mapping for the DMA-coherent memory.
 */
static int arm_coherent_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		 void *cpu_addr, dma_addr_t dma_addr, size_t size,
		 struct dma_attrs *attrs)
{
	return __arm_dma_mmap(dev, vma, cpu_addr, dma_addr, size, attrs);
}

int arm_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		 void *cpu_addr, dma_addr_t dma_addr, size_t size,
		 struct dma_attrs *attrs)
{
#ifdef CONFIG_MMU
	vma->vm_page_prot = __get_dma_pgprot(attrs, vma->vm_page_prot);
#endif	/* CONFIG_MMU */
	return __arm_dma_mmap(dev, vma, cpu_addr, dma_addr, size, attrs);
}

/*
 * Free a buffer as defined by the above mapping.
 */
static void __arm_dma_free(struct device *dev, size_t size, void *cpu_addr,
			   dma_addr_t handle, struct dma_attrs *attrs,
			   bool is_coherent)
{
	struct page *page = pfn_to_page(dma_to_pfn(dev, handle));
	struct arm_dma_buffer *buf;
	struct arm_dma_free_args args = {
		.dev = dev,
		.size = PAGE_ALIGN(size),
		.cpu_addr = cpu_addr,
		.page = page,
		.want_vaddr = !dma_get_attr(DMA_ATTR_NO_KERNEL_MAPPING, attrs),
	};

	buf = arm_dma_buffer_find(cpu_addr);
	if (WARN(!buf, "Freeing invalid buffer %p\n", cpu_addr))
		return;

	buf->allocator->free(&args);
	kfree(buf);
}

void arm_dma_free(struct device *dev, size_t size, void *cpu_addr,
		  dma_addr_t handle, struct dma_attrs *attrs)
{
	__arm_dma_free(dev, size, cpu_addr, handle, attrs, false);
}

static void arm_coherent_dma_free(struct device *dev, size_t size, void *cpu_addr,
				  dma_addr_t handle, struct dma_attrs *attrs)
{
	__arm_dma_free(dev, size, cpu_addr, handle, attrs, true);
}

int arm_dma_get_sgtable(struct device *dev, struct sg_table *sgt,
		 void *cpu_addr, dma_addr_t handle, size_t size,
		 struct dma_attrs *attrs)
{
	struct page *page = pfn_to_page(dma_to_pfn(dev, handle));
	int ret;

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (unlikely(ret))
		return ret;

	sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	return 0;
}

static void dma_cache_maint_page(struct page *page, unsigned long offset,
	size_t size, enum dma_data_direction dir,
	void (*op)(const void *, size_t, int))
{
	unsigned long pfn;
	size_t left = size;

	pfn = page_to_pfn(page) + offset / PAGE_SIZE;
	offset %= PAGE_SIZE;

	/*
	 * A single sg entry may refer to multiple physically contiguous
	 * pages.  But we still need to process highmem pages individually.
	 * If highmem is not configured then the bulk of this loop gets
	 * optimized out.
	 */
	do {
		size_t len = left;
		void *vaddr;

		page = pfn_to_page(pfn);

		if (PageHighMem(page)) {
			if (len + offset > PAGE_SIZE)
				len = PAGE_SIZE - offset;

			if (cache_is_vipt_nonaliasing()) {
				vaddr = kmap_atomic(page);
				op(vaddr + offset, len, dir);
				kunmap_atomic(vaddr);
			} else {
				vaddr = kmap_high_get(page);
				if (vaddr) {
					op(vaddr + offset, len, dir);
					kunmap_high(page);
				}
			}
		} else {
			vaddr = page_address(page) + offset;
			op(vaddr, len, dir);
		}
		offset = 0;
		pfn++;
		left -= len;
	} while (left);
}

/*
 * Make an area consistent for devices.
 * Note: Drivers should NOT use this function directly, as it will break
 * platforms with CONFIG_DMABOUNCE.
 * Use the driver DMA support - see dma-mapping.h (dma_sync_*)
 */
static void __dma_page_cpu_to_dev(struct page *page, unsigned long off,
	size_t size, enum dma_data_direction dir)
{
	phys_addr_t paddr;

	dma_cache_maint_page(page, off, size, dir, dmac_map_area);

	paddr = page_to_phys(page) + off;
	if (dir == DMA_FROM_DEVICE) {
		outer_inv_range(paddr, paddr + size);
	} else {
		outer_clean_range(paddr, paddr + size);
	}
	/* FIXME: non-speculating: flush on bidirectional mappings? */
}

static void __dma_page_dev_to_cpu(struct page *page, unsigned long off,
	size_t size, enum dma_data_direction dir)
{
	phys_addr_t paddr = page_to_phys(page) + off;

	/* FIXME: non-speculating: not required */
	/* in any case, don't bother invalidating if DMA to device */
	if (dir != DMA_TO_DEVICE) {
		outer_inv_range(paddr, paddr + size);

		dma_cache_maint_page(page, off, size, dir, dmac_unmap_area);
	}

	/*
	 * Mark the D-cache clean for these pages to avoid extra flushing.
	 */
	if (dir != DMA_TO_DEVICE && size >= PAGE_SIZE) {
		unsigned long pfn;
		size_t left = size;

		pfn = page_to_pfn(page) + off / PAGE_SIZE;
		off %= PAGE_SIZE;
		if (off) {
			pfn++;
			left -= PAGE_SIZE - off;
		}
		while (left >= PAGE_SIZE) {
			page = pfn_to_page(pfn++);
			set_bit(PG_dcache_clean, &page->flags);
			left -= PAGE_SIZE;
		}
	}
}

/**
 * arm_dma_map_sg - map a set of SG buffers for streaming mode DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map
 * @dir: DMA transfer direction
 *
 * Map a set of buffers described by scatterlist in streaming mode for DMA.
 * This is the scatter-gather version of the dma_map_single interface.
 * Here the scatter gather list elements are each tagged with the
 * appropriate dma address and length.  They are obtained via
 * sg_dma_{address,length}.
 *
 * Device ownership issues as mentioned for dma_map_single are the same
 * here.
 */
int arm_dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir, struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	struct scatterlist *s;
	int i, j;

	for_each_sg(sg, s, nents, i) {
#ifdef CONFIG_NEED_SG_DMA_LENGTH
		s->dma_length = s->length;
#endif
		s->dma_address = ops->map_page(dev, sg_page(s), s->offset,
						s->length, dir, attrs);
		if (dma_mapping_error(dev, s->dma_address))
			goto bad_mapping;
	}
	return nents;

 bad_mapping:
	for_each_sg(sg, s, i, j)
		ops->unmap_page(dev, sg_dma_address(s), sg_dma_len(s), dir, attrs);
	return 0;
}

/**
 * arm_dma_unmap_sg - unmap a set of SG buffers mapped by dma_map_sg
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to unmap (same as was passed to dma_map_sg)
 * @dir: DMA transfer direction (same as was passed to dma_map_sg)
 *
 * Unmap a set of streaming mode DMA translations.  Again, CPU access
 * rules concerning calls here are the same as for dma_unmap_single().
 */
void arm_dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir, struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	struct scatterlist *s;

	int i;

	for_each_sg(sg, s, nents, i)
		ops->unmap_page(dev, sg_dma_address(s), sg_dma_len(s), dir, attrs);
}

/**
 * arm_dma_sync_sg_for_cpu
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map (returned from dma_map_sg)
 * @dir: DMA transfer direction (same as was passed to dma_map_sg)
 */
void arm_dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i)
		ops->sync_single_for_cpu(dev, sg_dma_address(s), s->length,
					 dir);
}

/**
 * arm_dma_sync_sg_for_device
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map (returned from dma_map_sg)
 * @dir: DMA transfer direction (same as was passed to dma_map_sg)
 */
void arm_dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i)
		ops->sync_single_for_device(dev, sg_dma_address(s), s->length,
					    dir);
}

/*
 * Return whether the given device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during bus mastering, then you would pass 0x00ffffff as the mask
 * to this function.
 */
int dma_supported(struct device *dev, u64 mask)
{
	return __dma_supported(dev, mask, false);
}
EXPORT_SYMBOL(dma_supported);

int arm_dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}

#define PREALLOC_DMA_DEBUG_ENTRIES	4096

static int __init dma_debug_do_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);
	return 0;
}
fs_initcall(dma_debug_do_init);

#ifdef CONFIG_ARM_DMA_USE_IOMMU

/* IOMMU */

static int extend_iommu_mapping(struct dma_iommu_mapping *mapping);

static inline dma_addr_t __alloc_iova(struct dma_iommu_mapping *mapping,
				      size_t size)
{
	unsigned int order = get_order(size);
	unsigned int align = 0;
	unsigned int count, start;
	size_t mapping_size = mapping->bits << PAGE_SHIFT;
	unsigned long flags;
	dma_addr_t iova;
	int i;

	if (order > CONFIG_ARM_DMA_IOMMU_ALIGNMENT)
		order = CONFIG_ARM_DMA_IOMMU_ALIGNMENT;

	count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	align = (1 << order) - 1;

	spin_lock_irqsave(&mapping->lock, flags);
	for (i = 0; i < mapping->nr_bitmaps; i++) {
		start = bitmap_find_next_zero_area(mapping->bitmaps[i],
				mapping->bits, 0, count, align);

		if (start > mapping->bits)
			continue;

		bitmap_set(mapping->bitmaps[i], start, count);
		break;
	}

	/*
	 * No unused range found. Try to extend the existing mapping
	 * and perform a second attempt to reserve an IO virtual
	 * address range of size bytes.
	 */
	if (i == mapping->nr_bitmaps) {
		if (extend_iommu_mapping(mapping)) {
			spin_unlock_irqrestore(&mapping->lock, flags);
			return DMA_ERROR_CODE;
		}

		start = bitmap_find_next_zero_area(mapping->bitmaps[i],
				mapping->bits, 0, count, align);

		if (start > mapping->bits) {
			spin_unlock_irqrestore(&mapping->lock, flags);
			return DMA_ERROR_CODE;
		}

		bitmap_set(mapping->bitmaps[i], start, count);
	}
	spin_unlock_irqrestore(&mapping->lock, flags);

	iova = mapping->base + (mapping_size * i);
	iova += start << PAGE_SHIFT;

	return iova;
}

static inline void __free_iova(struct dma_iommu_mapping *mapping,
			       dma_addr_t addr, size_t size)
{
	unsigned int start, count;
	size_t mapping_size = mapping->bits << PAGE_SHIFT;
	unsigned long flags;
	dma_addr_t bitmap_base;
	u32 bitmap_index;

	if (!size)
		return;

	bitmap_index = (u32) (addr - mapping->base) / (u32) mapping_size;
	BUG_ON(addr < mapping->base || bitmap_index > mapping->extensions);

	bitmap_base = mapping->base + mapping_size * bitmap_index;

	start = (addr - bitmap_base) >>	PAGE_SHIFT;

	if (addr + size > bitmap_base + mapping_size) {
		/*
		 * The address range to be freed reaches into the iova
		 * range of the next bitmap. This should not happen as
		 * we don't allow this in __alloc_iova (at the
		 * moment).
		 */
		BUG();
	} else
		count = size >> PAGE_SHIFT;

	spin_lock_irqsave(&mapping->lock, flags);
	bitmap_clear(mapping->bitmaps[bitmap_index], start, count);
	spin_unlock_irqrestore(&mapping->lock, flags);
}

/* We'll try 2M, 1M, 64K, and finally 4K; array must end with 0! */
static const int iommu_order_array[] = { 9, 8, 4, 0 };

static struct page **__iommu_alloc_buffer(struct device *dev, size_t size,
					  gfp_t gfp, struct dma_attrs *attrs)
{
	struct page **pages;
	int count = size >> PAGE_SHIFT;
	int array_size = count * sizeof(struct page *);
	int i = 0;
	int order_idx = 0;

	if (array_size <= PAGE_SIZE)
		pages = kzalloc(array_size, GFP_KERNEL);
	else
		pages = vzalloc(array_size);
	if (!pages)
		return NULL;

	if (dma_get_attr(DMA_ATTR_FORCE_CONTIGUOUS, attrs))
	{
		unsigned long order = get_order(size);
		struct page *page;

		page = dma_alloc_from_contiguous(dev, count, order);
		if (!page)
			goto error;

		__dma_clear_buffer(page, size);

		for (i = 0; i < count; i++)
			pages[i] = page + i;

		return pages;
	}

	/* Go straight to 4K chunks if caller says it's OK. */
	if (dma_get_attr(DMA_ATTR_ALLOC_SINGLE_PAGES, attrs))
		order_idx = ARRAY_SIZE(iommu_order_array) - 1;

	/*
	 * IOMMU can map any pages, so himem can also be used here
	 */
	gfp |= __GFP_NOWARN | __GFP_HIGHMEM;

	while (count) {
		int j, order;

		order = iommu_order_array[order_idx];

		/* Drop down when we get small */
		if (__fls(count) < order) {
			order_idx++;
			continue;
		}

		if (order) {
			/* See if it's easy to allocate a high-order chunk */
			pages[i] = alloc_pages(gfp | __GFP_NORETRY, order);

			/* Go down a notch at first sign of pressure */
			if (!pages[i]) {
				order_idx++;
				continue;
			}
		} else {
			pages[i] = alloc_pages(gfp, 0);
			if (!pages[i])
				goto error;
		}

		if (order) {
			split_page(pages[i], order);
			j = 1 << order;
			while (--j)
				pages[i + j] = pages[i] + j;
		}

		__dma_clear_buffer(pages[i], PAGE_SIZE << order);
		i += 1 << order;
		count -= 1 << order;
	}

	return pages;
error:
	while (i--)
		if (pages[i])
			__free_pages(pages[i], 0);
	kvfree(pages);
	return NULL;
}

static int __iommu_free_buffer(struct device *dev, struct page **pages,
			       size_t size, struct dma_attrs *attrs)
{
	int count = size >> PAGE_SHIFT;
	int i;

	if (dma_get_attr(DMA_ATTR_FORCE_CONTIGUOUS, attrs)) {
		dma_release_from_contiguous(dev, pages[0], count);
	} else {
		for (i = 0; i < count; i++)
			if (pages[i])
				__free_pages(pages[i], 0);
	}

	kvfree(pages);
	return 0;
}

/*
 * Create a CPU mapping for a specified pages
 */
static void *
__iommu_alloc_remap(struct page **pages, size_t size, gfp_t gfp, pgprot_t prot,
		    const void *caller)
{
	return dma_common_pages_remap(pages, size,
			VM_ARM_DMA_CONSISTENT | VM_USERMAP, prot, caller);
}

/*
 * Create a mapping in device IO address space for specified pages
 */
static dma_addr_t
__iommu_create_mapping(struct device *dev, struct page **pages, size_t size)
{
	struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	dma_addr_t dma_addr, iova;
	int i;

	dma_addr = __alloc_iova(mapping, size);
	if (dma_addr == DMA_ERROR_CODE)
		return dma_addr;

	iova = dma_addr;
	for (i = 0; i < count; ) {
		int ret;

		unsigned int next_pfn = page_to_pfn(pages[i]) + 1;
		phys_addr_t phys = page_to_phys(pages[i]);
		unsigned int len, j;

		for (j = i + 1; j < count; j++, next_pfn++)
			if (page_to_pfn(pages[j]) != next_pfn)
				break;

		len = (j - i) << PAGE_SHIFT;
		ret = iommu_map(mapping->domain, iova, phys, len,
				IOMMU_READ|IOMMU_WRITE);
		if (ret < 0)
			goto fail;
		iova += len;
		i = j;
	}
	return dma_addr;
fail:
	iommu_unmap(mapping->domain, dma_addr, iova-dma_addr);
	__free_iova(mapping, dma_addr, size);
	return DMA_ERROR_CODE;
}

static int __iommu_remove_mapping(struct device *dev, dma_addr_t iova, size_t size)
{
	struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);

	/*
	 * add optional in-page offset from iova to size and align
	 * result to page size
	 */
	size = PAGE_ALIGN((iova & ~PAGE_MASK) + size);
	iova &= PAGE_MASK;

	iommu_unmap(mapping->domain, iova, size);
	__free_iova(mapping, iova, size);
	return 0;
}

static struct page **__atomic_get_pages(void *addr)
{
	struct page *page;
	phys_addr_t phys;

	phys = gen_pool_virt_to_phys(atomic_pool, (unsigned long)addr);
	page = phys_to_page(phys);

	return (struct page **)page;
}

static struct page **__iommu_get_pages(void *cpu_addr, struct dma_attrs *attrs)
{
	struct vm_struct *area;

	if (__in_atomic_pool(cpu_addr, PAGE_SIZE))
		return __atomic_get_pages(cpu_addr);

	if (dma_get_attr(DMA_ATTR_NO_KERNEL_MAPPING, attrs))
		return cpu_addr;

	area = find_vm_area(cpu_addr);
	if (area && (area->flags & VM_ARM_DMA_CONSISTENT))
		return area->pages;
	return NULL;
}

static void *__iommu_alloc_atomic(struct device *dev, size_t size,
				  dma_addr_t *handle)
{
	struct page *page;
	void *addr;

	addr = __alloc_from_pool(size, &page);
	if (!addr)
		return NULL;

	*handle = __iommu_create_mapping(dev, &page, size);
	if (*handle == DMA_ERROR_CODE)
		goto err_mapping;

	return addr;

err_mapping:
	__free_from_pool(addr, size);
	return NULL;
}

static void __iommu_free_atomic(struct device *dev, void *cpu_addr,
				dma_addr_t handle, size_t size)
{
	__iommu_remove_mapping(dev, handle, size);
	__free_from_pool(cpu_addr, size);
}

static void *arm_iommu_alloc_attrs(struct device *dev, size_t size,
	    dma_addr_t *handle, gfp_t gfp, struct dma_attrs *attrs)
{
	pgprot_t prot = __get_dma_pgprot(attrs, PAGE_KERNEL);
	struct page **pages;
	void *addr = NULL;

	*handle = DMA_ERROR_CODE;
	size = PAGE_ALIGN(size);

	if (!gfpflags_allow_blocking(gfp))
		return __iommu_alloc_atomic(dev, size, handle);

	/*
	 * Following is a work-around (a.k.a. hack) to prevent pages
	 * with __GFP_COMP being passed to split_page() which cannot
	 * handle them.  The real problem is that this flag probably
	 * should be 0 on ARM as it is not supported on this
	 * platform; see CONFIG_HUGETLBFS.
	 */
	gfp &= ~(__GFP_COMP);

	pages = __iommu_alloc_buffer(dev, size, gfp, attrs);
	if (!pages)
		return NULL;

	*handle = __iommu_create_mapping(dev, pages, size);
	if (*handle == DMA_ERROR_CODE)
		goto err_buffer;

	if (dma_get_attr(DMA_ATTR_NO_KERNEL_MAPPING, attrs))
		return pages;

	addr = __iommu_alloc_remap(pages, size, gfp, prot,
				   __builtin_return_address(0));
	if (!addr)
		goto err_mapping;

	return addr;

err_mapping:
	__iommu_remove_mapping(dev, *handle, size);
err_buffer:
	__iommu_free_buffer(dev, pages, size, attrs);
	return NULL;
}

static int arm_iommu_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
		    void *cpu_addr, dma_addr_t dma_addr, size_t size,
		    struct dma_attrs *attrs)
{
	unsigned long uaddr = vma->vm_start;
	unsigned long usize = vma->vm_end - vma->vm_start;
	struct page **pages = __iommu_get_pages(cpu_addr, attrs);
	unsigned long nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long off = vma->vm_pgoff;

	vma->vm_page_prot = __get_dma_pgprot(attrs, vma->vm_page_prot);

	if (!pages)
		return -ENXIO;

	if (off >= nr_pages || (usize >> PAGE_SHIFT) > nr_pages - off)
		return -ENXIO;

	pages += off;

	do {
		int ret = vm_insert_page(vma, uaddr, *pages++);
		if (ret) {
			pr_err("Remapping memory failed: %d\n", ret);
			return ret;
		}
		uaddr += PAGE_SIZE;
		usize -= PAGE_SIZE;
	} while (usize > 0);

	return 0;
}

/*
 * free a page as defined by the above mapping.
 * Must not be called with IRQs disabled.
 */
void arm_iommu_free_attrs(struct device *dev, size_t size, void *cpu_addr,
			  dma_addr_t handle, struct dma_attrs *attrs)
{
	struct page **pages;
	size = PAGE_ALIGN(size);

	if (__in_atomic_pool(cpu_addr, size)) {
		__iommu_free_atomic(dev, cpu_addr, handle, size);
		return;
	}

	pages = __iommu_get_pages(cpu_addr, attrs);
	if (!pages) {
		WARN(1, "trying to free invalid coherent area: %p\n", cpu_addr);
		return;
	}

	if (!dma_get_attr(DMA_ATTR_NO_KERNEL_MAPPING, attrs)) {
		dma_common_free_remap(cpu_addr, size,
			VM_ARM_DMA_CONSISTENT | VM_USERMAP);
	}

	__iommu_remove_mapping(dev, handle, size);
	__iommu_free_buffer(dev, pages, size, attrs);
}

static int arm_iommu_get_sgtable(struct device *dev, struct sg_table *sgt,
				 void *cpu_addr, dma_addr_t dma_addr,
				 size_t size, struct dma_attrs *attrs)
{
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	struct page **pages = __iommu_get_pages(cpu_addr, attrs);

	if (!pages)
		return -ENXIO;

	return sg_alloc_table_from_pages(sgt, pages, count, 0, size,
					 GFP_KERNEL);
}

static int __dma_direction_to_prot(enum dma_data_direction dir)
{
	int prot;

	switch (dir) {
	case DMA_BIDIRECTIONAL:
		prot = IOMMU_READ | IOMMU_WRITE;
		break;
	case DMA_TO_DEVICE:
		prot = IOMMU_READ;
		break;
	case DMA_FROM_DEVICE:
		prot = IOMMU_WRITE;
		break;
	default:
		prot = 0;
	}

	return prot;
}

/*
 * Map a part of the scatter-gather list into contiguous io address space
 */
static int __map_sg_chunk(struct device *dev, struct scatterlist *sg,
			  size_t size, dma_addr_t *handle,
			  enum dma_data_direction dir, struct dma_attrs *attrs,
			  bool is_coherent)
{
	struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);
	dma_addr_t iova, iova_base;
	int ret = 0;
	unsigned int count;
	struct scatterlist *s;
	int prot;

	size = PAGE_ALIGN(size);
	*handle = DMA_ERROR_CODE;

	iova_base = iova = __alloc_iova(mapping, size);
	if (iova == DMA_ERROR_CODE)
		return -ENOMEM;

	for (count = 0, s = sg; count < (size >> PAGE_SHIFT); s = sg_next(s)) {
		phys_addr_t phys = page_to_phys(sg_page(s));
		unsigned int len = PAGE_ALIGN(s->offset + s->length);

		if (!is_coherent &&
			!dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
			__dma_page_cpu_to_dev(sg_page(s), s->offset, s->length, dir);

		prot = __dma_direction_to_prot(dir);

		ret = iommu_map(mapping->domain, iova, phys, len, prot);
		if (ret < 0)
			goto fail;
		count += len >> PAGE_SHIFT;
		iova += len;
	}
	*handle = iova_base;

	return 0;
fail:
	iommu_unmap(mapping->domain, iova_base, count * PAGE_SIZE);
	__free_iova(mapping, iova_base, size);
	return ret;
}

static int __iommu_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		     enum dma_data_direction dir, struct dma_attrs *attrs,
		     bool is_coherent)
{
	struct scatterlist *s = sg, *dma = sg, *start = sg;
	int i, count = 0;
	unsigned int offset = s->offset;
	unsigned int size = s->offset + s->length;
	unsigned int max = dma_get_max_seg_size(dev);

	for (i = 1; i < nents; i++) {
		s = sg_next(s);

		s->dma_address = DMA_ERROR_CODE;
		s->dma_length = 0;

		if (s->offset || (size & ~PAGE_MASK) || size + s->length > max) {
			if (__map_sg_chunk(dev, start, size, &dma->dma_address,
			    dir, attrs, is_coherent) < 0)
				goto bad_mapping;

			dma->dma_address += offset;
			dma->dma_length = size - offset;

			size = offset = s->offset;
			start = s;
			dma = sg_next(dma);
			count += 1;
		}
		size += s->length;
	}
	if (__map_sg_chunk(dev, start, size, &dma->dma_address, dir, attrs,
		is_coherent) < 0)
		goto bad_mapping;

	dma->dma_address += offset;
	dma->dma_length = size - offset;

	return count+1;

bad_mapping:
	for_each_sg(sg, s, count, i)
		__iommu_remove_mapping(dev, sg_dma_address(s), sg_dma_len(s));
	return 0;
}

/**
 * arm_coherent_iommu_map_sg - map a set of SG buffers for streaming mode DMA
 * @dev: valid struct device pointer
 * @sg: list of buffers
 * @nents: number of buffers to map
 * @dir: DMA transfer direction
 *
 * Map a set of i/o coherent buffers described by scatterlist in streaming
 * mode for DMA. The scatter gather list elements are merged together (if
 * possible) and tagged with the appropriate dma address and length. They are
 * obtained via sg_dma_{address,length}.
 */
int arm_coherent_iommu_map_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, struct dma_attrs *attrs)
{
	return __iommu_map_sg(dev, sg, nents, dir, attrs, true);
}

/**
 * arm_iommu_map_sg - map a set of SG buffers for streaming mode DMA
 * @dev: valid struct device pointer
 * @sg: list of buffers
 * @nents: number of buffers to map
 * @dir: DMA transfer direction
 *
 * Map a set of buffers described by scatterlist in streaming mode for DMA.
 * The scatter gather list elements are merged together (if possible) and
 * tagged with the appropriate dma address and length. They are obtained via
 * sg_dma_{address,length}.
 */
int arm_iommu_map_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, struct dma_attrs *attrs)
{
	return __iommu_map_sg(dev, sg, nents, dir, attrs, false);
}

static void __iommu_unmap_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, struct dma_attrs *attrs,
		bool is_coherent)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		if (sg_dma_len(s))
			__iommu_remove_mapping(dev, sg_dma_address(s),
					       sg_dma_len(s));
		if (!is_coherent &&
		    !dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
			__dma_page_dev_to_cpu(sg_page(s), s->offset,
					      s->length, dir);
	}
}

/**
 * arm_coherent_iommu_unmap_sg - unmap a set of SG buffers mapped by dma_map_sg
 * @dev: valid struct device pointer
 * @sg: list of buffers
 * @nents: number of buffers to unmap (same as was passed to dma_map_sg)
 * @dir: DMA transfer direction (same as was passed to dma_map_sg)
 *
 * Unmap a set of streaming mode DMA translations.  Again, CPU access
 * rules concerning calls here are the same as for dma_unmap_single().
 */
void arm_coherent_iommu_unmap_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, struct dma_attrs *attrs)
{
	__iommu_unmap_sg(dev, sg, nents, dir, attrs, true);
}

/**
 * arm_iommu_unmap_sg - unmap a set of SG buffers mapped by dma_map_sg
 * @dev: valid struct device pointer
 * @sg: list of buffers
 * @nents: number of buffers to unmap (same as was passed to dma_map_sg)
 * @dir: DMA transfer direction (same as was passed to dma_map_sg)
 *
 * Unmap a set of streaming mode DMA translations.  Again, CPU access
 * rules concerning calls here are the same as for dma_unmap_single().
 */
void arm_iommu_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
			enum dma_data_direction dir, struct dma_attrs *attrs)
{
	__iommu_unmap_sg(dev, sg, nents, dir, attrs, false);
}

/**
 * arm_iommu_sync_sg_for_cpu
 * @dev: valid struct device pointer
 * @sg: list of buffers
 * @nents: number of buffers to map (returned from dma_map_sg)
 * @dir: DMA transfer direction (same as was passed to dma_map_sg)
 */
void arm_iommu_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i)
		__dma_page_dev_to_cpu(sg_page(s), s->offset, s->length, dir);

}

/**
 * arm_iommu_sync_sg_for_device
 * @dev: valid struct device pointer
 * @sg: list of buffers
 * @nents: number of buffers to map (returned from dma_map_sg)
 * @dir: DMA transfer direction (same as was passed to dma_map_sg)
 */
void arm_iommu_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i)
		__dma_page_cpu_to_dev(sg_page(s), s->offset, s->length, dir);
}


/**
 * arm_coherent_iommu_map_page
 * @dev: valid struct device pointer
 * @page: page that buffer resides in
 * @offset: offset into page for start of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Coherent IOMMU aware version of arm_dma_map_page()
 */
static dma_addr_t arm_coherent_iommu_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size, enum dma_data_direction dir,
	     struct dma_attrs *attrs)
{
	struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);
	dma_addr_t dma_addr;
	int ret, prot, len = PAGE_ALIGN(size + offset);

	dma_addr = __alloc_iova(mapping, len);
	if (dma_addr == DMA_ERROR_CODE)
		return dma_addr;

	prot = __dma_direction_to_prot(dir);

	ret = iommu_map(mapping->domain, dma_addr, page_to_phys(page), len, prot);
	if (ret < 0)
		goto fail;

	return dma_addr + offset;
fail:
	__free_iova(mapping, dma_addr, len);
	return DMA_ERROR_CODE;
}

/**
 * arm_iommu_map_page
 * @dev: valid struct device pointer
 * @page: page that buffer resides in
 * @offset: offset into page for start of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * IOMMU aware version of arm_dma_map_page()
 */
static dma_addr_t arm_iommu_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size, enum dma_data_direction dir,
	     struct dma_attrs *attrs)
{
	if (!dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		__dma_page_cpu_to_dev(page, offset, size, dir);

	return arm_coherent_iommu_map_page(dev, page, offset, size, dir, attrs);
}

/**
 * arm_coherent_iommu_unmap_page
 * @dev: valid struct device pointer
 * @handle: DMA address of buffer
 * @size: size of buffer (same as passed to dma_map_page)
 * @dir: DMA transfer direction (same as passed to dma_map_page)
 *
 * Coherent IOMMU aware version of arm_dma_unmap_page()
 */
static void arm_coherent_iommu_unmap_page(struct device *dev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);
	dma_addr_t iova = handle & PAGE_MASK;
	int offset = handle & ~PAGE_MASK;
	int len = PAGE_ALIGN(size + offset);

	if (!iova)
		return;

	iommu_unmap(mapping->domain, iova, len);
	__free_iova(mapping, iova, len);
}

/**
 * arm_iommu_unmap_page
 * @dev: valid struct device pointer
 * @handle: DMA address of buffer
 * @size: size of buffer (same as passed to dma_map_page)
 * @dir: DMA transfer direction (same as passed to dma_map_page)
 *
 * IOMMU aware version of arm_dma_unmap_page()
 */
static void arm_iommu_unmap_page(struct device *dev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);
	dma_addr_t iova = handle & PAGE_MASK;
	struct page *page = phys_to_page(iommu_iova_to_phys(mapping->domain, iova));
	int offset = handle & ~PAGE_MASK;
	int len = PAGE_ALIGN(size + offset);

	if (!iova)
		return;

	if (!dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		__dma_page_dev_to_cpu(page, offset, size, dir);

	iommu_unmap(mapping->domain, iova, len);
	__free_iova(mapping, iova, len);
}

static void arm_iommu_sync_single_for_cpu(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);
	dma_addr_t iova = handle & PAGE_MASK;
	struct page *page = phys_to_page(iommu_iova_to_phys(mapping->domain, iova));
	unsigned int offset = handle & ~PAGE_MASK;

	if (!iova)
		return;

	__dma_page_dev_to_cpu(page, offset, size, dir);
}

static void arm_iommu_sync_single_for_device(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);
	dma_addr_t iova = handle & PAGE_MASK;
	struct page *page = phys_to_page(iommu_iova_to_phys(mapping->domain, iova));
	unsigned int offset = handle & ~PAGE_MASK;

	if (!iova)
		return;

	__dma_page_cpu_to_dev(page, offset, size, dir);
}

struct dma_map_ops iommu_ops = {
	.alloc		= arm_iommu_alloc_attrs,
	.free		= arm_iommu_free_attrs,
	.mmap		= arm_iommu_mmap_attrs,
	.get_sgtable	= arm_iommu_get_sgtable,

	.map_page		= arm_iommu_map_page,
	.unmap_page		= arm_iommu_unmap_page,
	.sync_single_for_cpu	= arm_iommu_sync_single_for_cpu,
	.sync_single_for_device	= arm_iommu_sync_single_for_device,

	.map_sg			= arm_iommu_map_sg,
	.unmap_sg		= arm_iommu_unmap_sg,
	.sync_sg_for_cpu	= arm_iommu_sync_sg_for_cpu,
	.sync_sg_for_device	= arm_iommu_sync_sg_for_device,

	.set_dma_mask		= arm_dma_set_mask,
};

struct dma_map_ops iommu_coherent_ops = {
	.alloc		= arm_iommu_alloc_attrs,
	.free		= arm_iommu_free_attrs,
	.mmap		= arm_iommu_mmap_attrs,
	.get_sgtable	= arm_iommu_get_sgtable,

	.map_page	= arm_coherent_iommu_map_page,
	.unmap_page	= arm_coherent_iommu_unmap_page,

	.map_sg		= arm_coherent_iommu_map_sg,
	.unmap_sg	= arm_coherent_iommu_unmap_sg,

	.set_dma_mask	= arm_dma_set_mask,
};

/**
 * arm_iommu_create_mapping
 * @bus: pointer to the bus holding the client device (for IOMMU calls)
 * @base: start address of the valid IO address space
 * @size: maximum size of the valid IO address space
 *
 * Creates a mapping structure which holds information about used/unused
 * IO address ranges, which is required to perform memory allocation and
 * mapping with IOMMU aware functions.
 *
 * The client device need to be attached to the mapping with
 * arm_iommu_attach_device function.
 */
struct dma_iommu_mapping *
arm_iommu_create_mapping(struct bus_type *bus, dma_addr_t base, u64 size)
{
	unsigned int bits = size >> PAGE_SHIFT;
	unsigned int bitmap_size = BITS_TO_LONGS(bits) * sizeof(long);
	struct dma_iommu_mapping *mapping;
	int extensions = 1;
	int err = -ENOMEM;

	/* currently only 32-bit DMA address space is supported */
	if (size > DMA_BIT_MASK(32) + 1)
		return ERR_PTR(-ERANGE);

	if (!bitmap_size)
		return ERR_PTR(-EINVAL);

	if (bitmap_size > PAGE_SIZE) {
		extensions = bitmap_size / PAGE_SIZE;
		bitmap_size = PAGE_SIZE;
	}

	mapping = kzalloc(sizeof(struct dma_iommu_mapping), GFP_KERNEL);
	if (!mapping)
		goto err;

	mapping->bitmap_size = bitmap_size;
	mapping->bitmaps = kzalloc(extensions * sizeof(unsigned long *),
				GFP_KERNEL);
	if (!mapping->bitmaps)
		goto err2;

	mapping->bitmaps[0] = kzalloc(bitmap_size, GFP_KERNEL);
	if (!mapping->bitmaps[0])
		goto err3;

	mapping->nr_bitmaps = 1;
	mapping->extensions = extensions;
	mapping->base = base;
	mapping->bits = BITS_PER_BYTE * bitmap_size;

	spin_lock_init(&mapping->lock);

	mapping->domain = iommu_domain_alloc(bus);
	if (!mapping->domain)
		goto err4;

	kref_init(&mapping->kref);
	return mapping;
err4:
	kfree(mapping->bitmaps[0]);
err3:
	kfree(mapping->bitmaps);
err2:
	kfree(mapping);
err:
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(arm_iommu_create_mapping);

static void release_iommu_mapping(struct kref *kref)
{
	int i;
	struct dma_iommu_mapping *mapping =
		container_of(kref, struct dma_iommu_mapping, kref);

	iommu_domain_free(mapping->domain);
	for (i = 0; i < mapping->nr_bitmaps; i++)
		kfree(mapping->bitmaps[i]);
	kfree(mapping->bitmaps);
	kfree(mapping);
}

static int extend_iommu_mapping(struct dma_iommu_mapping *mapping)
{
	int next_bitmap;

	if (mapping->nr_bitmaps >= mapping->extensions)
		return -EINVAL;

	next_bitmap = mapping->nr_bitmaps;
	mapping->bitmaps[next_bitmap] = kzalloc(mapping->bitmap_size,
						GFP_ATOMIC);
	if (!mapping->bitmaps[next_bitmap])
		return -ENOMEM;

	mapping->nr_bitmaps++;

	return 0;
}

void arm_iommu_release_mapping(struct dma_iommu_mapping *mapping)
{
	if (mapping)
		kref_put(&mapping->kref, release_iommu_mapping);
}
EXPORT_SYMBOL_GPL(arm_iommu_release_mapping);

static int __arm_iommu_attach_device(struct device *dev,
				     struct dma_iommu_mapping *mapping)
{
	int err;

	err = iommu_attach_device(mapping->domain, dev);
	if (err)
		return err;

	kref_get(&mapping->kref);
	to_dma_iommu_mapping(dev) = mapping;

	pr_debug("Attached IOMMU controller to %s device.\n", dev_name(dev));
	return 0;
}

/**
 * arm_iommu_attach_device
 * @dev: valid struct device pointer
 * @mapping: io address space mapping structure (returned from
 *	arm_iommu_create_mapping)
 *
 * Attaches specified io address space mapping to the provided device.
 * This replaces the dma operations (dma_map_ops pointer) with the
 * IOMMU aware version.
 *
 * More than one client might be attached to the same io address space
 * mapping.
 */
int arm_iommu_attach_device(struct device *dev,
			    struct dma_iommu_mapping *mapping)
{
	int err;

	err = __arm_iommu_attach_device(dev, mapping);
	if (err)
		return err;

	set_dma_ops(dev, &iommu_ops);
	return 0;
}
EXPORT_SYMBOL_GPL(arm_iommu_attach_device);

static void __arm_iommu_detach_device(struct device *dev)
{
	struct dma_iommu_mapping *mapping;

	mapping = to_dma_iommu_mapping(dev);
	if (!mapping) {
		dev_warn(dev, "Not attached\n");
		return;
	}

	iommu_detach_device(mapping->domain, dev);
	kref_put(&mapping->kref, release_iommu_mapping);
	to_dma_iommu_mapping(dev) = NULL;

	pr_debug("Detached IOMMU controller from %s device.\n", dev_name(dev));
}

/**
 * arm_iommu_detach_device
 * @dev: valid struct device pointer
 *
 * Detaches the provided device from a previously attached map.
 * This voids the dma operations (dma_map_ops pointer)
 */
void arm_iommu_detach_device(struct device *dev)
{
	__arm_iommu_detach_device(dev);
	set_dma_ops(dev, NULL);
}
EXPORT_SYMBOL_GPL(arm_iommu_detach_device);

static struct dma_map_ops *arm_get_iommu_dma_map_ops(bool coherent)
{
	return coherent ? &iommu_coherent_ops : &iommu_ops;
}

static bool arm_setup_iommu_dma_ops(struct device *dev, u64 dma_base, u64 size,
				    struct iommu_ops *iommu)
{
	struct dma_iommu_mapping *mapping;

	if (!iommu)
		return false;

	mapping = arm_iommu_create_mapping(dev->bus, dma_base, size);
	if (IS_ERR(mapping)) {
		pr_warn("Failed to create %llu-byte IOMMU mapping for device %s\n",
				size, dev_name(dev));
		return false;
	}

	if (__arm_iommu_attach_device(dev, mapping)) {
		pr_warn("Failed to attached device %s to IOMMU_mapping\n",
				dev_name(dev));
		arm_iommu_release_mapping(mapping);
		return false;
	}

	return true;
}

static void arm_teardown_iommu_dma_ops(struct device *dev)
{
	struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);

	if (!mapping)
		return;

	__arm_iommu_detach_device(dev);
	arm_iommu_release_mapping(mapping);
}

#else

static bool arm_setup_iommu_dma_ops(struct device *dev, u64 dma_base, u64 size,
				    struct iommu_ops *iommu)
{
	return false;
}

static void arm_teardown_iommu_dma_ops(struct device *dev) { }

#define arm_get_iommu_dma_map_ops arm_get_dma_map_ops

#endif	/* CONFIG_ARM_DMA_USE_IOMMU */

static struct dma_map_ops *arm_get_dma_map_ops(bool coherent)
{
	return coherent ? &arm_coherent_dma_ops : &arm_dma_ops;
}

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			struct iommu_ops *iommu, bool coherent)
{
	struct dma_map_ops *dma_ops;

	dev->archdata.dma_coherent = coherent;
	if (arm_setup_iommu_dma_ops(dev, dma_base, size, iommu))
		dma_ops = arm_get_iommu_dma_map_ops(coherent);
	else
		dma_ops = arm_get_dma_map_ops(coherent);

	set_dma_ops(dev, dma_ops);
}

void arch_teardown_dma_ops(struct device *dev)
{
	arm_teardown_iommu_dma_ops(dev);
}
