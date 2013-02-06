/* linux/arch/arm/plat-s5p/s5p_iovmm.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_EXYNOS_IOMMU_DEBUG
#define DEBUG
#endif

#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/iommu.h>
#include <linux/genalloc.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/atomic.h>
#include <linux/rculist.h>

#include <plat/iovmm.h>

#define IOVA_START 0xC0000000
#define IOVM_SIZE (SZ_1G - SZ_4K) /* last 4K is for error values */

struct s5p_vm_region {
	struct list_head node;
	dma_addr_t start;
	size_t size;
};

struct s5p_iovmm {
	struct list_head node;		/* element of s5p_iovmm_list */
	struct rcu_head rcu;
	struct iommu_domain *domain;
	struct device *dev;
	struct gen_pool *vmm_pool;
	struct list_head regions_list;	/* list of s5p_vm_region */
	atomic_t activations;
	int num_setup;
	spinlock_t lock;
};

static LIST_HEAD(s5p_iovmm_list);

static struct s5p_iovmm *find_iovmm(struct device *dev)
{
	struct s5p_iovmm *vmm;

	list_for_each_entry(vmm, &s5p_iovmm_list, node)
		if ((vmm->dev == dev) && (vmm->num_setup > 0))
			return vmm;

	return NULL;
}

static struct s5p_vm_region *find_region(struct s5p_iovmm *vmm, dma_addr_t iova)
{
	struct s5p_vm_region *region;

	list_for_each_entry(region, &vmm->regions_list, node)
		if (region->start == iova)
			return region;

	return NULL;
}

int iovmm_setup(struct device *dev)
{
	struct s5p_iovmm *vmm = NULL;
	struct list_head *pos;
	int ret;

	list_for_each(pos, &s5p_iovmm_list) {
		vmm = list_entry(pos, struct s5p_iovmm, node);
		if (vmm->dev == dev) {
			struct s5p_iovmm *rcu_vmm;

			rcu_vmm = kmalloc(sizeof(*rcu_vmm), GFP_KERNEL);
			if (rcu_vmm == NULL)
				return -ENOMEM;

			memcpy(rcu_vmm, vmm, sizeof(*vmm));
			rcu_vmm->num_setup++;
			list_replace_rcu(&vmm->node, &rcu_vmm->node);

			kfree(vmm);

			return 0;
		}
	}

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

	/* (1GB - 4KB) addr space from 0xC0000000 */
	ret = gen_pool_add(vmm->vmm_pool, IOVA_START, IOVM_SIZE, -1);
	if (ret)
		goto err_setup_domain;

	vmm->domain = iommu_domain_alloc(&platform_bus_type);
	if (!vmm->domain) {
		ret = -ENOMEM;
		goto err_setup_domain;
	}

	vmm->dev = dev;
	vmm->num_setup = 1;

	spin_lock_init(&vmm->lock);

	INIT_LIST_HEAD(&vmm->node);
	INIT_LIST_HEAD(&vmm->regions_list);
	atomic_set(&vmm->activations, 0);

	list_add_rcu(&vmm->node, &s5p_iovmm_list);

	dev_dbg(dev, "IOVMM: Created %#x B IOVMM from %#x.\n",
						IOVM_SIZE, IOVA_START);

	return 0;
err_setup_domain:
	gen_pool_destroy(vmm->vmm_pool);
err_setup_genalloc:
	kfree(vmm);
err_setup_alloc:
	dev_dbg(dev, "IOVMM: Failed to create IOVMM (%d)\n", ret);
	return ret;
}

static void iovmm_destroy(struct rcu_head *rcu)
{
	struct s5p_iovmm *vmm = container_of(rcu, struct s5p_iovmm, rcu);
	struct list_head *pos, *tmp;

	while (WARN_ON(atomic_dec_return(&vmm->activations) > 0))
		iommu_detach_device(vmm->domain, vmm->dev);

	iommu_domain_free(vmm->domain);

	WARN_ON(!list_empty(&vmm->regions_list));

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

	dev_dbg(vmm->dev, "IOVMM: Removed IOVMM\n");

	kfree(vmm);
}

void iovmm_cleanup(struct device *dev)
{
	struct s5p_iovmm *vmm, *n;

	list_for_each_entry_safe(vmm, n, &s5p_iovmm_list, node) {
		if (vmm->dev == dev) {
			struct s5p_iovmm *rcu_vmm = NULL;

			while (rcu_vmm == NULL) /* should success */
				rcu_vmm = kmalloc(sizeof(*rcu_vmm), GFP_ATOMIC);

			memcpy(rcu_vmm, vmm, sizeof(*vmm));
			rcu_vmm->num_setup--;
			list_replace_rcu(&vmm->node, &rcu_vmm->node);

			kfree(vmm);

			if (rcu_vmm->num_setup == 0) {
				list_del_rcu(&rcu_vmm->node);
				call_rcu(&rcu_vmm->rcu, iovmm_destroy);
			}

			return;
		}
	}

	WARN(true, "%s: No IOVMM exist for %s\n", __func__, dev_name(dev));
}

int iovmm_activate(struct device *dev)
{
	struct s5p_iovmm *vmm;
	int ret = 0;

	rcu_read_lock();

	vmm = find_iovmm(dev);
	if (WARN_ON(!vmm)) {
		rcu_read_unlock();
		return -EINVAL;
	}

	ret = iommu_attach_device(vmm->domain, vmm->dev);
	if (!ret)
		atomic_inc(&vmm->activations);

	rcu_read_unlock();

	return ret;
}

void iovmm_deactivate(struct device *dev)
{
	struct s5p_iovmm *vmm;

	rcu_read_lock();

	vmm = find_iovmm(dev);
	if (WARN_ON(!vmm)) {
		rcu_read_unlock();
		return;
	}

	iommu_detach_device(vmm->domain, vmm->dev);

	atomic_add_unless(&vmm->activations, -1, 0);

	rcu_read_unlock();
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
	unsigned long flags;
#ifdef CONFIG_EXYNOS_IOVMM_ALIGN64K
	size_t iova_size = 0;
#endif

	rcu_read_lock();

	vmm = find_iovmm(dev);
	if (WARN_ON(!vmm))
		goto err_map_nomem;

	for (; sg_dma_len(sg) < offset; sg = sg_next(sg))
		offset -= sg_dma_len(sg);

	start_off = offset_in_page(sg_phys(sg) + offset);
	size = PAGE_ALIGN(size + start_off);

	order = __fls(min_t(size_t, size, SZ_1M));

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		goto err_map_nomem;

	INIT_LIST_HEAD(&region->node);

	spin_lock_irqsave(&vmm->lock, flags);

#ifdef CONFIG_EXYNOS_IOVMM_ALIGN64K
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

		if (iommu_map(vmm->domain, addr, phys, len, 0))
			break;

		addr += len;
		mapped_size += len;
	} while ((sg = sg_next(sg)) && (mapped_size < size));

	BUG_ON(mapped_size > size);

	if (mapped_size < size)
		goto err_map_map;

#ifdef CONFIG_EXYNOS_IOVMM_ALIGN64K
	if (iova_size != size) {
		addr = start + size;
		size = iova_size;

		for (; addr < start + size; addr += PAGE_SIZE) {
			if (iommu_map(vmm->domain, addr,
				page_to_phys(ZERO_PAGE(0)), PAGE_SIZE, 0))
				goto err_map_map;

			mapped_size += PAGE_SIZE;
		}
	}
#endif

	region->start = start + start_off;
	region->size = size;

	list_add(&region->node, &vmm->regions_list);

	spin_unlock_irqrestore(&vmm->lock, flags);

	dev_dbg(dev, "IOVMM: Allocated VM region @ %#x/%#X bytes.\n",
					region->start, region->size);

	rcu_read_unlock();

	return region->start;

err_map_map:
	iommu_unmap(vmm->domain, start, mapped_size);
	gen_pool_free(vmm->vmm_pool, start, size);
err_map_nomem_lock:
	spin_unlock_irqrestore(&vmm->lock, flags);
	kfree(region);
err_map_nomem:
	dev_dbg(dev, "IOVMM: Failed to allocated VM region for %#x bytes.\n",
									size);
	rcu_read_unlock();

	return (dma_addr_t)0;
}

void iovmm_unmap(struct device *dev, dma_addr_t iova)
{
	struct s5p_vm_region *region;
	struct s5p_iovmm *vmm;
	unsigned long flags;
	size_t unmapped_size;

	rcu_read_lock();

	vmm = find_iovmm(dev);

	if (WARN_ON(!vmm)) {
		rcu_read_unlock();
		return;
	}

	spin_lock_irqsave(&vmm->lock, flags);

	region = find_region(vmm, iova);
	if (WARN_ON(!region))
		goto err_region_not_found;

	region->start = round_down(region->start, PAGE_SIZE);

	gen_pool_free(vmm->vmm_pool, region->start, region->size);
	list_del(&region->node);

	unmapped_size = iommu_unmap(vmm->domain, region->start, region->size);

	WARN_ON(unmapped_size != region->size);
	dev_dbg(dev, "IOVMM: Unmapped %#x bytes from %#x.\n",
					unmapped_size, region->start);

	kfree(region);
err_region_not_found:
	spin_unlock_irqrestore(&vmm->lock, flags);

	rcu_read_unlock();
}

int iovmm_map_oto(struct device *dev, phys_addr_t phys, size_t size)
{
	struct s5p_vm_region *region;
	struct s5p_iovmm *vmm;
	unsigned long flags;
	int ret;

	rcu_read_lock();

	vmm = find_iovmm(dev);
	if (WARN_ON(!vmm)) {
		ret = -EINVAL;
		goto err_map_nomem;
	}

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region) {
		ret = -ENOMEM;
		goto err_map_nomem;
	}

	if (WARN_ON((phys + size) >= IOVA_START)) {
		dev_err(dev,
			"Unable to create one to one mapping for %#x @ %#x\n",
			size, phys);
		ret = -EINVAL;
		goto err_out_of_memory;
	}

	if (WARN_ON(phys & ~PAGE_MASK))
		phys = round_down(phys, PAGE_SIZE);

	spin_lock_irqsave(&vmm->lock, flags);

	ret = iommu_map(vmm->domain, (dma_addr_t)phys, phys, size, 0);
	if (ret < 0)
		goto err_map_failed;

	region->start = (dma_addr_t)phys;
	region->size = size;
	INIT_LIST_HEAD(&region->node);

	list_add(&region->node, &vmm->regions_list);

	spin_unlock_irqrestore(&vmm->lock, flags);

	rcu_read_unlock();

	return 0;

err_map_failed:
	spin_unlock_irqrestore(&vmm->lock, flags);
err_out_of_memory:
	kfree(region);
err_map_nomem:
	rcu_read_unlock();

	return ret;
}

void iovmm_unmap_oto(struct device *dev, phys_addr_t phys)
{
	struct s5p_vm_region *region;
	struct s5p_iovmm *vmm;
	unsigned long flags;
	size_t unmapped_size;

	rcu_read_lock();

	vmm = find_iovmm(dev);
	if (WARN_ON(!vmm)) {
		rcu_read_unlock();
		return;
	}

	if (WARN_ON(phys & ~PAGE_MASK))
		phys = round_down(phys, PAGE_SIZE);

	spin_lock_irqsave(&vmm->lock, flags);

	region = find_region(vmm, (dma_addr_t)phys);
	if (WARN_ON(!region))
		goto err_region_not_found;

	list_del(&region->node);

	unmapped_size = iommu_unmap(vmm->domain, region->start, region->size);

	WARN_ON(unmapped_size != region->size);
	dev_dbg(dev, "IOVMM: Unmapped %#x bytes from %#x.\n",
					unmapped_size, region->start);

	kfree(region);
err_region_not_found:
	spin_unlock_irqrestore(&vmm->lock, flags);

	rcu_read_unlock();
}
