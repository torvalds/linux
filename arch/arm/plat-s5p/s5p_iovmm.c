/* linux/arch/arm/plat-s5p/s5p_iovmm.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/ion.h>
#include <linux/iommu.h>
#include <linux/genalloc.h>
#include <linux/err.h>
#include <linux/spinlock.h>

#include <plat/s5p-iovmm.h>

struct s5p_vm_region {
	struct list_head node;
	dma_addr_t start;
	size_t size;
};

struct s5p_iovmm {
	struct list_head node;		/* element of s5p_iovmm_list */
	struct iommu_domain *domain;
	struct device *dev;
	struct gen_pool *vmm_pool;
	struct list_head regions_list;	/* list of s5p_vm_region */
	bool   active;
	struct mutex lock;
};

static DEFINE_RWLOCK(iovmm_list_lock);
static LIST_HEAD(s5p_iovmm_list);

static struct s5p_iovmm *find_iovmm(struct device *dev)
{
	struct list_head *pos;
	struct s5p_iovmm *vmm = NULL;

	read_lock(&iovmm_list_lock);
	list_for_each(pos, &s5p_iovmm_list) {
		vmm = list_entry(pos, struct s5p_iovmm, node);
		if (vmm->dev == dev)
			break;
	}
	read_unlock(&iovmm_list_lock);
	return vmm;
}

static struct s5p_vm_region *find_region(struct s5p_iovmm *vmm, dma_addr_t iova)
{
	struct list_head *pos;
	struct s5p_vm_region *region;

	list_for_each(pos, &vmm->regions_list) {
		region = list_entry(pos, struct s5p_vm_region, node);
		if (region->start == iova)
			return region;
	}
	return NULL;
}

#ifdef CONFIG_DRM_EXYNOS_IOMMU
void *iovmm_setup(unsigned long s_iova, unsigned long size)
{
	struct s5p_iovmm *vmm;
	int ret;

	vmm = kzalloc(sizeof(*vmm), GFP_KERNEL);
	if (!vmm) {
		ret = -ENOMEM;
		goto err_setup_alloc;
	}

	vmm->vmm_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!vmm->vmm_pool) {
		ret = -ENOMEM;
		goto err_setup_genalloc;
	}

	/* device address space starts from s_iova to s_iova + size */
	ret = gen_pool_add(vmm->vmm_pool, s_iova, size, -1);
	if (ret)
		goto err_setup_domain;

	vmm->domain = iommu_domain_alloc();
	if (!vmm->domain) {
		ret = -ENOMEM;
		goto err_setup_domain;
	}

	mutex_init(&vmm->lock);

	INIT_LIST_HEAD(&vmm->node);
	INIT_LIST_HEAD(&vmm->regions_list);

	write_lock(&iovmm_list_lock);
	list_add(&vmm->node, &s5p_iovmm_list);
	write_unlock(&iovmm_list_lock);

	return vmm;
err_setup_domain:
	gen_pool_destroy(vmm->vmm_pool);
err_setup_genalloc:
	kfree(vmm);
err_setup_alloc:
	return ERR_PTR(ret);
}

void iovmm_cleanup(void *in_vmm)
{
	struct s5p_iovmm *vmm = in_vmm;

	WARN_ON(!vmm);

	if (vmm) {
		struct list_head *pos, *tmp;

		iommu_domain_free(vmm->domain);

		list_for_each_safe(pos, tmp, &vmm->regions_list) {
			struct s5p_vm_region *region;

			region = list_entry(pos, struct s5p_vm_region, node);

			/* No need to unmap the region because
			 * iommu_domain_free() frees the page table */
			gen_pool_free(vmm->vmm_pool, region->start,
								region->size);

			kfree(list_entry(pos, struct s5p_vm_region, node));
		}

		gen_pool_destroy(vmm->vmm_pool);

		write_lock(&iovmm_list_lock);
		list_del(&vmm->node);
		write_unlock(&iovmm_list_lock);

		kfree(vmm);
	}
}

int iovmm_activate(void *in_vmm, struct device *dev)
{
	struct s5p_iovmm *vmm = in_vmm;
	int ret = 0;

	if (WARN_ON(!vmm))
		return -EINVAL;

	mutex_lock(&vmm->lock);

	ret = iommu_attach_device(vmm->domain, dev);
	if (!ret)
		vmm->active = true;

	mutex_unlock(&vmm->lock);

	return ret;
}

void iovmm_deactivate(void *in_vmm, struct device *dev)
{
	struct s5p_iovmm *vmm = in_vmm;

	if (WARN_ON(!vmm))
		return;

	iommu_detach_device(vmm->domain, dev);

	vmm->active = false;
}

dma_addr_t iovmm_map(void *in_vmm, struct scatterlist *sg, off_t offset,
								size_t size)
{
	off_t start_off;
	dma_addr_t addr, start = 0;
	size_t mapped_size = 0;
	struct s5p_vm_region *region;
	struct s5p_iovmm *vmm = in_vmm;
	int order;
#ifdef CONFIG_S5P_SYSTEM_MMU_WA5250ERR
	size_t iova_size = 0;
#endif

	BUG_ON(!sg);

	if (WARN_ON(!vmm))
		goto err_map_nomem;

	for (; sg_dma_len(sg) < offset; sg = sg_next(sg))
		offset -= sg_dma_len(sg);

	mutex_lock(&vmm->lock);

	start_off = offset_in_page(sg_phys(sg) + offset);
	size = PAGE_ALIGN(size + start_off);

	order = __fls(min(size, (size_t)SZ_1M));
#ifdef CONFIG_S5P_SYSTEM_MMU_WA5250ERR
	iova_size = ALIGN(size, SZ_64K);
	start = (dma_addr_t)gen_pool_alloc_aligned(vmm->vmm_pool, iova_size,
									order);
#else
	start = (dma_addr_t)gen_pool_alloc_aligned(vmm->vmm_pool, size, order);
#endif
	if (!start)
		goto err_map_nomem_lock;

	addr = start;
	do {
		phys_addr_t phys;
		size_t len;

		phys = sg_phys(sg);
		len = sg_dma_len(sg);

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

		while (len > 0) {
			order = min3(__ffs(phys), __ffs(addr), __fls(len));

			if (iommu_map(vmm->domain, addr, phys,
							order - PAGE_SHIFT, 0))
				goto err_map_map;

			addr += (1 << order);
			phys += (1 << order);
			len -= (1 << order);
			mapped_size += (1 << order);
		}
	} while ((sg = sg_next(sg)) && (mapped_size < size));

	BUG_ON(mapped_size > size);

	if (mapped_size < size)
		goto err_map_map;

#ifdef CONFIG_S5P_SYSTEM_MMU_WA5250ERR
	if (iova_size != size) {
		/* System MMU v3 support in SMDK5250 EVT0 */
		addr = start + size;
		size = iova_size;

		for (; addr < start + size; addr += PAGE_SIZE) {
			if (iommu_map(vmm->domain, addr,
					page_to_phys(ZERO_PAGE(0)), 0, 0)) {
				goto err_map_map;
			}
			mapped_size += PAGE_SIZE;
		}
	}
#endif
	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		goto err_map_map;

	region->start = start + start_off;
	region->size = size;
	INIT_LIST_HEAD(&region->node);

	list_add(&region->node, &vmm->regions_list);

	mutex_unlock(&vmm->lock);

	return region->start;
err_map_map:
	while (addr >= start) {
		int order;
		mapped_size = addr - start;

		if (mapped_size == 0) /* Mapping failed at the first page */
			mapped_size = size;

		BUG_ON(mapped_size < PAGE_SIZE);

		order = min(__fls(mapped_size), __ffs(start));

		iommu_unmap(vmm->domain, start, order - PAGE_SHIFT);

		start += 1 << order;
		mapped_size -= 1 << order;
	}
	gen_pool_free(vmm->vmm_pool, start, size);

err_map_nomem_lock:
	mutex_unlock(&vmm->lock);
err_map_nomem:
	return (dma_addr_t)0;
}

void iovmm_unmap(void *in_vmm, dma_addr_t iova)
{
	struct s5p_vm_region *region;
	struct s5p_iovmm *vmm = in_vmm;

	if (WARN_ON(!vmm))
		return;

	mutex_lock(&vmm->lock);

	region = find_region(vmm, iova);
	if (WARN_ON(!region))
		goto err_region_not_found;

	region->start = round_down(region->start, PAGE_SIZE);

	gen_pool_free(vmm->vmm_pool, region->start, region->size);
	list_del(&region->node);

	while (region->size != 0) {
		int order;

		order = min(__fls(region->size), __ffs(region->start));

		iommu_unmap(vmm->domain, region->start, order - PAGE_SHIFT);

		region->start += 1 << order;
		region->size -= 1 << order;
	}

	kfree(region);

err_region_not_found:
	mutex_unlock(&vmm->lock);
}
#else
int iovmm_setup(struct device *dev)
{
	struct s5p_iovmm *vmm;
	int ret;

	vmm = kzalloc(sizeof(*vmm), GFP_KERNEL);
	if (!vmm) {
		ret = -ENOMEM;
		goto err_setup_alloc;
	}

	vmm->vmm_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!vmm->vmm_pool) {
		ret = -ENOMEM;
		goto err_setup_genalloc;
	}

	/* 1GB addr space from 0x80000000 */
	ret = gen_pool_add(vmm->vmm_pool, 0x80000000, 0x40000000, -1);
	if (ret)
		goto err_setup_domain;

	vmm->domain = iommu_domain_alloc();
	if (!vmm->domain) {
		ret = -ENOMEM;
		goto err_setup_domain;
	}

	vmm->dev = dev;

	mutex_init(&vmm->lock);

	INIT_LIST_HEAD(&vmm->node);
	INIT_LIST_HEAD(&vmm->regions_list);

	write_lock(&iovmm_list_lock);
	list_add(&vmm->node, &s5p_iovmm_list);
	write_unlock(&iovmm_list_lock);

	return 0;
err_setup_domain:
	gen_pool_destroy(vmm->vmm_pool);
err_setup_genalloc:
	kfree(vmm);
err_setup_alloc:
	return ret;
}

void iovmm_cleanup(struct device *dev)
{
	struct s5p_iovmm *vmm;

	vmm = find_iovmm(dev);

	WARN_ON(!vmm);
	if (vmm) {
		struct list_head *pos, *tmp;

		if (vmm->active)
			iommu_detach_device(vmm->domain, dev);

		iommu_domain_free(vmm->domain);

		list_for_each_safe(pos, tmp, &vmm->regions_list) {
			struct s5p_vm_region *region;

			region = list_entry(pos, struct s5p_vm_region, node);

			/* No need to unmap the region because
			 * iommu_domain_free() frees the page table */
			gen_pool_free(vmm->vmm_pool, region->start,
								region->size);

			kfree(list_entry(pos, struct s5p_vm_region, node));
		}

		gen_pool_destroy(vmm->vmm_pool);

		write_lock(&iovmm_list_lock);
		list_del(&vmm->node);
		write_unlock(&iovmm_list_lock);

		kfree(vmm);
	}
}

int iovmm_activate(struct device *dev)
{
	struct s5p_iovmm *vmm;
	int ret = 0;

	vmm = find_iovmm(dev);
	if (WARN_ON(!vmm))
		return -EINVAL;

	mutex_lock(&vmm->lock);

	ret = iommu_attach_device(vmm->domain, vmm->dev);
	if (!ret)
		vmm->active = true;

	mutex_unlock(&vmm->lock);

	return ret;
}

void iovmm_deactivate(struct device *dev)
{
	struct s5p_iovmm *vmm;

	vmm = find_iovmm(dev);
	if (WARN_ON(!vmm))
		return;

	iommu_detach_device(vmm->domain, vmm->dev);

	vmm->active = false;
}

dma_addr_t iovmm_map(struct device *dev, struct scatterlist *sg, off_t offset,
								size_t size)
{
	off_t start_off;
	dma_addr_t addr, start = 0;
	size_t mapped_size = 0;
	struct s5p_vm_region *region;
	struct s5p_iovmm *vmm;
	int order;
#ifdef CONFIG_S5P_SYSTEM_MMU_WA5250ERR
	size_t iova_size = 0;
#endif

	BUG_ON(!sg);

	vmm = find_iovmm(dev);
	if (WARN_ON(!vmm))
		goto err_map_nomem;

	for (; sg_dma_len(sg) < offset; sg = sg_next(sg))
		offset -= sg_dma_len(sg);

	mutex_lock(&vmm->lock);

	start_off = offset_in_page(sg_phys(sg) + offset);
	size = PAGE_ALIGN(size + start_off);

	order = __fls(min(size, (size_t)SZ_1M));
#ifdef CONFIG_S5P_SYSTEM_MMU_WA5250ERR
	iova_size = ALIGN(size, SZ_64K);
	start = (dma_addr_t)gen_pool_alloc_aligned(vmm->vmm_pool, iova_size,
									order);
#else
	start = (dma_addr_t)gen_pool_alloc_aligned(vmm->vmm_pool, size, order);
#endif
	if (!start)
		goto err_map_nomem_lock;

	addr = start;
	do {
		phys_addr_t phys;
		size_t len;

		phys = sg_phys(sg);
		len = sg_dma_len(sg);

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

		while (len > 0) {
			order = min3(__ffs(phys), __ffs(addr), __fls(len));

			if (iommu_map(vmm->domain, addr, phys,
							order - PAGE_SHIFT, 0))
				goto err_map_map;

			addr += (1 << order);
			phys += (1 << order);
			len -= (1 << order);
			mapped_size += (1 << order);
		}
	} while ((sg = sg_next(sg)) && (mapped_size < size));

	BUG_ON(mapped_size > size);

	if (mapped_size < size)
		goto err_map_map;

#ifdef CONFIG_S5P_SYSTEM_MMU_WA5250ERR
	if (iova_size != size) {
		/* System MMU v3 support in SMDK5250 EVT0 */
		addr = start + size;
		size = iova_size;

		for (; addr < start + size; addr += PAGE_SIZE) {
			if (iommu_map(vmm->domain, addr,
					page_to_phys(ZERO_PAGE(0)), 0, 0)) {
				goto err_map_map;
			}
			mapped_size += PAGE_SIZE;
		}
	}
#endif
	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		goto err_map_map;

	region->start = start + start_off;
	region->size = size;
	INIT_LIST_HEAD(&region->node);

	list_add(&region->node, &vmm->regions_list);

	mutex_unlock(&vmm->lock);

	return region->start;
err_map_map:
	while (addr >= start) {
		int order;
		mapped_size = addr - start;

		if (mapped_size == 0) /* Mapping failed at the first page */
			mapped_size = size;

		BUG_ON(mapped_size < PAGE_SIZE);

		order = min(__fls(mapped_size), __ffs(start));

		iommu_unmap(vmm->domain, start, order - PAGE_SHIFT);

		start += 1 << order;
		mapped_size -= 1 << order;
	}
	gen_pool_free(vmm->vmm_pool, start, size);

err_map_nomem_lock:
	mutex_unlock(&vmm->lock);
err_map_nomem:
	return (dma_addr_t)0;
}

void iovmm_unmap(struct device *dev, dma_addr_t iova)
{
	struct s5p_vm_region *region;
	struct s5p_iovmm *vmm;

	vmm = find_iovmm(dev);

	if (WARN_ON(!vmm))
		return;

	mutex_lock(&vmm->lock);

	region = find_region(vmm, iova);
	if (WARN_ON(!region))
		goto err_region_not_found;

	region->start = round_down(region->start, PAGE_SIZE);

	gen_pool_free(vmm->vmm_pool, region->start, region->size);
	list_del(&region->node);

	while (region->size != 0) {
		int order;

		order = min(__fls(region->size), __ffs(region->start));

		iommu_unmap(vmm->domain, region->start, order - PAGE_SHIFT);

		region->start += 1 << order;
		region->size -= 1 << order;
	}

	kfree(region);

err_region_not_found:
	mutex_unlock(&vmm->lock);
}
#endif

static int __init s5p_iovmm_init(void)
{
	return 0;
}
arch_initcall(s5p_iovmm_init);
