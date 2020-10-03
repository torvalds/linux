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
#include <linux/iommu.h>
#include <linux/uuid.h>
#include <linux/vdpa.h>
#include <linux/nospec.h>
#include <linux/vhost.h>
#include <linux/virtio_net.h>
#include <linux/kernel.h>

#include "vhost.h"

enum {
	VHOST_VDPA_BACKEND_FEATURES =
	(1ULL << VHOST_BACKEND_F_IOTLB_MSG_V2) |
	(1ULL << VHOST_BACKEND_F_IOTLB_BATCH),
};

#define VHOST_VDPA_DEV_MAX (1U << MINORBITS)

struct vhost_vdpa {
	struct vhost_dev vdev;
	struct iommu_domain *domain;
	struct vhost_virtqueue *vqs;
	struct completion completion;
	struct vdpa_device *vdpa;
	struct device dev;
	struct cdev cdev;
	atomic_t opened;
	int nvqs;
	int virtio_id;
	int minor;
	struct eventfd_ctx *config_ctx;
	int in_batch;
};

static DEFINE_IDA(vhost_vdpa_ida);

static dev_t vhost_vdpa_major;

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
	spin_lock(&vq->call_ctx.ctx_lock);
	irq_bypass_unregister_producer(&vq->call_ctx.producer);
	if (!vq->call_ctx.ctx || irq < 0) {
		spin_unlock(&vq->call_ctx.ctx_lock);
		return;
	}

	vq->call_ctx.producer.token = vq->call_ctx.ctx;
	vq->call_ctx.producer.irq = irq;
	ret = irq_bypass_register_producer(&vq->call_ctx.producer);
	spin_unlock(&vq->call_ctx.ctx_lock);
}

static void vhost_vdpa_unsetup_vq_irq(struct vhost_vdpa *v, u16 qid)
{
	struct vhost_virtqueue *vq = &v->vqs[qid];

	spin_lock(&vq->call_ctx.ctx_lock);
	irq_bypass_unregister_producer(&vq->call_ctx.producer);
	spin_unlock(&vq->call_ctx.ctx_lock);
}

static void vhost_vdpa_reset(struct vhost_vdpa *v)
{
	struct vdpa_device *vdpa = v->vdpa;

	vdpa_reset(vdpa);
	v->in_batch = 0;
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
	int nvqs = v->nvqs;
	u16 i;

	if (copy_from_user(&status, statusp, sizeof(status)))
		return -EFAULT;

	status_old = ops->get_status(vdpa);

	/*
	 * Userspace shouldn't remove status bits unless reset the
	 * status to 0.
	 */
	if (status != 0 && (ops->get_status(vdpa) & ~status) != 0)
		return -EINVAL;

	ops->set_status(vdpa, status);

	if ((status & VIRTIO_CONFIG_S_DRIVER_OK) && !(status_old & VIRTIO_CONFIG_S_DRIVER_OK))
		for (i = 0; i < nvqs; i++)
			vhost_vdpa_setup_vq_irq(v, i);

	if ((status_old & VIRTIO_CONFIG_S_DRIVER_OK) && !(status & VIRTIO_CONFIG_S_DRIVER_OK))
		for (i = 0; i < nvqs; i++)
			vhost_vdpa_unsetup_vq_irq(v, i);

	return 0;
}

static int vhost_vdpa_config_validate(struct vhost_vdpa *v,
				      struct vhost_vdpa_config *c)
{
	long size = 0;

	switch (v->virtio_id) {
	case VIRTIO_ID_NET:
		size = sizeof(struct virtio_net_config);
		break;
	}

	if (c->len == 0)
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
	const struct vdpa_config_ops *ops = vdpa->config;
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

	if (copy_from_user(buf, c->buf, config.len)) {
		kvfree(buf);
		return -EFAULT;
	}

	ops->set_config(vdpa, config.off, buf, config.len);

	kvfree(buf);
	return 0;
}

static long vhost_vdpa_get_features(struct vhost_vdpa *v, u64 __user *featurep)
{
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	u64 features;

	features = ops->get_features(vdpa);

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
	if (v->config_ctx)
		eventfd_ctx_put(v->config_ctx);
}

static long vhost_vdpa_set_config_call(struct vhost_vdpa *v, u32 __user *argp)
{
	struct vdpa_callback cb;
	int fd;
	struct eventfd_ctx *ctx;

	cb.callback = vhost_vdpa_config_cb;
	cb.private = v->vdpa;
	if (copy_from_user(&fd, argp, sizeof(fd)))
		return  -EFAULT;

	ctx = fd == VHOST_FILE_UNBIND ? NULL : eventfd_ctx_fdget(fd);
	swap(ctx, v->config_ctx);

	if (!IS_ERR_OR_NULL(ctx))
		eventfd_ctx_put(ctx);

	if (IS_ERR(v->config_ctx))
		return PTR_ERR(v->config_ctx);

	v->vdpa->config->set_config_cb(v->vdpa, &cb);

	return 0;
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
	case VHOST_GET_VRING_BASE:
		r = ops->get_vq_state(v->vdpa, idx, &vq_state);
		if (r)
			return r;

		vq->last_avail_idx = vq_state.avail_index;
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
		vq_state.avail_index = vq->last_avail_idx;
		if (ops->set_vq_state(vdpa, idx, &vq_state))
			r = -EINVAL;
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
	long r;

	if (cmd == VHOST_SET_BACKEND_FEATURES) {
		r = copy_from_user(&features, featurep, sizeof(features));
		if (r)
			return r;
		if (features & ~VHOST_VDPA_BACKEND_FEATURES)
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
	case VHOST_SET_LOG_BASE:
	case VHOST_SET_LOG_FD:
		r = -ENOIOCTLCMD;
		break;
	case VHOST_VDPA_SET_CONFIG_CALL:
		r = vhost_vdpa_set_config_call(v, argp);
		break;
	case VHOST_GET_BACKEND_FEATURES:
		features = VHOST_VDPA_BACKEND_FEATURES;
		r = copy_to_user(featurep, &features, sizeof(features));
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

static void vhost_vdpa_iotlb_unmap(struct vhost_vdpa *v, u64 start, u64 last)
{
	struct vhost_dev *dev = &v->vdev;
	struct vhost_iotlb *iotlb = dev->iotlb;
	struct vhost_iotlb_map *map;
	struct page *page;
	unsigned long pfn, pinned;

	while ((map = vhost_iotlb_itree_first(iotlb, start, last)) != NULL) {
		pinned = map->size >> PAGE_SHIFT;
		for (pfn = map->addr >> PAGE_SHIFT;
		     pinned > 0; pfn++, pinned--) {
			page = pfn_to_page(pfn);
			if (map->perm & VHOST_ACCESS_WO)
				set_page_dirty_lock(page);
			unpin_user_page(page);
		}
		atomic64_sub(map->size >> PAGE_SHIFT, &dev->mm->pinned_vm);
		vhost_iotlb_map_free(iotlb, map);
	}
}

static void vhost_vdpa_iotlb_free(struct vhost_vdpa *v)
{
	struct vhost_dev *dev = &v->vdev;

	vhost_vdpa_iotlb_unmap(v, 0ULL, 0ULL - 1);
	kfree(dev->iotlb);
	dev->iotlb = NULL;
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

static int vhost_vdpa_map(struct vhost_vdpa *v,
			  u64 iova, u64 size, u64 pa, u32 perm)
{
	struct vhost_dev *dev = &v->vdev;
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	int r = 0;

	r = vhost_iotlb_add_range(dev->iotlb, iova, iova + size - 1,
				  pa, perm);
	if (r)
		return r;

	if (ops->dma_map) {
		r = ops->dma_map(vdpa, iova, size, pa, perm);
	} else if (ops->set_map) {
		if (!v->in_batch)
			r = ops->set_map(vdpa, dev->iotlb);
	} else {
		r = iommu_map(v->domain, iova, pa, size,
			      perm_to_iommu_flags(perm));
	}

	if (r)
		vhost_iotlb_del_range(dev->iotlb, iova, iova + size - 1);

	return r;
}

static void vhost_vdpa_unmap(struct vhost_vdpa *v, u64 iova, u64 size)
{
	struct vhost_dev *dev = &v->vdev;
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;

	vhost_vdpa_iotlb_unmap(v, iova, iova + size - 1);

	if (ops->dma_map) {
		ops->dma_unmap(vdpa, iova, size);
	} else if (ops->set_map) {
		if (!v->in_batch)
			ops->set_map(vdpa, dev->iotlb);
	} else {
		iommu_unmap(v->domain, iova, size);
	}
}

static int vhost_vdpa_process_iotlb_update(struct vhost_vdpa *v,
					   struct vhost_iotlb_msg *msg)
{
	struct vhost_dev *dev = &v->vdev;
	struct vhost_iotlb *iotlb = dev->iotlb;
	struct page **page_list;
	unsigned long list_size = PAGE_SIZE / sizeof(struct page *);
	unsigned int gup_flags = FOLL_LONGTERM;
	unsigned long npages, cur_base, map_pfn, last_pfn = 0;
	unsigned long locked, lock_limit, pinned, i;
	u64 iova = msg->iova;
	int ret = 0;

	if (vhost_iotlb_itree_first(iotlb, msg->iova,
				    msg->iova + msg->size - 1))
		return -EEXIST;

	page_list = (struct page **) __get_free_page(GFP_KERNEL);
	if (!page_list)
		return -ENOMEM;

	if (msg->perm & VHOST_ACCESS_WO)
		gup_flags |= FOLL_WRITE;

	npages = PAGE_ALIGN(msg->size + (iova & ~PAGE_MASK)) >> PAGE_SHIFT;
	if (!npages)
		return -EINVAL;

	mmap_read_lock(dev->mm);

	locked = atomic64_add_return(npages, &dev->mm->pinned_vm);
	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	if (locked > lock_limit) {
		ret = -ENOMEM;
		goto out;
	}

	cur_base = msg->uaddr & PAGE_MASK;
	iova &= PAGE_MASK;

	while (npages) {
		pinned = min_t(unsigned long, npages, list_size);
		ret = pin_user_pages(cur_base, pinned,
				     gup_flags, page_list, NULL);
		if (ret != pinned)
			goto out;

		if (!last_pfn)
			map_pfn = page_to_pfn(page_list[0]);

		for (i = 0; i < ret; i++) {
			unsigned long this_pfn = page_to_pfn(page_list[i]);
			u64 csize;

			if (last_pfn && (this_pfn != last_pfn + 1)) {
				/* Pin a contiguous chunk of memory */
				csize = (last_pfn - map_pfn + 1) << PAGE_SHIFT;
				if (vhost_vdpa_map(v, iova, csize,
						   map_pfn << PAGE_SHIFT,
						   msg->perm))
					goto out;
				map_pfn = this_pfn;
				iova += csize;
			}

			last_pfn = this_pfn;
		}

		cur_base += ret << PAGE_SHIFT;
		npages -= ret;
	}

	/* Pin the rest chunk */
	ret = vhost_vdpa_map(v, iova, (last_pfn - map_pfn + 1) << PAGE_SHIFT,
			     map_pfn << PAGE_SHIFT, msg->perm);
out:
	if (ret) {
		vhost_vdpa_unmap(v, msg->iova, msg->size);
		atomic64_sub(npages, &dev->mm->pinned_vm);
	}
	mmap_read_unlock(dev->mm);
	free_page((unsigned long)page_list);
	return ret;
}

static int vhost_vdpa_process_iotlb_msg(struct vhost_dev *dev,
					struct vhost_iotlb_msg *msg)
{
	struct vhost_vdpa *v = container_of(dev, struct vhost_vdpa, vdev);
	struct vdpa_device *vdpa = v->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	int r = 0;

	r = vhost_dev_check_owner(dev);
	if (r)
		return r;

	switch (msg->type) {
	case VHOST_IOTLB_UPDATE:
		r = vhost_vdpa_process_iotlb_update(v, msg);
		break;
	case VHOST_IOTLB_INVALIDATE:
		vhost_vdpa_unmap(v, msg->iova, msg->size);
		break;
	case VHOST_IOTLB_BATCH_BEGIN:
		v->in_batch = true;
		break;
	case VHOST_IOTLB_BATCH_END:
		if (v->in_batch && ops->set_map)
			ops->set_map(vdpa, dev->iotlb);
		v->in_batch = false;
		break;
	default:
		r = -EINVAL;
		break;
	}

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

	if (!iommu_capable(bus, IOMMU_CAP_CACHE_COHERENCY))
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

static int vhost_vdpa_open(struct inode *inode, struct file *filep)
{
	struct vhost_vdpa *v;
	struct vhost_dev *dev;
	struct vhost_virtqueue **vqs;
	int nvqs, i, r, opened;

	v = container_of(inode->i_cdev, struct vhost_vdpa, cdev);

	opened = atomic_cmpxchg(&v->opened, 0, 1);
	if (opened)
		return -EBUSY;

	nvqs = v->nvqs;
	vhost_vdpa_reset(v);

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

	dev->iotlb = vhost_iotlb_alloc(0, 0);
	if (!dev->iotlb) {
		r = -ENOMEM;
		goto err_init_iotlb;
	}

	r = vhost_vdpa_alloc_domain(v);
	if (r)
		goto err_init_iotlb;

	filep->private_data = v;

	return 0;

err_init_iotlb:
	vhost_dev_cleanup(&v->vdev);
	kfree(vqs);
err:
	atomic_dec(&v->opened);
	return r;
}

static void vhost_vdpa_clean_irq(struct vhost_vdpa *v)
{
	struct vhost_virtqueue *vq;
	int i;

	for (i = 0; i < v->nvqs; i++) {
		vq = &v->vqs[i];
		if (vq->call_ctx.producer.irq)
			irq_bypass_unregister_producer(&vq->call_ctx.producer);
	}
}

static int vhost_vdpa_release(struct inode *inode, struct file *filep)
{
	struct vhost_vdpa *v = filep->private_data;
	struct vhost_dev *d = &v->vdev;

	mutex_lock(&d->mutex);
	filep->private_data = NULL;
	vhost_vdpa_reset(v);
	vhost_dev_stop(&v->vdev);
	vhost_vdpa_iotlb_free(v);
	vhost_vdpa_free_domain(v);
	vhost_vdpa_config_put(v);
	vhost_vdpa_clean_irq(v);
	vhost_dev_cleanup(&v->vdev);
	kfree(v->vdev.vqs);
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
			    notify.addr >> PAGE_SHIFT, PAGE_SIZE,
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
	int r;

	/* Currently, we only accept the network devices. */
	if (ops->get_device_id(vdpa) != VIRTIO_ID_NET)
		return -ENOTSUPP;

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

	return 0;

err:
	put_device(&v->dev);
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
