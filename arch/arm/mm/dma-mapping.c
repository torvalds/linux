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
#include <linux/module.h>
#include <linux/mm.h>
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

#include <asm/memory.h>
#include <asm/highmem.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/mach/arch.h>
#include <asm/dma-iommu.h>
#include <asm/mach/map.h>
#include <asm/system_info.h>
#include <asm/dma-contiguous.h>

#include "mm.h"

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
	if (!arch_is_coherent() && !dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		__dma_page_cpu_to_dev(page, offset, size, dir);
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
	if (!arch_is_coherent() && !dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		__dma_page_dev_to_cpu(pfn_to_page(dma_to_pfn(dev, handle)),
				      handle & ~PAGE_MASK, size, dir);
}

static void arm_dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	unsigned int offset = handle & (PAGE_SIZE - 1);
	struct page *page = pfn_to_page(dma_to_pfn(dev, handle-offset));
	if (!arch_is_coherent())
		__dma_page_dev_to_cpu(page, offset, size, dir);
}

static void arm_dma_sync_single_for_device(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	unsigned int offset = handle & (PAGE_SIZE - 1);
	struct page *page = pfn_to_page(dma_to_pfn(dev, handle-offset));
	if (!arch_is_coherent())
		__dma_page_cpu_to_dev(page, offset, size, dir);
}

static int arm_dma_set_mask(struct device *dev, u64 dma_mask);

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

static u64 get_coherent_dma_mask(struct device *dev)
{
	u64 mask = (u64)arm_dma_limit;

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

		if ((~mask) & (u64)arm_dma_limit) {
			dev_warn(dev, "coherent DMA mask %#llx is smaller "
				 "than system GFP_DMA mask %#llx\n",
				 mask, (u64)arm_dma_limit);
			return 0;
		}
	}

	return mask;
}

static void __dma_clear_buffer(struct page *page, size_t size)
{
	void *ptr;
	/*
	 * Ensure that the allocated pages are zeroed, and that any data
	 * lurking in the kernel direct-mapped region is invalidated.
	 */
	ptr = page_address(page);
	if (ptr) {
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
#ifdef CONFIG_HUGETLB_PAGE
#error ARM Coherent DMA allocator does not (yet) support huge TLB
#endif

static void *__alloc_from_contiguous(struct device *dev, size_t size,
				     pgprot_t prot, struct page **ret_page);

static void *__alloc_remap_buffer(struct device *dev, size_t size, gfp_t gfp,
				 pgprot_t prot, struct page **ret_page,
				 const void *caller);

static void *
__dma_alloc_remap(struct page *page, size_t size, gfp_t gfp, pgprot_t prot,
	const void *caller)
{
	struct vm_struct *area;
	unsigned long addr;

	/*
	 * DMA allocation can be mapped to user space, so lets
	 * set VM_USERMAP flags too.
	 */
	area = get_vm_area_caller(size, VM_ARM_DMA_CONSISTENT | VM_USERMAP,
				  caller);
	if (!area)
		return NULL;
	addr = (unsigned long)area->addr;
	area->phys_addr = __pfn_to_phys(page_to_pfn(page));

	if (ioremap_page_range(addr, addr + size, area->phys_addr, prot)) {
		vunmap((void *)addr);
		return NULL;
	}
	return (void *)addr;
}

static void __dma_free_remap(void *cpu_addr, size_t size)
{
	unsigned int flags = VM_ARM_DMA_CONSISTENT | VM_USERMAP;
	struct vm_struct *area = find_vm_area(cpu_addr);
	if (!area || (area->flags & flags) != flags) {
		WARN(1, "trying to free invalid coherent area: %p\n", cpu_addr);
		return;
	}
	unmap_kernel_range((unsigned long)cpu_addr, size);
	vunmap(cpu_addr);
}

#define DEFAULT_DMA_COHERENT_POOL_SIZE	SZ_256K

struct dma_pool {
	size_t size;
	spinlock_t lock;
	unsigned long *bitmap;
	unsigned long nr_pages;
	void *vaddr;
	struct page *page;
};

static struct dma_pool atomic_pool = {
	.size = DEFAULT_DMA_COHERENT_POOL_SIZE,
};

static int __init early_coherent_pool(char *p)
{
	atomic_pool.size = memparse(p, &p);
	return 0;
}
early_param("coherent_pool", early_coherent_pool);

void __init init_dma_coherent_pool_size(unsigned long size)
{
	/*
	 * Catch any attempt to set the pool size too late.
	 */
	BUG_ON(atomic_pool.vaddr);

	/*
	 * Set architecture specific coherent pool size only if
	 * it has not been changed by kernel command line parameter.
	 */
	if (atomic_pool.size == DEFAULT_DMA_COHERENT_POOL_SIZE)
		atomic_pool.size = size;
}

/*
 * Initialise the coherent pool for atomic allocations.
 */
static int __init atomic_pool_init(void)
{
	struct dma_pool *pool = &atomic_pool;
	pgprot_t prot = pgprot_dmacoherent(pgprot_kernel);
	unsigned long nr_pages = pool->size >> PAGE_SHIFT;
	unsigned long *bitmap;
	struct page *page;
	void *ptr;
	int bitmap_size = BITS_TO_LONGS(nr_pages) * sizeof(long);

	bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!bitmap)
		goto no_bitmap;

	if (IS_ENABLED(CONFIG_CMA))
		ptr = __alloc_from_contiguous(NULL, pool->size, prot, &page);
	else
		ptr = __alloc_remap_buffer(NULL, pool->size, GFP_KERNEL, prot,
					   &page, NULL);
	if (ptr) {
		spin_lock_init(&pool->lock);
		pool->vaddr = ptr;
		pool->page = page;
		pool->bitmap = bitmap;
		pool->nr_pages = nr_pages;
		pr_info("DMA: preallocated %u KiB pool for atomic coherent allocations\n",
		       (unsigned)pool->size / 1024);
		return 0;
	}
	kfree(bitmap);
no_bitmap:
	pr_err("DMA: failed to allocate %u KiB pool for atomic coherent allocation\n",
	       (unsigned)pool->size / 1024);
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
		 * Clear previous low-memory mapping
		 */
		for (addr = __phys_to_virt(start); addr < __phys_to_virt(end);
		     addr += PMD_SIZE)
			pmd_clear(pmd_off_k(addr));

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
	dsb();
	flush_tlb_kernel_range(start, end);
}

static void *__alloc_remap_buffer(struct device *dev, size_t size, gfp_t gfp,
				 pgprot_t prot, struct page **ret_page,
				 const void *caller)
{
	struct page *page;
	void *ptr;
	page = __dma_alloc_buffer(dev, size, gfp);
	if (!page)
		return NULL;

	ptr = __dma_alloc_remap(page, size, gfp, prot, caller);
	if (!ptr) {
		__dma_free_buffer(page, size);
		return NULL;
	}

	*ret_page = page;
	return ptr;
}

static void *__alloc_from_pool(size_t size, struct page **ret_page)
{
	struct dma_pool *pool = &atomic_pool;
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned int pageno;
	unsigned long flags;
	void *ptr = NULL;
	unsigned long align_mask;

	if (!pool->vaddr) {
		WARN(1, "coherent pool not initialised!\n");
		return NULL;
	}

	/*
	 * Align the region allocation - allocations from pool are rather
	 * small, so align them to their order in pages, minimum is a page
	 * size. This helps reduce fragmentation of the DMA space.
	 */
	align_mask = (1 << get_order(size)) - 1;

	spin_lock_irqsave(&pool->lock, flags);
	pageno = bitmap_find_next_zero_area(pool->bitmap, pool->nr_pages,
					    0, count, align_mask);
	if (pageno < pool->nr_pages) {
		bitmap_set(pool->bitmap, pageno, count);
		ptr = pool->vaddr + PAGE_SIZE * pageno;
		*ret_page = pool->page + pageno;
	} else {
		pr_err_once("ERROR: %u KiB atomic DMA coherent pool is too small!\n"
			    "Please increase it with coherent_pool= kernel parameter!\n",
			    (unsigned)pool->size / 1024);
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	return ptr;
}

static int __free_from_pool(void *start, size_t size)
{
	struct dma_pool *pool = &atomic_pool;
	unsigned long pageno, count;
	unsigned long flags;

	if (start < pool->vaddr || start > pool->vaddr + pool->size)
		return 0;

	if (start + size > pool->vaddr + pool->size) {
		WARN(1, "freeing wrong coherent size from pool\n");
		return 0;
	}

	pageno = (start - pool->vaddr) >> PAGE_SHIFT;
	count = size >> PAGE_SHIFT;

	spin_lock_irqsave(&pool->lock, flags);
	bitmap_clear(pool->bitmap, pageno, count);
	spin_unlock_irqrestore(&pool->lock, flags);

	return 1;
}

static void *__alloc_from_contiguous(struct device *dev, size_t size,
				     pgprot_t prot, struct page **ret_page)
{
	unsigned long order = get_order(size);
	size_t count = size >> PAGE_SHIFT;
	struct page *page;

	page = dma_alloc_from_contiguous(dev, count, order);
	if (!page)
		return NULL;

	__dma_clear_buffer(page, size);
	__dma_remap(page, size, prot);

	*ret_page = page;
	return page_address(page);
}

static void __free_from_contiguous(struct device *dev, struct page *page,
				   size_t size)
{
	__dma_remap(page, size, pgprot_kernel);
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

#define __get_dma_pgprot(attrs, prot)	__pgprot(0)
#define __alloc_remap_buffer(dev, size, gfp, prot, ret, c)	NULL
#define __alloc_from_pool(size, ret_page)			NULL
#define __alloc_from_contiguous(dev, size, prot, ret)		NULL
#define __free_from_pool(cpu_addr, size)			0
#define __free_from_contiguous(dev, page, size)			do { } while (0)
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



static void *__dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
			 gfp_t gfp, pgprot_t prot, const void *caller)
{
	u64 mask = get_coherent_dma_mask(dev);
	struct page *page;
	void *addr;

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

	*handle = DMA_ERROR_CODE;
	size = PAGE_ALIGN(size);

	if (arch_is_coherent() || nommu())
		addr = __alloc_simple_buffer(dev, size, gfp, &page);
	else if (gfp & GFP_ATOMIC)
		addr = __alloc_from_pool(size, &page);
	else if (!IS_ENABLED(CONFIG_CMA))
		addr = __alloc_remap_buffer(dev, size, gfp, prot, &page, caller);
	else
		addr = __alloc_from_contiguous(dev, size, prot, &page);

	if (addr)
		*handle = pfn_to_dma(dev, page_to_pfn(page));

	return addr;
}

/*
 * Allocate DMA-coherent memory space and return both the kernel remapped
 * virtual and bus address for that space.
 */
void *arm_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
		    gfp_t gfp, struct dma_attrs *attrs)
{
	pgprot_t prot = __get_dma_pgprot(attrs, pgprot_kernel);
	void *memory;

	if (dma_alloc_from_coherent(dev, size, handle, &memory))
		return memory;

	return __dma_alloc(dev, size, handle, gfp, prot,
			   __builtin_return_address(0));
}

/*
 * Create userspace mapping for the DMA-coherent memory.
 */
int arm_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		 void *cpu_addr, dma_addr_t dma_addr, size_t size,
		 struct dma_attrs *attrs)
{
	int ret = -ENXIO;
#ifdef CONFIG_MMU
	unsigned long nr_vma_pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	unsigned long nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long pfn = dma_to_pfn(dev, dma_addr);
	unsigned long off = vma->vm_pgoff;

	vma->vm_page_prot = __get_dma_pgprot(attrs, vma->vm_page_prot);

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
 * Free a buffer as defined by the above mapping.
 */
void arm_dma_free(struct device *dev, size_t size, void *cpu_addr,
		  dma_addr_t handle, struct dma_attrs *attrs)
{
	struct page *page = pfn_to_page(dma_to_pfn(dev, handle));

	if (dma_release_from_coherent(dev, get_order(size), cpu_addr))
		return;

	size = PAGE_ALIGN(size);

	if (arch_is_coherent() || nommu()) {
		__dma_free_buffer(page, size);
	} else if (__free_from_pool(cpu_addr, size)) {
		return;
	} else if (!IS_ENABLED(CONFIG_CMA)) {
		__dma_free_remap(cpu_addr, size);
		__dma_free_buffer(page, size);
	} else {
		/*
		 * Non-atomic allocations cannot be freed with IRQs disabled
		 */
		WARN_ON(irqs_disabled());
		__free_from_contiguous(dev, page, size);
	}
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
	/*
	 * A single sg entry may refer to multiple physically contiguous
	 * pages.  But we still need to process highmem pages individually.
	 * If highmem is not configured then the bulk of this loop gets
	 * optimized out.
	 */
	size_t left = size;
	do {
		size_t len = left;
		void *vaddr;

		if (PageHighMem(page)) {
			if (len + offset > PAGE_SIZE) {
				if (offset >= PAGE_SIZE) {
					page += offset / PAGE_SIZE;
					offset %= PAGE_SIZE;
				}
				len = PAGE_SIZE - offset;
			}
			vaddr = kmap_high_get(page);
			if (vaddr) {
				vaddr += offset;
				op(vaddr, len, dir);
				kunmap_high(page);
			} else if (cache_is_vipt()) {
				/* unmapped pages might still be cached */
				vaddr = kmap_atomic(page);
				op(vaddr + offset, len, dir);
				kunmap_atomic(vaddr);
			}
		} else {
			vaddr = page_address(page) + offset;
			op(vaddr, len, dir);
		}
		offset = 0;
		page++;
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
	unsigned long paddr;

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
	unsigned long paddr = page_to_phys(page) + off;

	/* FIXME: non-speculating: not required */
	/* don't bother invalidating if DMA to device */
	if (dir != DMA_TO_DEVICE)
		outer_inv_range(paddr, paddr + size);

	dma_cache_maint_page(page, off, size, dir, dmac_unmap_area);

	/*
	 * Mark the D-cache clean for this page to avoid extra flushing.
	 */
	if (dir != DMA_TO_DEVICE && off == 0 && size >= PAGE_SIZE)
		set_bit(PG_dcache_clean, &page->flags);
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
	if (mask < (u64)arm_dma_limit)
		return 0;
	return 1;
}
EXPORT_SYMBOL(dma_supported);

static int arm_dma_set_mask(struct device *dev, u64 dma_mask)
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

static inline dma_addr_t __alloc_iova(struct dma_iommu_mapping *mapping,
				      size_t size)
{
	unsigned int order = get_order(size);
	unsigned int align = 0;
	unsigned int count, start;
	unsigned long flags;

	count = ((PAGE_ALIGN(size) >> PAGE_SHIFT) +
		 (1 << mapping->order) - 1) >> mapping->order;

	if (order > mapping->order)
		align = (1 << (order - mapping->order)) - 1;

	spin_lock_irqsave(&mapping->lock, flags);
	start = bitmap_find_next_zero_area(mapping->bitmap, mapping->bits, 0,
					   count, align);
	if (start > mapping->bits) {
		spin_unlock_irqrestore(&mapping->lock, flags);
		return DMA_ERROR_CODE;
	}

	bitmap_set(mapping->bitmap, start, count);
	spin_unlock_irqrestore(&mapping->lock, flags);

	return mapping->base + (start << (mapping->order + PAGE_SHIFT));
}

static inline void __free_iova(struct dma_iommu_mapping *mapping,
			       dma_addr_t addr, size_t size)
{
	unsigned int start = (addr - mapping->base) >>
			     (mapping->order + PAGE_SHIFT);
	unsigned int count = ((size >> PAGE_SHIFT) +
			      (1 << mapping->order) - 1) >> mapping->order;
	unsigned long flags;

	spin_lock_irqsave(&mapping->lock, flags);
	bitmap_clear(mapping->bitmap, start, count);
	spin_unlock_irqrestore(&mapping->lock, flags);
}

static struct page **__iommu_alloc_buffer(struct device *dev, size_t size, gfp_t gfp)
{
	struct page **pages;
	int count = size >> PAGE_SHIFT;
	int array_size = count * sizeof(struct page *);
	int i = 0;

	if (array_size <= PAGE_SIZE)
		pages = kzalloc(array_size, gfp);
	else
		pages = vzalloc(array_size);
	if (!pages)
		return NULL;

	while (count) {
		int j, order = __fls(count);

		pages[i] = alloc_pages(gfp | __GFP_NOWARN, order);
		while (!pages[i] && order)
			pages[i] = alloc_pages(gfp | __GFP_NOWARN, --order);
		if (!pages[i])
			goto error;

		if (order)
			split_page(pages[i], order);
		j = 1 << order;
		while (--j)
			pages[i + j] = pages[i] + j;

		__dma_clear_buffer(pages[i], PAGE_SIZE << order);
		i += 1 << order;
		count -= 1 << order;
	}

	return pages;
error:
	while (i--)
		if (pages[i])
			__free_pages(pages[i], 0);
	if (array_size <= PAGE_SIZE)
		kfree(pages);
	else
		vfree(pages);
	return NULL;
}

static int __iommu_free_buffer(struct device *dev, struct page **pages, size_t size)
{
	int count = size >> PAGE_SHIFT;
	int array_size = count * sizeof(struct page *);
	int i;
	for (i = 0; i < count; i++)
		if (pages[i])
			__free_pages(pages[i], 0);
	if (array_size <= PAGE_SIZE)
		kfree(pages);
	else
		vfree(pages);
	return 0;
}

/*
 * Create a CPU mapping for a specified pages
 */
static void *
__iommu_alloc_remap(struct page **pages, size_t size, gfp_t gfp, pgprot_t prot,
		    const void *caller)
{
	unsigned int i, nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	struct vm_struct *area;
	unsigned long p;

	area = get_vm_area_caller(size, VM_ARM_DMA_CONSISTENT | VM_USERMAP,
				  caller);
	if (!area)
		return NULL;

	area->pages = pages;
	area->nr_pages = nr_pages;
	p = (unsigned long)area->addr;

	for (i = 0; i < nr_pages; i++) {
		phys_addr_t phys = __pfn_to_phys(page_to_pfn(pages[i]));
		if (ioremap_page_range(p, p + PAGE_SIZE, phys, prot))
			goto err;
		p += PAGE_SIZE;
	}
	return area->addr;
err:
	unmap_kernel_range((unsigned long)area->addr, size);
	vunmap(area->addr);
	return NULL;
}

/*
 * Create a mapping in device IO address space for specified pages
 */
static dma_addr_t
__iommu_create_mapping(struct device *dev, struct page **pages, size_t size)
{
	struct dma_iommu_mapping *mapping = dev->archdata.mapping;
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	dma_addr_t dma_addr, iova;
	int i, ret = DMA_ERROR_CODE;

	dma_addr = __alloc_iova(mapping, size);
	if (dma_addr == DMA_ERROR_CODE)
		return dma_addr;

	iova = dma_addr;
	for (i = 0; i < count; ) {
		unsigned int next_pfn = page_to_pfn(pages[i]) + 1;
		phys_addr_t phys = page_to_phys(pages[i]);
		unsigned int len, j;

		for (j = i + 1; j < count; j++, next_pfn++)
			if (page_to_pfn(pages[j]) != next_pfn)
				break;

		len = (j - i) << PAGE_SHIFT;
		ret = iommu_map(mapping->domain, iova, phys, len, 0);
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
	struct dma_iommu_mapping *mapping = dev->archdata.mapping;

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

static struct page **__iommu_get_pages(void *cpu_addr, struct dma_attrs *attrs)
{
	struct vm_struct *area;

	if (dma_get_attr(DMA_ATTR_NO_KERNEL_MAPPING, attrs))
		return cpu_addr;

	area = find_vm_area(cpu_addr);
	if (area && (area->flags & VM_ARM_DMA_CONSISTENT))
		return area->pages;
	return NULL;
}

static void *arm_iommu_alloc_attrs(struct device *dev, size_t size,
	    dma_addr_t *handle, gfp_t gfp, struct dma_attrs *attrs)
{
	pgprot_t prot = __get_dma_pgprot(attrs, pgprot_kernel);
	struct page **pages;
	void *addr = NULL;

	*handle = DMA_ERROR_CODE;
	size = PAGE_ALIGN(size);

	pages = __iommu_alloc_buffer(dev, size, gfp);
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
	__iommu_free_buffer(dev, pages, size);
	return NULL;
}

static int arm_iommu_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
		    void *cpu_addr, dma_addr_t dma_addr, size_t size,
		    struct dma_attrs *attrs)
{
	unsigned long uaddr = vma->vm_start;
	unsigned long usize = vma->vm_end - vma->vm_start;
	struct page **pages = __iommu_get_pages(cpu_addr, attrs);

	vma->vm_page_prot = __get_dma_pgprot(attrs, vma->vm_page_prot);

	if (!pages)
		return -ENXIO;

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
	struct page **pages = __iommu_get_pages(cpu_addr, attrs);
	size = PAGE_ALIGN(size);

	if (!pages) {
		WARN(1, "trying to free invalid coherent area: %p\n", cpu_addr);
		return;
	}

	if (!dma_get_attr(DMA_ATTR_NO_KERNEL_MAPPING, attrs)) {
		unmap_kernel_range((unsigned long)cpu_addr, size);
		vunmap(cpu_addr);
	}

	__iommu_remove_mapping(dev, handle, size);
	__iommu_free_buffer(dev, pages, size);
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

/*
 * Map a part of the scatter-gather list into contiguous io address space
 */
static int __map_sg_chunk(struct device *dev, struct scatterlist *sg,
			  size_t size, dma_addr_t *handle,
			  enum dma_data_direction dir, struct dma_attrs *attrs)
{
	struct dma_iommu_mapping *mapping = dev->archdata.mapping;
	dma_addr_t iova, iova_base;
	int ret = 0;
	unsigned int count;
	struct scatterlist *s;

	size = PAGE_ALIGN(size);
	*handle = DMA_ERROR_CODE;

	iova_base = iova = __alloc_iova(mapping, size);
	if (iova == DMA_ERROR_CODE)
		return -ENOMEM;

	for (count = 0, s = sg; count < (size >> PAGE_SHIFT); s = sg_next(s)) {
		phys_addr_t phys = page_to_phys(sg_page(s));
		unsigned int len = PAGE_ALIGN(s->offset + s->length);

		if (!arch_is_coherent() &&
		    !dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
			__dma_page_cpu_to_dev(sg_page(s), s->offset, s->length, dir);

		ret = iommu_map(mapping->domain, iova, phys, len, 0);
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
int arm_iommu_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		     enum dma_data_direction dir, struct dma_attrs *attrs)
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
			    dir, attrs) < 0)
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
	if (__map_sg_chunk(dev, start, size, &dma->dma_address, dir, attrs) < 0)
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
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		if (sg_dma_len(s))
			__iommu_remove_mapping(dev, sg_dma_address(s),
					       sg_dma_len(s));
		if (!arch_is_coherent() &&
		    !dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
			__dma_page_dev_to_cpu(sg_page(s), s->offset,
					      s->length, dir);
	}
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
		if (!arch_is_coherent())
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
		if (!arch_is_coherent())
			__dma_page_cpu_to_dev(sg_page(s), s->offset, s->length, dir);
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
	struct dma_iommu_mapping *mapping = dev->archdata.mapping;
	dma_addr_t dma_addr;
	int ret, len = PAGE_ALIGN(size + offset);

	if (!arch_is_coherent() && !dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		__dma_page_cpu_to_dev(page, offset, size, dir);

	dma_addr = __alloc_iova(mapping, len);
	if (dma_addr == DMA_ERROR_CODE)
		return dma_addr;

	ret = iommu_map(mapping->domain, dma_addr, page_to_phys(page), len, 0);
	if (ret < 0)
		goto fail;

	return dma_addr + offset;
fail:
	__free_iova(mapping, dma_addr, len);
	return DMA_ERROR_CODE;
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
	struct dma_iommu_mapping *mapping = dev->archdata.mapping;
	dma_addr_t iova = handle & PAGE_MASK;
	struct page *page = phys_to_page(iommu_iova_to_phys(mapping->domain, iova));
	int offset = handle & ~PAGE_MASK;
	int len = PAGE_ALIGN(size + offset);

	if (!iova)
		return;

	if (!arch_is_coherent() && !dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		__dma_page_dev_to_cpu(page, offset, size, dir);

	iommu_unmap(mapping->domain, iova, len);
	__free_iova(mapping, iova, len);
}

static void arm_iommu_sync_single_for_cpu(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	struct dma_iommu_mapping *mapping = dev->archdata.mapping;
	dma_addr_t iova = handle & PAGE_MASK;
	struct page *page = phys_to_page(iommu_iova_to_phys(mapping->domain, iova));
	unsigned int offset = handle & ~PAGE_MASK;

	if (!iova)
		return;

	if (!arch_is_coherent())
		__dma_page_dev_to_cpu(page, offset, size, dir);
}

static void arm_iommu_sync_single_for_device(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	struct dma_iommu_mapping *mapping = dev->archdata.mapping;
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
};

/**
 * arm_iommu_create_mapping
 * @bus: pointer to the bus holding the client device (for IOMMU calls)
 * @base: start address of the valid IO address space
 * @size: size of the valid IO address space
 * @order: accuracy of the IO addresses allocations
 *
 * Creates a mapping structure which holds information about used/unused
 * IO address ranges, which is required to perform memory allocation and
 * mapping with IOMMU aware functions.
 *
 * The client device need to be attached to the mapping with
 * arm_iommu_attach_device function.
 */
struct dma_iommu_mapping *
arm_iommu_create_mapping(struct bus_type *bus, dma_addr_t base, size_t size,
			 int order)
{
	unsigned int count = size >> (PAGE_SHIFT + order);
	unsigned int bitmap_size = BITS_TO_LONGS(count) * sizeof(long);
	struct dma_iommu_mapping *mapping;
	int err = -ENOMEM;

	if (!count)
		return ERR_PTR(-EINVAL);

	mapping = kzalloc(sizeof(struct dma_iommu_mapping), GFP_KERNEL);
	if (!mapping)
		goto err;

	mapping->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!mapping->bitmap)
		goto err2;

	mapping->base = base;
	mapping->bits = BITS_PER_BYTE * bitmap_size;
	mapping->order = order;
	spin_lock_init(&mapping->lock);

	mapping->domain = iommu_domain_alloc(bus);
	if (!mapping->domain)
		goto err3;

	kref_init(&mapping->kref);
	return mapping;
err3:
	kfree(mapping->bitmap);
err2:
	kfree(mapping);
err:
	return ERR_PTR(err);
}

static void release_iommu_mapping(struct kref *kref)
{
	struct dma_iommu_mapping *mapping =
		container_of(kref, struct dma_iommu_mapping, kref);

	iommu_domain_free(mapping->domain);
	kfree(mapping->bitmap);
	kfree(mapping);
}

void arm_iommu_release_mapping(struct dma_iommu_mapping *mapping)
{
	if (mapping)
		kref_put(&mapping->kref, release_iommu_mapping);
}

/**
 * arm_iommu_attach_device
 * @dev: valid struct device pointer
 * @mapping: io address space mapping structure (returned from
 *	arm_iommu_create_mapping)
 *
 * Attaches specified io address space mapping to the provided device,
 * this replaces the dma operations (dma_map_ops pointer) with the
 * IOMMU aware version. More than one client might be attached to
 * the same io address space mapping.
 */
int arm_iommu_attach_device(struct device *dev,
			    struct dma_iommu_mapping *mapping)
{
	int err;

	err = iommu_attach_device(mapping->domain, dev);
	if (err)
		return err;

	kref_get(&mapping->kref);
	dev->archdata.mapping = mapping;
	set_dma_ops(dev, &iommu_ops);

	pr_info("Attached IOMMU controller to %s device.\n", dev_name(dev));
	return 0;
}

#endif
