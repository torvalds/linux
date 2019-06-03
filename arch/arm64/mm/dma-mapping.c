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
#include <linux/memblock.h>
#include <linux/cache.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/genalloc.h>
#include <linux/dma-direct.h>
#include <linux/dma-noncoherent.h>
#include <linux/dma-contiguous.h>
#include <linux/vmalloc.h>
#include <linux/swiotlb.h>
#include <linux/pci.h>

#include <asm/cacheflush.h>

pgprot_t arch_dma_mmap_pgprot(struct device *dev, pgprot_t prot,
		unsigned long attrs)
{
	if (!dev_is_dma_coherent(dev) || (attrs & DMA_ATTR_WRITE_COMBINE))
		return pgprot_writecombine(prot);
	return prot;
}

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	__dma_map_area(phys_to_virt(paddr), size, dir);
}

void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	__dma_unmap_area(phys_to_virt(paddr), size, dir);
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	__dma_flush_area(page_address(page), size);
}

#ifdef CONFIG_IOMMU_DMA
static int __swiotlb_get_sgtable_page(struct sg_table *sgt,
				      struct page *page, size_t size)
{
	int ret = sg_alloc_table(sgt, 1, GFP_KERNEL);

	if (!ret)
		sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);

	return ret;
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
#endif /* CONFIG_IOMMU_DMA */

static int __init arm64_dma_init(void)
{
	WARN_TAINT(ARCH_DMA_MINALIGN < cache_line_size(),
		   TAINT_CPU_OUT_OF_SPEC,
		   "ARCH_DMA_MINALIGN smaller than CTR_EL0.CWG (%d < %d)",
		   ARCH_DMA_MINALIGN, cache_line_size());
	return dma_atomic_pool_init(GFP_DMA32, __pgprot(PROT_NORMAL_NC));
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
	bool coherent = dev_is_dma_coherent(dev);
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
			addr = dma_alloc_from_pool(size, &page, gfp);
		}
		if (!addr)
			return NULL;

		*handle = iommu_dma_map_page(dev, page, 0, iosize, ioprot);
		if (*handle == DMA_MAPPING_ERROR) {
			if (coherent)
				__free_pages(page, get_order(size));
			else
				dma_free_from_pool(addr, size);
			addr = NULL;
		}
	} else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS) {
		pgprot_t prot = arch_dma_mmap_pgprot(dev, PAGE_KERNEL, attrs);
		struct page *page;

		page = dma_alloc_from_contiguous(dev, size >> PAGE_SHIFT,
					get_order(size), gfp & __GFP_NOWARN);
		if (!page)
			return NULL;

		*handle = iommu_dma_map_page(dev, page, 0, iosize, ioprot);
		if (*handle == DMA_MAPPING_ERROR) {
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
		pgprot_t prot = arch_dma_mmap_pgprot(dev, PAGE_KERNEL, attrs);
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
	if (dma_in_atomic_pool(cpu_addr, size)) {
		iommu_dma_unmap_page(dev, handle, iosize, 0, 0);
		dma_free_from_pool(cpu_addr, size);
	} else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS) {
		struct page *page = vmalloc_to_page(cpu_addr);

		iommu_dma_unmap_page(dev, handle, iosize, 0, attrs);
		dma_release_from_contiguous(dev, page, size >> PAGE_SHIFT);
		dma_common_free_remap(cpu_addr, size, VM_USERMAP);
	} else if (is_vmalloc_addr(cpu_addr)){
		struct vm_struct *area = find_vm_area(cpu_addr);

		if (WARN_ON(!area || !area->pages))
			return;
		iommu_dma_free(dev, area->pages, iosize, &handle);
		dma_common_free_remap(cpu_addr, size, VM_USERMAP);
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

	vma->vm_page_prot = arch_dma_mmap_pgprot(dev, vma->vm_page_prot, attrs);

	if (dma_mmap_from_dev_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	if (!is_vmalloc_addr(cpu_addr)) {
		unsigned long pfn = page_to_pfn(virt_to_page(cpu_addr));
		return __swiotlb_mmap_pfn(vma, pfn, size);
	}

	if (attrs & DMA_ATTR_FORCE_CONTIGUOUS) {
		/*
		 * DMA_ATTR_FORCE_CONTIGUOUS allocations are always remapped,
		 * hence in the vmalloc space.
		 */
		unsigned long pfn = vmalloc_to_pfn(cpu_addr);
		return __swiotlb_mmap_pfn(vma, pfn, size);
	}

	area = find_vm_area(cpu_addr);
	if (WARN_ON(!area || !area->pages))
		return -ENXIO;

	return iommu_dma_mmap(area->pages, size, vma);
}

static int __iommu_get_sgtable(struct device *dev, struct sg_table *sgt,
			       void *cpu_addr, dma_addr_t dma_addr,
			       size_t size, unsigned long attrs)
{
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	struct vm_struct *area = find_vm_area(cpu_addr);

	if (!is_vmalloc_addr(cpu_addr)) {
		struct page *page = virt_to_page(cpu_addr);
		return __swiotlb_get_sgtable_page(sgt, page, size);
	}

	if (attrs & DMA_ATTR_FORCE_CONTIGUOUS) {
		/*
		 * DMA_ATTR_FORCE_CONTIGUOUS allocations are always remapped,
		 * hence in the vmalloc space.
		 */
		struct page *page = vmalloc_to_page(cpu_addr);
		return __swiotlb_get_sgtable_page(sgt, page, size);
	}

	if (WARN_ON(!area || !area->pages))
		return -ENXIO;

	return sg_alloc_table_from_pages(sgt, area->pages, count, 0, size,
					 GFP_KERNEL);
}

static void __iommu_sync_single_for_cpu(struct device *dev,
					dma_addr_t dev_addr, size_t size,
					enum dma_data_direction dir)
{
	phys_addr_t phys;

	if (dev_is_dma_coherent(dev))
		return;

	phys = iommu_iova_to_phys(iommu_get_dma_domain(dev), dev_addr);
	arch_sync_dma_for_cpu(dev, phys, size, dir);
}

static void __iommu_sync_single_for_device(struct device *dev,
					   dma_addr_t dev_addr, size_t size,
					   enum dma_data_direction dir)
{
	phys_addr_t phys;

	if (dev_is_dma_coherent(dev))
		return;

	phys = iommu_iova_to_phys(iommu_get_dma_domain(dev), dev_addr);
	arch_sync_dma_for_device(dev, phys, size, dir);
}

static dma_addr_t __iommu_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction dir,
				   unsigned long attrs)
{
	bool coherent = dev_is_dma_coherent(dev);
	int prot = dma_info_to_prot(dir, coherent, attrs);
	dma_addr_t dev_addr = iommu_dma_map_page(dev, page, offset, size, prot);

	if (!coherent && !(attrs & DMA_ATTR_SKIP_CPU_SYNC) &&
	    dev_addr != DMA_MAPPING_ERROR)
		__dma_map_area(page_address(page) + offset, size, dir);

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
	int i;

	if (dev_is_dma_coherent(dev))
		return;

	for_each_sg(sgl, sg, nelems, i)
		arch_sync_dma_for_cpu(dev, sg_phys(sg), sg->length, dir);
}

static void __iommu_sync_sg_for_device(struct device *dev,
				       struct scatterlist *sgl, int nelems,
				       enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	if (dev_is_dma_coherent(dev))
		return;

	for_each_sg(sgl, sg, nelems, i)
		arch_sync_dma_for_device(dev, sg_phys(sg), sg->length, dir);
}

static int __iommu_map_sg_attrs(struct device *dev, struct scatterlist *sgl,
				int nelems, enum dma_data_direction dir,
				unsigned long attrs)
{
	bool coherent = dev_is_dma_coherent(dev);

	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		__iommu_sync_sg_for_device(dev, sgl, nelems, dir);

	return iommu_dma_map_sg(dev, sgl, nelems,
				dma_info_to_prot(dir, coherent, attrs));
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
};

static int __init __iommu_dma_init(void)
{
	return iommu_dma_init();
}
arch_initcall(__iommu_dma_init);

static void __iommu_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
				  const struct iommu_ops *ops)
{
	struct iommu_domain *domain;

	if (!ops)
		return;

	/*
	 * The IOMMU core code allocates the default DMA domain, which the
	 * underlying IOMMU driver needs to support via the dma-iommu layer.
	 */
	domain = iommu_get_domain_for_dev(dev);

	if (!domain)
		goto out_err;

	if (domain->type == IOMMU_DOMAIN_DMA) {
		if (iommu_dma_init_domain(domain, dma_base, size, dev))
			goto out_err;

		dev->dma_ops = &iommu_dma_ops;
	}

	return;

out_err:
	 pr_warn("Failed to set up IOMMU for device %s; retaining platform DMA ops\n",
		 dev_name(dev));
}

void arch_teardown_dma_ops(struct device *dev)
{
	dev->dma_ops = NULL;
}

#else

static void __iommu_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
				  const struct iommu_ops *iommu)
{ }

#endif  /* CONFIG_IOMMU_DMA */

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			const struct iommu_ops *iommu, bool coherent)
{
	dev->dma_coherent = coherent;
	__iommu_setup_dma_ops(dev, dma_base, size, iommu);

#ifdef CONFIG_XEN
	if (xen_initial_domain())
		dev->dma_ops = xen_dma_ops;
#endif
}
