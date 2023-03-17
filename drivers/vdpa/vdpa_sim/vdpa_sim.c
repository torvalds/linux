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
#include <uapi/linux/vdpa.h>

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
		 "Maximum number of iotlb entries for each address space. 0 means unlimited. (default: 2048)");

#define VDPASIM_QUEUE_ALIGN PAGE_SIZE
#define VDPASIM_QUEUE_MAX 256
#define VDPASIM_VENDOR_ID 0

static struct vdpasim *vdpa_to_sim(struct vdpa_device *vdpa)
{
	return container_of(vdpa, struct vdpasim, vdpa);
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
	uint16_t last_avail_idx = vq->vring.last_avail_idx;

	vringh_init_iotlb(&vq->vring, vdpasim->features, vq->num, true,
			  (struct vring_desc *)(uintptr_t)vq->desc_addr,
			  (struct vring_avail *)
			  (uintptr_t)vq->driver_addr,
			  (struct vring_used *)
			  (uintptr_t)vq->device_addr);

	vq->vring.last_avail_idx = last_avail_idx;
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

	spin_lock(&vdpasim->iommu_lock);

	for (i = 0; i < vdpasim->dev_attr.nvqs; i++) {
		vdpasim_vq_reset(vdpasim, &vdpasim->vqs[i]);
		vringh_set_iotlb(&vdpasim->vqs[i].vring, &vdpasim->iommu[0],
				 &vdpasim->iommu_lock);
	}

	for (i = 0; i < vdpasim->dev_attr.nas; i++) {
		vhost_iotlb_reset(&vdpasim->iommu[i]);
		vhost_iotlb_add_range(&vdpasim->iommu[i], 0, ULONG_MAX,
				      0, VHOST_MAP_RW);
		vdpasim->iommu_pt[i] = true;
	}

	vdpasim->running = true;
	spin_unlock(&vdpasim->iommu_lock);

	vdpasim->features = 0;
	vdpasim->status = 0;
	++vdpasim->generation;
}

static const struct vdpa_config_ops vdpasim_config_ops;
static const struct vdpa_config_ops vdpasim_batch_config_ops;

struct vdpasim *vdpasim_create(struct vdpasim_dev_attr *dev_attr,
			       const struct vdpa_dev_set_config *config)
{
	const struct vdpa_config_ops *ops;
	struct vdpa_device *vdpa;
	struct vdpasim *vdpasim;
	struct device *dev;
	int i, ret = -ENOMEM;

	if (!dev_attr->alloc_size)
		return ERR_PTR(-EINVAL);

	if (config->mask & BIT_ULL(VDPA_ATTR_DEV_FEATURES)) {
		if (config->device_features &
		    ~dev_attr->supported_features)
			return ERR_PTR(-EINVAL);
		dev_attr->supported_features =
			config->device_features;
	}

	if (batch_mapping)
		ops = &vdpasim_batch_config_ops;
	else
		ops = &vdpasim_config_ops;

	vdpa = __vdpa_alloc_device(NULL, ops,
				   dev_attr->ngroups, dev_attr->nas,
				   dev_attr->alloc_size,
				   dev_attr->name, false);
	if (IS_ERR(vdpa)) {
		ret = PTR_ERR(vdpa);
		goto err_alloc;
	}

	vdpasim = vdpa_to_sim(vdpa);
	vdpasim->dev_attr = *dev_attr;
	INIT_WORK(&vdpasim->work, dev_attr->work_fn);
	spin_lock_init(&vdpasim->lock);
	spin_lock_init(&vdpasim->iommu_lock);

	dev = &vdpasim->vdpa.dev;
	dev->dma_mask = &dev->coherent_dma_mask;
	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64)))
		goto err_iommu;
	vdpasim->vdpa.mdev = dev_attr->mgmt_dev;

	vdpasim->config = kzalloc(dev_attr->config_size, GFP_KERNEL);
	if (!vdpasim->config)
		goto err_iommu;

	vdpasim->vqs = kcalloc(dev_attr->nvqs, sizeof(struct vdpasim_virtqueue),
			       GFP_KERNEL);
	if (!vdpasim->vqs)
		goto err_iommu;

	vdpasim->iommu = kmalloc_array(vdpasim->dev_attr.nas,
				       sizeof(*vdpasim->iommu), GFP_KERNEL);
	if (!vdpasim->iommu)
		goto err_iommu;

	vdpasim->iommu_pt = kmalloc_array(vdpasim->dev_attr.nas,
					  sizeof(*vdpasim->iommu_pt), GFP_KERNEL);
	if (!vdpasim->iommu_pt)
		goto err_iommu;

	for (i = 0; i < vdpasim->dev_attr.nas; i++)
		vhost_iotlb_init(&vdpasim->iommu[i], max_iotlb_entries, 0);

	vdpasim->buffer = kvmalloc(dev_attr->buffer_size, GFP_KERNEL);
	if (!vdpasim->buffer)
		goto err_iommu;

	for (i = 0; i < dev_attr->nvqs; i++)
		vringh_set_iotlb(&vdpasim->vqs[i].vring, &vdpasim->iommu[0],
				 &vdpasim->iommu_lock);

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

	if (!vdpasim->running &&
	    (vdpasim->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
		vdpasim->pending_kick = true;
		return;
	}

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
	bool old_ready;

	spin_lock(&vdpasim->lock);
	old_ready = vq->ready;
	vq->ready = ready;
	if (vq->ready && !old_ready) {
		vdpasim_queue_ready(vdpasim, idx);
	}
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

static int vdpasim_get_vq_stats(struct vdpa_device *vdpa, u16 idx,
				struct sk_buff *msg,
				struct netlink_ext_ack *extack)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	if (vdpasim->dev_attr.get_stats)
		return vdpasim->dev_attr.get_stats(vdpasim, idx,
						   msg, extack);
	return -EOPNOTSUPP;
}

static u32 vdpasim_get_vq_align(struct vdpa_device *vdpa)
{
	return VDPASIM_QUEUE_ALIGN;
}

static u32 vdpasim_get_vq_group(struct vdpa_device *vdpa, u16 idx)
{
	/* RX and TX belongs to group 0, CVQ belongs to group 1 */
	if (idx == 2)
		return 1;
	else
		return 0;
}

static u64 vdpasim_get_device_features(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	return vdpasim->dev_attr.supported_features;
}

static int vdpasim_set_driver_features(struct vdpa_device *vdpa, u64 features)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	/* DMA mapping must be done by driver */
	if (!(features & (1ULL << VIRTIO_F_ACCESS_PLATFORM)))
		return -EINVAL;

	vdpasim->features = features & vdpasim->dev_attr.supported_features;

	return 0;
}

static u64 vdpasim_get_driver_features(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	return vdpasim->features;
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

static int vdpasim_suspend(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	spin_lock(&vdpasim->lock);
	vdpasim->running = false;
	spin_unlock(&vdpasim->lock);

	return 0;
}

static int vdpasim_resume(struct vdpa_device *vdpa)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	int i;

	spin_lock(&vdpasim->lock);
	vdpasim->running = true;

	if (vdpasim->pending_kick) {
		/* Process pending descriptors */
		for (i = 0; i < vdpasim->dev_attr.nvqs; ++i)
			vdpasim_kick_vq(vdpa, i);

		vdpasim->pending_kick = false;
	}

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

static int vdpasim_set_group_asid(struct vdpa_device *vdpa, unsigned int group,
				  unsigned int asid)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vhost_iotlb *iommu;
	int i;

	if (group > vdpasim->dev_attr.ngroups)
		return -EINVAL;

	if (asid >= vdpasim->dev_attr.nas)
		return -EINVAL;

	iommu = &vdpasim->iommu[asid];

	spin_lock(&vdpasim->lock);

	for (i = 0; i < vdpasim->dev_attr.nvqs; i++)
		if (vdpasim_get_vq_group(vdpa, i) == group)
			vringh_set_iotlb(&vdpasim->vqs[i].vring, iommu,
					 &vdpasim->iommu_lock);

	spin_unlock(&vdpasim->lock);

	return 0;
}

static int vdpasim_set_map(struct vdpa_device *vdpa, unsigned int asid,
			   struct vhost_iotlb *iotlb)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	struct vhost_iotlb_map *map;
	struct vhost_iotlb *iommu;
	u64 start = 0ULL, last = 0ULL - 1;
	int ret;

	if (asid >= vdpasim->dev_attr.nas)
		return -EINVAL;

	spin_lock(&vdpasim->iommu_lock);

	iommu = &vdpasim->iommu[asid];
	vhost_iotlb_reset(iommu);
	vdpasim->iommu_pt[asid] = false;

	for (map = vhost_iotlb_itree_first(iotlb, start, last); map;
	     map = vhost_iotlb_itree_next(map, start, last)) {
		ret = vhost_iotlb_add_range(iommu, map->start,
					    map->last, map->addr, map->perm);
		if (ret)
			goto err;
	}
	spin_unlock(&vdpasim->iommu_lock);
	return 0;

err:
	vhost_iotlb_reset(iommu);
	spin_unlock(&vdpasim->iommu_lock);
	return ret;
}

static int vdpasim_dma_map(struct vdpa_device *vdpa, unsigned int asid,
			   u64 iova, u64 size,
			   u64 pa, u32 perm, void *opaque)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);
	int ret;

	if (asid >= vdpasim->dev_attr.nas)
		return -EINVAL;

	spin_lock(&vdpasim->iommu_lock);
	if (vdpasim->iommu_pt[asid]) {
		vhost_iotlb_reset(&vdpasim->iommu[asid]);
		vdpasim->iommu_pt[asid] = false;
	}
	ret = vhost_iotlb_add_range_ctx(&vdpasim->iommu[asid], iova,
					iova + size - 1, pa, perm, opaque);
	spin_unlock(&vdpasim->iommu_lock);

	return ret;
}

static int vdpasim_dma_unmap(struct vdpa_device *vdpa, unsigned int asid,
			     u64 iova, u64 size)
{
	struct vdpasim *vdpasim = vdpa_to_sim(vdpa);

	if (asid >= vdpasim->dev_attr.nas)
		return -EINVAL;

	if (vdpasim->iommu_pt[asid]) {
		vhost_iotlb_reset(&vdpasim->iommu[asid]);
		vdpasim->iommu_pt[asid] = false;
	}

	spin_lock(&vdpasim->iommu_lock);
	vhost_iotlb_del_range(&vdpasim->iommu[asid], iova, iova + size - 1);
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

	kvfree(vdpasim->buffer);
	for (i = 0; i < vdpasim->dev_attr.nas; i++)
		vhost_iotlb_reset(&vdpasim->iommu[i]);
	kfree(vdpasim->iommu);
	kfree(vdpasim->iommu_pt);
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
	.get_vendor_vq_stats    = vdpasim_get_vq_stats,
	.get_vq_state           = vdpasim_get_vq_state,
	.get_vq_align           = vdpasim_get_vq_align,
	.get_vq_group           = vdpasim_get_vq_group,
	.get_device_features    = vdpasim_get_device_features,
	.set_driver_features    = vdpasim_set_driver_features,
	.get_driver_features    = vdpasim_get_driver_features,
	.set_config_cb          = vdpasim_set_config_cb,
	.get_vq_num_max         = vdpasim_get_vq_num_max,
	.get_device_id          = vdpasim_get_device_id,
	.get_vendor_id          = vdpasim_get_vendor_id,
	.get_status             = vdpasim_get_status,
	.set_status             = vdpasim_set_status,
	.reset			= vdpasim_reset,
	.suspend		= vdpasim_suspend,
	.resume			= vdpasim_resume,
	.get_config_size        = vdpasim_get_config_size,
	.get_config             = vdpasim_get_config,
	.set_config             = vdpasim_set_config,
	.get_generation         = vdpasim_get_generation,
	.get_iova_range         = vdpasim_get_iova_range,
	.set_group_asid         = vdpasim_set_group_asid,
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
	.get_vendor_vq_stats    = vdpasim_get_vq_stats,
	.get_vq_state           = vdpasim_get_vq_state,
	.get_vq_align           = vdpasim_get_vq_align,
	.get_vq_group           = vdpasim_get_vq_group,
	.get_device_features    = vdpasim_get_device_features,
	.set_driver_features    = vdpasim_set_driver_features,
	.get_driver_features    = vdpasim_get_driver_features,
	.set_config_cb          = vdpasim_set_config_cb,
	.get_vq_num_max         = vdpasim_get_vq_num_max,
	.get_device_id          = vdpasim_get_device_id,
	.get_vendor_id          = vdpasim_get_vendor_id,
	.get_status             = vdpasim_get_status,
	.set_status             = vdpasim_set_status,
	.reset			= vdpasim_reset,
	.suspend		= vdpasim_suspend,
	.resume			= vdpasim_resume,
	.get_config_size        = vdpasim_get_config_size,
	.get_config             = vdpasim_get_config,
	.set_config             = vdpasim_set_config,
	.get_generation         = vdpasim_get_generation,
	.get_iova_range         = vdpasim_get_iova_range,
	.set_group_asid         = vdpasim_set_group_asid,
	.set_map                = vdpasim_set_map,
	.free                   = vdpasim_free,
};

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE(DRV_LICENSE);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
