/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Adapted from:
 *
 * virtio for kvm on s390
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Christian Borntraeger <borntraeger@de.ibm.com>
 *
 * Intel Virtio Over PCIe (VOP) driver.
 *
 */
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>

#include "vop_main.h"

#define VOP_MAX_VRINGS 4

/*
 * _vop_vdev - Allocated per virtio device instance injected by the peer.
 *
 * @vdev: Virtio device
 * @desc: Virtio device page descriptor
 * @dc: Virtio device control
 * @vpdev: VOP device which is the parent for this virtio device
 * @vr: Buffer for accessing the VRING
 * @used: Buffer for used
 * @used_size: Size of the used buffer
 * @reset_done: Track whether VOP reset is complete
 * @virtio_cookie: Cookie returned upon requesting a interrupt
 * @c2h_vdev_db: The doorbell used by the guest to interrupt the host
 * @h2c_vdev_db: The doorbell used by the host to interrupt the guest
 * @dnode: The destination node
 */
struct _vop_vdev {
	struct virtio_device vdev;
	struct mic_device_desc __iomem *desc;
	struct mic_device_ctrl __iomem *dc;
	struct vop_device *vpdev;
	void __iomem *vr[VOP_MAX_VRINGS];
	dma_addr_t used[VOP_MAX_VRINGS];
	int used_size[VOP_MAX_VRINGS];
	struct completion reset_done;
	struct mic_irq *virtio_cookie;
	int c2h_vdev_db;
	int h2c_vdev_db;
	int dnode;
};

#define to_vopvdev(vd) container_of(vd, struct _vop_vdev, vdev)

#define _vop_aligned_desc_size(d) __mic_align(_vop_desc_size(d), 8)

/* Helper API to obtain the parent of the virtio device */
static inline struct device *_vop_dev(struct _vop_vdev *vdev)
{
	return vdev->vdev.dev.parent;
}

static inline unsigned _vop_desc_size(struct mic_device_desc __iomem *desc)
{
	return sizeof(*desc)
		+ ioread8(&desc->num_vq) * sizeof(struct mic_vqconfig)
		+ ioread8(&desc->feature_len) * 2
		+ ioread8(&desc->config_len);
}

static inline struct mic_vqconfig __iomem *
_vop_vq_config(struct mic_device_desc __iomem *desc)
{
	return (struct mic_vqconfig __iomem *)(desc + 1);
}

static inline u8 __iomem *
_vop_vq_features(struct mic_device_desc __iomem *desc)
{
	return (u8 __iomem *)(_vop_vq_config(desc) + ioread8(&desc->num_vq));
}

static inline u8 __iomem *
_vop_vq_configspace(struct mic_device_desc __iomem *desc)
{
	return _vop_vq_features(desc) + ioread8(&desc->feature_len) * 2;
}

static inline unsigned
_vop_total_desc_size(struct mic_device_desc __iomem *desc)
{
	return _vop_aligned_desc_size(desc) + sizeof(struct mic_device_ctrl);
}

/* This gets the device's feature bits. */
static u64 vop_get_features(struct virtio_device *vdev)
{
	unsigned int i, bits;
	u32 features = 0;
	struct mic_device_desc __iomem *desc = to_vopvdev(vdev)->desc;
	u8 __iomem *in_features = _vop_vq_features(desc);
	int feature_len = ioread8(&desc->feature_len);

	bits = min_t(unsigned, feature_len, sizeof(vdev->features)) * 8;
	for (i = 0; i < bits; i++)
		if (ioread8(&in_features[i / 8]) & (BIT(i % 8)))
			features |= BIT(i);

	return features;
}

static void vop_transport_features(struct virtio_device *vdev)
{
	/*
	 * Packed ring isn't enabled on virtio_vop for now,
	 * because virtio_vop uses vring_new_virtqueue() which
	 * creates virtio rings on preallocated memory.
	 */
	__virtio_clear_bit(vdev, VIRTIO_F_RING_PACKED);
}

static int vop_finalize_features(struct virtio_device *vdev)
{
	unsigned int i, bits;
	struct mic_device_desc __iomem *desc = to_vopvdev(vdev)->desc;
	u8 feature_len = ioread8(&desc->feature_len);
	/* Second half of bitmap is features we accept. */
	u8 __iomem *out_features =
		_vop_vq_features(desc) + feature_len;

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	/* Give virtio_vop a chance to accept features. */
	vop_transport_features(vdev);

	memset_io(out_features, 0, feature_len);
	bits = min_t(unsigned, feature_len,
		     sizeof(vdev->features)) * 8;
	for (i = 0; i < bits; i++) {
		if (__virtio_test_bit(vdev, i))
			iowrite8(ioread8(&out_features[i / 8]) | (1 << (i % 8)),
				 &out_features[i / 8]);
	}
	return 0;
}

/*
 * Reading and writing elements in config space
 */
static void vop_get(struct virtio_device *vdev, unsigned int offset,
		    void *buf, unsigned len)
{
	struct mic_device_desc __iomem *desc = to_vopvdev(vdev)->desc;

	if (offset + len > ioread8(&desc->config_len))
		return;
	memcpy_fromio(buf, _vop_vq_configspace(desc) + offset, len);
}

static void vop_set(struct virtio_device *vdev, unsigned int offset,
		    const void *buf, unsigned len)
{
	struct mic_device_desc __iomem *desc = to_vopvdev(vdev)->desc;

	if (offset + len > ioread8(&desc->config_len))
		return;
	memcpy_toio(_vop_vq_configspace(desc) + offset, buf, len);
}

/*
 * The operations to get and set the status word just access the status
 * field of the device descriptor. set_status also interrupts the host
 * to tell about status changes.
 */
static u8 vop_get_status(struct virtio_device *vdev)
{
	return ioread8(&to_vopvdev(vdev)->desc->status);
}

static void vop_set_status(struct virtio_device *dev, u8 status)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);
	struct vop_device *vpdev = vdev->vpdev;

	if (!status)
		return;
	iowrite8(status, &vdev->desc->status);
	vpdev->hw_ops->send_intr(vpdev, vdev->c2h_vdev_db);
}

/* Inform host on a virtio device reset and wait for ack from host */
static void vop_reset_inform_host(struct virtio_device *dev)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);
	struct mic_device_ctrl __iomem *dc = vdev->dc;
	struct vop_device *vpdev = vdev->vpdev;
	int retry;

	iowrite8(0, &dc->host_ack);
	iowrite8(1, &dc->vdev_reset);
	vpdev->hw_ops->send_intr(vpdev, vdev->c2h_vdev_db);

	/* Wait till host completes all card accesses and acks the reset */
	for (retry = 100; retry--;) {
		if (ioread8(&dc->host_ack))
			break;
		msleep(100);
	};

	dev_dbg(_vop_dev(vdev), "%s: retry: %d\n", __func__, retry);

	/* Reset status to 0 in case we timed out */
	iowrite8(0, &vdev->desc->status);
}

static void vop_reset(struct virtio_device *dev)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);

	dev_dbg(_vop_dev(vdev), "%s: virtio id %d\n",
		__func__, dev->id.device);

	vop_reset_inform_host(dev);
	complete_all(&vdev->reset_done);
}

/*
 * The virtio_ring code calls this API when it wants to notify the Host.
 */
static bool vop_notify(struct virtqueue *vq)
{
	struct _vop_vdev *vdev = vq->priv;
	struct vop_device *vpdev = vdev->vpdev;

	vpdev->hw_ops->send_intr(vpdev, vdev->c2h_vdev_db);
	return true;
}

static void vop_del_vq(struct virtqueue *vq, int n)
{
	struct _vop_vdev *vdev = to_vopvdev(vq->vdev);
	struct vring *vr = (struct vring *)(vq + 1);
	struct vop_device *vpdev = vdev->vpdev;

	dma_unmap_single(&vpdev->dev, vdev->used[n],
			 vdev->used_size[n], DMA_BIDIRECTIONAL);
	free_pages((unsigned long)vr->used, get_order(vdev->used_size[n]));
	vring_del_virtqueue(vq);
	vpdev->hw_ops->iounmap(vpdev, vdev->vr[n]);
	vdev->vr[n] = NULL;
}

static void vop_del_vqs(struct virtio_device *dev)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);
	struct virtqueue *vq, *n;
	int idx = 0;

	dev_dbg(_vop_dev(vdev), "%s\n", __func__);

	list_for_each_entry_safe(vq, n, &dev->vqs, list)
		vop_del_vq(vq, idx++);
}

/*
 * This routine will assign vring's allocated in host/io memory. Code in
 * virtio_ring.c however continues to access this io memory as if it were local
 * memory without io accessors.
 */
static struct virtqueue *vop_find_vq(struct virtio_device *dev,
				     unsigned index,
				     void (*callback)(struct virtqueue *vq),
				     const char *name, bool ctx)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);
	struct vop_device *vpdev = vdev->vpdev;
	struct mic_vqconfig __iomem *vqconfig;
	struct mic_vqconfig config;
	struct virtqueue *vq;
	void __iomem *va;
	struct _mic_vring_info __iomem *info;
	void *used;
	int vr_size, _vr_size, err, magic;
	struct vring *vr;
	u8 type = ioread8(&vdev->desc->type);

	if (index >= ioread8(&vdev->desc->num_vq))
		return ERR_PTR(-ENOENT);

	if (!name)
		return ERR_PTR(-ENOENT);

	/* First assign the vring's allocated in host memory */
	vqconfig = _vop_vq_config(vdev->desc) + index;
	memcpy_fromio(&config, vqconfig, sizeof(config));
	_vr_size = vring_size(le16_to_cpu(config.num), MIC_VIRTIO_RING_ALIGN);
	vr_size = PAGE_ALIGN(_vr_size + sizeof(struct _mic_vring_info));
	va = vpdev->hw_ops->ioremap(vpdev, le64_to_cpu(config.address),
			vr_size);
	if (!va)
		return ERR_PTR(-ENOMEM);
	vdev->vr[index] = va;
	memset_io(va, 0x0, _vr_size);
	vq = vring_new_virtqueue(
				index,
				le16_to_cpu(config.num), MIC_VIRTIO_RING_ALIGN,
				dev,
				false,
				ctx,
				(void __force *)va, vop_notify, callback, name);
	if (!vq) {
		err = -ENOMEM;
		goto unmap;
	}
	info = va + _vr_size;
	magic = ioread32(&info->magic);

	if (WARN(magic != MIC_MAGIC + type + index, "magic mismatch")) {
		err = -EIO;
		goto unmap;
	}

	/* Allocate and reassign used ring now */
	vdev->used_size[index] = PAGE_ALIGN(sizeof(__u16) * 3 +
					     sizeof(struct vring_used_elem) *
					     le16_to_cpu(config.num));
	used = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(vdev->used_size[index]));
	if (!used) {
		err = -ENOMEM;
		dev_err(_vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, err);
		goto del_vq;
	}
	vdev->used[index] = dma_map_single(&vpdev->dev, used,
					    vdev->used_size[index],
					    DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&vpdev->dev, vdev->used[index])) {
		err = -ENOMEM;
		dev_err(_vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, err);
		goto free_used;
	}
	writeq(vdev->used[index], &vqconfig->used_address);
	/*
	 * To reassign the used ring here we are directly accessing
	 * struct vring_virtqueue which is a private data structure
	 * in virtio_ring.c. At the minimum, a BUILD_BUG_ON() in
	 * vring_new_virtqueue() would ensure that
	 *  (&vq->vring == (struct vring *) (&vq->vq + 1));
	 */
	vr = (struct vring *)(vq + 1);
	vr->used = used;

	vq->priv = vdev;
	return vq;
free_used:
	free_pages((unsigned long)used,
		   get_order(vdev->used_size[index]));
del_vq:
	vring_del_virtqueue(vq);
unmap:
	vpdev->hw_ops->iounmap(vpdev, vdev->vr[index]);
	return ERR_PTR(err);
}

static int vop_find_vqs(struct virtio_device *dev, unsigned nvqs,
			struct virtqueue *vqs[],
			vq_callback_t *callbacks[],
			const char * const names[], const bool *ctx,
			struct irq_affinity *desc)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);
	struct vop_device *vpdev = vdev->vpdev;
	struct mic_device_ctrl __iomem *dc = vdev->dc;
	int i, err, retry, queue_idx = 0;

	/* We must have this many virtqueues. */
	if (nvqs > ioread8(&vdev->desc->num_vq))
		return -ENOENT;

	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}

		dev_dbg(_vop_dev(vdev), "%s: %d: %s\n",
			__func__, i, names[i]);
		vqs[i] = vop_find_vq(dev, queue_idx++, callbacks[i], names[i],
				     ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	iowrite8(1, &dc->used_address_updated);
	/*
	 * Send an interrupt to the host to inform it that used
	 * rings have been re-assigned.
	 */
	vpdev->hw_ops->send_intr(vpdev, vdev->c2h_vdev_db);
	for (retry = 100; --retry;) {
		if (!ioread8(&dc->used_address_updated))
			break;
		msleep(100);
	};

	dev_dbg(_vop_dev(vdev), "%s: retry: %d\n", __func__, retry);
	if (!retry) {
		err = -ENODEV;
		goto error;
	}

	return 0;
error:
	vop_del_vqs(dev);
	return err;
}

/*
 * The config ops structure as defined by virtio config
 */
static struct virtio_config_ops vop_vq_config_ops = {
	.get_features = vop_get_features,
	.finalize_features = vop_finalize_features,
	.get = vop_get,
	.set = vop_set,
	.get_status = vop_get_status,
	.set_status = vop_set_status,
	.reset = vop_reset,
	.find_vqs = vop_find_vqs,
	.del_vqs = vop_del_vqs,
};

static irqreturn_t vop_virtio_intr_handler(int irq, void *data)
{
	struct _vop_vdev *vdev = data;
	struct vop_device *vpdev = vdev->vpdev;
	struct virtqueue *vq;

	vpdev->hw_ops->ack_interrupt(vpdev, vdev->h2c_vdev_db);
	list_for_each_entry(vq, &vdev->vdev.vqs, list)
		vring_interrupt(0, vq);

	return IRQ_HANDLED;
}

static void vop_virtio_release_dev(struct device *_d)
{
	struct virtio_device *vdev =
			container_of(_d, struct virtio_device, dev);
	struct _vop_vdev *vop_vdev =
			container_of(vdev, struct _vop_vdev, vdev);

	kfree(vop_vdev);
}

/*
 * adds a new device and register it with virtio
 * appropriate drivers are loaded by the device model
 */
static int _vop_add_device(struct mic_device_desc __iomem *d,
			   unsigned int offset, struct vop_device *vpdev,
			   int dnode)
{
	struct _vop_vdev *vdev, *reg_dev = NULL;
	int ret;
	u8 type = ioread8(&d->type);

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;

	vdev->vpdev = vpdev;
	vdev->vdev.dev.parent = &vpdev->dev;
	vdev->vdev.dev.release = vop_virtio_release_dev;
	vdev->vdev.id.device = type;
	vdev->vdev.config = &vop_vq_config_ops;
	vdev->desc = d;
	vdev->dc = (void __iomem *)d + _vop_aligned_desc_size(d);
	vdev->dnode = dnode;
	vdev->vdev.priv = (void *)(u64)dnode;
	init_completion(&vdev->reset_done);

	vdev->h2c_vdev_db = vpdev->hw_ops->next_db(vpdev);
	vdev->virtio_cookie = vpdev->hw_ops->request_irq(vpdev,
			vop_virtio_intr_handler, "virtio intr",
			vdev, vdev->h2c_vdev_db);
	if (IS_ERR(vdev->virtio_cookie)) {
		ret = PTR_ERR(vdev->virtio_cookie);
		goto kfree;
	}
	iowrite8((u8)vdev->h2c_vdev_db, &vdev->dc->h2c_vdev_db);
	vdev->c2h_vdev_db = ioread8(&vdev->dc->c2h_vdev_db);

	ret = register_virtio_device(&vdev->vdev);
	reg_dev = vdev;
	if (ret) {
		dev_err(_vop_dev(vdev),
			"Failed to register vop device %u type %u\n",
			offset, type);
		goto free_irq;
	}
	writeq((u64)vdev, &vdev->dc->vdev);
	dev_dbg(_vop_dev(vdev), "%s: registered vop device %u type %u vdev %p\n",
		__func__, offset, type, vdev);

	return 0;

free_irq:
	vpdev->hw_ops->free_irq(vpdev, vdev->virtio_cookie, vdev);
kfree:
	if (reg_dev)
		put_device(&vdev->vdev.dev);
	else
		kfree(vdev);
	return ret;
}

/*
 * match for a vop device with a specific desc pointer
 */
static int vop_match_desc(struct device *dev, void *data)
{
	struct virtio_device *_dev = dev_to_virtio(dev);
	struct _vop_vdev *vdev = to_vopvdev(_dev);

	return vdev->desc == (void __iomem *)data;
}

static void _vop_handle_config_change(struct mic_device_desc __iomem *d,
				      unsigned int offset,
				      struct vop_device *vpdev)
{
	struct mic_device_ctrl __iomem *dc
		= (void __iomem *)d + _vop_aligned_desc_size(d);
	struct _vop_vdev *vdev = (struct _vop_vdev *)readq(&dc->vdev);

	if (ioread8(&dc->config_change) != MIC_VIRTIO_PARAM_CONFIG_CHANGED)
		return;

	dev_dbg(&vpdev->dev, "%s %d\n", __func__, __LINE__);
	virtio_config_changed(&vdev->vdev);
	iowrite8(1, &dc->guest_ack);
}

/*
 * removes a virtio device if a hot remove event has been
 * requested by the host.
 */
static int _vop_remove_device(struct mic_device_desc __iomem *d,
			      unsigned int offset, struct vop_device *vpdev)
{
	struct mic_device_ctrl __iomem *dc
		= (void __iomem *)d + _vop_aligned_desc_size(d);
	struct _vop_vdev *vdev = (struct _vop_vdev *)readq(&dc->vdev);
	u8 status;
	int ret = -1;

	if (ioread8(&dc->config_change) == MIC_VIRTIO_PARAM_DEV_REMOVE) {
		dev_dbg(&vpdev->dev,
			"%s %d config_change %d type %d vdev %p\n",
			__func__, __LINE__,
			ioread8(&dc->config_change), ioread8(&d->type), vdev);
		status = ioread8(&d->status);
		reinit_completion(&vdev->reset_done);
		unregister_virtio_device(&vdev->vdev);
		vpdev->hw_ops->free_irq(vpdev, vdev->virtio_cookie, vdev);
		iowrite8(-1, &dc->h2c_vdev_db);
		if (status & VIRTIO_CONFIG_S_DRIVER_OK)
			wait_for_completion(&vdev->reset_done);
		put_device(&vdev->vdev.dev);
		iowrite8(1, &dc->guest_ack);
		dev_dbg(&vpdev->dev, "%s %d guest_ack %d\n",
			__func__, __LINE__, ioread8(&dc->guest_ack));
		iowrite8(-1, &d->type);
		ret = 0;
	}
	return ret;
}

#define REMOVE_DEVICES true

static void _vop_scan_devices(void __iomem *dp, struct vop_device *vpdev,
			      bool remove, int dnode)
{
	s8 type;
	unsigned int i;
	struct mic_device_desc __iomem *d;
	struct mic_device_ctrl __iomem *dc;
	struct device *dev;
	int ret;

	for (i = sizeof(struct mic_bootparam);
			i < MIC_DP_SIZE; i += _vop_total_desc_size(d)) {
		d = dp + i;
		dc = (void __iomem *)d + _vop_aligned_desc_size(d);
		/*
		 * This read barrier is paired with the corresponding write
		 * barrier on the host which is inserted before adding or
		 * removing a virtio device descriptor, by updating the type.
		 */
		rmb();
		type = ioread8(&d->type);

		/* end of list */
		if (type == 0)
			break;

		if (type == -1)
			continue;

		/* device already exists */
		dev = device_find_child(&vpdev->dev, (void __force *)d,
					vop_match_desc);
		if (dev) {
			if (remove)
				iowrite8(MIC_VIRTIO_PARAM_DEV_REMOVE,
					 &dc->config_change);
			put_device(dev);
			_vop_handle_config_change(d, i, vpdev);
			ret = _vop_remove_device(d, i, vpdev);
			if (remove) {
				iowrite8(0, &dc->config_change);
				iowrite8(0, &dc->guest_ack);
			}
			continue;
		}

		/* new device */
		dev_dbg(&vpdev->dev, "%s %d Adding new virtio device %p\n",
			__func__, __LINE__, d);
		if (!remove)
			_vop_add_device(d, i, vpdev, dnode);
	}
}

static void vop_scan_devices(struct vop_info *vi,
			     struct vop_device *vpdev, bool remove)
{
	void __iomem *dp = vpdev->hw_ops->get_remote_dp(vpdev);

	if (!dp)
		return;
	mutex_lock(&vi->vop_mutex);
	_vop_scan_devices(dp, vpdev, remove, vpdev->dnode);
	mutex_unlock(&vi->vop_mutex);
}

/*
 * vop_hotplug_device tries to find changes in the device page.
 */
static void vop_hotplug_devices(struct work_struct *work)
{
	struct vop_info *vi = container_of(work, struct vop_info,
					     hotplug_work);

	vop_scan_devices(vi, vi->vpdev, !REMOVE_DEVICES);
}

/*
 * Interrupt handler for hot plug/config changes etc.
 */
static irqreturn_t vop_extint_handler(int irq, void *data)
{
	struct vop_info *vi = data;
	struct mic_bootparam __iomem *bp;
	struct vop_device *vpdev = vi->vpdev;

	bp = vpdev->hw_ops->get_remote_dp(vpdev);
	dev_dbg(&vpdev->dev, "%s %d hotplug work\n",
		__func__, __LINE__);
	vpdev->hw_ops->ack_interrupt(vpdev, ioread8(&bp->h2c_config_db));
	schedule_work(&vi->hotplug_work);
	return IRQ_HANDLED;
}

static int vop_driver_probe(struct vop_device *vpdev)
{
	struct vop_info *vi;
	int rc;

	vi = kzalloc(sizeof(*vi), GFP_KERNEL);
	if (!vi) {
		rc = -ENOMEM;
		goto exit;
	}
	dev_set_drvdata(&vpdev->dev, vi);
	vi->vpdev = vpdev;

	mutex_init(&vi->vop_mutex);
	INIT_WORK(&vi->hotplug_work, vop_hotplug_devices);
	if (vpdev->dnode) {
		rc = vop_host_init(vi);
		if (rc < 0)
			goto free;
	} else {
		struct mic_bootparam __iomem *bootparam;

		vop_scan_devices(vi, vpdev, !REMOVE_DEVICES);

		vi->h2c_config_db = vpdev->hw_ops->next_db(vpdev);
		vi->cookie = vpdev->hw_ops->request_irq(vpdev,
							vop_extint_handler,
							"virtio_config_intr",
							vi, vi->h2c_config_db);
		if (IS_ERR(vi->cookie)) {
			rc = PTR_ERR(vi->cookie);
			goto free;
		}
		bootparam = vpdev->hw_ops->get_remote_dp(vpdev);
		iowrite8(vi->h2c_config_db, &bootparam->h2c_config_db);
	}
	vop_init_debugfs(vi);
	return 0;
free:
	kfree(vi);
exit:
	return rc;
}

static void vop_driver_remove(struct vop_device *vpdev)
{
	struct vop_info *vi = dev_get_drvdata(&vpdev->dev);

	if (vpdev->dnode) {
		vop_host_uninit(vi);
	} else {
		struct mic_bootparam __iomem *bootparam =
			vpdev->hw_ops->get_remote_dp(vpdev);
		if (bootparam)
			iowrite8(-1, &bootparam->h2c_config_db);
		vpdev->hw_ops->free_irq(vpdev, vi->cookie, vi);
		flush_work(&vi->hotplug_work);
		vop_scan_devices(vi, vpdev, REMOVE_DEVICES);
	}
	vop_exit_debugfs(vi);
	kfree(vi);
}

static struct vop_device_id id_table[] = {
	{ VOP_DEV_TRNSP, VOP_DEV_ANY_ID },
	{ 0 },
};

static struct vop_driver vop_driver = {
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table = id_table,
	.probe = vop_driver_probe,
	.remove = vop_driver_remove,
};

module_vop_driver(vop_driver);

MODULE_DEVICE_TABLE(mbus, id_table);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Virtio Over PCIe (VOP) driver");
MODULE_LICENSE("GPL v2");
