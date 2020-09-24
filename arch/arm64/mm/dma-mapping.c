/*
 * SWIOTLB-based DMA API implementation
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/gfp.h>
#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/cache.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/genalloc.h>
#include <linux/dma-direct.h>
#include <linux/dma-contiguous.h>
#include <linux/mm.h>
#include <linux/iommu.h>
#include <linux/vmalloc.h>
#include <linux/swiotlb.h>
#include <linux/dma-removed.h>
#include <linux/pci.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/dma-iommu.h>
#include <linux/of_address.h>
#include <linux/dma-mapping-fast.h>

static int swiotlb __ro_after_init;

static pgprot_t __get_dma_pgprot(unsigned long attrs, pgprot_t prot,
				 bool coherent)
{
	if (attrs & DMA_ATTR_STRONGLY_ORDERED)
		return pgprot_noncached(prot);
	else if (!coherent || (attrs & DMA_ATTR_WRITE_COMBINE))
		return pgprot_writecombine(prot);
	return prot;
}

static bool is_dma_coherent(struct device *dev, unsigned long attrs)
{

	if (attrs & DMA_ATTR_FORCE_COHERENT)
		return true;
	else if (attrs & DMA_ATTR_FORCE_NON_COHERENT)
		return false;
	else if (is_device_dma_coherent(dev))
		return true;
	else
		return false;
}
static struct gen_pool *atomic_pool __ro_after_init;

#define NO_KERNEL_MAPPING_DUMMY 0x2222
#define DEFAULT_DMA_COHERENT_POOL_SIZE  SZ_256K
static size_t atomic_pool_size __initdata = DEFAULT_DMA_COHERENT_POOL_SIZE;

static int __init early_coherent_pool(char *p)
{
	atomic_pool_size = memparse(p, &p);
	return 0;
}
early_param("coherent_pool", early_coherent_pool);

static void *__alloc_from_pool(size_t size, struct page **ret_page, gfp_t flags)
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
		memset(ptr, 0, size);
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

static int __dma_update_pte(pte_t *pte, pgtable_t token, unsigned long addr,
			    void *data)
{
	struct page *page = virt_to_page(addr);
	pgprot_t prot = *(pgprot_t *)data;

	set_pte(pte, mk_pte(page, prot));
	return 0;
}

static int __dma_clear_pte(pte_t *pte, pgtable_t token, unsigned long addr,
			    void *data)
{
	pte_clear(&init_mm, addr, pte);
	return 0;
}

static void __dma_remap(struct page *page, size_t size, pgprot_t prot,
			bool no_kernel_map)
{
	unsigned long start = (unsigned long) page_address(page);
	unsigned long end = start + size;
	int (*func)(pte_t *pte, pgtable_t token, unsigned long addr,
			    void *data);

	if (no_kernel_map)
		func = __dma_clear_pte;
	else
		func = __dma_update_pte;

	apply_to_page_range(&init_mm, start, size, func, &prot);
	/* ensure prot is applied before returning */
	mb();
	flush_tlb_kernel_range(start, end);
}


static void *__dma_alloc(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t flags,
			 unsigned long attrs)
{
	struct page *page;
	void *ptr, *coherent_ptr;
	bool coherent = is_dma_coherent(dev, attrs);
	pgprot_t prot = __get_dma_pgprot(attrs, PAGE_KERNEL, false);

	size = PAGE_ALIGN(size);

	if (!coherent && !gfpflags_allow_blocking(flags)) {
		struct page *page = NULL;
		void *addr = __alloc_from_pool(size, &page, flags);

		if (addr)
			*dma_handle = phys_to_dma(dev, page_to_phys(page));

		return addr;
	}

	ptr = swiotlb_alloc(dev, size, dma_handle, flags, attrs);

	if (!ptr)
		goto no_mem;

	/* no need for non-cacheable mapping if coherent */
	if (coherent)
		return ptr;

	__dma_flush_area(ptr, size);

	if (attrs & DMA_ATTR_NO_KERNEL_MAPPING) {
		coherent_ptr = (void *)NO_KERNEL_MAPPING_DUMMY;
		__dma_remap(virt_to_page(ptr), size, __pgprot(0), true);
	} else {
		if ((attrs & DMA_ATTR_STRONGLY_ORDERED))
			__dma_remap(virt_to_page(ptr), size, __pgprot(0), true);

		/* create a coherent mapping */
		page = virt_to_page(ptr);
		coherent_ptr = dma_common_contiguous_remap(
					page, size, VM_USERMAP, prot,
					__builtin_return_address(0));
		if (!coherent_ptr)
			goto no_map;
	}
	return coherent_ptr;

no_map:
	if ((attrs & DMA_ATTR_NO_KERNEL_MAPPING) ||
	    (attrs & DMA_ATTR_STRONGLY_ORDERED))
		__dma_remap(phys_to_page(dma_to_phys(dev, *dma_handle)),
				size, PAGE_KERNEL, false);

	swiotlb_free(dev, size, ptr, *dma_handle, attrs);
no_mem:
	return NULL;
}

static void __dma_free(struct device *dev, size_t size,
		       void *vaddr, dma_addr_t dma_handle,
		       unsigned long attrs)
{
	void *swiotlb_addr = phys_to_virt(dma_to_phys(dev, dma_handle));

	size = PAGE_ALIGN(size);

	if (!is_dma_coherent(dev, attrs)) {
		if (__free_from_pool(vaddr, size))
			return;
		if (!(attrs & DMA_ATTR_NO_KERNEL_MAPPING))
			vunmap(vaddr);
	}
	if ((attrs & DMA_ATTR_NO_KERNEL_MAPPING) ||
	    (attrs & DMA_ATTR_STRONGLY_ORDERED))
		__dma_remap(phys_to_page(dma_to_phys(dev, dma_handle)),
				size, PAGE_KERNEL, false);

	swiotlb_free(dev, size, swiotlb_addr, dma_handle, attrs);
}

static dma_addr_t __swiotlb_map_page(struct device *dev, struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction dir,
				     unsigned long attrs)
{
	dma_addr_t dev_addr;

	dev_addr = swiotlb_map_page(dev, page, offset, size, dir, attrs);
	if (!is_dma_coherent(dev, attrs) &&
	    (attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		__dma_map_area(phys_to_virt(dma_to_phys(dev, dev_addr)), size, dir);

	return dev_addr;
}


static void __swiotlb_unmap_page(struct device *dev, dma_addr_t dev_addr,
				 size_t size, enum dma_data_direction dir,
				 unsigned long attrs)
{
	if (!is_dma_coherent(dev, attrs) &&
	    (attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		__dma_unmap_area(phys_to_virt(dma_to_phys(dev, dev_addr)), size, dir);
	swiotlb_unmap_page(dev, dev_addr, size, dir, attrs);
}

static int __swiotlb_map_sg_attrs(struct device *dev, struct scatterlist *sgl,
				  int nelems, enum dma_data_direction dir,
				  unsigned long attrs)
{
	struct scatterlist *sg;
	int i, ret;

	ret = swiotlb_map_sg_attrs(dev, sgl, nelems, dir, attrs);
	if (!is_dma_coherent(dev, attrs) &&
	    (attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		for_each_sg(sgl, sg, ret, i)
			__dma_map_area(phys_to_virt(dma_to_phys(dev, sg->dma_address)),
				       sg->length, dir);

	return ret;
}

static void __swiotlb_unmap_sg_attrs(struct device *dev,
				     struct scatterlist *sgl, int nelems,
				     enum dma_data_direction dir,
				     unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	if (!is_dma_coherent(dev, attrs) &&
	    (attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		for_each_sg(sgl, sg, nelems, i)
			__dma_unmap_area(phys_to_virt(dma_to_phys(dev, sg->dma_address)),
					 sg->length, dir);
	swiotlb_unmap_sg_attrs(dev, sgl, nelems, dir, attrs);
}

static void __swiotlb_sync_single_for_cpu(struct device *dev,
					  dma_addr_t dev_addr, size_t size,
					  enum dma_data_direction dir)
{
	if (!is_device_dma_coherent(dev))
		__dma_unmap_area(phys_to_virt(dma_to_phys(dev, dev_addr)), size, dir);
	swiotlb_sync_single_for_cpu(dev, dev_addr, size, dir);
}

static void __swiotlb_sync_single_for_device(struct device *dev,
					     dma_addr_t dev_addr, size_t size,
					     enum dma_data_direction dir)
{
	swiotlb_sync_single_for_device(dev, dev_addr, size, dir);
	if (!is_device_dma_coherent(dev))
		__dma_map_area(phys_to_virt(dma_to_phys(dev, dev_addr)), size, dir);
}

static void __swiotlb_sync_sg_for_cpu(struct device *dev,
				      struct scatterlist *sgl, int nelems,
				      enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	if (!is_device_dma_coherent(dev))
		for_each_sg(sgl, sg, nelems, i)
			__dma_unmap_area(phys_to_virt(dma_to_phys(dev, sg->dma_address)),
					 sg->length, dir);
	swiotlb_sync_sg_for_cpu(dev, sgl, nelems, dir);
}

static void __swiotlb_sync_sg_for_device(struct device *dev,
					 struct scatterlist *sgl, int nelems,
					 enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	swiotlb_sync_sg_for_device(dev, sgl, nelems, dir);
	if (!is_device_dma_coherent(dev))
		for_each_sg(sgl, sg, nelems, i)
			__dma_map_area(phys_to_virt(dma_to_phys(dev, sg->dma_address)),
				       sg->length, dir);
}

static int __swiotlb_mmap_pfn(struct vm_area_struct *vma,
			      unsigned long pfn, size_t size)
{
	int ret = -ENXIO;
	unsigned long nr_vma_pages = vma_pages(vma);
	unsigned long nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long off = vma->vm_pgoff;

	if (off < nr_pages && nr_vma_pages <= (nr_pages - off)) {
		ret = remap_pfn_range(vma, vma->vm_start,
				      pfn + off,
				      vma->vm_end - vma->vm_start,
				      vma->vm_page_prot);
	}

	return ret;
}

static int __swiotlb_mmap(struct device *dev,
			  struct vm_area_struct *vma,
			  void *cpu_addr, dma_addr_t dma_addr, size_t size,
			  unsigned long attrs)
{
	int ret = -ENXIO;
	unsigned long pfn = dma_to_phys(dev, dma_addr) >> PAGE_SHIFT;

	vma->vm_page_prot = __get_dma_pgprot(attrs, vma->vm_page_prot,
			is_dma_coherent(dev, attrs));

	if (dma_mmap_from_dev_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	return __swiotlb_mmap_pfn(vma, pfn, size);
}

static int __swiotlb_get_sgtable_page(struct sg_table *sgt,
				      struct page *page, size_t size)
{
	int ret = sg_alloc_table(sgt, 1, GFP_KERNEL);

	if (!ret)
		sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);

	return ret;
}

static int __swiotlb_get_sgtable(struct device *dev, struct sg_table *sgt,
				 void *cpu_addr, dma_addr_t handle, size_t size,
				 unsigned long attrs)
{
	struct page *page = phys_to_page(dma_to_phys(dev, handle));

	return __swiotlb_get_sgtable_page(sgt, page, size);
}

static int __swiotlb_dma_supported(struct device *hwdev, u64 mask)
{
	if (swiotlb)
		return swiotlb_dma_supported(hwdev, mask);
	return 1;
}

static void *arm64_dma_remap(struct device *dev, void *cpu_addr,
			dma_addr_t handle, size_t size,
			unsigned long attrs)
{
	struct page *page = phys_to_page(dma_to_phys(dev, handle));
	bool coherent = is_device_dma_coherent(dev);
	pgprot_t prot = __get_dma_pgprot(attrs, PAGE_KERNEL, coherent);
	unsigned long offset = handle & ~PAGE_MASK;
	struct vm_struct *area;
	unsigned long addr;

	size = PAGE_ALIGN(size + offset);

	/*
	 * DMA allocation can be mapped to user space, so lets
	 * set VM_USERMAP flags too.
	 */
	area = get_vm_area(size, VM_USERMAP);
	if (!area)
		return NULL;

	addr = (unsigned long)area->addr;
	area->phys_addr = __pfn_to_phys(page_to_pfn(page));

	if (ioremap_page_range(addr, addr + size, area->phys_addr, prot)) {
		vunmap((void *)addr);
		return NULL;
	}
	return (void *)addr + offset;
}

static void arm64_dma_unremap(struct device *dev, void *remapped_addr,
				size_t size)
{
	struct vm_struct *area;

	size = PAGE_ALIGN(size);
	remapped_addr = (void *)((unsigned long)remapped_addr & PAGE_MASK);

	area = find_vm_area(remapped_addr);
	if (!area) {
		WARN(1, "trying to free invalid coherent area: %pK\n",
			remapped_addr);
		return;
	}
	vunmap(remapped_addr);
	flush_tlb_kernel_range((unsigned long)remapped_addr,
			(unsigned long)(remapped_addr + size));
}

static int __swiotlb_dma_mapping_error(struct device *hwdev, dma_addr_t addr)
{
	if (swiotlb)
		return swiotlb_dma_mapping_error(hwdev, addr);
	return 0;
}

static const struct dma_map_ops arm64_swiotlb_dma_ops = {
	.alloc = __dma_alloc,
	.free = __dma_free,
	.mmap = __swiotlb_mmap,
	.get_sgtable = __swiotlb_get_sgtable,
	.map_page = __swiotlb_map_page,
	.unmap_page = __swiotlb_unmap_page,
	.map_sg = __swiotlb_map_sg_attrs,
	.unmap_sg = __swiotlb_unmap_sg_attrs,
	.sync_single_for_cpu = __swiotlb_sync_single_for_cpu,
	.sync_single_for_device = __swiotlb_sync_single_for_device,
	.sync_sg_for_cpu = __swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = __swiotlb_sync_sg_for_device,
	.dma_supported = __swiotlb_dma_supported,
	.mapping_error = __swiotlb_dma_mapping_error,
	.remap = arm64_dma_remap,
	.unremap = arm64_dma_unremap,
};

static int __init atomic_pool_init(void)
{
	pgprot_t prot = __pgprot(PROT_NORMAL_NC);
	unsigned long nr_pages = atomic_pool_size >> PAGE_SHIFT;
	struct page *page;
	void *addr;
	unsigned int pool_size_order = get_order(atomic_pool_size);

	if (dev_get_cma_area(NULL))
		page = dma_alloc_from_contiguous(NULL, nr_pages,
						 pool_size_order, false);
	else
		page = alloc_pages(GFP_DMA32, pool_size_order);

	if (page) {
		int ret;
		void *page_addr = page_address(page);

		memset(page_addr, 0, atomic_pool_size);
		__dma_flush_area(page_addr, atomic_pool_size);

		atomic_pool = gen_pool_create(PAGE_SHIFT, -1);
		if (!atomic_pool)
			goto free_page;

		addr = dma_common_contiguous_remap(page, atomic_pool_size,
					VM_USERMAP, prot, atomic_pool_init);

		if (!addr)
			goto destroy_genpool;

		ret = gen_pool_add_virt(atomic_pool, (unsigned long)addr,
					page_to_phys(page),
					atomic_pool_size, -1);
		if (ret)
			goto remove_mapping;

		gen_pool_set_algo(atomic_pool,
				  gen_pool_first_fit_order_align,
				  NULL);

		pr_info("DMA: preallocated %zu KiB pool for atomic allocations\n",
			atomic_pool_size / 1024);
		return 0;
	}
	goto out;

remove_mapping:
	dma_common_free_remap(addr, atomic_pool_size, VM_USERMAP, false);
destroy_genpool:
	gen_pool_destroy(atomic_pool);
	atomic_pool = NULL;
free_page:
	if (!dma_release_from_contiguous(NULL, page, nr_pages))
		__free_pages(page, pool_size_order);
out:
	pr_err("DMA: failed to allocate %zu KiB pool for atomic coherent allocation\n",
		atomic_pool_size / 1024);
	return -ENOMEM;
}

/********************************************
 * The following APIs are for dummy DMA ops *
 ********************************************/

static void *__dummy_alloc(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t flags,
			   unsigned long attrs)
{
	WARN(1, "dma alloc failure, device may be missing a call to arch_setup_dma_ops");
	return NULL;
}

static void __dummy_free(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle,
			 unsigned long attrs)
{
}

static int __dummy_mmap(struct device *dev,
			struct vm_area_struct *vma,
			void *cpu_addr, dma_addr_t dma_addr, size_t size,
			unsigned long attrs)
{
	return -ENXIO;
}

static dma_addr_t __dummy_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction dir,
				   unsigned long attrs)
{
	return 0;
}

static void __dummy_unmap_page(struct device *dev, dma_addr_t dev_addr,
			       size_t size, enum dma_data_direction dir,
			       unsigned long attrs)
{
}

static int __dummy_map_sg(struct device *dev, struct scatterlist *sgl,
			  int nelems, enum dma_data_direction dir,
			  unsigned long attrs)
{
	return 0;
}

static void __dummy_unmap_sg(struct device *dev,
			     struct scatterlist *sgl, int nelems,
			     enum dma_data_direction dir,
			     unsigned long attrs)
{
}

static void __dummy_sync_single(struct device *dev,
				dma_addr_t dev_addr, size_t size,
				enum dma_data_direction dir)
{
}

static void __dummy_sync_sg(struct device *dev,
			    struct scatterlist *sgl, int nelems,
			    enum dma_data_direction dir)
{
}

static int __dummy_mapping_error(struct device *hwdev, dma_addr_t dma_addr)
{
	return 1;
}

static int __dummy_dma_supported(struct device *hwdev, u64 mask)
{
	return 0;
}

const struct dma_map_ops dummy_dma_ops = {
	.alloc                  = __dummy_alloc,
	.free                   = __dummy_free,
	.mmap                   = __dummy_mmap,
	.map_page               = __dummy_map_page,
	.unmap_page             = __dummy_unmap_page,
	.map_sg                 = __dummy_map_sg,
	.unmap_sg               = __dummy_unmap_sg,
	.sync_single_for_cpu    = __dummy_sync_single,
	.sync_single_for_device = __dummy_sync_single,
	.sync_sg_for_cpu        = __dummy_sync_sg,
	.sync_sg_for_device     = __dummy_sync_sg,
	.mapping_error          = __dummy_mapping_error,
	.dma_supported          = __dummy_dma_supported,
};
EXPORT_SYMBOL(dummy_dma_ops);

static int __init arm64_dma_init(void)
{
	if (swiotlb_force == SWIOTLB_FORCE ||
	    max_pfn > (arm64_dma_phys_limit >> PAGE_SHIFT))
		swiotlb = 1;

	WARN_TAINT(ARCH_DMA_MINALIGN < cache_line_size(),
		   TAINT_CPU_OUT_OF_SPEC,
		   "ARCH_DMA_MINALIGN smaller than CTR_EL0.CWG (%d < %d)",
		   ARCH_DMA_MINALIGN, cache_line_size());

	return atomic_pool_init();
}
arch_initcall(arm64_dma_init);

#ifdef CONFIG_IOMMU_DMA
#include <linux/dma-iommu.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>

/* Thankfully, all cache ops are by VA so we can ignore phys here */
static void flush_page(struct device *dev, const void *virt, phys_addr_t phys)
{
	__dma_flush_area(virt, PAGE_SIZE);
}

static void *__iommu_alloc_attrs(struct device *dev, size_t size,
				 dma_addr_t *handle, gfp_t gfp,
				 unsigned long attrs)
{
	bool coherent = is_dma_coherent(dev, attrs);
	int ioprot = dma_info_to_prot(DMA_BIDIRECTIONAL, coherent, attrs);
	size_t iosize = size;
	void *addr;

	if (WARN(!dev, "cannot create IOMMU mapping for unknown device\n"))
		return NULL;

	size = PAGE_ALIGN(size);

	/*
	 * Some drivers rely on this, and we probably don't want the
	 * possibility of stale kernel data being read by devices anyway.
	 */
	if (!(attrs & DMA_ATTR_SKIP_ZEROING))
		gfp |= __GFP_ZERO;

	if (!gfpflags_allow_blocking(gfp)) {
		struct page *page;
		/*
		 * In atomic context we can't remap anything, so we'll only
		 * get the virtually contiguous buffer we need by way of a
		 * physically contiguous allocation.
		 */
		if (coherent) {
			page = alloc_pages(gfp, get_order(size));
			addr = page ? page_address(page) : NULL;
		} else {
			addr = __alloc_from_pool(size, &page, gfp);
		}
		if (!addr)
			return NULL;

		*handle = iommu_dma_map_page(dev, page, 0, iosize, ioprot);
		if (iommu_dma_mapping_error(dev, *handle)) {
			if (coherent)
				__free_pages(page, get_order(size));
			else
				__free_from_pool(addr, size);
			addr = NULL;
		}
	} else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS) {
		pgprot_t prot = __get_dma_pgprot(attrs, PAGE_KERNEL, coherent);
		struct page *page;

		page = dma_alloc_from_contiguous(dev, size >> PAGE_SHIFT,
					get_order(size), gfp & __GFP_NOWARN);
		if (!page)
			return NULL;

		*handle = iommu_dma_map_page(dev, page, 0, iosize, ioprot);
		if (iommu_dma_mapping_error(dev, *handle)) {
			dma_release_from_contiguous(dev, page,
						    size >> PAGE_SHIFT);
			return NULL;
		}
		addr = dma_common_contiguous_remap(page, size, VM_USERMAP,
						   prot,
						   __builtin_return_address(0));
		if (addr) {
			if (!coherent)
				__dma_flush_area(page_to_virt(page), iosize);
			memset(addr, 0, size);
		} else {
			iommu_dma_unmap_page(dev, *handle, iosize, 0, attrs);
			dma_release_from_contiguous(dev, page,
						    size >> PAGE_SHIFT);
		}
	} else {
		pgprot_t prot = __get_dma_pgprot(attrs, PAGE_KERNEL, coherent);
		struct page **pages;

		pages = iommu_dma_alloc(dev, iosize, gfp, attrs, ioprot,
					handle, flush_page);
		if (!pages)
			return NULL;

		addr = dma_common_pages_remap(pages, size, VM_USERMAP, prot,
					      __builtin_return_address(0));
		if (!addr)
			iommu_dma_free(dev, pages, iosize, handle);
	}
	return addr;
}

static void __iommu_free_attrs(struct device *dev, size_t size, void *cpu_addr,
			       dma_addr_t handle, unsigned long attrs)
{
	size_t iosize = size;

	size = PAGE_ALIGN(size);
	/*
	 * @cpu_addr will be one of 4 things depending on how it was allocated:
	 * - A remapped array of pages for contiguous allocations.
	 * - A remapped array of pages from iommu_dma_alloc(), for all
	 *   non-atomic allocations.
	 * - A non-cacheable alias from the atomic pool, for atomic
	 *   allocations by non-coherent devices.
	 * - A normal lowmem address, for atomic allocations by
	 *   coherent devices.
	 * Hence how dodgy the below logic looks...
	 */
	if (__in_atomic_pool(cpu_addr, size)) {
		iommu_dma_unmap_page(dev, handle, iosize, 0, 0);
		__free_from_pool(cpu_addr, size);
	} else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS) {
		struct page *page = vmalloc_to_page(cpu_addr);
		iommu_dma_unmap_page(dev, handle, iosize, 0, attrs);
		dma_release_from_contiguous(dev, page, size >> PAGE_SHIFT);
		dma_common_free_remap(cpu_addr, size, VM_USERMAP, false);
	} else if (is_vmalloc_addr(cpu_addr)) {
		struct vm_struct *area = find_vm_area(cpu_addr);

		if (WARN_ON(!area || !area->pages))
			return;
		iommu_dma_free(dev, area->pages, iosize, &handle);
		dma_common_free_remap(cpu_addr, size, VM_USERMAP, false);
	} else {
		iommu_dma_unmap_page(dev, handle, iosize, 0, 0);
		__free_pages(virt_to_page(cpu_addr), get_order(size));
	}
}

static int __iommu_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
			      void *cpu_addr, dma_addr_t dma_addr, size_t size,
			      unsigned long attrs)
{
	struct vm_struct *area;
	int ret;
	unsigned long pfn = 0;

	vma->vm_page_prot = __get_dma_pgprot(attrs, vma->vm_page_prot,
					     is_dma_coherent(dev, attrs));

	if (dma_mmap_from_dev_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	area = find_vm_area(cpu_addr);

	if (area && area->pages)
		return iommu_dma_mmap(area->pages, size, vma);
	else if (!is_vmalloc_addr(cpu_addr))
		pfn = page_to_pfn(virt_to_page(cpu_addr));
	else if (is_vmalloc_addr(cpu_addr))
		/*
		 * DMA_ATTR_FORCE_CONTIGUOUS and atomic pool allocations are
		 * always remapped, hence in the vmalloc space.
		 */
		pfn = vmalloc_to_pfn(cpu_addr);

	if (pfn)
		return __swiotlb_mmap_pfn(vma, pfn, size);

	return -ENXIO;
}

static int __iommu_get_sgtable(struct device *dev, struct sg_table *sgt,
			       void *cpu_addr, dma_addr_t dma_addr,
			       size_t size, unsigned long attrs)
{
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	struct page *page = NULL;
	struct vm_struct *area = find_vm_area(cpu_addr);

	if (area && area->pages)
		return sg_alloc_table_from_pages(sgt, area->pages, count, 0,
					size, GFP_KERNEL);
	else if (!is_vmalloc_addr(cpu_addr))
		page = virt_to_page(cpu_addr);
	else if (is_vmalloc_addr(cpu_addr))
		/*
		 * DMA_ATTR_FORCE_CONTIGUOUS and atomic pool allocations
		 * are always remapped, hence in the vmalloc space.
		 */
		page = vmalloc_to_page(cpu_addr);

	if (page)
		return __swiotlb_get_sgtable_page(sgt, page, size);
	return -ENXIO;
}

static void __iommu_sync_single_for_cpu(struct device *dev,
					dma_addr_t dev_addr, size_t size,
					enum dma_data_direction dir)
{
	phys_addr_t phys;
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);

	if (!domain || iommu_is_iova_coherent(domain, dev_addr))
		return;

	phys = iommu_iova_to_phys(domain, dev_addr);
	__dma_unmap_area(phys_to_virt(phys), size, dir);
}

static void __iommu_sync_single_for_device(struct device *dev,
					   dma_addr_t dev_addr, size_t size,
					   enum dma_data_direction dir)
{
	phys_addr_t phys;
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);

	if (!domain || iommu_is_iova_coherent(domain, dev_addr))
		return;

	phys = iommu_iova_to_phys(domain, dev_addr);
	__dma_map_area(phys_to_virt(phys), size, dir);
}

static dma_addr_t __iommu_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction dir,
				   unsigned long attrs)
{
	bool coherent = is_dma_coherent(dev, attrs);
	int prot = dma_info_to_prot(dir, coherent, attrs);
	dma_addr_t dev_addr = iommu_dma_map_page(dev, page, offset, size, prot);

	if (!iommu_dma_mapping_error(dev, dev_addr) &&
	    (attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		__iommu_sync_single_for_device(dev, dev_addr, size, dir);

	return dev_addr;
}

static void __iommu_unmap_page(struct device *dev, dma_addr_t dev_addr,
			       size_t size, enum dma_data_direction dir,
			       unsigned long attrs)
{
	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		__iommu_sync_single_for_cpu(dev, dev_addr, size, dir);

	iommu_dma_unmap_page(dev, dev_addr, size, dir, attrs);
}

static void __iommu_sync_sg_for_cpu(struct device *dev,
				    struct scatterlist *sgl, int nelems,
				    enum dma_data_direction dir)
{
	struct scatterlist *sg;
	dma_addr_t iova = sg_dma_address(sgl);
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	int i;

	if (!domain || iommu_is_iova_coherent(domain, iova))
		return;

	for_each_sg(sgl, sg, nelems, i)
		__dma_unmap_area(sg_virt(sg), sg->length, dir);
}

static void __iommu_sync_sg_for_device(struct device *dev,
				       struct scatterlist *sgl, int nelems,
				       enum dma_data_direction dir)
{
	struct scatterlist *sg;
	dma_addr_t iova = sg_dma_address(sgl);
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	int i;

	if (!domain || iommu_is_iova_coherent(domain, iova))
		return;

	for_each_sg(sgl, sg, nelems, i)
		__dma_map_area(sg_virt(sg), sg->length, dir);
}

static int __iommu_map_sg_attrs(struct device *dev, struct scatterlist *sgl,
				int nelems, enum dma_data_direction dir,
				unsigned long attrs)
{
	bool coherent = is_dma_coherent(dev, attrs);
	int ret;

	ret =  iommu_dma_map_sg(dev, sgl, nelems,
				dma_info_to_prot(dir, coherent, attrs));
	if (!ret)
		return ret;

	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		__iommu_sync_sg_for_device(dev, sgl, nelems, dir);

	return ret;
}

static void __iommu_unmap_sg_attrs(struct device *dev,
				   struct scatterlist *sgl, int nelems,
				   enum dma_data_direction dir,
				   unsigned long attrs)
{
	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		__iommu_sync_sg_for_cpu(dev, sgl, nelems, dir);

	iommu_dma_unmap_sg(dev, sgl, nelems, dir, attrs);
}

static const struct dma_map_ops iommu_dma_ops = {
	.alloc = __iommu_alloc_attrs,
	.free = __iommu_free_attrs,
	.mmap = __iommu_mmap_attrs,
	.get_sgtable = __iommu_get_sgtable,
	.map_page = __iommu_map_page,
	.unmap_page = __iommu_unmap_page,
	.map_sg = __iommu_map_sg_attrs,
	.unmap_sg = __iommu_unmap_sg_attrs,
	.sync_single_for_cpu = __iommu_sync_single_for_cpu,
	.sync_single_for_device = __iommu_sync_single_for_device,
	.sync_sg_for_cpu = __iommu_sync_sg_for_cpu,
	.sync_sg_for_device = __iommu_sync_sg_for_device,
	.map_resource = iommu_dma_map_resource,
	.unmap_resource = iommu_dma_unmap_resource,
	.mapping_error = iommu_dma_mapping_error,
};

static int __init __iommu_dma_init(void)
{
	return iommu_dma_init();
}
arch_initcall(__iommu_dma_init);

void arch_teardown_dma_ops(struct device *dev)
{
	dev->dma_ops = NULL;
}
#endif  /* CONFIG_IOMMU_DMA */

static void arm_iommu_setup_dma_ops(struct device *dev, u64 dma_base, u64 size);

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			const struct iommu_ops *iommu, bool coherent)
{
	if (!dev->dma_ops) {
		if (dev->removed_mem)
			set_dma_ops(dev, &removed_dma_ops);
		else
			dev->dma_ops = &arm64_swiotlb_dma_ops;
	}

	dev->archdata.dma_coherent = coherent;
	arm_iommu_setup_dma_ops(dev, dma_base, size);

#ifdef CONFIG_XEN
	if (xen_initial_domain()) {
		dev->archdata.dev_dma_ops = dev->dma_ops;
		dev->dma_ops = xen_dma_ops;
	}
#endif
}
EXPORT_SYMBOL_GPL(arch_setup_dma_ops);

#ifdef CONFIG_ARM64_DMA_USE_IOMMU

/* guards initialization of default_domain->iova_cookie */
static DEFINE_MUTEX(iommu_dma_init_mutex);

static int
iommu_init_mapping(struct device *dev, struct dma_iommu_mapping *mapping)
{
	struct iommu_domain *domain = mapping->domain;
	dma_addr_t dma_base = mapping->base;
	u64 size = mapping->bits << PAGE_SHIFT;
	int ret;
	bool own_cookie;

	/*
	 * if own_cookie is false, then we are sharing the iova_cookie with
	 * another driver, and should not free it on error. Cleanup will be
	 * done when the iommu_domain is freed.
	 */
	own_cookie = !domain->iova_cookie;

	if (own_cookie) {
		ret = iommu_get_dma_cookie(domain);
		if (ret) {
			dev_err(dev, "iommu_get_dma_cookie failed: %d\n", ret);
			return ret;
		}
	}

	ret = iommu_dma_init_domain(domain, dma_base, size, dev);
	if (ret) {
		dev_err(dev, "iommu_dma_init_domain failed: %d\n", ret);
		if (own_cookie)
			iommu_put_dma_cookie(domain);
		return ret;
	}

	mapping->ops = &iommu_dma_ops;
	return 0;
}

static int arm_iommu_get_dma_cookie(struct device *dev,
				    struct dma_iommu_mapping *mapping)
{
	int s1_bypass = 0, is_fast = 0;
	int err = 0;

	mutex_lock(&iommu_dma_init_mutex);

	iommu_domain_get_attr(mapping->domain, DOMAIN_ATTR_S1_BYPASS,
					&s1_bypass);
	iommu_domain_get_attr(mapping->domain, DOMAIN_ATTR_FAST, &is_fast);

	if (s1_bypass)
		mapping->ops = &arm64_swiotlb_dma_ops;
	else if (is_fast)
		err = fast_smmu_init_mapping(dev, mapping);
	else
		err = iommu_init_mapping(dev, mapping);

	mutex_unlock(&iommu_dma_init_mutex);
	return err;
}

/*
 * Checks for "qcom,iommu-dma-addr-pool" property.
 * If not present, leaves dma_addr and dma_size unmodified.
 */
static void arm_iommu_get_dma_window(struct device *dev, u64 *dma_addr,
					u64 *dma_size)
{
	struct device_node *np;
	int naddr, nsize, len;
	const __be32 *ranges;

	if (!dev->of_node)
		return;

	np = of_parse_phandle(dev->of_node, "qcom,iommu-group", 0);
	if (!np)
		np = dev->of_node;

	ranges = of_get_property(np, "qcom,iommu-dma-addr-pool", &len);
	if (!ranges)
		return;

	len /= sizeof(u32);
	naddr = of_n_addr_cells(np);
	nsize = of_n_size_cells(np);
	if (len < naddr + nsize) {
		dev_err(dev, "Invalid length for qcom,iommu-dma-addr-pool, expected %d cells\n",
			naddr + nsize);
		return;
	}
	if (naddr == 0 || nsize == 0) {
		dev_err(dev, "Invalid #address-cells %d or #size-cells %d\n",
			naddr, nsize);
		return;
	}

	*dma_addr = of_read_number(ranges, naddr);
	*dma_size = of_read_number(ranges + naddr, nsize);
}

static void arm_iommu_setup_dma_ops(struct device *dev, u64 dma_base, u64 size)
{
	struct iommu_domain *domain;
	struct iommu_group *group;
	struct dma_iommu_mapping mapping = {0};

	group = dev->iommu_group;
	if (!group)
		return;

	arm_iommu_get_dma_window(dev, &dma_base, &size);

	domain = iommu_get_domain_for_dev(dev);
	if (!domain)
		return;

	/* Allow iommu-debug to call arch_setup_dma_ops to reconfigure itself */
	if (domain->type != IOMMU_DOMAIN_DMA &&
	    !of_device_is_compatible(dev->of_node, "iommu-debug-test")) {
		dev_err(dev, "Invalid iommu domain type!\n");
		return;
	}

	mapping.base = dma_base;
	mapping.bits = size >> PAGE_SHIFT;
	mapping.domain = domain;

	if (arm_iommu_get_dma_cookie(dev, &mapping)) {
		dev_err(dev, "Failed to get dma cookie\n");
		return;
	}

	set_dma_ops(dev, mapping.ops);
}

#else /*!CONFIG_ARM64_DMA_USE_IOMMU */

static void arm_iommu_setup_dma_ops(struct device *dev, u64 dma_base, u64 size)
{
}
#endif
