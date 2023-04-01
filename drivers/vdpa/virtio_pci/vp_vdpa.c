// SPDX-License-Identifier: GPL-2.0-only
/*
 * vDPA bridge driver for modern virtio-pci device
 *
 * Copyright (c) 2020, Red Hat Inc. All rights reserved.
 * Author: Jason Wang <jasowang@redhat.com>
 *
 * Based on virtio_pci_modern.c.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vdpa.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include <linux/virtio_pci_modern.h>
#include <uapi/linux/vdpa.h>

#define VP_VDPA_QUEUE_MAX 256
#define VP_VDPA_DRIVER_NAME "vp_vdpa"
#define VP_VDPA_NAME_SIZE 256

struct vp_vring {
	void __iomem *notify;
	char msix_name[VP_VDPA_NAME_SIZE];
	struct vdpa_callback cb;
	resource_size_t notify_pa;
	int irq;
};

struct vp_vdpa {
	struct vdpa_device vdpa;
	struct virtio_pci_modern_device *mdev;
	struct vp_vring *vring;
	struct vdpa_callback config_cb;
	u64 device_features;
	char msix_name[VP_VDPA_NAME_SIZE];
	int config_irq;
	int queues;
	int vectors;
};

struct vp_vdpa_mgmtdev {
	struct vdpa_mgmt_dev mgtdev;
	struct virtio_pci_modern_device *mdev;
	struct vp_vdpa *vp_vdpa;
};

static struct vp_vdpa *vdpa_to_vp(struct vdpa_device *vdpa)
{
	return container_of(vdpa, struct vp_vdpa, vdpa);
}

static struct virtio_pci_modern_device *vdpa_to_mdev(struct vdpa_device *vdpa)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);

	return vp_vdpa->mdev;
}

static struct virtio_pci_modern_device *vp_vdpa_to_mdev(struct vp_vdpa *vp_vdpa)
{
	return vp_vdpa->mdev;
}

static u64 vp_vdpa_get_device_features(struct vdpa_device *vdpa)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);

	return vp_vdpa->device_features;
}

static int vp_vdpa_set_driver_features(struct vdpa_device *vdpa, u64 features)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	vp_modern_set_features(mdev, features);

	return 0;
}

static u64 vp_vdpa_get_driver_features(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return vp_modern_get_driver_features(mdev);
}

static u8 vp_vdpa_get_status(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return vp_modern_get_status(mdev);
}

static int vp_vdpa_get_vq_irq(struct vdpa_device *vdpa, u16 idx)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	int irq = vp_vdpa->vring[idx].irq;

	if (irq == VIRTIO_MSI_NO_VECTOR)
		return -EINVAL;

	return irq;
}

static void vp_vdpa_free_irq(struct vp_vdpa *vp_vdpa)
{
	struct virtio_pci_modern_device *mdev = vp_vdpa_to_mdev(vp_vdpa);
	struct pci_dev *pdev = mdev->pci_dev;
	int i;

	for (i = 0; i < vp_vdpa->queues; i++) {
		if (vp_vdpa->vring[i].irq != VIRTIO_MSI_NO_VECTOR) {
			vp_modern_queue_vector(mdev, i, VIRTIO_MSI_NO_VECTOR);
			devm_free_irq(&pdev->dev, vp_vdpa->vring[i].irq,
				      &vp_vdpa->vring[i]);
			vp_vdpa->vring[i].irq = VIRTIO_MSI_NO_VECTOR;
		}
	}

	if (vp_vdpa->config_irq != VIRTIO_MSI_NO_VECTOR) {
		vp_modern_config_vector(mdev, VIRTIO_MSI_NO_VECTOR);
		devm_free_irq(&pdev->dev, vp_vdpa->config_irq, vp_vdpa);
		vp_vdpa->config_irq = VIRTIO_MSI_NO_VECTOR;
	}

	if (vp_vdpa->vectors) {
		pci_free_irq_vectors(pdev);
		vp_vdpa->vectors = 0;
	}
}

static irqreturn_t vp_vdpa_vq_handler(int irq, void *arg)
{
	struct vp_vring *vring = arg;

	if (vring->cb.callback)
		return vring->cb.callback(vring->cb.private);

	return IRQ_HANDLED;
}

static irqreturn_t vp_vdpa_config_handler(int irq, void *arg)
{
	struct vp_vdpa *vp_vdpa = arg;

	if (vp_vdpa->config_cb.callback)
		return vp_vdpa->config_cb.callback(vp_vdpa->config_cb.private);

	return IRQ_HANDLED;
}

static int vp_vdpa_request_irq(struct vp_vdpa *vp_vdpa)
{
	struct virtio_pci_modern_device *mdev = vp_vdpa_to_mdev(vp_vdpa);
	struct pci_dev *pdev = mdev->pci_dev;
	int i, ret, irq;
	int queues = vp_vdpa->queues;
	int vectors = queues + 1;

	ret = pci_alloc_irq_vectors(pdev, vectors, vectors, PCI_IRQ_MSIX);
	if (ret != vectors) {
		dev_err(&pdev->dev,
			"vp_vdpa: fail to allocate irq vectors want %d but %d\n",
			vectors, ret);
		return ret;
	}

	vp_vdpa->vectors = vectors;

	for (i = 0; i < queues; i++) {
		snprintf(vp_vdpa->vring[i].msix_name, VP_VDPA_NAME_SIZE,
			"vp-vdpa[%s]-%d\n", pci_name(pdev), i);
		irq = pci_irq_vector(pdev, i);
		ret = devm_request_irq(&pdev->dev, irq,
				       vp_vdpa_vq_handler,
				       0, vp_vdpa->vring[i].msix_name,
				       &vp_vdpa->vring[i]);
		if (ret) {
			dev_err(&pdev->dev,
				"vp_vdpa: fail to request irq for vq %d\n", i);
			goto err;
		}
		vp_modern_queue_vector(mdev, i, i);
		vp_vdpa->vring[i].irq = irq;
	}

	snprintf(vp_vdpa->msix_name, VP_VDPA_NAME_SIZE, "vp-vdpa[%s]-config\n",
		 pci_name(pdev));
	irq = pci_irq_vector(pdev, queues);
	ret = devm_request_irq(&pdev->dev, irq,	vp_vdpa_config_handler, 0,
			       vp_vdpa->msix_name, vp_vdpa);
	if (ret) {
		dev_err(&pdev->dev,
			"vp_vdpa: fail to request irq for vq %d\n", i);
			goto err;
	}
	vp_modern_config_vector(mdev, queues);
	vp_vdpa->config_irq = irq;

	return 0;
err:
	vp_vdpa_free_irq(vp_vdpa);
	return ret;
}

static void vp_vdpa_set_status(struct vdpa_device *vdpa, u8 status)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_modern_device *mdev = vp_vdpa_to_mdev(vp_vdpa);
	u8 s = vp_vdpa_get_status(vdpa);

	if (status & VIRTIO_CONFIG_S_DRIVER_OK &&
	    !(s & VIRTIO_CONFIG_S_DRIVER_OK)) {
		vp_vdpa_request_irq(vp_vdpa);
	}

	vp_modern_set_status(mdev, status);
}

static int vp_vdpa_reset(struct vdpa_device *vdpa)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_modern_device *mdev = vp_vdpa_to_mdev(vp_vdpa);
	u8 s = vp_vdpa_get_status(vdpa);

	vp_modern_set_status(mdev, 0);

	if (s & VIRTIO_CONFIG_S_DRIVER_OK)
		vp_vdpa_free_irq(vp_vdpa);

	return 0;
}

static u16 vp_vdpa_get_vq_num_max(struct vdpa_device *vdpa)
{
	return VP_VDPA_QUEUE_MAX;
}

static int vp_vdpa_get_vq_state(struct vdpa_device *vdpa, u16 qid,
				struct vdpa_vq_state *state)
{
	/* Note that this is not supported by virtio specification, so
	 * we return -EOPNOTSUPP here. This means we can't support live
	 * migration, vhost device start/stop.
	 */
	return -EOPNOTSUPP;
}

static int vp_vdpa_set_vq_state_split(struct vdpa_device *vdpa,
				      const struct vdpa_vq_state *state)
{
	const struct vdpa_vq_state_split *split = &state->split;

	if (split->avail_index == 0)
		return 0;

	return -EOPNOTSUPP;
}

static int vp_vdpa_set_vq_state_packed(struct vdpa_device *vdpa,
				       const struct vdpa_vq_state *state)
{
	const struct vdpa_vq_state_packed *packed = &state->packed;

	if (packed->last_avail_counter == 1 &&
	    packed->last_avail_idx == 0 &&
	    packed->last_used_counter == 1 &&
	    packed->last_used_idx == 0)
		return 0;

	return -EOPNOTSUPP;
}

static int vp_vdpa_set_vq_state(struct vdpa_device *vdpa, u16 qid,
				const struct vdpa_vq_state *state)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	/* Note that this is not supported by virtio specification.
	 * But if the state is by chance equal to the device initial
	 * state, we can let it go.
	 */
	if ((vp_modern_get_status(mdev) & VIRTIO_CONFIG_S_FEATURES_OK) &&
	    !vp_modern_get_queue_enable(mdev, qid)) {
		if (vp_modern_get_driver_features(mdev) &
		    BIT_ULL(VIRTIO_F_RING_PACKED))
			return vp_vdpa_set_vq_state_packed(vdpa, state);
		else
			return vp_vdpa_set_vq_state_split(vdpa,	state);
	}

	return -EOPNOTSUPP;
}

static void vp_vdpa_set_vq_cb(struct vdpa_device *vdpa, u16 qid,
			      struct vdpa_callback *cb)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);

	vp_vdpa->vring[qid].cb = *cb;
}

static void vp_vdpa_set_vq_ready(struct vdpa_device *vdpa,
				 u16 qid, bool ready)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	vp_modern_set_queue_enable(mdev, qid, ready);
}

static bool vp_vdpa_get_vq_ready(struct vdpa_device *vdpa, u16 qid)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return vp_modern_get_queue_enable(mdev, qid);
}

static void vp_vdpa_set_vq_num(struct vdpa_device *vdpa, u16 qid,
			       u32 num)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	vp_modern_set_queue_size(mdev, qid, num);
}

static int vp_vdpa_set_vq_address(struct vdpa_device *vdpa, u16 qid,
				  u64 desc_area, u64 driver_area,
				  u64 device_area)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	vp_modern_queue_address(mdev, qid, desc_area,
				driver_area, device_area);

	return 0;
}

static void vp_vdpa_kick_vq(struct vdpa_device *vdpa, u16 qid)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);

	vp_iowrite16(qid, vp_vdpa->vring[qid].notify);
}

static u32 vp_vdpa_get_generation(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return vp_modern_generation(mdev);
}

static u32 vp_vdpa_get_device_id(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return mdev->id.device;
}

static u32 vp_vdpa_get_vendor_id(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return mdev->id.vendor;
}

static u32 vp_vdpa_get_vq_align(struct vdpa_device *vdpa)
{
	return PAGE_SIZE;
}

static size_t vp_vdpa_get_config_size(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return mdev->device_len;
}

static void vp_vdpa_get_config(struct vdpa_device *vdpa,
			       unsigned int offset,
			       void *buf, unsigned int len)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_modern_device *mdev = vp_vdpa_to_mdev(vp_vdpa);
	u8 old, new;
	u8 *p;
	int i;

	do {
		old = vp_ioread8(&mdev->common->config_generation);
		p = buf;
		for (i = 0; i < len; i++)
			*p++ = vp_ioread8(mdev->device + offset + i);

		new = vp_ioread8(&mdev->common->config_generation);
	} while (old != new);
}

static void vp_vdpa_set_config(struct vdpa_device *vdpa,
			       unsigned int offset, const void *buf,
			       unsigned int len)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_modern_device *mdev = vp_vdpa_to_mdev(vp_vdpa);
	const u8 *p = buf;
	int i;

	for (i = 0; i < len; i++)
		vp_iowrite8(*p++, mdev->device + offset + i);
}

static void vp_vdpa_set_config_cb(struct vdpa_device *vdpa,
				  struct vdpa_callback *cb)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);

	vp_vdpa->config_cb = *cb;
}

static struct vdpa_notification_area
vp_vdpa_get_vq_notification(struct vdpa_device *vdpa, u16 qid)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_modern_device *mdev = vp_vdpa_to_mdev(vp_vdpa);
	struct vdpa_notification_area notify;

	notify.addr = vp_vdpa->vring[qid].notify_pa;
	notify.size = mdev->notify_offset_multiplier;

	return notify;
}

static const struct vdpa_config_ops vp_vdpa_ops = {
	.get_device_features = vp_vdpa_get_device_features,
	.set_driver_features = vp_vdpa_set_driver_features,
	.get_driver_features = vp_vdpa_get_driver_features,
	.get_status	= vp_vdpa_get_status,
	.set_status	= vp_vdpa_set_status,
	.reset		= vp_vdpa_reset,
	.get_vq_num_max	= vp_vdpa_get_vq_num_max,
	.get_vq_state	= vp_vdpa_get_vq_state,
	.get_vq_notification = vp_vdpa_get_vq_notification,
	.set_vq_state	= vp_vdpa_set_vq_state,
	.set_vq_cb	= vp_vdpa_set_vq_cb,
	.set_vq_ready	= vp_vdpa_set_vq_ready,
	.get_vq_ready	= vp_vdpa_get_vq_ready,
	.set_vq_num	= vp_vdpa_set_vq_num,
	.set_vq_address	= vp_vdpa_set_vq_address,
	.kick_vq	= vp_vdpa_kick_vq,
	.get_generation	= vp_vdpa_get_generation,
	.get_device_id	= vp_vdpa_get_device_id,
	.get_vendor_id	= vp_vdpa_get_vendor_id,
	.get_vq_align	= vp_vdpa_get_vq_align,
	.get_config_size = vp_vdpa_get_config_size,
	.get_config	= vp_vdpa_get_config,
	.set_config	= vp_vdpa_set_config,
	.set_config_cb  = vp_vdpa_set_config_cb,
	.get_vq_irq	= vp_vdpa_get_vq_irq,
};

static void vp_vdpa_free_irq_vectors(void *data)
{
	pci_free_irq_vectors(data);
}

static int vp_vdpa_dev_add(struct vdpa_mgmt_dev *v_mdev, const char *name,
			   const struct vdpa_dev_set_config *add_config)
{
	struct vp_vdpa_mgmtdev *vp_vdpa_mgtdev =
		container_of(v_mdev, struct vp_vdpa_mgmtdev, mgtdev);

	struct virtio_pci_modern_device *mdev = vp_vdpa_mgtdev->mdev;
	struct pci_dev *pdev = mdev->pci_dev;
	struct device *dev = &pdev->dev;
	struct vp_vdpa *vp_vdpa = NULL;
	u64 device_features;
	int ret, i;

	vp_vdpa = vdpa_alloc_device(struct vp_vdpa, vdpa,
				    dev, &vp_vdpa_ops, 1, 1, name, false);

	if (IS_ERR(vp_vdpa)) {
		dev_err(dev, "vp_vdpa: Failed to allocate vDPA structure\n");
		return PTR_ERR(vp_vdpa);
	}

	vp_vdpa_mgtdev->vp_vdpa = vp_vdpa;

	vp_vdpa->vdpa.dma_dev = &pdev->dev;
	vp_vdpa->queues = vp_modern_get_num_queues(mdev);
	vp_vdpa->mdev = mdev;

	device_features = vp_modern_get_features(mdev);
	if (add_config->mask & BIT_ULL(VDPA_ATTR_DEV_FEATURES)) {
		if (add_config->device_features & ~device_features) {
			ret = -EINVAL;
			dev_err(&pdev->dev, "Try to provision features "
				"that are not supported by the device: "
				"device_features 0x%llx provisioned 0x%llx\n",
				device_features, add_config->device_features);
			goto err;
		}
		device_features = add_config->device_features;
	}
	vp_vdpa->device_features = device_features;

	ret = devm_add_action_or_reset(dev, vp_vdpa_free_irq_vectors, pdev);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed for adding devres for freeing irq vectors\n");
		goto err;
	}

	vp_vdpa->vring = devm_kcalloc(&pdev->dev, vp_vdpa->queues,
				      sizeof(*vp_vdpa->vring),
				      GFP_KERNEL);
	if (!vp_vdpa->vring) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Fail to allocate virtqueues\n");
		goto err;
	}

	for (i = 0; i < vp_vdpa->queues; i++) {
		vp_vdpa->vring[i].irq = VIRTIO_MSI_NO_VECTOR;
		vp_vdpa->vring[i].notify =
			vp_modern_map_vq_notify(mdev, i,
						&vp_vdpa->vring[i].notify_pa);
		if (!vp_vdpa->vring[i].notify) {
			ret = -EINVAL;
			dev_warn(&pdev->dev, "Fail to map vq notify %d\n", i);
			goto err;
		}
	}
	vp_vdpa->config_irq = VIRTIO_MSI_NO_VECTOR;

	vp_vdpa->vdpa.mdev = &vp_vdpa_mgtdev->mgtdev;
	ret = _vdpa_register_device(&vp_vdpa->vdpa, vp_vdpa->queues);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register to vdpa bus\n");
		goto err;
	}

	return 0;

err:
	put_device(&vp_vdpa->vdpa.dev);
	return ret;
}

static void vp_vdpa_dev_del(struct vdpa_mgmt_dev *v_mdev,
			    struct vdpa_device *dev)
{
	struct vp_vdpa_mgmtdev *vp_vdpa_mgtdev =
		container_of(v_mdev, struct vp_vdpa_mgmtdev, mgtdev);

	struct vp_vdpa *vp_vdpa = vp_vdpa_mgtdev->vp_vdpa;

	_vdpa_unregister_device(&vp_vdpa->vdpa);
	vp_vdpa_mgtdev->vp_vdpa = NULL;
}

static const struct vdpa_mgmtdev_ops vp_vdpa_mdev_ops = {
	.dev_add = vp_vdpa_dev_add,
	.dev_del = vp_vdpa_dev_del,
};

static int vp_vdpa_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct vp_vdpa_mgmtdev *vp_vdpa_mgtdev = NULL;
	struct vdpa_mgmt_dev *mgtdev;
	struct device *dev = &pdev->dev;
	struct virtio_pci_modern_device *mdev = NULL;
	struct virtio_device_id *mdev_id = NULL;
	int err;

	vp_vdpa_mgtdev = kzalloc(sizeof(*vp_vdpa_mgtdev), GFP_KERNEL);
	if (!vp_vdpa_mgtdev)
		return -ENOMEM;

	mgtdev = &vp_vdpa_mgtdev->mgtdev;
	mgtdev->ops = &vp_vdpa_mdev_ops;
	mgtdev->device = dev;

	mdev = kzalloc(sizeof(struct virtio_pci_modern_device), GFP_KERNEL);
	if (!mdev) {
		err = -ENOMEM;
		goto mdev_err;
	}

	mdev_id = kzalloc(sizeof(struct virtio_device_id), GFP_KERNEL);
	if (!mdev_id) {
		err = -ENOMEM;
		goto mdev_id_err;
	}

	vp_vdpa_mgtdev->mdev = mdev;
	mdev->pci_dev = pdev;

	err = pcim_enable_device(pdev);
	if (err) {
		goto probe_err;
	}

	err = vp_modern_probe(mdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to probe modern PCI device\n");
		goto probe_err;
	}

	mdev_id->device = mdev->id.device;
	mdev_id->vendor = mdev->id.vendor;
	mgtdev->id_table = mdev_id;
	mgtdev->max_supported_vqs = vp_modern_get_num_queues(mdev);
	mgtdev->supported_features = vp_modern_get_features(mdev);
	mgtdev->config_attr_mask = (1 << VDPA_ATTR_DEV_FEATURES);
	pci_set_master(pdev);
	pci_set_drvdata(pdev, vp_vdpa_mgtdev);

	err = vdpa_mgmtdev_register(mgtdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register vdpa mgmtdev device\n");
		goto register_err;
	}

	return 0;

register_err:
	vp_modern_remove(vp_vdpa_mgtdev->mdev);
probe_err:
	kfree(mdev_id);
mdev_id_err:
	kfree(mdev);
mdev_err:
	kfree(vp_vdpa_mgtdev);
	return err;
}

static void vp_vdpa_remove(struct pci_dev *pdev)
{
	struct vp_vdpa_mgmtdev *vp_vdpa_mgtdev = pci_get_drvdata(pdev);
	struct virtio_pci_modern_device *mdev = NULL;

	mdev = vp_vdpa_mgtdev->mdev;
	vdpa_mgmtdev_unregister(&vp_vdpa_mgtdev->mgtdev);
	vp_modern_remove(mdev);
	kfree(vp_vdpa_mgtdev->mgtdev.id_table);
	kfree(mdev);
	kfree(vp_vdpa_mgtdev);
}

static struct pci_driver vp_vdpa_driver = {
	.name		= "vp-vdpa",
	.id_table	= NULL, /* only dynamic ids */
	.probe		= vp_vdpa_probe,
	.remove		= vp_vdpa_remove,
};

module_pci_driver(vp_vdpa_driver);

MODULE_AUTHOR("Jason Wang <jasowang@redhat.com>");
MODULE_DESCRIPTION("vp-vdpa");
MODULE_LICENSE("GPL");
MODULE_VERSION("1");
