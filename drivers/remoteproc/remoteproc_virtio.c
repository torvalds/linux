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
#include <linux/rpmsg.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <linux/err.h>
#include <linux/kref.h>
#include <linux/slab.h>

#include "remoteproc_internal.h"

/**
 * struct rproc_virtio_vq_info - virtqueue state
 * @vq_id: a unique index of this virtqueue (unique for this @rproc)
 * @rproc: handle to the remote processor
 *
 * Such a struct will be maintained for every virtqueue we're
 * using to communicate with the remote processor
 */
struct rproc_virtio_vq_info {
	__u16 vq_id;
	struct rproc *rproc;
};

/* kick the remote processor, and let it know which virtqueue to poke at */
static void rproc_virtio_notify(struct virtqueue *vq)
{
	struct rproc_virtio_vq_info *rpvq = vq->priv;
	struct rproc *rproc = rpvq->rproc;

	dev_dbg(rproc->dev, "kicking vq id: %d\n", rpvq->vq_id);

	rproc->ops->kick(rproc, rpvq->vq_id);
}

/**
 * rproc_vq_interrupt() - tell remoteproc that a virtqueue is interrupted
 * @rproc: handle to the remote processor
 * @vq_id: index of the signalled virtqueue
 *
 * This function should be called by the platform-specific rproc driver,
 * when the remote processor signals that a specific virtqueue has pending
 * messages available.
 *
 * Returns IRQ_NONE if no message was found in the @vq_id virtqueue,
 * and otherwise returns IRQ_HANDLED.
 */
irqreturn_t rproc_vq_interrupt(struct rproc *rproc, int vq_id)
{
	return vring_interrupt(0, rproc->rvdev->vq[vq_id]);
}
EXPORT_SYMBOL(rproc_vq_interrupt);

static struct virtqueue *rp_find_vq(struct virtio_device *vdev,
				    unsigned id,
				    void (*callback)(struct virtqueue *vq),
				    const char *name)
{
	struct rproc *rproc = vdev_to_rproc(vdev);
	struct rproc_vdev *rvdev = rproc->rvdev;
	struct rproc_virtio_vq_info *rpvq;
	struct virtqueue *vq;
	void *addr;
	int ret, len;

	rpvq = kmalloc(sizeof(*rpvq), GFP_KERNEL);
	if (!rpvq)
		return ERR_PTR(-ENOMEM);

	rpvq->rproc = rproc;
	rpvq->vq_id = id;

	addr = rvdev->vring[id].va;
	len = rvdev->vring[id].len;

	dev_dbg(rproc->dev, "vring%d: va %p qsz %d\n", id, addr, len);

	/*
	 * Create the new vq, and tell virtio we're not interested in
	 * the 'weak' smp barriers, since we're talking with a real device.
	 */
	vq = vring_new_virtqueue(len, AMP_VRING_ALIGN, vdev, false, addr,
					rproc_virtio_notify, callback, name);
	if (!vq) {
		dev_err(rproc->dev, "vring_new_virtqueue %s failed\n", name);
		ret = -ENOMEM;
		goto free_rpvq;
	}

	rvdev->vq[id] = vq;
	vq->priv = rpvq;

	return vq;

free_rpvq:
	kfree(rpvq);
	return ERR_PTR(ret);
}

static void rproc_virtio_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;
	struct rproc *rproc = vdev_to_rproc(vdev);

	/* power down the remote processor before deleting vqs */
	rproc_shutdown(rproc);

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		struct rproc_virtio_vq_info *rpvq = vq->priv;
		vring_del_virtqueue(vq);
		kfree(rpvq);
	}
}

static int rproc_virtio_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char *names[])
{
	struct rproc *rproc = vdev_to_rproc(vdev);
	int i, ret;

	/* we maintain two virtqueues per remote processor (for RX and TX) */
	if (nvqs != 2)
		return -EINVAL;

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
		dev_err(rproc->dev, "rproc_boot() failed %d\n", ret);
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
 * The plan is to provide this via the VIRTIO HDR resource entry
 * which is part of the firmware: this way the remote processor
 * will be able to access the status values as set by us.
 */
static u8 rproc_virtio_get_status(struct virtio_device *vdev)
{
	return 0;
}

static void rproc_virtio_set_status(struct virtio_device *vdev, u8 status)
{
	dev_dbg(&vdev->dev, "new status: %d\n", status);
}

static void rproc_virtio_reset(struct virtio_device *vdev)
{
	dev_dbg(&vdev->dev, "reset !\n");
}

/* provide the vdev features as retrieved from the firmware */
static u32 rproc_virtio_get_features(struct virtio_device *vdev)
{
	struct rproc *rproc = vdev_to_rproc(vdev);

	/* we only support a single vdev device for now */
	return rproc->rvdev->dfeatures;
}

static void rproc_virtio_finalize_features(struct virtio_device *vdev)
{
	struct rproc *rproc = vdev_to_rproc(vdev);

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
	rproc->rvdev->gfeatures = vdev->features[0];
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
 * to decrement the remote processor's refcount taken when vdev was
 * added.
 *
 * Never call this function directly; it will be called by the driver
 * core when needed.
 */
static void rproc_vdev_release(struct device *dev)
{
	struct virtio_device *vdev = dev_to_virtio(dev);
	struct rproc *rproc = vdev_to_rproc(vdev);

	kref_put(&rproc->refcount, rproc_release);
}

/**
 * rproc_add_rpmsg_vdev() - create an rpmsg virtio device
 * @rproc: the rproc handle
 *
 * This function is called if virtio rpmsg support was found in the
 * firmware of the remote processor.
 *
 * Today we only support creating a single rpmsg vdev (virtio device),
 * but the plan is to remove this limitation. At that point this interface
 * will be revised/extended.
 */
int rproc_add_rpmsg_vdev(struct rproc *rproc)
{
	struct device *dev = rproc->dev;
	struct rproc_vdev *rvdev = rproc->rvdev;
	int ret;

	rvdev->vdev.id.device	= VIRTIO_ID_RPMSG,
	rvdev->vdev.config	= &rproc_virtio_config_ops,
	rvdev->vdev.dev.parent	= dev;
	rvdev->vdev.dev.release	= rproc_vdev_release;

	/*
	 * We're indirectly making a non-temporary copy of the rproc pointer
	 * here, because drivers probed with this vdev will indirectly
	 * access the wrapping rproc.
	 *
	 * Therefore we must increment the rproc refcount here, and decrement
	 * it _only_ when the vdev is released.
	 */
	kref_get(&rproc->refcount);

	ret = register_virtio_device(&rvdev->vdev);
	if (ret) {
		kref_put(&rproc->refcount, rproc_release);
		dev_err(dev, "failed to register vdev: %d\n", ret);
	}

	return ret;
}

/**
 * rproc_remove_rpmsg_vdev() - remove an rpmsg vdev device
 * @rproc: the rproc handle
 *
 * This function is called whenever @rproc is removed _iff_ an rpmsg
 * vdev was created beforehand.
 */
void rproc_remove_rpmsg_vdev(struct rproc *rproc)
{
	struct rproc_vdev *rvdev = rproc->rvdev;

	unregister_virtio_device(&rvdev->vdev);
}
