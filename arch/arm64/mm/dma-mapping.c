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
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/genalloc.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/dma-iommu.h>
#include <linux/vmalloc.h>
#include <linux/swiotlb.h>

#include <asm/cacheflush.h>

static struct gen_pool *atomic_pool;

#define DEFAULT_DMA_COHERENT_POOL_SIZE  SZ_256K
static size_t atomic_pool_size __initdata = DEFAULT_DMA_COHERENT_POOL_SIZE;

static int __init early_coherent_pool(char *p)
{
	atomic_pool_size = memparse(p, &p);
	return 0;
}
early_param("coherent_pool", early_coherent_pool);

void *arch_alloc_from_atomic_pool(size_t size, struct page **ret_page, gfp_t flags)
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

static void *__dma_alloc_coherent(struct device *dev, size_t size,
				  dma_addr_t *dma_handle, gfp_t flags,
				  struct dma_attrs *attrs)
{
	if (dev == NULL) {
		WARN_ONCE(1, "Use an actual device structure for DMA allocation\n");
		return NULL;
	}

	if (IS_ENABLED(CONFIG_ZONE_DMA) &&
	    dev->coherent_dma_mask <= DMA_BIT_MASK(32))
		flags |= GFP_DMA;
	if (dev_get_cma_area(dev) && gfpflags_allow_blocking(flags)) {
		struct page *page;
		void *addr;

		page = dma_alloc_from_contiguous(dev, size >> PAGE_SHIFT,
							get_order(size));
		if (!page)
			return NULL;

		*dma_handle = phys_to_dma(dev, page_to_phys(page));
		addr = page_address(page);
		memset(addr, 0, size);
		return addr;
	} else {
		return swiotlb_alloc_coherent(dev, size, dma_handle, flags);
	}
}

static void __dma_free_coherent(struct device *dev, size_t size,
				void *vaddr, dma_addr_t dma_handle,
				struct dma_attrs *attrs)
{
	bool freed;
	phys_addr_t paddr = dma_to_phys(dev, dma_handle);

	if (dev == NULL) {
		WARN_ONCE(1, "Use an actual device structure for DMA allocation\n");
		return;
	}

	freed = dma_release_from_contiguous(dev,
					phys_to_page(paddr),
					size >> PAGE_SHIFT);
	if (!freed)
		swiotlb_free_coherent(dev, size, vaddr, dma_handle);
}

static void *__dma_alloc(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t flags,
			 struct dma_attrs *attrs)
{
	struct page *page;
	void *ptr, *coherent_ptr;
	bool coherent = is_device_dma_coherent(dev);
	pgprot_t prot = arch_get_dma_pgprot(attrs, PAGE_KERNEL, false);

	size = PAGE_ALIGN(size);

	if (!coherent && !gfpflags_allow_blocking(flags)) {
		struct page *page = NULL;
		void *addr = arch_alloc_from_atomic_pool(size, &page, flags);

		if (addr)
			*dma_handle = phys_to_dma(dev, page_to_phys(page));

		return addr;
	}

	ptr = __dma_alloc_coherent(dev, size, dma_handle, flags, attrs);
	if (!ptr)
		goto no_mem;

	/* no need for non-cacheable mapping if coherent */
	if (coherent)
		return ptr;

	/* remove any dirty cache lines on the kernel alias */
	__dma_flush_range(ptr, ptr + size);

	/* create a coherent mapping */
	page = virt_to_page(ptr);
	coherent_ptr = dma_common_contiguous_remap(page, size, VM_USERMAP,
						   prot, __builtin_return_address(0));
	if (!coherent_ptr)
		goto no_map;

	return coherent_ptr;

no_map:
	__dma_free_coherent(dev, size, ptr, *dma_handle, attrs);
no_mem:
	*dma_handle = DMA_ERROR_CODE;
	return NULL;
}

static void __dma_free(struct device *dev, size_t size,
		       void *vaddr, dma_addr_t dma_handle,
		       struct dma_attrs *attrs)
{
	void *swiotlb_addr = phys_to_virt(dma_to_phys(dev, dma_handle));

	size = PAGE_ALIGN(size);

	if (!is_device_dma_coherent(dev)) {
		if (arch_free_from_atomic_pool(vaddr, size))
			return;
		vunmap(vaddr);
	}
	__dma_free_coherent(dev, size, swiotlb_addr, dma_handle, attrs);
}

static dma_addr_t __swiotlb_map_page(struct device *dev, struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction dir,
				     struct dma_attrs *attrs)
{
	dma_addr_t dev_addr;

	dev_addr = swiotlb_map_page(dev, page, offset, size, dir, attrs);
	if (!is_device_dma_coherent(dev))
		__dma_map_area(phys_to_virt(dma_to_phys(dev, dev_addr)), size, dir);

	return dev_addr;
}


static void __swiotlb_unmap_page(struct device *dev, dma_addr_t dev_addr,
				 size_t size, enum dma_data_direction dir,
				 struct dma_attrs *attrs)
{
	if (!is_device_dma_coherent(dev))
		__dma_unmap_area(phys_to_virt(dma_to_phys(dev, dev_addr)), size, dir);
	swiotlb_unmap_page(dev, dev_addr, size, dir, attrs);
}

static int __swiotlb_map_sg_attrs(struct device *dev, struct scatterlist *sgl,
				  int nelems, enum dma_data_direction dir,
				  struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i, ret;

	ret = swiotlb_map_sg_attrs(dev, sgl, nelems, dir, attrs);
	if (!is_device_dma_coherent(dev))
		for_each_sg(sgl, sg, ret, i)
			__dma_map_area(phys_to_virt(dma_to_phys(dev, sg->dma_address)),
				       sg->length, dir);

	return ret;
}

static void __swiotlb_unmap_sg_attrs(struct device *dev,
				     struct scatterlist *sgl, int nelems,
				     enum dma_data_direction dir,
				     struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i;

	if (!is_device_dma_coherent(dev))
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

static int __swiotlb_mmap(struct device *dev,
			  struct vm_area_struct *vma,
			  void *cpu_addr, dma_addr_t dma_addr, size_t size,
			  struct dma_attrs *attrs)
{
	int ret = -ENXIO;
	unsigned long nr_vma_pages = (vma->vm_end - vma->vm_start) >>
					PAGE_SHIFT;
	unsigned long nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long pfn = dma_to_phys(dev, dma_addr) >> PAGE_SHIFT;
	unsigned long off = vma->vm_pgoff;

	vma->vm_page_prot = arch_get_dma_pgprot(attrs, vma->vm_page_prot,
					     is_device_dma_coherent(dev));

	if (dma_mmap_from_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	if (off < nr_pages && nr_vma_pages <= (nr_pages - off)) {
		ret = remap_pfn_range(vma, vma->vm_start,
				      pfn + off,
				      vma->vm_end - vma->vm_start,
				      vma->vm_page_prot);
	}

	return ret;
}

static int __swiotlb_get_sgtable(struct device *dev, struct sg_table *sgt,
				 void *cpu_addr, dma_addr_t handle, size_t size,
				 struct dma_attrs *attrs)
{
	int ret = sg_alloc_table(sgt, 1, GFP_KERNEL);

	if (!ret)
		sg_set_page(sgt->sgl, phys_to_page(dma_to_phys(dev, handle)),
			    PAGE_ALIGN(size), 0);

	return ret;
}

static struct dma_map_ops swiotlb_dma_ops = {
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
	.dma_supported = swiotlb_dma_supported,
	.mapping_error = swiotlb_dma_mapping_error,
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
							pool_size_order);
	else
		page = alloc_pages(GFP_DMA, pool_size_order);

	if (page) {
		int ret;
		void *page_addr = page_address(page);

		memset(page_addr, 0, atomic_pool_size);
		__dma_flush_range(page_addr, page_addr + atomic_pool_size);

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
				  (void *)PAGE_SHIFT);

		pr_info("DMA: preallocated %zu KiB pool for atomic allocations\n",
			atomic_pool_size / 1024);
		return 0;
	}
	goto out;

remove_mapping:
	dma_common_free_remap(addr, atomic_pool_size, VM_USERMAP);
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
			   struct dma_attrs *attrs)
{
	return NULL;
}

static void __dummy_free(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle,
			 struct dma_attrs *attrs)
{
}

static int __dummy_mmap(struct device *dev,
			struct vm_area_struct *vma,
			void *cpu_addr, dma_addr_t dma_addr, size_t size,
			struct dma_attrs *attrs)
{
	return -ENXIO;
}

static dma_addr_t __dummy_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction dir,
				   struct dma_attrs *attrs)
{
	return DMA_ERROR_CODE;
}

static void __dummy_unmap_page(struct device *dev, dma_addr_t dev_addr,
			       size_t size, enum dma_data_direction dir,
			       struct dma_attrs *attrs)
{
}

static int __dummy_map_sg(struct device *dev, struct scatterlist *sgl,
			  int nelems, enum dma_data_direction dir,
			  struct dma_attrs *attrs)
{
	return 0;
}

static void __dummy_unmap_sg(struct device *dev,
			     struct scatterlist *sgl, int nelems,
			     enum dma_data_direction dir,
			     struct dma_attrs *attrs)
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

struct dma_map_ops dummy_dma_ops = {
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
	return atomic_pool_init();
}
arch_initcall(arm64_dma_init);

#define PREALLOC_DMA_DEBUG_ENTRIES	4096

static int __init dma_debug_do_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);
	return 0;
}
fs_initcall(dma_debug_do_init);

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			struct iommu_ops *iommu, bool coherent)
{
	dev->archdata.dma_coherent = coherent;

	if (!common_iommu_setup_dma_ops(dev, dma_base, size, iommu))
		arch_set_dma_ops(dev, &swiotlb_dma_ops);
}

void arch_teardown_dma_ops(struct device *dev)
{
	common_iommu_teardown_dma_ops(dev);
}
