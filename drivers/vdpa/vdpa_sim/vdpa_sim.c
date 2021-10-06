// SPDX-License-Identifier: GPL-2.0-only
/*
 * VDPA device simulator core.
 *
 * Copyright (c) 2020, Red Hat Inc. All rights reserved.
 *     Author: Jason Wang <jasowang@redhat.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/dma-map-ops.h>
#include <linux/vringh.h>
#include <linux/vdpa.h>
#include <linux/vhost_iotlb.h>
#include <linux/iova.h>

#include "vdpa_sim.h"

#define DRV_VERSION  "0.1"
#define DRV_AUTHOR   "Jason Wang <jasowang@redhat.com>"
#define DRV_DESC     "vDPA Device Simulator core"
#define DRV_LICENSE  "GPL v2"

static int batch_mapping = 1;
module_param(batch_mapping, int, 0444);
MODULE_PARM_DESC(batch_mapping, "Batched mapping 1 -Enable; 0 - Disable");

static int max_iotlb_entries = 2048;
module_param(max_iotlb_entries, int, 0444);
MODULE_PARM_DESC(max_iotlb_entries,
		 "Maximum number of iotlb entries. 0 means unlimited. (default: 2048)");

#define VDPASIM_QUEUE_ALIGN PAGE_SIZE
#define VDPASIM_QUEUE_MAX 256
#define VDPASIM_VENDOR_ID 0

static struct vdpasim *vdpa_to_sim(struct vdpa_device *vdpa)
{
	return container_of(vdpa, struct vdpasim, vdpa);
}

static struct vdpasim *dev_to_sim(struct device *dev)
{
	struct vdpa_device *vdpa = dev_to_vdpa(dev);

	return vdpa_to_sim(vdpa);
}

static void vdpasim_vq_notify(struct vringh *vring)
{
	struct vdpasim_virtqueue *vq =
		container_of(vring, struct vdpasim_virtqueue, vring);

	if (!vq->cb)
		return;

	vq->cb(vq->private);
}

static void vdpasim_queue_ready(struct vdpasim *vdpasim, unsigned int idx)
{
	struct vdpasim_virtqueue *vq = &vdpasim->vqs[idx];

	vringh_init_iotlb(&vq->vring, vdpasim->dev_attr.supported_features,
			  VDPASIM_QUEUE_MAX, false,
			  (struct vring_desc *)(uintptr_t)vq->desc_addr,
			  (struct vring_avail *)
			  (uintptr_t)vq->driver_addr,
			  (struct vring_used *)
			  (uintptr_t)vq->device_addr);

	vq->vring.notify = vdpasim_vq_notify;
}

static void vdpasim_vq_reset(struct vdpasim *vdpasim,
			     struct vdpasim_virtqueue *vq)
{
	vq->ready = false;
	vq->desc_addr = 0;
	vq->driver_addr = 0;
	vq->device_addr = 0;
	vq->cb = NULL;
	vq->private = NULL;
	vringh_init_iotlb(&vq->vring, vdpasim->dev_attr.supported_features,
			  VDPASIM_QUEUE_MAX, false, NULL, NULL, NULL);

	vq->vring.notify = NULL;
}

static void vdpasim_do_reset(struct vdpasim *vdpasim)
{
	int i;

	for (i = 0; i < vdpasim->dev_attr.nvqs; i++)
		vdpasim_vq_reset(vdpasim, &vdpasim->vqs[i]);

	spin_lock(&vdpasim->iommu_lock);
	vhost_iotlb_reset(vdpasim->iommu);
	spin_unlock(&vdpasim->iommu_lock);

	vdpasim->features = 0;
	vdpasim->status = 0;
	++vdpasim->generation;
}

static int dir_to_perm(enum dma_data_direction dir)
{
	int perm = -EFAULT;

	switch (dir) {
	case DMA_FROM_DEVICE:
		perm = VHOST_MAP_WO;
		break;
	case DMA_TO_DEVICE:
		perm = VHOST_MAP_RO;
		break;
	case DMA_BIDIRECTIONAL:
		perm = VHOST_MAP_RW;
		break;
	default:
		break;
	}

	return perm;
}

static dma_addr_t vdpasim_map_range(struct vdpasim *vdpasim, phys_addr_t paddr,
				    size_t size, unsigned int perm)
{
	struct iova *iova;
	dma_addr_t dma_addr;
	int ret;

	/* We set the limit_pfn to the maximum (ULONG_MAX - 1) */
	iova = alloc_iova(&vdpasim->iova, size >> iova_shift(&vdpasim->iova),
			  ULONG_MAX - 1, true);
	if (!iova)
		return DMA_MAPPING_ERROR;

	dma_addr = iova_dma_addr(&vdpasim->iova, iova);

	spin_lock(&vdpasim->iommu_lock);
	ret = vhost_iotlb_add_range(vdpasim->iommu, (u64)dma_addr,
				    (u64)dma_addr + size - 1, (u64)paddr, perm);
	spin_unlock(&vdpasim->iommu_lock);

	if (ret) {
		__free_iova(&vdpasim->iova, iova);
		return DMA_MAPPING_ERROR;
	}

	return dma_addr;
}

static void vdpasim_unmap_range(struct vdpasim *vdpasim, dma_addr_t dma_addr,
				size_t size)
{
	spin_lock(&vdpasim->iommu_lock);
	vhost_iotlb_del_range(vdpasim->iommu, (u64)dma_addr,
			      (u64)dma_addr + size - 1);
	spin_unlock(&vdpasim->iommu_lock);

	free_iova(&vdpasim->iova, iova_pfn(&vdpasim->iova, dma_addr));
}

static dma_addr_t vdpasim_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction dir,
				   unsigned long attrs)
{
	struct vdpasim *vdpasim = dev_to_sim(dev);
	phys_addr_t paddr = page_to_phys(page) + offset;
	int perm = dir_to_perm(dir);

	if (perm < 0)
		return DMA_MAPPING_ERROR;

	return vdpasim_map_range(vdpasim, paddr, size, perm);
}

static void vdpasim_unmap_page(struct device *dev, dma_addr_t dma_addr,
			       size_t size, enum dma_data_direction dir,
			       unsigned long attrs)
{
	struct vdpasim *vdpasim = dev_to_sim(dev);

	vdpasim_unmap_range(vdpasim, dma_addr, size);
}

static void *vdpasim_alloc_coherent(struct device *dev, size_t size,
				    dma_addr_t *dma_addr, gfp_t flag,
				    unsigned long attrs)
{
	struct vdpasim *vdpasim = dev_to_sim(dev);
	phys_addr_t paddr;
	void *addr;

	addr = kmalloc(size, flag);
	if (!addr) {
		*dma_addr = DMA_MAPPING_ERROR;
		return NULL;
	}

	paddr = virt_to_phys(addr);

	*dma_addr = vdpasim_map_range(vdpasim, paddr, size, VHOST_MAP_RW);
	if (*dma_addr == DMA_MAPPING_ERROR) {
		kfree(addr);
		return NULL;
	}

	return addr;
}

static void vdpasim_free_coherent(struct device *dev, size_t size,
				  void *vaddr, dma_addr_t dma_addr,
				  unsigned long attrs)
{
	struct vdpasim *vdpasim = dev_to_sim(dev);

	vdpasim_unmap_range(vdpasim, dma_addr, size);

	kfree(vaddr);
}

static const struct dma_map_ops vdpasim_dma_ops = {
	.map_page = vdpasim_map_page,
	.unmap_page = vdpasim_unmap_page,
	.alloc = vdpasim_alloc_coherent,
	.free = vdpasim_free_coherent,
};

static const struct vdpa_config_ops vdpasim_config_ops;
static const struct vdpa_config_ops vdpasim_batch_config_ops;

struct vdpasim *vdpasim_create(struct vdpasim_dev_attr *dev_attr)
{
	const struct vdpa_config_ops *ops;
	struct vdpasim *vdpasim;
	struct device *dev;
	int i, ret = -ENOMEM;

	if (batch_mapping)
		ops = &vdpasim_batch_config_ops;
	else
		ops = &vdpasim_config_ops;

	vdpasim = vdpa_alloc_device(struct vdpasim, vdpa, NULL, ops,
				    dev_attr->name, false);
	if (IS_ERR(vdpasim)) {
		ret = PTR_ERR(vdpasim);
		goto err_alloc;
	}

	vdpasim->dev_attr = *dev_attr;
	INIT_WORK(&vdpasim->work, dev_attr->work_fn);
	spin_lock_init(&vdpasim->lock);
	spin_lock_init(&vdpasim->iommu_lock);

	dev = &vdpasim->vdpa.dev;
	dev->dma_mask = &dev->coherent_dma_mask;
	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64)))
		goto err_iommu;
	set_dma_ops(dev, &vdpasim_dma_ops);
	vdpasim->vdpa.mdev = dev_attr->mgmt_dev;

	vdpasim->config = kzalloc(dev_attr->config_size, GFP_KERNEL);
	if (!vdpasim->config)
		goto err_iommu;

	vdpasim->vqs = kcalloc(dev_attr->nvqs, sizeof(struct vdpasim_virtqueue),
			       GFP_KERNEL);
	if (!vdpasim->vqs)
		goto err_iommu;

	vdpasim->iommu = vhost_iotlb_alloc(max_iotlb_entries, 0);
	if (!vdpasim->iommu)
		goto err_iommu;

	vdpasim->buffer = kvmalloc(dev_attr->buffer_size, GFP_KERNEL);
	if (!vdpasim->buffer)
		goto err_iommu;

	for (i = 0; i < dev_attr->nvqs; i++)
		vringh_set_iotlb(&vdpasim->vqs[i].vring, vdpasim->iommu,
				 &vdpasim->iommu_lock);

	ret = iova_cache_get();
	if (ret)
		goto err_iommu;

	/* For simplicity we use an IOVA allocator with byte granularity */
	init_iova_domain(&vdpasim->iova, 1, 0);

	vdpasim->vdpa.dma_dev = dev;

	return vdpasim;

err_iommu:
	put_device(dev);
err_alloc:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(vdpasim_create);

static int vdpasim_set_vq_address(struct vdpa_device *vdpa, u16 idx,
				  u64 desc_area, u64 driver_area,
				  u64 device_area)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vdpasim_virtqueue *vq = &vdpasim->vqs[idx];

	vq->desc_addr = desc_area;
	vq->driver_addr = driver_area;
	vq->device_addr = device_area;

	return 0;
}

static void vdpasim_set_vq_num(struct vdpa_device *vdpa, u16 idx, u32 num)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vdpasim_virtqueue *vq = &vdpasim->vqs[idx];

	vq->num = num;
}

static void vdpasim_kick_vq(struct vdpa_device *vdpa, u16 idx)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vdpasim_virtqueue *vq = &vdpasim->vqs[idx];

	if (vq->ready)
		schedule_work(&vdpasim->work);
}

static void vdpasim_set_vq_cb(struct vdpa_device *vdpa, u16 idx,
			      struct vdpa_callback *cb)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vdpasim_virtqueue *vq = &vdpasim->vqs[idx];

	vq->cb = cb->callback;
	vq->private = cb->private;
}

static void vdpasim_set_vq_ready(struct vdpa_device *vdpa, u16 idx, bool ready)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vdpasim_virtqueue *vq = &vdpasim->vqs[idx];

	spin_lock(&vdpasim->lock);
	vq->ready = ready;
	if (vq->ready)
		vdpasim_queue_ready(vdpasim, idx);
	spin_unlock(&vdpasim->lock);
}

static bool vdpasim_get_vq_ready(struct vdpa_device *vdpa, u16 idx)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vdpasim_virtqueue *vq = &vdpasim->vqs[idx];

	return vq->ready;
}

static int vdpasim_set_vq_state(struct vdpa_device *vdpa, u16 idx,
				const struct vdpa_vq_state *state)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vdpasim_virtqueue *vq = &vdpasim->vqs[idx];
	struct vringh *vrh = &vq->vring;

	spin_lock(&vdpasim->lock);
	vrh->last_avail_idx = state->split.avail_index;
	spin_unlock(&vdpasim->lock);

	return 0;
}

static int vdpasim_get_vq_state(struct vdpa_device *vdpa, u16 idx,
				struct vdpa_vq_state *state)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vdpasim_virtqueue *vq = &vdpasim->vqs[idx];
	struct vringh *vrh = &vq->vring;

	state->split.avail_index = vrh->last_avail_idx;
	return 0;
}

static u32 vdpasim_get_vq_align(struct vdpa_device *vdpa)
{
	return VDPASIM_QUEUE_ALIGN;
}

static u64 vdpasim_get_features(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	return vdpasim->dev_attr.supported_features;
}

static int vdpasim_set_features(struct vdpa_device *vdpa, u64 features)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	/* DMA mapping must be done by driver */
	if (!(features & (1ULL << VIRTIO_F_ACCESS_PLATFORM)))
		return -EINVAL;

	vdpasim->features = features & vdpasim->dev_attr.supported_features;

	return 0;
}

static void vdpasim_set_config_cb(struct vdpa_device *vdpa,
				  struct vdpa_callback *cb)
{
	/* We don't support config interrupt */
}

static u16 vdpasim_get_vq_num_max(struct vdpa_device *vdpa)
{
	return VDPASIM_QUEUE_MAX;
}

static u32 vdpasim_get_device_id(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	return vdpasim->dev_attr.id;
}

static u32 vdpasim_get_vendor_id(struct vdpa_device *vdpa)
{
	return VDPASIM_VENDOR_ID;
}

static u8 vdpasim_get_status(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	u8 status;

	spin_lock(&vdpasim->lock);
	status = vdpasim->status;
	spin_unlock(&vdpasim->lock);

	return status;
}

static void vdpasim_set_status(struct vdpa_device *vdpa, u8 status)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	spin_lock(&vdpasim->lock);
	vdpasim->status = status;
	spin_unlock(&vdpasim->lock);
}

static int vdpasim_reset(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	spin_lock(&vdpasim->lock);
	vdpasim->status = 0;
	vdpasim_do_reset(vdpasim);
	spin_unlock(&vdpasim->lock);

	return 0;
}

static size_t vdpasim_get_config_size(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	return vdpasim->dev_attr.config_size;
}

static void vdpasim_get_config(struct vdpa_device *vdpa, unsigned int offset,
			     void *buf, unsigned int len)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	if (offset + len > vdpasim->dev_attr.config_size)
		return;

	if (vdpasim->dev_attr.get_config)
		vdpasim->dev_attr.get_config(vdpasim, vdpasim->config);

	memcpy(buf, vdpasim->config + offset, len);
}

static void vdpasim_set_config(struct vdpa_device *vdpa, unsigned int offset,
			     const void *buf, unsigned int len)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	if (offset + len > vdpasim->dev_attr.config_size)
		return;

	memcpy(vdpasim->config + offset, buf, len);

	if (vdpasim->dev_attr.set_config)
		vdpasim->dev_attr.set_config(vdpasim, vdpasim->config);
}

static u32 vdpasim_get_generation(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	return vdpasim->generation;
}

static struct vdpa_iova_range vdpasim_get_iova_range(struct vdpa_device *vdpa)
{
	struct vdpa_iova_range range = {
		.first = 0ULL,
		.last = ULLONG_MAX,
	};

	return range;
}

static int vdpasim_set_map(struct vdpa_device *vdpa,
			   struct vhost_iotlb *iotlb)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vhost_iotlb_map *map;
	u64 start = 0ULL, last = 0ULL - 1;
	int ret;

	spin_lock(&vdpasim->iommu_lock);
	vhost_iotlb_reset(vdpasim->iommu);

	for (map = vhost_iotlb_itree_first(iotlb, start, last); map;
	     map = vhost_iotlb_itree_next(map, start, last)) {
		ret = vhost_iotlb_add_range(vdpasim->iommu, map->start,
					    map->last, map->addr, map->perm);
		if (ret)
			goto err;
	}
	spin_unlock(&vdpasim->iommu_lock);
	return 0;

err:
	vhost_iotlb_reset(vdpasim->iommu);
	spin_unlock(&vdpasim->iommu_lock);
	return ret;
}

static int vdpasim_dma_map(struct vdpa_device *vdpa, u64 iova, u64 size,
			   u64 pa, u32 perm, void *opaque)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	int ret;

	spin_lock(&vdpasim->iommu_lock);
	ret = vhost_iotlb_add_range_ctx(vdpasim->iommu, iova, iova + size - 1,
					pa, perm, opaque);
	spin_unlock(&vdpasim->iommu_lock);

	return ret;
}

static int vdpasim_dma_unmap(struct vdpa_device *vdpa, u64 iova, u64 size)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	spin_lock(&vdpasim->iommu_lock);
	vhost_iotlb_del_range(vdpasim->iommu, iova, iova + size - 1);
	spin_unlock(&vdpasim->iommu_lock);

	return 0;
}

static void vdpasim_free(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	int i;

	cancel_work_sync(&vdpasim->work);

	for (i = 0; i < vdpasim->dev_attr.nvqs; i++) {
		vringh_kiov_cleanup(&vdpasim->vqs[i].out_iov);
		vringh_kiov_cleanup(&vdpasim->vqs[i].in_iov);
	}

	put_iova_domain(&vdpasim->iova);
	iova_cache_put();
	kvfree(vdpasim->buffer);
	if (vdpasim->iommu)
		vhost_iotlb_free(vdpasim->iommu);
	kfree(vdpasim->vqs);
	kfree(vdpasim->config);
}

static const struct vdpa_config_ops vdpasim_config_ops = {
	.set_vq_address         = vdpasim_set_vq_address,
	.set_vq_num             = vdpasim_set_vq_num,
	.kick_vq                = vdpasim_kick_vq,
	.set_vq_cb              = vdpasim_set_vq_cb,
	.set_vq_ready           = vdpasim_set_vq_ready,
	.get_vq_ready           = vdpasim_get_vq_ready,
	.set_vq_state           = vdpasim_set_vq_state,
	.get_vq_state           = vdpasim_get_vq_state,
	.get_vq_align           = vdpasim_get_vq_align,
	.get_features           = vdpasim_get_features,
	.set_features           = vdpasim_set_features,
	.set_config_cb          = vdpasim_set_config_cb,
	.get_vq_num_max         = vdpasim_get_vq_num_max,
	.get_device_id          = vdpasim_get_device_id,
	.get_vendor_id          = vdpasim_get_vendor_id,
	.get_status             = vdpasim_get_status,
	.set_status             = vdpasim_set_status,
	.reset			= vdpasim_reset,
	.get_config_size        = vdpasim_get_config_size,
	.get_config             = vdpasim_get_config,
	.set_config             = vdpasim_set_config,
	.get_generation         = vdpasim_get_generation,
	.get_iova_range         = vdpasim_get_iova_range,
	.dma_map                = vdpasim_dma_map,
	.dma_unmap              = vdpasim_dma_unmap,
	.free                   = vdpasim_free,
};

static const struct vdpa_config_ops vdpasim_batch_config_ops = {
	.set_vq_address         = vdpasim_set_vq_address,
	.set_vq_num             = vdpasim_set_vq_num,
	.kick_vq                = vdpasim_kick_vq,
	.set_vq_cb              = vdpasim_set_vq_cb,
	.set_vq_ready           = vdpasim_set_vq_ready,
	.get_vq_ready           = vdpasim_get_vq_ready,
	.set_vq_state           = vdpasim_set_vq_state,
	.get_vq_state           = vdpasim_get_vq_state,
	.get_vq_align           = vdpasim_get_vq_align,
	.get_features           = vdpasim_get_features,
	.set_features           = vdpasim_set_features,
	.set_config_cb          = vdpasim_set_config_cb,
	.get_vq_num_max         = vdpasim_get_vq_num_max,
	.get_device_id          = vdpasim_get_device_id,
	.get_vendor_id          = vdpasim_get_vendor_id,
	.get_status             = vdpasim_get_status,
	.set_status             = vdpasim_set_status,
	.reset			= vdpasim_reset,
	.get_config_size        = vdpasim_get_config_size,
	.get_config             = vdpasim_get_config,
	.set_config             = vdpasim_set_config,
	.get_generation         = vdpasim_get_generation,
	.get_iova_range         = vdpasim_get_iova_range,
	.set_map                = vdpasim_set_map,
	.free                   = vdpasim_free,
};

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE(DRV_LICENSE);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
