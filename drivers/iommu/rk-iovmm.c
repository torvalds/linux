/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_ROCKCHIP_IOMMU_DEBUG
#define DEBUG
#endif

#include <linux/kernel.h>
#include <linux/hardirq.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/err.h>

#include <linux/of.h>
#include <linux/of_platform.h>

#include "rk-iommu.h"

#define IOMMU_REGION_GUARD		(2<<PAGE_SHIFT)

static struct rk_vm_region *find_region(struct rk_iovmm *vmm, dma_addr_t iova)
{
	struct rk_vm_region *region;

	list_for_each_entry(region, &vmm->regions_list, node)
		if (region->start == iova)
			return region;

	return NULL;
}

int rockchip_iovmm_invalidate_tlb(struct device *dev)
{
	int ret = rockchip_iommu_tlb_invalidate_global(dev);

	return ret;
}

void rockchip_iovmm_set_fault_handler(struct device *dev,
				       rockchip_iommu_fault_handler_t handler)
{
	struct iommu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);

	data->fault_handler = handler;
}

int rockchip_iovmm_activate(struct device *dev)
{
	struct rk_iovmm *vmm = rockchip_get_iovmm(dev);

	return iommu_attach_device(vmm->domain, dev);
}

void rockchip_iovmm_deactivate(struct device *dev)
{
	struct rk_iovmm *vmm = rockchip_get_iovmm(dev);

	iommu_detach_device(vmm->domain, dev);
}

dma_addr_t rockchip_iovmm_map(struct device *dev,
	struct scatterlist *sg, off_t offset, size_t size)
{
	off_t start_off;
	dma_addr_t addr, start = 0;
	size_t mapped_size = 0;
	struct rk_vm_region *region;
	struct rk_iovmm *vmm = rockchip_get_iovmm(dev);
	int order;
	int ret;

	for (; sg->length < offset; sg = sg_next(sg))
		offset -= sg->length;

	start_off = offset_in_page(sg_phys(sg) + offset);
	size = PAGE_ALIGN(size + start_off);

	order = __fls(min_t(size_t, size, SZ_1M));

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region) {
		ret = -ENOMEM;
		goto err_map_nomem;
	}

	start = (dma_addr_t)gen_pool_alloc(vmm->vmm_pool,
					   size+IOMMU_REGION_GUARD);
	if (!start) {
		ret = -ENOMEM;
		goto err_map_noiomem;
	}

	pr_debug("%s: size = %zx\n", __func__, size);

	addr = start;
	do {
		phys_addr_t phys;
		size_t len;

		phys = sg_phys(sg);
		len = sg->length;

		/* if back to back sg entries are contiguous consolidate them */
		while (sg_next(sg) && sg_phys(sg) + sg->length == sg_phys(sg_next(sg))) {
			sg = sg_next(sg);
			len += sg->length;
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
		pr_debug("addr = %pad, phys = %pa, len = %zx\n", &addr, &phys, len);
		ret = iommu_map(vmm->domain, addr, phys, len, 0);
		if (ret)
			break;

		addr += len;
		mapped_size += len;
	} while ((sg = sg_next(sg)) && (mapped_size < size));

	BUG_ON(mapped_size > size);

	if (mapped_size < size)
		goto err_map_map;

	region->start = start + start_off;
	region->size = size;

	INIT_LIST_HEAD(&region->node);

	spin_lock(&vmm->lock);

	list_add(&region->node, &vmm->regions_list);

	spin_unlock(&vmm->lock);

	ret = rockchip_iommu_tlb_invalidate(dev);
	if (ret) {
		spin_lock(&vmm->lock);
		list_del(&region->node);
		spin_unlock(&vmm->lock);
		goto err_map_map;
	}
	dev_dbg(dev->archdata.iommu, "IOVMM: Allocated VM region @ %p/%#X bytes.\n",
	&region->start, region->size);

	return region->start;

err_map_map:
	iommu_unmap(vmm->domain, start, mapped_size);
	gen_pool_free(vmm->vmm_pool, start, size);
err_map_noiomem:
	kfree(region);
err_map_nomem:
	dev_err(dev->archdata.iommu, "IOVMM: Failed to allocated VM region for %zx bytes.\n", size);
	return (dma_addr_t)ret;
}

void rockchip_iovmm_unmap(struct device *dev, dma_addr_t iova)
{
	struct rk_vm_region *region;
	struct rk_iovmm *vmm = rockchip_get_iovmm(dev);
	size_t unmapped_size;

	/* This function must not be called in IRQ handlers */
	BUG_ON(in_irq());

	spin_lock(&vmm->lock);

	region = find_region(vmm, iova);
	if (WARN_ON(!region)) {
		spin_unlock(&vmm->lock);
		return;
	}

	list_del(&region->node);

	spin_unlock(&vmm->lock);

	region->start = round_down(region->start, PAGE_SIZE);

	unmapped_size = iommu_unmap(vmm->domain,
				    region->start, region->size);
	/*
	rockchip_iommu_tlb_invalidate(dev);
	*/
	gen_pool_free(vmm->vmm_pool, region->start,
		      region->size+IOMMU_REGION_GUARD);

	WARN_ON(unmapped_size != region->size);

	dev_dbg(dev->archdata.iommu, "IOVMM: Unmapped %zx bytes from %pad.\n",
		unmapped_size, &region->start);

	kfree(region);
}

int rockchip_iovmm_map_oto(struct device *dev, phys_addr_t phys, size_t size)
{
	struct rk_vm_region *region;
	struct rk_iovmm *vmm = rockchip_get_iovmm(dev);
	int ret;

	if (WARN_ON((phys + size) >= IOVA_START)) {
		dev_err(dev->archdata.iommu, "Unable to create one to one mapping for %zx @ %pa\n",
		       size, &phys);
		return -EINVAL;
	}

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return -ENOMEM;

	if (WARN_ON(phys & ~PAGE_MASK))
		phys = round_down(phys, PAGE_SIZE);


	ret = iommu_map(vmm->domain, (dma_addr_t)phys, phys, size, 0);
	if (ret < 0) {
		kfree(region);
		return ret;
	}

	region->start = (dma_addr_t)phys;
	region->size = size;
	INIT_LIST_HEAD(&region->node);

	spin_lock(&vmm->lock);

	list_add(&region->node, &vmm->regions_list);

	spin_unlock(&vmm->lock);

	ret = rockchip_iommu_tlb_invalidate(dev);
	if (ret)
		return ret;

	return 0;
}

void rockchip_iovmm_unmap_oto(struct device *dev, phys_addr_t phys)
{
	struct rk_vm_region *region;
	struct rk_iovmm *vmm = rockchip_get_iovmm(dev);
	size_t unmapped_size;

	/* This function must not be called in IRQ handlers */
	BUG_ON(in_irq());

	if (WARN_ON(phys & ~PAGE_MASK))
		phys = round_down(phys, PAGE_SIZE);

	spin_lock(&vmm->lock);

	region = find_region(vmm, (dma_addr_t)phys);
	if (WARN_ON(!region)) {
		spin_unlock(&vmm->lock);
		return;
	}

	list_del(&region->node);

	spin_unlock(&vmm->lock);

	unmapped_size = iommu_unmap(vmm->domain, region->start, region->size);
	WARN_ON(unmapped_size != region->size);
	dev_dbg(dev->archdata.iommu, "IOVMM: Unmapped %zx bytes from %pad.\n",
	       unmapped_size, &region->start);

	kfree(region);
}

int rockchip_init_iovmm(struct device *iommu, struct rk_iovmm *vmm)
{
	int ret = 0;

	vmm->vmm_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!vmm->vmm_pool) {
		ret = -ENOMEM;
		goto err_setup_genalloc;
	}

	/* (1GB - 4KB) addr space from 0x10000000 */
	ret = gen_pool_add(vmm->vmm_pool, IOVA_START, IOVM_SIZE, -1);
	if (ret)
		goto err_setup_domain;

	vmm->domain = iommu_domain_alloc(&platform_bus_type);
	if (!vmm->domain) {
		ret = -ENOMEM;
		goto err_setup_domain;
	}

	spin_lock_init(&vmm->lock);

	INIT_LIST_HEAD(&vmm->regions_list);

	dev_info(iommu, "IOVMM: Created %#x B IOVMM from %#x.\n",
		IOVM_SIZE, IOVA_START);
	return 0;
err_setup_domain:
	gen_pool_destroy(vmm->vmm_pool);
err_setup_genalloc:
	dev_err(iommu, "IOVMM: Failed to create IOVMM (%d)\n", ret);

	return ret;
}
