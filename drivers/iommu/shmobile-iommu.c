/*
 * IOMMU for IPMMU/IPMMUI
 * Copyright (C) 2012  Hideki EIRAKU
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <asm/dma-iommu.h>
#include "shmobile-ipmmu.h"

#define L1_SIZE CONFIG_SHMOBILE_IOMMU_L1SIZE
#define L1_LEN (L1_SIZE / 4)
#define L1_ALIGN L1_SIZE
#define L2_SIZE SZ_1K
#define L2_LEN (L2_SIZE / 4)
#define L2_ALIGN L2_SIZE

struct shmobile_iommu_domain_pgtable {
	uint32_t *pgtable;
	dma_addr_t handle;
};

struct shmobile_iommu_archdata {
	struct list_head attached_list;
	struct dma_iommu_mapping *iommu_mapping;
	spinlock_t attach_lock;
	struct shmobile_iommu_domain *attached;
	int num_attached_devices;
	struct shmobile_ipmmu *ipmmu;
};

struct shmobile_iommu_domain {
	struct shmobile_iommu_domain_pgtable l1, l2[L1_LEN];
	spinlock_t map_lock;
	spinlock_t attached_list_lock;
	struct list_head attached_list;
};

static struct shmobile_iommu_archdata *ipmmu_archdata;
static struct kmem_cache *l1cache, *l2cache;

static int pgtable_alloc(struct shmobile_iommu_domain_pgtable *pgtable,
			 struct kmem_cache *cache, size_t size)
{
	pgtable->pgtable = kmem_cache_zalloc(cache, GFP_ATOMIC);
	if (!pgtable->pgtable)
		return -ENOMEM;
	pgtable->handle = dma_map_single(NULL, pgtable->pgtable, size,
					 DMA_TO_DEVICE);
	return 0;
}

static void pgtable_free(struct shmobile_iommu_domain_pgtable *pgtable,
			 struct kmem_cache *cache, size_t size)
{
	dma_unmap_single(NULL, pgtable->handle, size, DMA_TO_DEVICE);
	kmem_cache_free(cache, pgtable->pgtable);
}

static uint32_t pgtable_read(struct shmobile_iommu_domain_pgtable *pgtable,
			     unsigned int index)
{
	return pgtable->pgtable[index];
}

static void pgtable_write(struct shmobile_iommu_domain_pgtable *pgtable,
			  unsigned int index, unsigned int count, uint32_t val)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		pgtable->pgtable[index + i] = val;
	dma_sync_single_for_device(NULL, pgtable->handle + index * sizeof(val),
				   sizeof(val) * count, DMA_TO_DEVICE);
}

static int shmobile_iommu_domain_init(struct iommu_domain *domain)
{
	struct shmobile_iommu_domain *sh_domain;
	int i, ret;

	sh_domain = kmalloc(sizeof(*sh_domain), GFP_KERNEL);
	if (!sh_domain)
		return -ENOMEM;
	ret = pgtable_alloc(&sh_domain->l1, l1cache, L1_SIZE);
	if (ret < 0) {
		kfree(sh_domain);
		return ret;
	}
	for (i = 0; i < L1_LEN; i++)
		sh_domain->l2[i].pgtable = NULL;
	spin_lock_init(&sh_domain->map_lock);
	spin_lock_init(&sh_domain->attached_list_lock);
	INIT_LIST_HEAD(&sh_domain->attached_list);
	domain->priv = sh_domain;
	return 0;
}

static void shmobile_iommu_domain_destroy(struct iommu_domain *domain)
{
	struct shmobile_iommu_domain *sh_domain = domain->priv;
	int i;

	for (i = 0; i < L1_LEN; i++) {
		if (sh_domain->l2[i].pgtable)
			pgtable_free(&sh_domain->l2[i], l2cache, L2_SIZE);
	}
	pgtable_free(&sh_domain->l1, l1cache, L1_SIZE);
	kfree(sh_domain);
	domain->priv = NULL;
}

static int shmobile_iommu_attach_device(struct iommu_domain *domain,
					struct device *dev)
{
	struct shmobile_iommu_archdata *archdata = dev->archdata.iommu;
	struct shmobile_iommu_domain *sh_domain = domain->priv;
	int ret = -EBUSY;

	if (!archdata)
		return -ENODEV;
	spin_lock(&sh_domain->attached_list_lock);
	spin_lock(&archdata->attach_lock);
	if (archdata->attached != sh_domain) {
		if (archdata->attached)
			goto err;
		ipmmu_tlb_set(archdata->ipmmu, sh_domain->l1.handle, L1_SIZE,
			      0);
		ipmmu_tlb_flush(archdata->ipmmu);
		archdata->attached = sh_domain;
		archdata->num_attached_devices = 0;
		list_add(&archdata->attached_list, &sh_domain->attached_list);
	}
	archdata->num_attached_devices++;
	ret = 0;
err:
	spin_unlock(&archdata->attach_lock);
	spin_unlock(&sh_domain->attached_list_lock);
	return ret;
}

static void shmobile_iommu_detach_device(struct iommu_domain *domain,
					 struct device *dev)
{
	struct shmobile_iommu_archdata *archdata = dev->archdata.iommu;
	struct shmobile_iommu_domain *sh_domain = domain->priv;

	if (!archdata)
		return;
	spin_lock(&sh_domain->attached_list_lock);
	spin_lock(&archdata->attach_lock);
	archdata->num_attached_devices--;
	if (!archdata->num_attached_devices) {
		ipmmu_tlb_set(archdata->ipmmu, 0, 0, 0);
		ipmmu_tlb_flush(archdata->ipmmu);
		archdata->attached = NULL;
		list_del(&archdata->attached_list);
	}
	spin_unlock(&archdata->attach_lock);
	spin_unlock(&sh_domain->attached_list_lock);
}

static void domain_tlb_flush(struct shmobile_iommu_domain *sh_domain)
{
	struct shmobile_iommu_archdata *archdata;

	spin_lock(&sh_domain->attached_list_lock);
	list_for_each_entry(archdata, &sh_domain->attached_list, attached_list)
		ipmmu_tlb_flush(archdata->ipmmu);
	spin_unlock(&sh_domain->attached_list_lock);
}

static int l2alloc(struct shmobile_iommu_domain *sh_domain,
		   unsigned int l1index)
{
	int ret;

	if (!sh_domain->l2[l1index].pgtable) {
		ret = pgtable_alloc(&sh_domain->l2[l1index], l2cache, L2_SIZE);
		if (ret < 0)
			return ret;
	}
	pgtable_write(&sh_domain->l1, l1index, 1,
		      sh_domain->l2[l1index].handle | 0x1);
	return 0;
}

static void l2realfree(struct shmobile_iommu_domain_pgtable *l2)
{
	if (l2->pgtable)
		pgtable_free(l2, l2cache, L2_SIZE);
}

static void l2free(struct shmobile_iommu_domain *sh_domain,
		   unsigned int l1index,
		   struct shmobile_iommu_domain_pgtable *l2)
{
	pgtable_write(&sh_domain->l1, l1index, 1, 0);
	if (sh_domain->l2[l1index].pgtable) {
		*l2 = sh_domain->l2[l1index];
		sh_domain->l2[l1index].pgtable = NULL;
	}
}

static int shmobile_iommu_map(struct iommu_domain *domain, unsigned long iova,
			      phys_addr_t paddr, size_t size, int prot)
{
	struct shmobile_iommu_domain_pgtable l2 = { .pgtable = NULL };
	struct shmobile_iommu_domain *sh_domain = domain->priv;
	unsigned int l1index, l2index;
	int ret;

	l1index = iova >> 20;
	switch (size) {
	case SZ_4K:
		l2index = (iova >> 12) & 0xff;
		spin_lock(&sh_domain->map_lock);
		ret = l2alloc(sh_domain, l1index);
		if (!ret)
			pgtable_write(&sh_domain->l2[l1index], l2index, 1,
				      paddr | 0xff2);
		spin_unlock(&sh_domain->map_lock);
		break;
	case SZ_64K:
		l2index = (iova >> 12) & 0xf0;
		spin_lock(&sh_domain->map_lock);
		ret = l2alloc(sh_domain, l1index);
		if (!ret)
			pgtable_write(&sh_domain->l2[l1index], l2index, 0x10,
				      paddr | 0xff1);
		spin_unlock(&sh_domain->map_lock);
		break;
	case SZ_1M:
		spin_lock(&sh_domain->map_lock);
		l2free(sh_domain, l1index, &l2);
		pgtable_write(&sh_domain->l1, l1index, 1, paddr | 0xc02);
		spin_unlock(&sh_domain->map_lock);
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}
	if (!ret)
		domain_tlb_flush(sh_domain);
	l2realfree(&l2);
	return ret;
}

static size_t shmobile_iommu_unmap(struct iommu_domain *domain,
				   unsigned long iova, size_t size)
{
	struct shmobile_iommu_domain_pgtable l2 = { .pgtable = NULL };
	struct shmobile_iommu_domain *sh_domain = domain->priv;
	unsigned int l1index, l2index;
	uint32_t l2entry = 0;
	size_t ret = 0;

	l1index = iova >> 20;
	if (!(iova & 0xfffff) && size >= SZ_1M) {
		spin_lock(&sh_domain->map_lock);
		l2free(sh_domain, l1index, &l2);
		spin_unlock(&sh_domain->map_lock);
		ret = SZ_1M;
		goto done;
	}
	l2index = (iova >> 12) & 0xff;
	spin_lock(&sh_domain->map_lock);
	if (sh_domain->l2[l1index].pgtable)
		l2entry = pgtable_read(&sh_domain->l2[l1index], l2index);
	switch (l2entry & 3) {
	case 1:
		if (l2index & 0xf)
			break;
		pgtable_write(&sh_domain->l2[l1index], l2index, 0x10, 0);
		ret = SZ_64K;
		break;
	case 2:
		pgtable_write(&sh_domain->l2[l1index], l2index, 1, 0);
		ret = SZ_4K;
		break;
	}
	spin_unlock(&sh_domain->map_lock);
done:
	if (ret)
		domain_tlb_flush(sh_domain);
	l2realfree(&l2);
	return ret;
}

static phys_addr_t shmobile_iommu_iova_to_phys(struct iommu_domain *domain,
					       dma_addr_t iova)
{
	struct shmobile_iommu_domain *sh_domain = domain->priv;
	uint32_t l1entry = 0, l2entry = 0;
	unsigned int l1index, l2index;

	l1index = iova >> 20;
	l2index = (iova >> 12) & 0xff;
	spin_lock(&sh_domain->map_lock);
	if (sh_domain->l2[l1index].pgtable)
		l2entry = pgtable_read(&sh_domain->l2[l1index], l2index);
	else
		l1entry = pgtable_read(&sh_domain->l1, l1index);
	spin_unlock(&sh_domain->map_lock);
	switch (l2entry & 3) {
	case 1:
		return (l2entry & ~0xffff) | (iova & 0xffff);
	case 2:
		return (l2entry & ~0xfff) | (iova & 0xfff);
	default:
		if ((l1entry & 3) == 2)
			return (l1entry & ~0xfffff) | (iova & 0xfffff);
		return 0;
	}
}

static int find_dev_name(struct shmobile_ipmmu *ipmmu, const char *dev_name)
{
	unsigned int i, n = ipmmu->num_dev_names;

	for (i = 0; i < n; i++) {
		if (strcmp(ipmmu->dev_names[i], dev_name) == 0)
			return 1;
	}
	return 0;
}

static int shmobile_iommu_add_device(struct device *dev)
{
	struct shmobile_iommu_archdata *archdata = ipmmu_archdata;
	struct dma_iommu_mapping *mapping;

	if (!find_dev_name(archdata->ipmmu, dev_name(dev)))
		return 0;
	mapping = archdata->iommu_mapping;
	if (!mapping) {
		mapping = arm_iommu_create_mapping(&platform_bus_type, 0,
						   L1_LEN << 20, 0);
		if (IS_ERR(mapping))
			return PTR_ERR(mapping);
		archdata->iommu_mapping = mapping;
	}
	dev->archdata.iommu = archdata;
	if (arm_iommu_attach_device(dev, mapping))
		pr_err("arm_iommu_attach_device failed\n");
	return 0;
}

static struct iommu_ops shmobile_iommu_ops = {
	.domain_init = shmobile_iommu_domain_init,
	.domain_destroy = shmobile_iommu_domain_destroy,
	.attach_dev = shmobile_iommu_attach_device,
	.detach_dev = shmobile_iommu_detach_device,
	.map = shmobile_iommu_map,
	.unmap = shmobile_iommu_unmap,
	.iova_to_phys = shmobile_iommu_iova_to_phys,
	.add_device = shmobile_iommu_add_device,
	.pgsize_bitmap = SZ_1M | SZ_64K | SZ_4K,
};

int ipmmu_iommu_init(struct shmobile_ipmmu *ipmmu)
{
	static struct shmobile_iommu_archdata *archdata;

	l1cache = kmem_cache_create("shmobile-iommu-pgtable1", L1_SIZE,
				    L1_ALIGN, SLAB_HWCACHE_ALIGN, NULL);
	if (!l1cache)
		return -ENOMEM;
	l2cache = kmem_cache_create("shmobile-iommu-pgtable2", L2_SIZE,
				    L2_ALIGN, SLAB_HWCACHE_ALIGN, NULL);
	if (!l2cache) {
		kmem_cache_destroy(l1cache);
		return -ENOMEM;
	}
	archdata = kmalloc(sizeof(*archdata), GFP_KERNEL);
	if (!archdata) {
		kmem_cache_destroy(l1cache);
		kmem_cache_destroy(l2cache);
		return -ENOMEM;
	}
	spin_lock_init(&archdata->attach_lock);
	archdata->attached = NULL;
	archdata->ipmmu = ipmmu;
	ipmmu_archdata = archdata;
	bus_set_iommu(&platform_bus_type, &shmobile_iommu_ops);
	return 0;
}
