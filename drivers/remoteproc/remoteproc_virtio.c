// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote processor messaging transport (OMAP platform-specific bits)
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 */

#include <linux/dma-map-ops.h>
#include <linux/export.h>
#include <linux/of_reserved_mem.h>
#include <linux/remoteproc.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <linux/err.h>
#include <linux/kref.h>
#include <linux/slab.h>

#include "remoteproc_internal.h"

/* kick the remote processor, and let it know which virtqueue to poke at */
static bool rproc_virtio_notify(struct virtqueue *vq)
{
	struct rproc_vring *rvring = vq->priv;
	struct rproc *rproc = rvring->rvdev->rproc;
	int notifyid = rvring->notifyid;

	dev_dbg(&rproc->dev, "kicking vq index: %d\n", notifyid);

	rproc->ops->kick(rproc, notifyid);
	return true;
}

/**
 * rproc_vq_interrupt() - tell remoteproc that a virtqueue is interrupted
 * @rproc: handle to the remote processor
 * @notifyid: index of the signalled virtqueue (unique per this @rproc)
 *
 * This function should be called by the platform-specific rproc driver,
 * when the remote processor signals that a specific virtqueue has pending
 * messages available.
 *
 * Returns IRQ_NONE if no message was found in the @notifyid virtqueue,
 * and otherwise returns IRQ_HANDLED.
 */
irqreturn_t rproc_vq_interrupt(struct rproc *rproc, int notifyid)
{
	struct rproc_vring *rvring;

	dev_dbg(&rproc->dev, "vq index %d is interrupted\n", notifyid);

	rvring = idr_find(&rproc->notifyids, notifyid);
	if (!rvring || !rvring->vq)
		return IRQ_NONE;

	return vring_interrupt(0, rvring->vq);
}
EXPORT_SYMBOL(rproc_vq_interrupt);

static struct virtqueue *rp_find_vq(struct virtio_device *vdev,
				    unsigned int id,
				    void (*callback)(struct virtqueue *vq),
				    const char *name, bool ctx)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct rproc *rproc = vdev_to_rproc(vdev);
	struct device *dev = &rproc->dev;
	struct rproc_mem_entry *mem;
	struct rproc_vring *rvring;
	struct fw_rsc_vdev *rsc;
	struct virtqueue *vq;
	void *addr;
	int len, size;

	/* we're temporarily limited to two virtqueues per rvdev */
	if (id >= ARRAY_SIZE(rvdev->vring))
		return ERR_PTR(-EINVAL);

	if (!name)
		return NULL;

	/* Search allocated memory region by name */
	mem = rproc_find_carveout_by_name(rproc, "vdev%dvring%d", rvdev->index,
					  id);
	if (!mem || !mem->va)
		return ERR_PTR(-ENOMEM);

	rvring = &rvdev->vring[id];
	addr = mem->va;
	len = rvring->len;

	/* zero vring */
	size = vring_size(len, rvring->align);
	memset(addr, 0, size);

	dev_dbg(dev, "vring%d: va %pK qsz %d notifyid %d\n",
		id, addr, len, rvring->notifyid);

	/*
	 * Create the new vq, and tell virtio we're not interested in
	 * the 'weak' smp barriers, since we're talking with a real device.
	 */
	vq = vring_new_virtqueue(id, len, rvring->align, vdev, false, ctx,
				 addr, rproc_virtio_notify, callback, name);
	if (!vq) {
		dev_err(dev, "vring_new_virtqueue %s failed\n", name);
		rproc_free_vring(rvring);
		return ERR_PTR(-ENOMEM);
	}

	rvring->vq = vq;
	vq->priv = rvring;

	/* Update vring in resource table */
	rsc = (void *)rproc->table_ptr + rvdev->rsc_offset;
	rsc->vring[id].da = mem->da;

	return vq;
}

static void __rproc_virtio_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;
	struct rproc_vring *rvring;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		rvring = vq->priv;
		rvring->vq = NULL;
		vring_del_virtqueue(vq);
	}
}

static void rproc_virtio_del_vqs(struct virtio_device *vdev)
{
	__rproc_virtio_del_vqs(vdev);
}

static int rproc_virtio_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
				 struct virtqueue *vqs[],
				 vq_callback_t *callbacks[],
				 const char * const names[],
				 const bool * ctx,
				 struct irq_affinity *desc)
{
	int i, ret, queue_idx = 0;

	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}

		vqs[i] = rp_find_vq(vdev, queue_idx++, callbacks[i], names[i],
				    ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			ret = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	return 0;

error:
	__rproc_virtio_del_vqs(vdev);
	return ret;
}

static u8 rproc_virtio_get_status(struct virtio_device *vdev)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct fw_rsc_vdev *rsc;

	rsc = (void *)rvdev->rproc->table_ptr + rvdev->rsc_offset;

	return rsc->status;
}

static void rproc_virtio_set_status(struct virtio_device *vdev, u8 status)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct fw_rsc_vdev *rsc;

	rsc = (void *)rvdev->rproc->table_ptr + rvdev->rsc_offset;

	rsc->status = status;
	dev_dbg(&vdev->dev, "status: %d\n", status);
}

static void rproc_virtio_reset(struct virtio_device *vdev)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct fw_rsc_vdev *rsc;

	rsc = (void *)rvdev->rproc->table_ptr + rvdev->rsc_offset;

	rsc->status = 0;
	dev_dbg(&vdev->dev, "reset !\n");
}

/* provide the vdev features as retrieved from the firmware */
static u64 rproc_virtio_get_features(struct virtio_device *vdev)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct fw_rsc_vdev *rsc;

	rsc = (void *)rvdev->rproc->table_ptr + rvdev->rsc_offset;

	return rsc->dfeatures;
}

static void rproc_transport_features(struct virtio_device *vdev)
{
	/*
	 * Packed ring isn't enabled on remoteproc for now,
	 * because remoteproc uses vring_new_virtqueue() which
	 * creates virtio rings on preallocated memory.
	 */
	__virtio_clear_bit(vdev, VIRTIO_F_RING_PACKED);
}

static int rproc_virtio_finalize_features(struct virtio_device *vdev)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct fw_rsc_vdev *rsc;

	rsc = (void *)rvdev->rproc->table_ptr + rvdev->rsc_offset;

	/* Give virtio_ring a chance to accept features */
	vring_transport_features(vdev);

	/* Give virtio_rproc a chance to accept features. */
	rproc_transport_features(vdev);

	/* Make sure we don't have any features > 32 bits! */
	BUG_ON((u32)vdev->features != vdev->features);

	/*
	 * Remember the finalized features of our vdev, and provide it
	 * to the remote processor once it is powered on.
	 */
	rsc->gfeatures = vdev->features;

	return 0;
}

static void rproc_virtio_get(struct virtio_device *vdev, unsigned int offset,
			     void *buf, unsigned int len)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct fw_rsc_vdev *rsc;
	void *cfg;

	rsc = (void *)rvdev->rproc->table_ptr + rvdev->rsc_offset;
	cfg = &rsc->vring[rsc->num_of_vrings];

	if (offset + len > rsc->config_len || offset + len < len) {
		dev_err(&vdev->dev, "rproc_virtio_get: access out of bounds\n");
		return;
	}

	memcpy(buf, cfg + offset, len);
}

static void rproc_virtio_set(struct virtio_device *vdev, unsigned int offset,
			     const void *buf, unsigned int len)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct fw_rsc_vdev *rsc;
	void *cfg;

	rsc = (void *)rvdev->rproc->table_ptr + rvdev->rsc_offset;
	cfg = &rsc->vring[rsc->num_of_vrings];

	if (offset + len > rsc->config_len || offset + len < len) {
		dev_err(&vdev->dev, "rproc_virtio_set: access out of bounds\n");
		return;
	}

	memcpy(cfg + offset, buf, len);
}

static const struct virtio_config_ops rproc_virtio_config_ops = {
	.get_features	= rproc_virtio_get_features,
	.finalize_features = rproc_virtio_finalize_features,
	.find_vqs	= rproc_virtio_find_vqs,
	.del_vqs	= rproc_virtio_del_vqs,
	.reset		= rproc_virtio_reset,
	.set_status	= rproc_virtio_set_status,
	.get_status	= rproc_virtio_get_status,
	.get		= rproc_virtio_get,
	.set		= rproc_virtio_set,
};

/*
 * This function is called whenever vdev is released, and is responsible
 * to decrement the remote processor's refcount which was taken when vdev was
 * added.
 *
 * Never call this function directly; it will be called by the driver
 * core when needed.
 */
static void rproc_virtio_dev_release(struct device *dev)
{
	struct virtio_device *vdev = dev_to_virtio(dev);
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct rproc *rproc = vdev_to_rproc(vdev);

	kfree(vdev);

	kref_put(&rvdev->refcount, rproc_vdev_release);

	put_device(&rproc->dev);
}

/**
 * rproc_add_virtio_dev() - register an rproc-induced virtio device
 * @rvdev: the remote vdev
 * @id: the device type identification (used to match it with a driver).
 *
 * This function registers a virtio device. This vdev's partent is
 * the rproc device.
 *
 * Returns 0 on success or an appropriate error value otherwise.
 */
int rproc_add_virtio_dev(struct rproc_vdev *rvdev, int id)
{
	struct rproc *rproc = rvdev->rproc;
	struct device *dev = &rvdev->dev;
	struct virtio_device *vdev;
	struct rproc_mem_entry *mem;
	int ret;

	if (rproc->ops->kick == NULL) {
		ret = -EINVAL;
		dev_err(dev, ".kick method not defined for %s\n", rproc->name);
		goto out;
	}

	/* Try to find dedicated vdev buffer carveout */
	mem = rproc_find_carveout_by_name(rproc, "vdev%dbuffer", rvdev->index);
	if (mem) {
		phys_addr_t pa;

		if (mem->of_resm_idx != -1) {
			struct device_node *np = rproc->dev.parent->of_node;

			/* Associate reserved memory to vdev device */
			ret = of_reserved_mem_device_init_by_idx(dev, np,
								 mem->of_resm_idx);
			if (ret) {
				dev_err(dev, "Can't associate reserved memory\n");
				goto out;
			}
		} else {
			if (mem->va) {
				dev_warn(dev, "vdev %d buffer already mapped\n",
					 rvdev->index);
				pa = rproc_va_to_pa(mem->va);
			} else {
				/* Use dma address as carveout no memmapped yet */
				pa = (phys_addr_t)mem->dma;
			}

			/* Associate vdev buffer memory pool to vdev subdev */
			ret = dma_declare_coherent_memory(dev, pa,
							   mem->da,
							   mem->len);
			if (ret < 0) {
				dev_err(dev, "Failed to associate buffer\n");
				goto out;
			}
		}
	} else {
		struct device_node *np = rproc->dev.parent->of_node;

		/*
		 * If we don't have dedicated buffer, just attempt to re-assign
		 * the reserved memory from our parent. A default memory-region
		 * at index 0 from the parent's memory-regions is assigned for
		 * the rvdev dev to allocate from. Failure is non-critical and
		 * the allocations will fall back to global pools, so don't
		 * check return value either.
		 */
		of_reserved_mem_device_init_by_idx(dev, np, 0);
	}

	/* Allocate virtio device */
	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev) {
		ret = -ENOMEM;
		goto out;
	}
	vdev->id.device	= id,
	vdev->config = &rproc_virtio_config_ops,
	vdev->dev.parent = dev;
	vdev->dev.release = rproc_virtio_dev_release;

	/*
	 * We're indirectly making a non-temporary copy of the rproc pointer
	 * here, because drivers probed with this vdev will indirectly
	 * access the wrapping rproc.
	 *
	 * Therefore we must increment the rproc refcount here, and decrement
	 * it _only_ when the vdev is released.
	 */
	get_device(&rproc->dev);

	/* Reference the vdev and vring allocations */
	kref_get(&rvdev->refcount);

	ret = register_virtio_device(vdev);
	if (ret) {
		put_device(&vdev->dev);
		dev_err(dev, "failed to register vdev: %d\n", ret);
		goto out;
	}

	dev_info(dev, "registered %s (type %d)\n", dev_name(&vdev->dev), id);

out:
	return ret;
}

/**
 * rproc_remove_virtio_dev() - remove an rproc-induced virtio device
 * @dev: the virtio device
 * @data: must be null
 *
 * This function unregisters an existing virtio device.
 */
int rproc_remove_virtio_dev(struct device *dev, void *data)
{
	struct virtio_device *vdev = dev_to_virtio(dev);

	unregister_virtio_device(vdev);
	return 0;
}
