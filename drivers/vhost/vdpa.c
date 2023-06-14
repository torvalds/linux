// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2020 Intel Corporation.
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Author: Tiwei Bie <tiwei.bie@intel.com>
 *         Jason Wang <jasowang@redhat.com>
 *
 * Thanks Michael S. Tsirkin for the valuable comments and
 * suggestions.  And thanks to Cunming Liang and Zhihong Wang for all
 * their supports.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/uuid.h>
#include <linux/vdpa.h>
#include <linux/nospec.h>
#include <linux/vhost.h>

#include "vhost.h"

enum {
	VHOST_VDPA_BACKEND_FEATURES =
	(1ULL << VHOST_BACKEND_F_IOTLB_MSG_V2) |
	(1ULL << VHOST_BACKEND_F_IOTLB_BATCH) |
	(1ULL << VHOST_BACKEND_F_IOTLB_ASID),
};

#define VHOST_VDPA_DEV_MAX (1U << MINORBITS)

#define VHOST_VDPA_IOTLB_BUCKETS 16

struct vhost_vdpa_as {
	struct hlist_node hash_link;
	struct vhost_iotlb iotlb;
	u32 id;
};

struct vhost_vdpa {
	struct vhost_dev vdev;
	struct iommu_domain *domain;
	struct vhost_virtqueue *vqs;
	struct completion completion;
	struct vdpa_device *vdpa;
	struct hlist_head as[VHOST_VDPA_IOTLB_BUCKETS];
	struct device dev;
	struct cdev cdev;
	atomic_t opened;
	u32 nvqs;
	int virtio_id;
	int minor;
	struct eventfd_ctx *config_ctx;
	int in_batch;
	struct vdpa_iova_range range;
	u32 batch_asid;
};

static DEFINE_IDA(vhost_vdpa_ida);

static dev_t vhost_vdpa_major;

static void vhost_vdpa_iotlb_unmap(struct vhost_vdpa *v,
				   struct vhost_iotlb *iotlb, u64 start,
				   u64 last, u32 asid);

static inline u32 iotlb_to_asid(struct vhost_iotlb *iotlb)
{
	struct vhost_vdpa_as *as = container_of(iotlb, struct
						vhost_vdpa_as, iotlb);
	return as->id;
}

static struct vhost_vdpa_as *asid_to_as(struct vhost_vdpa *v, u32 asid)
{
	struct hlist_head *head = &v->as[asid % VHOST_VDPA_IOTLB_BUCKETS];
	struct vhost_vdpa_as *as;

	hlist_for_each_entry(as, head, hash_link)
		if (as->id == asid)
			return as;

	return NULL;
}

static struct vhost_iotlb *asid_to_iotlb(struct vhost_vdpa *v, u32 asid)
{
	struct vhost_vdpa_as *as = asid_to_as(v, asid);

	if (!as)
		return NULL;

	return &as->iotlb;
}

static struct vhost_vdpa_as *vhost_vdpa_alloc_as(struct vhost_vdpa *v, u32 asid)
{
	struct hlist_head *head = &v->as[asid % VHOST_VDPA_IOTLB_BUCKETS];
	struct vhost_vdpa_as *as;

	if (asid_to_as(v, asid))
		return NULL;

	if (asid >= v->vdpa->nas)
		return NULL;

	as = kmalloc(sizeof(*as), GFP_KERNEL);
	if (!as)
		return NULL;

	vhost_iotlb_init(&as->iotlb, 0, 0);
	as->id = asid;
	hlist_add_head(&as->hash_link, head);

	return as;
}

static struct vhost_vdpa_as *vhost_vdpa_find_alloc_as(struct vhost_vdpa *v,
						      u32 asid)
{
	struct vhost_vdpa_as *as = asid_to_as(v, asid);

	if (as)
		return as;

	return vhost_vdpa_alloc_as(v, asid);
}

static int vhost_vdpa_remove_as(struct vhost_vdpa *v, u32 asid)
{
	struct vhost_vdpa_as *as = asid_to_as(v, asid);

	if (!as)
		return -EINVAL;

	hlist_del(&as->hash_link);
	vhost_vdpa_iotlb_unmap(v, &as->iotlb, 0ULL, 0ULL - 1, asid);
	kfree(as);

	return 0;
}

static void handle_vq_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						  poll.work);
	struct vhost_vdpa *v = container_of(vq->dev, struct vhost_vdpa, vdev);
	const struct vdpa_config_ops *ops = v->vdpa->config;

	ops->kick_vq(v->vdpa, vq - v->vqs);
}

static irqreturn_t vhost_vdpa_virtqueue_cb(void *private)
{
	struct vhost_virtqueue *vq = private;
	struct eventfd_ctx *call_ctx = vq->call_ctx.ctx;

	if (call_ctx)
		eventfd_signal(call_ctx, 1);

	return IRQ_HANDLED;
}

static irqreturn_t vhost_vdpa_config_cb(void *private)
{
	struct vhost_vdpa *v = private;
	struct eventfd_ctx *config_ctx = v->config_ctx;

	if (config_ctx)
		eventfd_signal(config_ctx, 1);

	return IRQ_HANDLED;
}

static void vhost_vdpa_setup_vq_irq(struct vhost_vdpa *v, u16 qid)
{
	struct vhost_virtqueue *vq = &v->vqs[qid];
	const struct vdpa_config_ops *ops = v->vdpa->config;
	struct vdpa_device *vdpa = v->vdpa;
	int ret, irq;

	if (!ops->get_vq_irq)
		return;

	irq = ops->get_vq_irq(vdpa, qid);
	if (irq < 0)
		return;

	irq_bypass_unregister_producer(&vq->call_ctx.producer);
	if (!vq->call_ctx.ctx)
		return;

	vq->call_ctx.producer.token = vq->call_ctx.ctx;
	vq->call_ctx.producer.irq = irq;
	ret = irq_bypass_register_producer(&vq->call_ctx.producer);
	if (unlikely(ret))
		dev_info(&v->dev, "vq %u, irq bypass producer (token %p) registration fails, ret =  %d\n",
			 qid, vq->call_ctx.producer.token, ret);
}

static void vhost_vdpa_unsetup_vq_irq(struct vhost_vdpa *v, u16 qid)
{
	struct vhost_virtqueue *vq = &v->vqs[qid];

	irq_bypass_unregister_producer(&vq->call_ctx.producer);
}

static int vhost_vdpa_reset(struct vhost_vdpa *v)
{
	struct vdpa_device *vdpa = v->vdpa;

	v->in_batch = 0;

	return vdpa_reset(vdpa);
}

static long vhost_vdpa_get_device_id(struct vhost_vdpa *v, u8 __user *argp)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	u32 device_id;

	device_id = ops->get_device_id(vdpa);

	if (copy_to_user(argp, &device_id, sizeof(device_id)))
		return -EFAULT;

	return 0;
}

static long vhost_vdpa_get_status(struct vhost_vdpa *v, u8 __user *statusp)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	u8 status;

	status = ops->get_status(vdpa);

	if (copy_to_user(statusp, &status, sizeof(status)))
		return -EFAULT;

	return 0;
}

static long vhost_vdpa_set_status(struct vhost_vdpa *v, u8 __user *statusp)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	u8 status, status_old;
	u32 nvqs = v->nvqs;
	int ret;
	u16 i;

	if (copy_from_user(&status, statusp, sizeof(status)))
		return -EFAULT;

	status_old = ops->get_status(vdpa);

	/*
	 * Userspace shouldn't remove status bits unless reset the
	 * status to 0.
	 */
	if (status != 0 && (status_old & ~status) != 0)
		return -EINVAL;

	if ((status_old & VIRTIO_CONFIG_S_DRIVER_OK) && !(status & VIRTIO_CONFIG_S_DRIVER_OK))
		for (i = 0; i < nvqs; i++)
			vhost_vdpa_unsetup_vq_irq(v, i);

	if (status == 0) {
		ret = vdpa_reset(vdpa);
		if (ret)
			return ret;
	} else
		vdpa_set_status(vdpa, status);

	if ((status & VIRTIO_CONFIG_S_DRIVER_OK) && !(status_old & VIRTIO_CONFIG_S_DRIVER_OK))
		for (i = 0; i < nvqs; i++)
			vhost_vdpa_setup_vq_irq(v, i);

	return 0;
}

static int vhost_vdpa_config_validate(struct vhost_vdpa *v,
				      struct vhost_vdpa_config *c)
{
	struct vdpa_device *vdpa = v->vdpa;
	size_t size = vdpa->config->get_config_size(vdpa);

	if (c->len == 0 || c->off > size)
		return -EINVAL;

	if (c->len > size - c->off)
		return -E2BIG;

	return 0;
}

static long vhost_vdpa_get_config(struct vhost_vdpa *v,
				  struct vhost_vdpa_config __user *c)
{
	struct vdpa_device *vdpa = v->vdpa;
	struct vhost_vdpa_config config;
	unsigned long size = offsetof(struct vhost_vdpa_config, buf);
	u8 *buf;

	if (copy_from_user(&config, c, size))
		return -EFAULT;
	if (vhost_vdpa_config_validate(v, &config))
		return -EINVAL;
	buf = kvzalloc(config.len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	vdpa_get_config(vdpa, config.off, buf, config.len);

	if (copy_to_user(c->buf, buf, config.len)) {
		kvfree(buf);
		return -EFAULT;
	}

	kvfree(buf);
	return 0;
}

static long vhost_vdpa_set_config(struct vhost_vdpa *v,
				  struct vhost_vdpa_config __user *c)
{
	struct vdpa_device *vdpa = v->vdpa;
	struct vhost_vdpa_config config;
	unsigned long size = offsetof(struct vhost_vdpa_config, buf);
	u8 *buf;

	if (copy_from_user(&config, c, size))
		return -EFAULT;
	if (vhost_vdpa_config_validate(v, &config))
		return -EINVAL;

	buf = vmemdup_user(c->buf, config.len);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	vdpa_set_config(vdpa, config.off, buf, config.len);

	kvfree(buf);
	return 0;
}

static bool vhost_vdpa_can_suspend(const struct vhost_vdpa *v)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;

	return ops->suspend;
}

static long vhost_vdpa_get_features(struct vhost_vdpa *v, u64 __user *featurep)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	u64 features;

	features = ops->get_device_features(vdpa);

	if (copy_to_user(featurep, &features, sizeof(features)))
		return -EFAULT;

	return 0;
}

static long vhost_vdpa_set_features(struct vhost_vdpa *v, u64 __user *featurep)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	u64 features;

	/*
	 * It's not allowed to change the features after they have
	 * been negotiated.
	 */
	if (ops->get_status(vdpa) & VIRTIO_CONFIG_S_FEATURES_OK)
		return -EBUSY;

	if (copy_from_user(&features, featurep, sizeof(features)))
		return -EFAULT;

	if (vdpa_set_features(vdpa, features))
		return -EINVAL;

	return 0;
}

static long vhost_vdpa_get_vring_num(struct vhost_vdpa *v, u16 __user *argp)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	u16 num;

	num = ops->get_vq_num_max(vdpa);

	if (copy_to_user(argp, &num, sizeof(num)))
		return -EFAULT;

	return 0;
}

static void vhost_vdpa_config_put(struct vhost_vdpa *v)
{
	if (v->config_ctx) {
		eventfd_ctx_put(v->config_ctx);
		v->config_ctx = NULL;
	}
}

static long vhost_vdpa_set_config_call(struct vhost_vdpa *v, u32 __user *argp)
{
	struct vdpa_callback cb;
	int fd;
	struct eventfd_ctx *ctx;

	cb.callback = vhost_vdpa_config_cb;
	cb.private = v;
	if (copy_from_user(&fd, argp, sizeof(fd)))
		return  -EFAULT;

	ctx = fd == VHOST_FILE_UNBIND ? NULL : eventfd_ctx_fdget(fd);
	swap(ctx, v->config_ctx);

	if (!IS_ERR_OR_NULL(ctx))
		eventfd_ctx_put(ctx);

	if (IS_ERR(v->config_ctx)) {
		long ret = PTR_ERR(v->config_ctx);

		v->config_ctx = NULL;
		return ret;
	}

	v->vdpa->config->set_config_cb(v->vdpa, &cb);

	return 0;
}

static long vhost_vdpa_get_iova_range(struct vhost_vdpa *v, u32 __user *argp)
{
	struct vhost_vdpa_iova_range range = {
		.first = v->range.first,
		.last = v->range.last,
	};

	if (copy_to_user(argp, &range, sizeof(range)))
		return -EFAULT;
	return 0;
}

static long vhost_vdpa_get_config_size(struct vhost_vdpa *v, u32 __user *argp)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	u32 size;

	size = ops->get_config_size(vdpa);

	if (copy_to_user(argp, &size, sizeof(size)))
		return -EFAULT;

	return 0;
}

static long vhost_vdpa_get_vqs_count(struct vhost_vdpa *v, u32 __user *argp)
{
	struct vdpa_device *vdpa = v->vdpa;

	if (copy_to_user(argp, &vdpa->nvqs, sizeof(vdpa->nvqs)))
		return -EFAULT;

	return 0;
}

/* After a successful return of ioctl the device must not process more
 * virtqueue descriptors. The device can answer to read or writes of config
 * fields as if it were not suspended. In particular, writing to "queue_enable"
 * with a value of 1 will not make the device start processing buffers.
 */
static long vhost_vdpa_suspend(struct vhost_vdpa *v)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;

	if (!ops->suspend)
		return -EOPNOTSUPP;

	return ops->suspend(vdpa);
}

static long vhost_vdpa_vring_ioctl(struct vhost_vdpa *v, unsigned int cmd,
				   void __user *argp)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	struct vdpa_vq_state vq_state;
	struct vdpa_callback cb;
	struct vhost_virtqueue *vq;
	struct vhost_vring_state s;
	u32 idx;
	long r;

	r = get_user(idx, (u32 __user *)argp);
	if (r < 0)
		return r;

	if (idx >= v->nvqs)
		return -ENOBUFS;

	idx = array_index_nospec(idx, v->nvqs);
	vq = &v->vqs[idx];

	switch (cmd) {
	case VHOST_VDPA_SET_VRING_ENABLE:
		if (copy_from_user(&s, argp, sizeof(s)))
			return -EFAULT;
		ops->set_vq_ready(vdpa, idx, s.num);
		return 0;
	case VHOST_VDPA_GET_VRING_GROUP:
		if (!ops->get_vq_group)
			return -EOPNOTSUPP;
		s.index = idx;
		s.num = ops->get_vq_group(vdpa, idx);
		if (s.num >= vdpa->ngroups)
			return -EIO;
		else if (copy_to_user(argp, &s, sizeof(s)))
			return -EFAULT;
		return 0;
	case VHOST_VDPA_SET_GROUP_ASID:
		if (copy_from_user(&s, argp, sizeof(s)))
			return -EFAULT;
		if (s.num >= vdpa->nas)
			return -EINVAL;
		if (!ops->set_group_asid)
			return -EOPNOTSUPP;
		return ops->set_group_asid(vdpa, idx, s.num);
	case VHOST_GET_VRING_BASE:
		r = ops->get_vq_state(v->vdpa, idx, &vq_state);
		if (r)
			return r;

		if (vhost_has_feature(vq, VIRTIO_F_RING_PACKED)) {
			vq->last_avail_idx = vq_state.packed.last_avail_idx |
					     (vq_state.packed.last_avail_counter << 15);
			vq->last_used_idx = vq_state.packed.last_used_idx |
					    (vq_state.packed.last_used_counter << 15);
		} else {
			vq->last_avail_idx = vq_state.split.avail_index;
		}
		break;
	}

	r = vhost_vring_ioctl(&v->vdev, cmd, argp);
	if (r)
		return r;

	switch (cmd) {
	case VHOST_SET_VRING_ADDR:
		if (ops->set_vq_address(vdpa, idx,
					(u64)(uintptr_t)vq->desc,
					(u64)(uintptr_t)vq->avail,
					(u64)(uintptr_t)vq->used))
			r = -EINVAL;
		break;

	case VHOST_SET_VRING_BASE:
		if (vhost_has_feature(vq, VIRTIO_F_RING_PACKED)) {
			vq_state.packed.last_avail_idx = vq->last_avail_idx & 0x7fff;
			vq_state.packed.last_avail_counter = !!(vq->last_avail_idx & 0x8000);
			vq_state.packed.last_used_idx = vq->last_used_idx & 0x7fff;
			vq_state.packed.last_used_counter = !!(vq->last_used_idx & 0x8000);
		} else {
			vq_state.split.avail_index = vq->last_avail_idx;
		}
		r = ops->set_vq_state(vdpa, idx, &vq_state);
		break;

	case VHOST_SET_VRING_CALL:
		if (vq->call_ctx.ctx) {
			cb.callback = vhost_vdpa_virtqueue_cb;
			cb.private = vq;
		} else {
			cb.callback = NULL;
			cb.private = NULL;
		}
		ops->set_vq_cb(vdpa, idx, &cb);
		vhost_vdpa_setup_vq_irq(v, idx);
		break;

	case VHOST_SET_VRING_NUM:
		ops->set_vq_num(vdpa, idx, vq->num);
		break;
	}

	return r;
}

static long vhost_vdpa_unlocked_ioctl(struct file *filep,
				      unsigned int cmd, unsigned long arg)
{
	struct vhost_vdpa *v = filep->private_data;
	struct vhost_dev *d = &v->vdev;
	void __user *argp = (void __user *)arg;
	u64 __user *featurep = argp;
	u64 features;
	long r = 0;

	if (cmd == VHOST_SET_BACKEND_FEATURES) {
		if (copy_from_user(&features, featurep, sizeof(features)))
			return -EFAULT;
		if (features & ~(VHOST_VDPA_BACKEND_FEATURES |
				 BIT_ULL(VHOST_BACKEND_F_SUSPEND)))
			return -EOPNOTSUPP;
		if ((features & BIT_ULL(VHOST_BACKEND_F_SUSPEND)) &&
		     !vhost_vdpa_can_suspend(v))
			return -EOPNOTSUPP;
		vhost_set_backend_features(&v->vdev, features);
		return 0;
	}

	mutex_lock(&d->mutex);

	switch (cmd) {
	case VHOST_VDPA_GET_DEVICE_ID:
		r = vhost_vdpa_get_device_id(v, argp);
		break;
	case VHOST_VDPA_GET_STATUS:
		r = vhost_vdpa_get_status(v, argp);
		break;
	case VHOST_VDPA_SET_STATUS:
		r = vhost_vdpa_set_status(v, argp);
		break;
	case VHOST_VDPA_GET_CONFIG:
		r = vhost_vdpa_get_config(v, argp);
		break;
	case VHOST_VDPA_SET_CONFIG:
		r = vhost_vdpa_set_config(v, argp);
		break;
	case VHOST_GET_FEATURES:
		r = vhost_vdpa_get_features(v, argp);
		break;
	case VHOST_SET_FEATURES:
		r = vhost_vdpa_set_features(v, argp);
		break;
	case VHOST_VDPA_GET_VRING_NUM:
		r = vhost_vdpa_get_vring_num(v, argp);
		break;
	case VHOST_VDPA_GET_GROUP_NUM:
		if (copy_to_user(argp, &v->vdpa->ngroups,
				 sizeof(v->vdpa->ngroups)))
			r = -EFAULT;
		break;
	case VHOST_VDPA_GET_AS_NUM:
		if (copy_to_user(argp, &v->vdpa->nas, sizeof(v->vdpa->nas)))
			r = -EFAULT;
		break;
	case VHOST_SET_LOG_BASE:
	case VHOST_SET_LOG_FD:
		r = -ENOIOCTLCMD;
		break;
	case VHOST_VDPA_SET_CONFIG_CALL:
		r = vhost_vdpa_set_config_call(v, argp);
		break;
	case VHOST_GET_BACKEND_FEATURES:
		features = VHOST_VDPA_BACKEND_FEATURES;
		if (vhost_vdpa_can_suspend(v))
			features |= BIT_ULL(VHOST_BACKEND_F_SUSPEND);
		if (copy_to_user(featurep, &features, sizeof(features)))
			r = -EFAULT;
		break;
	case VHOST_VDPA_GET_IOVA_RANGE:
		r = vhost_vdpa_get_iova_range(v, argp);
		break;
	case VHOST_VDPA_GET_CONFIG_SIZE:
		r = vhost_vdpa_get_config_size(v, argp);
		break;
	case VHOST_VDPA_GET_VQS_COUNT:
		r = vhost_vdpa_get_vqs_count(v, argp);
		break;
	case VHOST_VDPA_SUSPEND:
		r = vhost_vdpa_suspend(v);
		break;
	default:
		r = vhost_dev_ioctl(&v->vdev, cmd, argp);
		if (r == -ENOIOCTLCMD)
			r = vhost_vdpa_vring_ioctl(v, cmd, argp);
		break;
	}

	mutex_unlock(&d->mutex);
	return r;
}
static void vhost_vdpa_general_unmap(struct vhost_vdpa *v,
				     struct vhost_iotlb_map *map, u32 asid)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	if (ops->dma_map) {
		ops->dma_unmap(vdpa, asid, map->start, map->size);
	} else if (ops->set_map == NULL) {
		iommu_unmap(v->domain, map->start, map->size);
	}
}

static void vhost_vdpa_pa_unmap(struct vhost_vdpa *v, struct vhost_iotlb *iotlb,
				u64 start, u64 last, u32 asid)
{
	struct vhost_dev *dev = &v->vdev;
	struct vhost_iotlb_map *map;
	struct page *page;
	unsigned long pfn, pinned;

	while ((map = vhost_iotlb_itree_first(iotlb, start, last)) != NULL) {
		pinned = PFN_DOWN(map->size);
		for (pfn = PFN_DOWN(map->addr);
		     pinned > 0; pfn++, pinned--) {
			page = pfn_to_page(pfn);
			if (map->perm & VHOST_ACCESS_WO)
				set_page_dirty_lock(page);
			unpin_user_page(page);
		}
		atomic64_sub(PFN_DOWN(map->size), &dev->mm->pinned_vm);
		vhost_vdpa_general_unmap(v, map, asid);
		vhost_iotlb_map_free(iotlb, map);
	}
}

static void vhost_vdpa_va_unmap(struct vhost_vdpa *v, struct vhost_iotlb *iotlb,
				u64 start, u64 last, u32 asid)
{
	struct vhost_iotlb_map *map;
	struct vdpa_map_file *map_file;

	while ((map = vhost_iotlb_itree_first(iotlb, start, last)) != NULL) {
		map_file = (struct vdpa_map_file *)map->opaque;
		fput(map_file->file);
		kfree(map_file);
		vhost_vdpa_general_unmap(v, map, asid);
		vhost_iotlb_map_free(iotlb, map);
	}
}

static void vhost_vdpa_iotlb_unmap(struct vhost_vdpa *v,
				   struct vhost_iotlb *iotlb, u64 start,
				   u64 last, u32 asid)
{
	struct vdpa_device *vdpa = v->vdpa;

	if (vdpa->use_va)
		return vhost_vdpa_va_unmap(v, iotlb, start, last, asid);

	return vhost_vdpa_pa_unmap(v, iotlb, start, last, asid);
}

static int perm_to_iommu_flags(u32 perm)
{
	int flags = 0;

	switch (perm) {
	case VHOST_ACCESS_WO:
		flags |= IOMMU_WRITE;
		break;
	case VHOST_ACCESS_RO:
		flags |= IOMMU_READ;
		break;
	case VHOST_ACCESS_RW:
		flags |= (IOMMU_WRITE | IOMMU_READ);
		break;
	default:
		WARN(1, "invalidate vhost IOTLB permission\n");
		break;
	}

	return flags | IOMMU_CACHE;
}

static int vhost_vdpa_map(struct vhost_vdpa *v, struct vhost_iotlb *iotlb,
			  u64 iova, u64 size, u64 pa, u32 perm, void *opaque)
{
	struct vhost_dev *dev = &v->vdev;
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	u32 asid = iotlb_to_asid(iotlb);
	int r = 0;

	r = vhost_iotlb_add_range_ctx(iotlb, iova, iova + size - 1,
				      pa, perm, opaque);
	if (r)
		return r;

	if (ops->dma_map) {
		r = ops->dma_map(vdpa, asid, iova, size, pa, perm, opaque);
	} else if (ops->set_map) {
		if (!v->in_batch)
			r = ops->set_map(vdpa, asid, iotlb);
	} else {
		r = iommu_map(v->domain, iova, pa, size,
			      perm_to_iommu_flags(perm));
	}
	if (r) {
		vhost_iotlb_del_range(iotlb, iova, iova + size - 1);
		return r;
	}

	if (!vdpa->use_va)
		atomic64_add(PFN_DOWN(size), &dev->mm->pinned_vm);

	return 0;
}

static void vhost_vdpa_unmap(struct vhost_vdpa *v,
			     struct vhost_iotlb *iotlb,
			     u64 iova, u64 size)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	u32 asid = iotlb_to_asid(iotlb);

	vhost_vdpa_iotlb_unmap(v, iotlb, iova, iova + size - 1, asid);

	if (ops->set_map) {
		if (!v->in_batch)
			ops->set_map(vdpa, asid, iotlb);
	}

}

static int vhost_vdpa_va_map(struct vhost_vdpa *v,
			     struct vhost_iotlb *iotlb,
			     u64 iova, u64 size, u64 uaddr, u32 perm)
{
	struct vhost_dev *dev = &v->vdev;
	u64 offset, map_size, map_iova = iova;
	struct vdpa_map_file *map_file;
	struct vm_area_struct *vma;
	int ret = 0;

	mmap_read_lock(dev->mm);

	while (size) {
		vma = find_vma(dev->mm, uaddr);
		if (!vma) {
			ret = -EINVAL;
			break;
		}
		map_size = min(size, vma->vm_end - uaddr);
		if (!(vma->vm_file && (vma->vm_flags & VM_SHARED) &&
			!(vma->vm_flags & (VM_IO | VM_PFNMAP))))
			goto next;

		map_file = kzalloc(sizeof(*map_file), GFP_KERNEL);
		if (!map_file) {
			ret = -ENOMEM;
			break;
		}
		offset = (vma->vm_pgoff << PAGE_SHIFT) + uaddr - vma->vm_start;
		map_file->offset = offset;
		map_file->file = get_file(vma->vm_file);
		ret = vhost_vdpa_map(v, iotlb, map_iova, map_size, uaddr,
				     perm, map_file);
		if (ret) {
			fput(map_file->file);
			kfree(map_file);
			break;
		}
next:
		size -= map_size;
		uaddr += map_size;
		map_iova += map_size;
	}
	if (ret)
		vhost_vdpa_unmap(v, iotlb, iova, map_iova - iova);

	mmap_read_unlock(dev->mm);

	return ret;
}

static int vhost_vdpa_pa_map(struct vhost_vdpa *v,
			     struct vhost_iotlb *iotlb,
			     u64 iova, u64 size, u64 uaddr, u32 perm)
{
	struct vhost_dev *dev = &v->vdev;
	struct page **page_list;
	unsigned long list_size = PAGE_SIZE / sizeof(struct page *);
	unsigned int gup_flags = FOLL_LONGTERM;
	unsigned long npages, cur_base, map_pfn, last_pfn = 0;
	unsigned long lock_limit, sz2pin, nchunks, i;
	u64 start = iova;
	long pinned;
	int ret = 0;

	/* Limit the use of memory for bookkeeping */
	page_list = (struct page **) __get_free_page(GFP_KERNEL);
	if (!page_list)
		return -ENOMEM;

	if (perm & VHOST_ACCESS_WO)
		gup_flags |= FOLL_WRITE;

	npages = PFN_UP(size + (iova & ~PAGE_MASK));
	if (!npages) {
		ret = -EINVAL;
		goto free;
	}

	mmap_read_lock(dev->mm);

	lock_limit = PFN_DOWN(rlimit(RLIMIT_MEMLOCK));
	if (npages + atomic64_read(&dev->mm->pinned_vm) > lock_limit) {
		ret = -ENOMEM;
		goto unlock;
	}

	cur_base = uaddr & PAGE_MASK;
	iova &= PAGE_MASK;
	nchunks = 0;

	while (npages) {
		sz2pin = min_t(unsigned long, npages, list_size);
		pinned = pin_user_pages(cur_base, sz2pin,
					gup_flags, page_list, NULL);
		if (sz2pin != pinned) {
			if (pinned < 0) {
				ret = pinned;
			} else {
				unpin_user_pages(page_list, pinned);
				ret = -ENOMEM;
			}
			goto out;
		}
		nchunks++;

		if (!last_pfn)
			map_pfn = page_to_pfn(page_list[0]);

		for (i = 0; i < pinned; i++) {
			unsigned long this_pfn = page_to_pfn(page_list[i]);
			u64 csize;

			if (last_pfn && (this_pfn != last_pfn + 1)) {
				/* Pin a contiguous chunk of memory */
				csize = PFN_PHYS(last_pfn - map_pfn + 1);
				ret = vhost_vdpa_map(v, iotlb, iova, csize,
						     PFN_PHYS(map_pfn),
						     perm, NULL);
				if (ret) {
					/*
					 * Unpin the pages that are left unmapped
					 * from this point on in the current
					 * page_list. The remaining outstanding
					 * ones which may stride across several
					 * chunks will be covered in the common
					 * error path subsequently.
					 */
					unpin_user_pages(&page_list[i],
							 pinned - i);
					goto out;
				}

				map_pfn = this_pfn;
				iova += csize;
				nchunks = 0;
			}

			last_pfn = this_pfn;
		}

		cur_base += PFN_PHYS(pinned);
		npages -= pinned;
	}

	/* Pin the rest chunk */
	ret = vhost_vdpa_map(v, iotlb, iova, PFN_PHYS(last_pfn - map_pfn + 1),
			     PFN_PHYS(map_pfn), perm, NULL);
out:
	if (ret) {
		if (nchunks) {
			unsigned long pfn;

			/*
			 * Unpin the outstanding pages which are yet to be
			 * mapped but haven't due to vdpa_map() or
			 * pin_user_pages() failure.
			 *
			 * Mapped pages are accounted in vdpa_map(), hence
			 * the corresponding unpinning will be handled by
			 * vdpa_unmap().
			 */
			WARN_ON(!last_pfn);
			for (pfn = map_pfn; pfn <= last_pfn; pfn++)
				unpin_user_page(pfn_to_page(pfn));
		}
		vhost_vdpa_unmap(v, iotlb, start, size);
	}
unlock:
	mmap_read_unlock(dev->mm);
free:
	free_page((unsigned long)page_list);
	return ret;

}

static int vhost_vdpa_process_iotlb_update(struct vhost_vdpa *v,
					   struct vhost_iotlb *iotlb,
					   struct vhost_iotlb_msg *msg)
{
	struct vdpa_device *vdpa = v->vdpa;

	if (msg->iova < v->range.first || !msg->size ||
	    msg->iova > U64_MAX - msg->size + 1 ||
	    msg->iova + msg->size - 1 > v->range.last)
		return -EINVAL;

	if (vhost_iotlb_itree_first(iotlb, msg->iova,
				    msg->iova + msg->size - 1))
		return -EEXIST;

	if (vdpa->use_va)
		return vhost_vdpa_va_map(v, iotlb, msg->iova, msg->size,
					 msg->uaddr, msg->perm);

	return vhost_vdpa_pa_map(v, iotlb, msg->iova, msg->size, msg->uaddr,
				 msg->perm);
}

static int vhost_vdpa_process_iotlb_msg(struct vhost_dev *dev, u32 asid,
					struct vhost_iotlb_msg *msg)
{
	struct vhost_vdpa *v = container_of(dev, struct vhost_vdpa, vdev);
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	struct vhost_iotlb *iotlb = NULL;
	struct vhost_vdpa_as *as = NULL;
	int r = 0;

	mutex_lock(&dev->mutex);

	r = vhost_dev_check_owner(dev);
	if (r)
		goto unlock;

	if (msg->type == VHOST_IOTLB_UPDATE ||
	    msg->type == VHOST_IOTLB_BATCH_BEGIN) {
		as = vhost_vdpa_find_alloc_as(v, asid);
		if (!as) {
			dev_err(&v->dev, "can't find and alloc asid %d\n",
				asid);
			r = -EINVAL;
			goto unlock;
		}
		iotlb = &as->iotlb;
	} else
		iotlb = asid_to_iotlb(v, asid);

	if ((v->in_batch && v->batch_asid != asid) || !iotlb) {
		if (v->in_batch && v->batch_asid != asid) {
			dev_info(&v->dev, "batch id %d asid %d\n",
				 v->batch_asid, asid);
		}
		if (!iotlb)
			dev_err(&v->dev, "no iotlb for asid %d\n", asid);
		r = -EINVAL;
		goto unlock;
	}

	switch (msg->type) {
	case VHOST_IOTLB_UPDATE:
		r = vhost_vdpa_process_iotlb_update(v, iotlb, msg);
		break;
	case VHOST_IOTLB_INVALIDATE:
		vhost_vdpa_unmap(v, iotlb, msg->iova, msg->size);
		break;
	case VHOST_IOTLB_BATCH_BEGIN:
		v->batch_asid = asid;
		v->in_batch = true;
		break;
	case VHOST_IOTLB_BATCH_END:
		if (v->in_batch && ops->set_map)
			ops->set_map(vdpa, asid, iotlb);
		v->in_batch = false;
		break;
	default:
		r = -EINVAL;
		break;
	}
unlock:
	mutex_unlock(&dev->mutex);

	return r;
}

static ssize_t vhost_vdpa_chr_write_iter(struct kiocb *iocb,
					 struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct vhost_vdpa *v = file->private_data;
	struct vhost_dev *dev = &v->vdev;

	return vhost_chr_write_iter(dev, from);
}

static int vhost_vdpa_alloc_domain(struct vhost_vdpa *v)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	struct device *dma_dev = vdpa_get_dma_dev(vdpa);
	struct bus_type *bus;
	int ret;

	/* Device want to do DMA by itself */
	if (ops->set_map || ops->dma_map)
		return 0;

	bus = dma_dev->bus;
	if (!bus)
		return -EFAULT;

	if (!device_iommu_capable(dma_dev, IOMMU_CAP_CACHE_COHERENCY))
		return -ENOTSUPP;

	v->domain = iommu_domain_alloc(bus);
	if (!v->domain)
		return -EIO;

	ret = iommu_attach_device(v->domain, dma_dev);
	if (ret)
		goto err_attach;

	return 0;

err_attach:
	iommu_domain_free(v->domain);
	v->domain = NULL;
	return ret;
}

static void vhost_vdpa_free_domain(struct vhost_vdpa *v)
{
	struct vdpa_device *vdpa = v->vdpa;
	struct device *dma_dev = vdpa_get_dma_dev(vdpa);

	if (v->domain) {
		iommu_detach_device(v->domain, dma_dev);
		iommu_domain_free(v->domain);
	}

	v->domain = NULL;
}

static void vhost_vdpa_set_iova_range(struct vhost_vdpa *v)
{
	struct vdpa_iova_range *range = &v->range;
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;

	if (ops->get_iova_range) {
		*range = ops->get_iova_range(vdpa);
	} else if (v->domain && v->domain->geometry.force_aperture) {
		range->first = v->domain->geometry.aperture_start;
		range->last = v->domain->geometry.aperture_end;
	} else {
		range->first = 0;
		range->last = ULLONG_MAX;
	}
}

static void vhost_vdpa_cleanup(struct vhost_vdpa *v)
{
	struct vhost_vdpa_as *as;
	u32 asid;

	for (asid = 0; asid < v->vdpa->nas; asid++) {
		as = asid_to_as(v, asid);
		if (as)
			vhost_vdpa_remove_as(v, asid);
	}

	vhost_vdpa_free_domain(v);
	vhost_dev_cleanup(&v->vdev);
	kfree(v->vdev.vqs);
}

static int vhost_vdpa_open(struct inode *inode, struct file *filep)
{
	struct vhost_vdpa *v;
	struct vhost_dev *dev;
	struct vhost_virtqueue **vqs;
	int r, opened;
	u32 i, nvqs;

	v = container_of(inode->i_cdev, struct vhost_vdpa, cdev);

	opened = atomic_cmpxchg(&v->opened, 0, 1);
	if (opened)
		return -EBUSY;

	nvqs = v->nvqs;
	r = vhost_vdpa_reset(v);
	if (r)
		goto err;

	vqs = kmalloc_array(nvqs, sizeof(*vqs), GFP_KERNEL);
	if (!vqs) {
		r = -ENOMEM;
		goto err;
	}

	dev = &v->vdev;
	for (i = 0; i < nvqs; i++) {
		vqs[i] = &v->vqs[i];
		vqs[i]->handle_kick = handle_vq_kick;
	}
	vhost_dev_init(dev, vqs, nvqs, 0, 0, 0, false,
		       vhost_vdpa_process_iotlb_msg);

	r = vhost_vdpa_alloc_domain(v);
	if (r)
		goto err_alloc_domain;

	vhost_vdpa_set_iova_range(v);

	filep->private_data = v;

	return 0;

err_alloc_domain:
	vhost_vdpa_cleanup(v);
err:
	atomic_dec(&v->opened);
	return r;
}

static void vhost_vdpa_clean_irq(struct vhost_vdpa *v)
{
	u32 i;

	for (i = 0; i < v->nvqs; i++)
		vhost_vdpa_unsetup_vq_irq(v, i);
}

static int vhost_vdpa_release(struct inode *inode, struct file *filep)
{
	struct vhost_vdpa *v = filep->private_data;
	struct vhost_dev *d = &v->vdev;

	mutex_lock(&d->mutex);
	filep->private_data = NULL;
	vhost_vdpa_clean_irq(v);
	vhost_vdpa_reset(v);
	vhost_dev_stop(&v->vdev);
	vhost_vdpa_config_put(v);
	vhost_vdpa_cleanup(v);
	mutex_unlock(&d->mutex);

	atomic_dec(&v->opened);
	complete(&v->completion);

	return 0;
}

#ifdef CONFIG_MMU
static vm_fault_t vhost_vdpa_fault(struct vm_fault *vmf)
{
	struct vhost_vdpa *v = vmf->vma->vm_file->private_data;
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	struct vdpa_notification_area notify;
	struct vm_area_struct *vma = vmf->vma;
	u16 index = vma->vm_pgoff;

	notify = ops->get_vq_notification(vdpa, index);

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (remap_pfn_range(vma, vmf->address & PAGE_MASK,
			    PFN_DOWN(notify.addr), PAGE_SIZE,
			    vma->vm_page_prot))
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}

static const struct vm_operations_struct vhost_vdpa_vm_ops = {
	.fault = vhost_vdpa_fault,
};

static int vhost_vdpa_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vhost_vdpa *v = vma->vm_file->private_data;
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	struct vdpa_notification_area notify;
	unsigned long index = vma->vm_pgoff;

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;
	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;
	if (vma->vm_flags & VM_READ)
		return -EINVAL;
	if (index > 65535)
		return -EINVAL;
	if (!ops->get_vq_notification)
		return -ENOTSUPP;

	/* To be safe and easily modelled by userspace, We only
	 * support the doorbell which sits on the page boundary and
	 * does not share the page with other registers.
	 */
	notify = ops->get_vq_notification(vdpa, index);
	if (notify.addr & (PAGE_SIZE - 1))
		return -EINVAL;
	if (vma->vm_end - vma->vm_start != notify.size)
		return -ENOTSUPP;

	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_ops = &vhost_vdpa_vm_ops;
	return 0;
}
#endif /* CONFIG_MMU */

static const struct file_operations vhost_vdpa_fops = {
	.owner		= THIS_MODULE,
	.open		= vhost_vdpa_open,
	.release	= vhost_vdpa_release,
	.write_iter	= vhost_vdpa_chr_write_iter,
	.unlocked_ioctl	= vhost_vdpa_unlocked_ioctl,
#ifdef CONFIG_MMU
	.mmap		= vhost_vdpa_mmap,
#endif /* CONFIG_MMU */
	.compat_ioctl	= compat_ptr_ioctl,
};

static void vhost_vdpa_release_dev(struct device *device)
{
	struct vhost_vdpa *v =
	       container_of(device, struct vhost_vdpa, dev);

	ida_simple_remove(&vhost_vdpa_ida, v->minor);
	kfree(v->vqs);
	kfree(v);
}

static int vhost_vdpa_probe(struct vdpa_device *vdpa)
{
	const struct vdpa_config_ops *ops = vdpa->config;
	struct vhost_vdpa *v;
	int minor;
	int i, r;

	/* We can't support platform IOMMU device with more than 1
	 * group or as
	 */
	if (!ops->set_map && !ops->dma_map &&
	    (vdpa->ngroups > 1 || vdpa->nas > 1))
		return -EOPNOTSUPP;

	v = kzalloc(sizeof(*v), GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!v)
		return -ENOMEM;

	minor = ida_simple_get(&vhost_vdpa_ida, 0,
			       VHOST_VDPA_DEV_MAX, GFP_KERNEL);
	if (minor < 0) {
		kfree(v);
		return minor;
	}

	atomic_set(&v->opened, 0);
	v->minor = minor;
	v->vdpa = vdpa;
	v->nvqs = vdpa->nvqs;
	v->virtio_id = ops->get_device_id(vdpa);

	device_initialize(&v->dev);
	v->dev.release = vhost_vdpa_release_dev;
	v->dev.parent = &vdpa->dev;
	v->dev.devt = MKDEV(MAJOR(vhost_vdpa_major), minor);
	v->vqs = kmalloc_array(v->nvqs, sizeof(struct vhost_virtqueue),
			       GFP_KERNEL);
	if (!v->vqs) {
		r = -ENOMEM;
		goto err;
	}

	r = dev_set_name(&v->dev, "vhost-vdpa-%u", minor);
	if (r)
		goto err;

	cdev_init(&v->cdev, &vhost_vdpa_fops);
	v->cdev.owner = THIS_MODULE;

	r = cdev_device_add(&v->cdev, &v->dev);
	if (r)
		goto err;

	init_completion(&v->completion);
	vdpa_set_drvdata(vdpa, v);

	for (i = 0; i < VHOST_VDPA_IOTLB_BUCKETS; i++)
		INIT_HLIST_HEAD(&v->as[i]);

	return 0;

err:
	put_device(&v->dev);
	ida_simple_remove(&vhost_vdpa_ida, v->minor);
	return r;
}

static void vhost_vdpa_remove(struct vdpa_device *vdpa)
{
	struct vhost_vdpa *v = vdpa_get_drvdata(vdpa);
	int opened;

	cdev_device_del(&v->cdev, &v->dev);

	do {
		opened = atomic_cmpxchg(&v->opened, 0, 1);
		if (!opened)
			break;
		wait_for_completion(&v->completion);
	} while (1);

	put_device(&v->dev);
}

static struct vdpa_driver vhost_vdpa_driver = {
	.driver = {
		.name	= "vhost_vdpa",
	},
	.probe	= vhost_vdpa_probe,
	.remove	= vhost_vdpa_remove,
};

static int __init vhost_vdpa_init(void)
{
	int r;

	r = alloc_chrdev_region(&vhost_vdpa_major, 0, VHOST_VDPA_DEV_MAX,
				"vhost-vdpa");
	if (r)
		goto err_alloc_chrdev;

	r = vdpa_register_driver(&vhost_vdpa_driver);
	if (r)
		goto err_vdpa_register_driver;

	return 0;

err_vdpa_register_driver:
	unregister_chrdev_region(vhost_vdpa_major, VHOST_VDPA_DEV_MAX);
err_alloc_chrdev:
	return r;
}
module_init(vhost_vdpa_init);

static void __exit vhost_vdpa_exit(void)
{
	vdpa_unregister_driver(&vhost_vdpa_driver);
	unregister_chrdev_region(vhost_vdpa_major, VHOST_VDPA_DEV_MAX);
}
module_exit(vhost_vdpa_exit);

MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("vDPA-based vhost backend for virtio");
