/*
 * Remote processor messaging transport (OMAP platform-specific bits)
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/export.h>
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
static void rproc_virtio_notify(struct virtqueue *vq)
{
	struct rproc_vring *rvring = vq->priv;
	struct rproc *rproc = rvring->rvdev->rproc;
	int notifyid = rvring->notifyid;

	dev_dbg(&rproc->dev, "kicking vq index: %d\n", notifyid);

	rproc->ops->kick(rproc, notifyid);
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
				    unsigned id,
				    void (*callback)(struct virtqueue *vq),
				    const char *name)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct rproc *rproc = vdev_to_rproc(vdev);
	struct device *dev = &rproc->dev;
	struct rproc_vring *rvring;
	struct virtqueue *vq;
	void *addr;
	int len, size, ret;

	/* we're temporarily limited to two virtqueues per rvdev */
	if (id >= ARRAY_SIZE(rvdev->vring))
		return ERR_PTR(-EINVAL);

	if (!name)
		return NULL;

	ret = rproc_alloc_vring(rvdev, id);
	if (ret)
		return ERR_PTR(ret);

	rvring = &rvdev->vring[id];
	addr = rvring->va;
	len = rvring->len;

	/* zero vring */
	size = vring_size(len, rvring->align);
	memset(addr, 0, size);

	dev_dbg(dev, "vring%d: va %p qsz %d notifyid %d\n",
					id, addr, len, rvring->notifyid);

	/*
	 * Create the new vq, and tell virtio we're not interested in
	 * the 'weak' smp barriers, since we're talking with a real device.
	 */
	vq = vring_new_virtqueue(id, len, rvring->align, vdev, false, addr,
					rproc_virtio_notify, callback, name);
	if (!vq) {
		dev_err(dev, "vring_new_virtqueue %s failed\n", name);
		rproc_free_vring(rvring);
		return ERR_PTR(-ENOMEM);
	}

	rvring->vq = vq;
	vq->priv = rvring;

	return vq;
}

static void rproc_virtio_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;
	struct rproc *rproc = vdev_to_rproc(vdev);
	struct rproc_vring *rvring;

	/* power down the remote processor before deleting vqs */
	rproc_shutdown(rproc);

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		rvring = vq->priv;
		rvring->vq = NULL;
		vring_del_virtqueue(vq);
		rproc_free_vring(rvring);
	}
}

static int rproc_virtio_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char *names[])
{
	struct rproc *rproc = vdev_to_rproc(vdev);
	int i, ret;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = rp_find_vq(vdev, i, callbacks[i], names[i]);
		if (IS_ERR(vqs[i])) {
			ret = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	/* now that the vqs are all set, boot the remote processor */
	ret = rproc_boot(rproc);
	if (ret) {
		dev_err(&rproc->dev, "rproc_boot() failed %d\n", ret);
		goto error;
	}

	return 0;

error:
	rproc_virtio_del_vqs(vdev);
	return ret;
}

/*
 * We don't support yet real virtio status semantics.
 *
 * The plan is to provide this via the VDEV resource entry
 * which is part of the firmware: this way the remote processor
 * will be able to access the status values as set by us.
 */
static u8 rproc_virtio_get_status(struct virtio_device *vdev)
{
	return 0;
}

static void rproc_virtio_set_status(struct virtio_device *vdev, u8 status)
{
	dev_dbg(&vdev->dev, "status: %d\n", status);
}

static void rproc_virtio_reset(struct virtio_device *vdev)
{
	dev_dbg(&vdev->dev, "reset !\n");
}

/* provide the vdev features as retrieved from the firmware */
static u32 rproc_virtio_get_features(struct virtio_device *vdev)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);

	return rvdev->dfeatures;
}

static void rproc_virtio_finalize_features(struct virtio_device *vdev)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);

	/* Give virtio_ring a chance to accept features */
	vring_transport_features(vdev);

	/*
	 * Remember the finalized features of our vdev, and provide it
	 * to the remote processor once it is powered on.
	 *
	 * Similarly to the status field, we don't expose yet the negotiated
	 * features to the remote processors at this point. This will be
	 * fixed as part of a small resource table overhaul and then an
	 * extension of the virtio resource entries.
	 */
	rvdev->gfeatures = vdev->features[0];
}

static struct virtio_config_ops rproc_virtio_config_ops = {
	.get_features	= rproc_virtio_get_features,
	.finalize_features = rproc_virtio_finalize_features,
	.find_vqs	= rproc_virtio_find_vqs,
	.del_vqs	= rproc_virtio_del_vqs,
	.reset		= rproc_virtio_reset,
	.set_status	= rproc_virtio_set_status,
	.get_status	= rproc_virtio_get_status,
};

/*
 * This function is called whenever vdev is released, and is responsible
 * to decrement the remote processor's refcount which was taken when vdev was
 * added.
 *
 * Never call this function directly; it will be called by the driver
 * core when needed.
 */
static void rproc_vdev_release(struct device *dev)
{
	struct virtio_device *vdev = dev_to_virtio(dev);
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct rproc *rproc = vdev_to_rproc(vdev);

	list_del(&rvdev->node);
	kfree(rvdev);

	put_device(&rproc->dev);
}

/**
 * rproc_add_virtio_dev() - register an rproc-induced virtio device
 * @rvdev: the remote vdev
 *
 * This function registers a virtio device. This vdev's partent is
 * the rproc device.
 *
 * Returns 0 on success or an appropriate error value otherwise.
 */
int rproc_add_virtio_dev(struct rproc_vdev *rvdev, int id)
{
	struct rproc *rproc = rvdev->rproc;
	struct device *dev = &rproc->dev;
	struct virtio_device *vdev = &rvdev->vdev;
	int ret;

	vdev->id.device	= id,
	vdev->config = &rproc_virtio_config_ops,
	vdev->dev.parent = dev;
	vdev->dev.release = rproc_vdev_release;

	/*
	 * We're indirectly making a non-temporary copy of the rproc pointer
	 * here, because drivers probed with this vdev will indirectly
	 * access the wrapping rproc.
	 *
	 * Therefore we must increment the rproc refcount here, and decrement
	 * it _only_ when the vdev is released.
	 */
	get_device(&rproc->dev);

	ret = register_virtio_device(vdev);
	if (ret) {
		put_device(&rproc->dev);
		dev_err(dev, "failed to register vdev: %d\n", ret);
		goto out;
	}

	dev_info(dev, "registered %s (type %d)\n", dev_name(&vdev->dev), id);

out:
	return ret;
}

/**
 * rproc_remove_virtio_dev() - remove an rproc-induced virtio device
 * @rvdev: the remote vdev
 *
 * This function unregisters an existing virtio device.
 */
void rproc_remove_virtio_dev(struct rproc_vdev *rvdev)
{
	unregister_virtio_device(&rvdev->vdev);
}
