// SPDX-License-Identifier: GPL-2.0-only
/*
 * VIRTIO based driver for vDPA device
 *
 * Copyright (c) 2020, Red Hat. All rights reserved.
 *     Author: Jason Wang <jasowang@redhat.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/group_cpus.h>
#include <linux/virtio.h>
#include <linux/vdpa.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>

#define MOD_VERSION  "0.1"
#define MOD_AUTHOR   "Jason Wang <jasowang@redhat.com>"
#define MOD_DESC     "vDPA bus driver for virtio devices"
#define MOD_LICENSE  "GPL v2"

struct virtio_vdpa_device {
	struct virtio_device vdev;
	struct vdpa_device *vdpa;
	u64 features;

	/* The lock to protect virtqueue list */
	spinlock_t lock;
	/* List of virtio_vdpa_vq_info */
	struct list_head virtqueues;
};

struct virtio_vdpa_vq_info {
	/* the actual virtqueue */
	struct virtqueue *vq;

	/* the list node for the virtqueues list */
	struct list_head node;
};

static inline struct virtio_vdpa_device *
to_virtio_vdpa_device(struct virtio_device *dev)
{
	return container_of(dev, struct virtio_vdpa_device, vdev);
}

static struct vdpa_device *vd_get_vdpa(struct virtio_device *vdev)
{
	return to_virtio_vdpa_device(vdev)->vdpa;
}

static void virtio_vdpa_get(struct virtio_device *vdev, unsigned int offset,
			    void *buf, unsigned int len)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);

	vdpa_get_config(vdpa, offset, buf, len);
}

static void virtio_vdpa_set(struct virtio_device *vdev, unsigned int offset,
			    const void *buf, unsigned int len)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);

	vdpa_set_config(vdpa, offset, buf, len);
}

static u32 virtio_vdpa_generation(struct virtio_device *vdev)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);
	const struct vdpa_config_ops *ops = vdpa->config;

	if (ops->get_generation)
		return ops->get_generation(vdpa);

	return 0;
}

static u8 virtio_vdpa_get_status(struct virtio_device *vdev)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);
	const struct vdpa_config_ops *ops = vdpa->config;

	return ops->get_status(vdpa);
}

static void virtio_vdpa_set_status(struct virtio_device *vdev, u8 status)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);

	return vdpa_set_status(vdpa, status);
}

static void virtio_vdpa_reset(struct virtio_device *vdev)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);

	vdpa_reset(vdpa, 0);
}

static bool virtio_vdpa_notify(struct virtqueue *vq)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vq->vdev);
	const struct vdpa_config_ops *ops = vdpa->config;

	ops->kick_vq(vdpa, vq->index);

	return true;
}

static bool virtio_vdpa_notify_with_data(struct virtqueue *vq)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vq->vdev);
	const struct vdpa_config_ops *ops = vdpa->config;
	u32 data = vring_notification_data(vq);

	ops->kick_vq_with_data(vdpa, data);

	return true;
}

static irqreturn_t virtio_vdpa_config_cb(void *private)
{
	struct virtio_vdpa_device *vd_dev = private;

	virtio_config_changed(&vd_dev->vdev);

	return IRQ_HANDLED;
}

static irqreturn_t virtio_vdpa_virtqueue_cb(void *private)
{
	struct virtio_vdpa_vq_info *info = private;

	return vring_interrupt(0, info->vq);
}

static struct virtqueue *
virtio_vdpa_setup_vq(struct virtio_device *vdev, unsigned int index,
		     void (*callback)(struct virtqueue *vq),
		     const char *name, bool ctx)
{
	struct virtio_vdpa_device *vd_dev = to_virtio_vdpa_device(vdev);
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);
	struct device *dma_dev;
	const struct vdpa_config_ops *ops = vdpa->config;
	struct virtio_vdpa_vq_info *info;
	bool (*notify)(struct virtqueue *vq) = virtio_vdpa_notify;
	struct vdpa_callback cb;
	struct virtqueue *vq;
	u64 desc_addr, driver_addr, device_addr;
	/* Assume split virtqueue, switch to packed if necessary */
	struct vdpa_vq_state state = {0};
	unsigned long flags;
	u32 align, max_num, min_num = 1;
	bool may_reduce_num = true;
	int err;

	if (!name)
		return NULL;

	if (index >= vdpa->nvqs)
		return ERR_PTR(-ENOENT);

	/* We cannot accept VIRTIO_F_NOTIFICATION_DATA without kick_vq_with_data */
	if (__virtio_test_bit(vdev, VIRTIO_F_NOTIFICATION_DATA)) {
		if (ops->kick_vq_with_data)
			notify = virtio_vdpa_notify_with_data;
		else
			__virtio_clear_bit(vdev, VIRTIO_F_NOTIFICATION_DATA);
	}

	/* Queue shouldn't already be set up. */
	if (ops->get_vq_ready(vdpa, index))
		return ERR_PTR(-ENOENT);

	/* Allocate and fill out our active queue description */
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);
	if (ops->get_vq_size)
		max_num = ops->get_vq_size(vdpa, index);
	else
		max_num = ops->get_vq_num_max(vdpa);

	if (max_num == 0) {
		err = -ENOENT;
		goto error_new_virtqueue;
	}

	if (ops->get_vq_num_min)
		min_num = ops->get_vq_num_min(vdpa);

	may_reduce_num = (max_num == min_num) ? false : true;

	/* Create the vring */
	align = ops->get_vq_align(vdpa);

	if (ops->get_vq_dma_dev)
		dma_dev = ops->get_vq_dma_dev(vdpa, index);
	else
		dma_dev = vdpa_get_dma_dev(vdpa);
	vq = vring_create_virtqueue_dma(index, max_num, align, vdev,
					true, may_reduce_num, ctx,
					notify, callback, name, dma_dev);
	if (!vq) {
		err = -ENOMEM;
		goto error_new_virtqueue;
	}

	vq->num_max = max_num;

	/* Setup virtqueue callback */
	cb.callback = callback ? virtio_vdpa_virtqueue_cb : NULL;
	cb.private = info;
	cb.trigger = NULL;
	ops->set_vq_cb(vdpa, index, &cb);
	ops->set_vq_num(vdpa, index, virtqueue_get_vring_size(vq));

	desc_addr = virtqueue_get_desc_addr(vq);
	driver_addr = virtqueue_get_avail_addr(vq);
	device_addr = virtqueue_get_used_addr(vq);

	if (ops->set_vq_address(vdpa, index,
				desc_addr, driver_addr,
				device_addr)) {
		err = -EINVAL;
		goto err_vq;
	}

	/* reset virtqueue state index */
	if (virtio_has_feature(vdev, VIRTIO_F_RING_PACKED)) {
		struct vdpa_vq_state_packed *s = &state.packed;

		s->last_avail_counter = 1;
		s->last_avail_idx = 0;
		s->last_used_counter = 1;
		s->last_used_idx = 0;
	}
	err = ops->set_vq_state(vdpa, index, &state);
	if (err)
		goto err_vq;

	ops->set_vq_ready(vdpa, index, 1);

	vq->priv = info;
	info->vq = vq;

	spin_lock_irqsave(&vd_dev->lock, flags);
	list_add(&info->node, &vd_dev->virtqueues);
	spin_unlock_irqrestore(&vd_dev->lock, flags);

	return vq;

err_vq:
	vring_del_virtqueue(vq);
error_new_virtqueue:
	ops->set_vq_ready(vdpa, index, 0);
	/* VDPA driver should make sure vq is stopeed here */
	WARN_ON(ops->get_vq_ready(vdpa, index));
	kfree(info);
	return ERR_PTR(err);
}

static void virtio_vdpa_del_vq(struct virtqueue *vq)
{
	struct virtio_vdpa_device *vd_dev = to_virtio_vdpa_device(vq->vdev);
	struct vdpa_device *vdpa = vd_dev->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	struct virtio_vdpa_vq_info *info = vq->priv;
	unsigned int index = vq->index;
	unsigned long flags;

	spin_lock_irqsave(&vd_dev->lock, flags);
	list_del(&info->node);
	spin_unlock_irqrestore(&vd_dev->lock, flags);

	/* Select and deactivate the queue (best effort) */
	ops->set_vq_ready(vdpa, index, 0);

	vring_del_virtqueue(vq);

	kfree(info);
}

static void virtio_vdpa_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list)
		virtio_vdpa_del_vq(vq);
}

static void default_calc_sets(struct irq_affinity *affd, unsigned int affvecs)
{
	affd->nr_sets = 1;
	affd->set_size[0] = affvecs;
}

static struct cpumask *
create_affinity_masks(unsigned int nvecs, struct irq_affinity *affd)
{
	unsigned int affvecs = 0, curvec, usedvecs, i;
	struct cpumask *masks = NULL;

	if (nvecs > affd->pre_vectors + affd->post_vectors)
		affvecs = nvecs - affd->pre_vectors - affd->post_vectors;

	if (!affd->calc_sets)
		affd->calc_sets = default_calc_sets;

	affd->calc_sets(affd, affvecs);

	if (!affvecs)
		return NULL;

	masks = kcalloc(nvecs, sizeof(*masks), GFP_KERNEL);
	if (!masks)
		return NULL;

	/* Fill out vectors at the beginning that don't need affinity */
	for (curvec = 0; curvec < affd->pre_vectors; curvec++)
		cpumask_setall(&masks[curvec]);

	for (i = 0, usedvecs = 0; i < affd->nr_sets; i++) {
		unsigned int this_vecs = affd->set_size[i];
		int j;
		struct cpumask *result = group_cpus_evenly(this_vecs);

		if (!result) {
			kfree(masks);
			return NULL;
		}

		for (j = 0; j < this_vecs; j++)
			cpumask_copy(&masks[curvec + j], &result[j]);
		kfree(result);

		curvec += this_vecs;
		usedvecs += this_vecs;
	}

	/* Fill out vectors at the end that don't need affinity */
	if (usedvecs >= affvecs)
		curvec = affd->pre_vectors + affvecs;
	else
		curvec = affd->pre_vectors + usedvecs;
	for (; curvec < nvecs; curvec++)
		cpumask_setall(&masks[curvec]);

	return masks;
}

static int virtio_vdpa_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
				struct virtqueue *vqs[],
				vq_callback_t *callbacks[],
				const char * const names[],
				const bool *ctx,
				struct irq_affinity *desc)
{
	struct virtio_vdpa_device *vd_dev = to_virtio_vdpa_device(vdev);
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);
	const struct vdpa_config_ops *ops = vdpa->config;
	struct irq_affinity default_affd = { 0 };
	struct cpumask *masks;
	struct vdpa_callback cb;
	bool has_affinity = desc && ops->set_vq_affinity;
	int i, err, queue_idx = 0;

	if (has_affinity) {
		masks = create_affinity_masks(nvqs, desc ? desc : &default_affd);
		if (!masks)
			return -ENOMEM;
	}

	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}

		vqs[i] = virtio_vdpa_setup_vq(vdev, queue_idx++,
					      callbacks[i], names[i], ctx ?
					      ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto err_setup_vq;
		}

		if (has_affinity)
			ops->set_vq_affinity(vdpa, i, &masks[i]);
	}

	cb.callback = virtio_vdpa_config_cb;
	cb.private = vd_dev;
	ops->set_config_cb(vdpa, &cb);
	if (has_affinity)
		kfree(masks);

	return 0;

err_setup_vq:
	virtio_vdpa_del_vqs(vdev);
	if (has_affinity)
		kfree(masks);
	return err;
}

static u64 virtio_vdpa_get_features(struct virtio_device *vdev)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);
	const struct vdpa_config_ops *ops = vdpa->config;

	return ops->get_device_features(vdpa);
}

static int virtio_vdpa_finalize_features(struct virtio_device *vdev)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	return vdpa_set_features(vdpa, vdev->features);
}

static const char *virtio_vdpa_bus_name(struct virtio_device *vdev)
{
	struct virtio_vdpa_device *vd_dev = to_virtio_vdpa_device(vdev);
	struct vdpa_device *vdpa = vd_dev->vdpa;

	return dev_name(&vdpa->dev);
}

static int virtio_vdpa_set_vq_affinity(struct virtqueue *vq,
				       const struct cpumask *cpu_mask)
{
	struct virtio_vdpa_device *vd_dev = to_virtio_vdpa_device(vq->vdev);
	struct vdpa_device *vdpa = vd_dev->vdpa;
	const struct vdpa_config_ops *ops = vdpa->config;
	unsigned int index = vq->index;

	if (ops->set_vq_affinity)
		return ops->set_vq_affinity(vdpa, index, cpu_mask);

	return 0;
}

static const struct cpumask *
virtio_vdpa_get_vq_affinity(struct virtio_device *vdev, int index)
{
	struct vdpa_device *vdpa = vd_get_vdpa(vdev);
	const struct vdpa_config_ops *ops = vdpa->config;

	if (ops->get_vq_affinity)
		return ops->get_vq_affinity(vdpa, index);

	return NULL;
}

static const struct virtio_config_ops virtio_vdpa_config_ops = {
	.get		= virtio_vdpa_get,
	.set		= virtio_vdpa_set,
	.generation	= virtio_vdpa_generation,
	.get_status	= virtio_vdpa_get_status,
	.set_status	= virtio_vdpa_set_status,
	.reset		= virtio_vdpa_reset,
	.find_vqs	= virtio_vdpa_find_vqs,
	.del_vqs	= virtio_vdpa_del_vqs,
	.get_features	= virtio_vdpa_get_features,
	.finalize_features = virtio_vdpa_finalize_features,
	.bus_name	= virtio_vdpa_bus_name,
	.set_vq_affinity = virtio_vdpa_set_vq_affinity,
	.get_vq_affinity = virtio_vdpa_get_vq_affinity,
};

static void virtio_vdpa_release_dev(struct device *_d)
{
	struct virtio_device *vdev =
	       container_of(_d, struct virtio_device, dev);
	struct virtio_vdpa_device *vd_dev =
	       container_of(vdev, struct virtio_vdpa_device, vdev);

	kfree(vd_dev);
}

static int virtio_vdpa_probe(struct vdpa_device *vdpa)
{
	const struct vdpa_config_ops *ops = vdpa->config;
	struct virtio_vdpa_device *vd_dev, *reg_dev = NULL;
	int ret = -EINVAL;

	vd_dev = kzalloc(sizeof(*vd_dev), GFP_KERNEL);
	if (!vd_dev)
		return -ENOMEM;

	vd_dev->vdev.dev.parent = vdpa_get_dma_dev(vdpa);
	vd_dev->vdev.dev.release = virtio_vdpa_release_dev;
	vd_dev->vdev.config = &virtio_vdpa_config_ops;
	vd_dev->vdpa = vdpa;
	INIT_LIST_HEAD(&vd_dev->virtqueues);
	spin_lock_init(&vd_dev->lock);

	vd_dev->vdev.id.device = ops->get_device_id(vdpa);
	if (vd_dev->vdev.id.device == 0)
		goto err;

	vd_dev->vdev.id.vendor = ops->get_vendor_id(vdpa);
	ret = register_virtio_device(&vd_dev->vdev);
	reg_dev = vd_dev;
	if (ret)
		goto err;

	vdpa_set_drvdata(vdpa, vd_dev);

	return 0;

err:
	if (reg_dev)
		put_device(&vd_dev->vdev.dev);
	else
		kfree(vd_dev);
	return ret;
}

static void virtio_vdpa_remove(struct vdpa_device *vdpa)
{
	struct virtio_vdpa_device *vd_dev = vdpa_get_drvdata(vdpa);

	unregister_virtio_device(&vd_dev->vdev);
}

static struct vdpa_driver virtio_vdpa_driver = {
	.driver = {
		.name	= "virtio_vdpa",
	},
	.probe	= virtio_vdpa_probe,
	.remove = virtio_vdpa_remove,
};

module_vdpa_driver(virtio_vdpa_driver);

MODULE_VERSION(MOD_VERSION);
MODULE_LICENSE(MOD_LICENSE);
MODULE_AUTHOR(MOD_AUTHOR);
MODULE_DESCRIPTION(MOD_DESC);
