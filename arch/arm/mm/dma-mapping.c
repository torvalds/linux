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
#include <linux/dma-iommu.h>
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
#include <asm/mach/map.h>
#include <asm/system_info.h>
#include <asm/dma-contiguous.h>

#include "dma.h"
#include "mm.h"

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

void *arch_alloc_from_atomic_pool(size_t size, struct page **ret_page, gfp_t gfp)
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

bool arch_in_atomic_pool(void *start, size_t size)
{
	return addr_in_gen_pool(atomic_pool, (unsigned long)start, size);
}

int arch_free_from_atomic_pool(void *start, size_t size)
{
	if (!arch_in_atomic_pool(start, size))
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

#define nommu() 0

#define __alloc_from_pool(size, ret, gfp) arch_alloc_from_atomic_pool(size, ret, gfp)
#define __free_from_pool(addr, size) arch_free_from_atomic_pool(addr, size)
#define __get_dma_pgprot(attrs, prot, coherent) arch_get_dma_pgprot(attrs, prot, coherent)

#else	/* !CONFIG_MMU */

#define nommu() 1

#define __get_dma_pgprot(attrs, prot, coherent)				__pgprot(0)
#define __alloc_remap_buffer(dev, size, gfp, prot, ret, c, wv)	NULL
#define __alloc_from_pool(size, ret_page, gfp)			NULL
#define __alloc_from_contiguous(dev, size, prot, ret, c, wv)	NULL
#define __free_from_atomic_pool(cpu_addr, size)			0
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



static void *__dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
			 gfp_t gfp, pgprot_t prot, bool is_coherent,
			 struct dma_attrs *attrs, const void *caller)
{
	u64 mask = get_coherent_dma_mask(dev);
	struct page *page = NULL;
	void *addr;
	bool want_vaddr;

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
	want_vaddr = !dma_get_attr(DMA_ATTR_NO_KERNEL_MAPPING, attrs);

	if (nommu())
		addr = __alloc_simple_buffer(dev, size, gfp, &page);
	else if (dev_get_cma_area(dev) && (gfp & __GFP_DIRECT_RECLAIM))
		addr = __alloc_from_contiguous(dev, size, prot, &page,
					       caller, want_vaddr);
	else if (is_coherent)
		addr = __alloc_simple_buffer(dev, size, gfp, &page);
	else if (!gfpflags_allow_blocking(gfp))
		addr = __alloc_from_pool(size, &page, gfp);
	else
		addr = __alloc_remap_buffer(dev, size, gfp, prot, &page,
					    caller, want_vaddr);

	if (page)
		*handle = pfn_to_dma(dev, page_to_pfn(page));

	return want_vaddr ? addr : page;
}

/*
 * Allocate DMA-coherent memory space and return both the kernel remapped
 * virtual and bus address for that space.
 */
void *arm_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
		    gfp_t gfp, struct dma_attrs *attrs)
{
	pgprot_t prot = __get_dma_pgprot(attrs, PAGE_KERNEL, false);

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
	vma->vm_page_prot = __get_dma_pgprot(attrs, vma->vm_page_prot, false);
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
	bool want_vaddr = !dma_get_attr(DMA_ATTR_NO_KERNEL_MAPPING, attrs);

	size = PAGE_ALIGN(size);

	if (nommu()) {
		__dma_free_buffer(page, size);
	} else if (!is_coherent && __free_from_pool(cpu_addr, size)) {
		return;
	} else if (!dev_get_cma_area(dev)) {
		if (want_vaddr && !is_coherent)
			__dma_free_remap(cpu_addr, size);
		__dma_free_buffer(page, size);
	} else {
		/*
		 * Non-atomic allocations cannot be freed with IRQs disabled
		 */
		WARN_ON(irqs_disabled());
		__free_from_contiguous(dev, page, cpu_addr, size, want_vaddr);
	}
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

/*
 * The whole dma_get_sgtable() idea is fundamentally unsafe - it seems
 * that the intention is to allow exporting memory allocated via the
 * coherent DMA APIs through the dma_buf API, which only accepts a
 * scattertable.  This presents a couple of problems:
 * 1. Not all memory allocated via the coherent DMA APIs is backed by
 *    a struct page
 * 2. Passing coherent DMA memory into the streaming APIs is not allowed
 *    as we will try to flush the memory through a different alias to that
 *    actually being used (and the flushes are redundant.)
 */
int arm_dma_get_sgtable(struct device *dev, struct sg_table *sgt,
		 void *cpu_addr, dma_addr_t handle, size_t size,
		 struct dma_attrs *attrs)
{
	unsigned long pfn = dma_to_pfn(dev, handle);
	struct page *page;
	int ret;

	/* If the PFN is not valid, we do not have a struct page */
	if (!pfn_valid(pfn))
		return -ENXIO;

	page = pfn_to_page(pfn);

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
void __dma_page_cpu_to_dev(struct page *page, unsigned long off,
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

void __dma_page_dev_to_cpu(struct page *page, unsigned long off,
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

static struct dma_map_ops *arm_get_dma_map_ops(bool coherent)
{
	return coherent ? &arm_coherent_dma_ops : &arm_dma_ops;
}

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			struct iommu_ops *iommu, bool coherent)
{
	dev->archdata.dma_coherent = coherent;

	if (!common_iommu_setup_dma_ops(dev, dma_base, size, iommu))
		arch_set_dma_ops(dev, arm_get_dma_map_ops(coherent));
}

void arch_teardown_dma_ops(struct device *dev)
{
	common_iommu_teardown_dma_ops(dev);
}
