/* linux/drivers/iommu/exynos_iovmm.c
 *
 * Copyright (c) 2011-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_EXYNOS_IOMMU_DEBUG
#define DEBUG
#endif

#include <linux/kernel.h>
#include <linux/hardirq.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/err.h>

#include <plat/iovmm.h>
#include <plat/sysmmu.h>

#include <mach/sysmmu.h>

#include "exynos-iommu.h"

#define IOVM_BITMAP_SIZE (IOVM_SIZE / PAGE_SIZE)

/* alloc_iovm_region - Allocate IO virtual memory region
 * vmm: virtual memory allocator
 * size: total size to allocate vm region from @vmm.
 * align: alignment constraints of the allocated virtual address
 * max_align: maximum alignment of allocated virtual address. allocated address
 *            does not need to satisfy larger alignment than max_align.
 * exact_align_mask: constraints of the special case that allocated address
 *            must satisfy when it is multiple of align but of max_align.
 *            If this is not 0, allocated address must satisfy the following
 *            constraint:
 *            ((allocated address) % max_align) / align = exact_align_mask
 * offset: must be smaller than PAGE_SIZE. Just a valut to be added to the
 *         allocated virtual address. This does not effect to the allocaded size
 *         and address.
 *
 * This function returns allocated IO virtual address that satisfies the given
 * constraints. Returns 0 if this function is not able to allocate IO virtual
 * memory
 */
static dma_addr_t alloc_iovm_region(struct exynos_iovmm *vmm, size_t size,
			size_t align, size_t max_align, size_t exact_align_mask,
			off_t offset)
{
	dma_addr_t index = 0;
	unsigned long end, i;
	struct exynos_vm_region *region;

	BUG_ON(align & (align - 1));
	BUG_ON(offset >= PAGE_SIZE);

	size >>= PAGE_SHIFT;
	align >>= PAGE_SHIFT;
	exact_align_mask >>= PAGE_SHIFT;
	max_align >>= PAGE_SHIFT;

	spin_lock(&vmm->bitmap_lock);
again:
	index = find_next_zero_bit(vmm->vm_map, IOVM_BITMAP_SIZE, index);

	if (align) {
		if (exact_align_mask) {
			if ((index & ~(align - 1) & (max_align - 1)) >
							exact_align_mask)
				index = ALIGN(index, max_align);
			index |= exact_align_mask;
		} else {
			index = ALIGN(index, align);
		}

		if (index >= IOVM_BITMAP_SIZE) {
			spin_unlock(&vmm->bitmap_lock);
			return 0;
		}

		if (test_bit(index, vmm->vm_map))
			goto again;
	}

	end = index + size;

	if (end >= IOVM_BITMAP_SIZE) {
		spin_unlock(&vmm->bitmap_lock);
		return 0;
	}

	i = find_next_bit(vmm->vm_map, end, index);
	if (i < end) {
		index = i + 1;
		goto again;
	}

	bitmap_set(vmm->vm_map, index, size);

	spin_unlock(&vmm->bitmap_lock);

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (unlikely(!region)) {
		spin_lock(&vmm->bitmap_lock);
		bitmap_clear(vmm->vm_map, index, size);
		spin_unlock(&vmm->bitmap_lock);
		return 0;
	}

	INIT_LIST_HEAD(&region->node);
	region->start = (index << PAGE_SHIFT) + IOVA_START + offset;
	region->size = size << PAGE_SHIFT;

	spin_lock(&vmm->vmlist_lock);
	list_add_tail(&region->node, &vmm->regions_list);
	spin_unlock(&vmm->vmlist_lock);

	return region->start;
}

static struct exynos_vm_region *remove_iovm_region(struct exynos_iovmm *vmm,
							dma_addr_t iova)
{
	struct exynos_vm_region *region;

	spin_lock(&vmm->vmlist_lock);

	list_for_each_entry(region, &vmm->regions_list, node) {
		if (region->start == iova) {
			list_del(&region->node);
			spin_unlock(&vmm->vmlist_lock);
			return region;
		}
	}

	spin_unlock(&vmm->vmlist_lock);

	return NULL;
}

static void free_iovm_region(struct exynos_iovmm *vmm,
				struct exynos_vm_region *region)
{
	if (!region)
		return;

	if (region->start < IOVA_START) {
		kfree(region);
		return;
	}

	spin_lock(&vmm->bitmap_lock);
	bitmap_clear(vmm->vm_map, (region->start - IOVA_START) >> PAGE_SHIFT,
						region->size >> PAGE_SHIFT);
	spin_unlock(&vmm->bitmap_lock);

	kfree(region);
}

static dma_addr_t add_iovm_region(struct exynos_iovmm *vmm,
					dma_addr_t start, size_t size)
{
	struct exynos_vm_region *region, *pos;

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return 0;

	INIT_LIST_HEAD(&region->node);
	region->start = start;
	region->size = size;

	spin_lock(&vmm->vmlist_lock);

	list_for_each_entry(pos, &vmm->regions_list, node) {
		if ((start < (pos->start + pos->size)) &&
					((start + size) > pos->start)) {
			spin_unlock(&vmm->vmlist_lock);
			kfree(region);
			return 0;
		}
	}

	list_add(&region->node, &vmm->regions_list);

	spin_unlock(&vmm->vmlist_lock);

	return start;
}

static void show_iovm_regions(struct exynos_iovmm *vmm)
{
	struct exynos_vm_region *pos;

	pr_err("LISTING IOVMM REGIONS...\n");
	spin_lock(&vmm->vmlist_lock);
	list_for_each_entry(pos, &vmm->regions_list, node) {
		pr_err("REGION: %#x ~ %#x (SIZE: %#x)\n", pos->start,
				pos->start + pos->size, pos->size);
	}
	spin_unlock(&vmm->vmlist_lock);
	pr_err("END OF LISTING IOVMM REGIONS...\n");
}

int iovmm_activate(struct device *dev)
{
	struct exynos_iovmm *vmm = exynos_get_iovmm(dev);

	return iommu_attach_device(vmm->domain, dev);
}

void iovmm_deactivate(struct device *dev)
{
	struct exynos_iovmm *vmm = exynos_get_iovmm(dev);

	iommu_detach_device(vmm->domain, dev);
}

/* iovmm_map - allocate and map IO virtual memory for the given device
 * dev: device that has IO virtual address space managed by IOVMM
 * sg: list of physically contiguous memory chunks. The preceding chunk needs to
 *     be larger than the following chunks in sg for efficient mapping and
 *     performance. If elements of sg are more than one, physical address of
 *     each chunk needs to be aligned by its size for efficent mapping and TLB
 *     utilization.
 * offset: offset in bytes to be mapped and accessed by dev.
 * size: size in bytes to be mapped and accessed by dev.
 *
 * This function allocates IO virtual memory for the given device and maps the
 * given physical memory conveyed by sg into the allocated IO memory region.
 * Returns allocated IO virtual address if it allocates and maps successfull.
 * Otherwise, minus error number. Caller must check if the return value of this
 * function with IS_ERR_VALUE().
 */
dma_addr_t iovmm_map(struct device *dev, struct scatterlist *sg, off_t offset,
								size_t size)
{
	off_t start_off;
	dma_addr_t addr, start = 0;
	size_t mapped_size = 0;
	struct exynos_iovmm *vmm = exynos_get_iovmm(dev);
	size_t exact_align_mask = 0;
	size_t max_align, align;
	int ret = 0;
	struct scatterlist *tsg;

	for (; (sg != NULL) && (sg_dma_len(sg) < offset); sg = sg_next(sg))
		offset -= sg_dma_len(sg);

	if (sg == NULL) {
		dev_err(dev, "IOVMM: invalid offset to %s.\n", __func__);
		return -EINVAL;
	}

	tsg = sg;

	start_off = offset_in_page(sg_phys(sg) + offset);
	size = PAGE_ALIGN(size + start_off);

	if (size >= SECT_SIZE)
		max_align = SECT_SIZE;
	else if (size < LPAGE_SIZE)
		max_align = SPAGE_SIZE;
	else
		max_align = LPAGE_SIZE;

	if (sg_next(sg) == NULL) {/* physically contiguous chunk */
		/* 'align' must be biggest 2^n that satisfies:
		 * 'address of physical memory' % 'align' = 0
		 */
		align = 1 << __ffs(page_to_phys(sg_page(sg)));

		exact_align_mask = page_to_phys(sg_page(sg)) & (max_align - 1);

		if ((size - exact_align_mask) < max_align) {
			max_align /= 16;
			exact_align_mask = exact_align_mask & (max_align - 1);
		}

		if (align > max_align)
			align = max_align;

		exact_align_mask &= ~(align - 1);
	} else {
		align = 1 << __ffs(page_to_phys(sg_page(sg)));
		align = min_t(size_t, align, max_align);
		max_align = align;
	}

	start = alloc_iovm_region(vmm, size, align, max_align,
				exact_align_mask, start_off);
	if (!start) {
		ret = -ENOMEM;
		goto err_map_nomem;
	}

	addr = start - start_off;
	do {
		phys_addr_t phys;
		size_t len;

		phys = sg_phys(sg);
		len = sg_dma_len(sg);

		/* if back to back sg entries are contiguous consolidate them */
		while (sg_next(sg) &&
		       sg_phys(sg) + sg_dma_len(sg) == sg_phys(sg_next(sg))) {
			len += sg_dma_len(sg_next(sg));
			sg = sg_next(sg);
		}

		if (offset > 0) {
			len -= offset;
			phys += offset;
			offset = 0;
		}

		if (offset_in_page(phys)) {
			len += offset_in_page(phys);
			phys = round_down(phys, PAGE_SIZE);
		}

		len = PAGE_ALIGN(len);

		if (len > (size - mapped_size))
			len = size - mapped_size;

		ret = iommu_map(vmm->domain, addr, phys, len, 0);
		if (ret)
			break;

		addr += len;
		mapped_size += len;
	} while ((sg = sg_next(sg)) && (mapped_size < size));

	BUG_ON(mapped_size > size);

	if (mapped_size < size) {
		if (!ret)
			ret = -EINVAL;
		goto err_map_map;
	}

	dev_dbg(dev, "IOVMM: Allocated VM region @ %#x/%#x bytes.\n",
								start, size);

	return start;

err_map_map:
	iommu_unmap(vmm->domain, start - start_off, mapped_size);
	free_iovm_region(vmm, remove_iovm_region(vmm, start));

	dev_err(dev,
	"Failed(%d) to map IOVMM REGION %#lx ~ %#lx (SIZE: %#x, mapped: %#x)\n",
		ret, start - start_off, start - start_off + size,
		size, mapped_size);
	addr = 0;
	do {
		pr_err("SGLIST[%d].size = %#x\n", addr++, tsg->length);
	} while ((tsg = sg_next(tsg)));

	show_iovm_regions(vmm);

err_map_nomem:
	dev_dbg(dev, "IOVMM: Failed to allocated VM region for %#x bytes.\n",
									size);
	return (dma_addr_t)ret;
}

void iovmm_unmap(struct device *dev, dma_addr_t iova)
{
	struct exynos_iovmm *vmm = exynos_get_iovmm(dev);
	struct exynos_vm_region *region;
	size_t unmap_size;

	/* This function must not be called in IRQ handlers */
	BUG_ON(in_irq());

	region = remove_iovm_region(vmm, iova);
	if (region) {
		if (WARN_ON(region->start != iova)) {
			dev_err(dev,
			"IOVMM: iova %#x and region %#x @ %#x mismatch\n",
				iova, region->size, region->start);
			show_iovm_regions(vmm);
			/* reinsert iovm region */
			add_iovm_region(vmm, region->start, region->size);
			kfree(region);
			return;
		}
		unmap_size = iommu_unmap(vmm->domain, iova & PAGE_MASK,
							region->size);
		if (unlikely(unmap_size != region->size)) {
			dev_err(dev, "Failed to unmap IOVMM REGION %#x ~ %#x "\
				"(SIZE: %#x, iova: %#x, unmapped: %#x)\n",
				region->start, region->start + region->size,
				region->size, iova, unmap_size);
			show_iovm_regions(vmm);
			kfree(region);
			BUG();
			return;
		}

		free_iovm_region(vmm, region);

		dev_dbg(dev, "IOVMM: Unmapped %#x bytes from %#x.\n",
						unmap_size, iova);
	} else {
		dev_err(dev, "IOVMM: No IOVM region %#x to free.\n", iova);
	}
}

int iovmm_map_oto(struct device *dev, phys_addr_t phys, size_t size)
{
	struct exynos_iovmm *vmm = exynos_get_iovmm(dev);
	int ret;

	BUG_ON(!IS_ALIGNED(phys, PAGE_SIZE));
	BUG_ON(!IS_ALIGNED(size, PAGE_SIZE));

	if (WARN_ON((phys + size) >= IOVA_START)) {
		dev_err(dev,
			"Unable to create one to one mapping for %#x @ %#x\n",
			size, phys);
		return -EINVAL;
	}

	if (!add_iovm_region(vmm, (dma_addr_t)phys, size))
		return -EADDRINUSE;

	ret = iommu_map(vmm->domain, (dma_addr_t)phys, phys, size, 0);
	if (ret < 0)
		free_iovm_region(vmm,
				remove_iovm_region(vmm, (dma_addr_t)phys));

	return ret;
}

void iovmm_unmap_oto(struct device *dev, phys_addr_t phys)
{
	struct exynos_iovmm *vmm = exynos_get_iovmm(dev);
	struct exynos_vm_region *region;
	size_t unmap_size;

	/* This function must not be called in IRQ handlers */
	BUG_ON(in_irq());
	BUG_ON(!IS_ALIGNED(phys, PAGE_SIZE));

	region = remove_iovm_region(vmm, (dma_addr_t)phys);
	if (region) {
		unmap_size = iommu_unmap(vmm->domain, (dma_addr_t)phys,
							region->size);
		WARN_ON(unmap_size != region->size);

		free_iovm_region(vmm, region);

		dev_dbg(dev, "IOVMM: Unmapped %#x bytes from %#x.\n",
						unmap_size, phys);
	}
}

int exynos_create_iovmm(struct device *dev)
{
	int ret = 0;
	struct exynos_iovmm *vmm;
	struct exynos_iommu_owner *owner = dev->archdata.iommu;

	if (WARN_ON(!owner)) {
		ret = -ENOSYS;
		goto err_alloc_vmm;
	}

	vmm = kmalloc(sizeof(*vmm), GFP_KERNEL);
	if (!vmm) {
		ret = -ENOMEM;
		goto err_alloc_vmm;
	}

	vmm->vm_map = kzalloc(
		ALIGN(IOVM_BITMAP_SIZE, BITS_PER_BYTE) / BITS_PER_BYTE,
		GFP_KERNEL);
	if (!vmm->vm_map) {
		ret = -ENOMEM;
		goto err_alloc_vmm_map;
	}

	vmm->domain = iommu_domain_alloc(&platform_bus_type);
	if (!vmm->domain) {
		ret = -ENOMEM;
		goto err_setup_domain;
	}

	spin_lock_init(&vmm->vmlist_lock);
	spin_lock_init(&vmm->bitmap_lock);

	INIT_LIST_HEAD(&vmm->regions_list);

	vmm->dev = dev;
	owner->vmm_data = vmm;

	dev_dbg(dev, "IOVMM: Created %#x B IOVMM from %#x.\n",
						IOVM_SIZE, IOVA_START);
	return 0;
err_setup_domain:
	kfree(vmm->vm_map);
err_alloc_vmm_map:
	kfree(vmm);
err_alloc_vmm:
	dev_dbg(dev, "IOVMM: Failed to create IOVMM (%d)\n", ret);

	return ret;
}
