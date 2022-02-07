// SPDX-License-Identifier: GPL-2.0-only
/*
 * vDPA bridge driver for Alibaba ENI(Elastic Network Interface)
 *
 * Copyright (c) 2021, Alibaba Inc. All rights reserved.
 * Author: Wu Zongyong <wuzongyong@linux.alibaba.com>
 *
 */

#include "linux/bits.h"
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vdpa.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include <linux/virtio_pci_legacy.h>
#include <uapi/linux/virtio_net.h>

#define ENI_MSIX_NAME_SIZE 256

#define ENI_ERR(pdev, fmt, ...)	\
	dev_err(&pdev->dev, "%s"fmt, "eni_vdpa: ", ##__VA_ARGS__)
#define ENI_DBG(pdev, fmt, ...)	\
	dev_dbg(&pdev->dev, "%s"fmt, "eni_vdpa: ", ##__VA_ARGS__)
#define ENI_INFO(pdev, fmt, ...) \
	dev_info(&pdev->dev, "%s"fmt, "eni_vdpa: ", ##__VA_ARGS__)

struct eni_vring {
	void __iomem *notify;
	char msix_name[ENI_MSIX_NAME_SIZE];
	struct vdpa_callback cb;
	int irq;
};

struct eni_vdpa {
	struct vdpa_device vdpa;
	struct virtio_pci_legacy_device ldev;
	struct eni_vring *vring;
	struct vdpa_callback config_cb;
	char msix_name[ENI_MSIX_NAME_SIZE];
	int config_irq;
	int queues;
	int vectors;
};

static struct eni_vdpa *vdpa_to_eni(struct vdpa_device *vdpa)
{
	return container_of(vdpa, struct eni_vdpa, vdpa);
}

static struct virtio_pci_legacy_device *vdpa_to_ldev(struct vdpa_device *vdpa)
{
	struct eni_vdpa *eni_vdpa = vdpa_to_eni(vdpa);

	return &eni_vdpa->ldev;
}

static u64 eni_vdpa_get_device_features(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);
	u64 features = vp_legacy_get_features(ldev);

	features |= BIT_ULL(VIRTIO_F_ACCESS_PLATFORM);
	features |= BIT_ULL(VIRTIO_F_ORDER_PLATFORM);

	return features;
}

static int eni_vdpa_set_driver_features(struct vdpa_device *vdpa, u64 features)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	if (!(features & BIT_ULL(VIRTIO_NET_F_MRG_RXBUF)) && features) {
		ENI_ERR(ldev->pci_dev,
			"VIRTIO_NET_F_MRG_RXBUF is not negotiated\n");
		return -EINVAL;
	}

	vp_legacy_set_features(ldev, (u32)features);

	return 0;
}

static u64 eni_vdpa_get_driver_features(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return vp_legacy_get_driver_features(ldev);
}

static u8 eni_vdpa_get_status(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return vp_legacy_get_status(ldev);
}

static int eni_vdpa_get_vq_irq(struct vdpa_device *vdpa, u16 idx)
{
	struct eni_vdpa *eni_vdpa = vdpa_to_eni(vdpa);
	int irq = eni_vdpa->vring[idx].irq;

	if (irq == VIRTIO_MSI_NO_VECTOR)
		return -EINVAL;

	return irq;
}

static void eni_vdpa_free_irq(struct eni_vdpa *eni_vdpa)
{
	struct virtio_pci_legacy_device *ldev = &eni_vdpa->ldev;
	struct pci_dev *pdev = ldev->pci_dev;
	int i;

	for (i = 0; i < eni_vdpa->queues; i++) {
		if (eni_vdpa->vring[i].irq != VIRTIO_MSI_NO_VECTOR) {
			vp_legacy_queue_vector(ldev, i, VIRTIO_MSI_NO_VECTOR);
			devm_free_irq(&pdev->dev, eni_vdpa->vring[i].irq,
				      &eni_vdpa->vring[i]);
			eni_vdpa->vring[i].irq = VIRTIO_MSI_NO_VECTOR;
		}
	}

	if (eni_vdpa->config_irq != VIRTIO_MSI_NO_VECTOR) {
		vp_legacy_config_vector(ldev, VIRTIO_MSI_NO_VECTOR);
		devm_free_irq(&pdev->dev, eni_vdpa->config_irq, eni_vdpa);
		eni_vdpa->config_irq = VIRTIO_MSI_NO_VECTOR;
	}

	if (eni_vdpa->vectors) {
		pci_free_irq_vectors(pdev);
		eni_vdpa->vectors = 0;
	}
}

static irqreturn_t eni_vdpa_vq_handler(int irq, void *arg)
{
	struct eni_vring *vring = arg;

	if (vring->cb.callback)
		return vring->cb.callback(vring->cb.private);

	return IRQ_HANDLED;
}

static irqreturn_t eni_vdpa_config_handler(int irq, void *arg)
{
	struct eni_vdpa *eni_vdpa = arg;

	if (eni_vdpa->config_cb.callback)
		return eni_vdpa->config_cb.callback(eni_vdpa->config_cb.private);

	return IRQ_HANDLED;
}

static int eni_vdpa_request_irq(struct eni_vdpa *eni_vdpa)
{
	struct virtio_pci_legacy_device *ldev = &eni_vdpa->ldev;
	struct pci_dev *pdev = ldev->pci_dev;
	int i, ret, irq;
	int queues = eni_vdpa->queues;
	int vectors = queues + 1;

	ret = pci_alloc_irq_vectors(pdev, vectors, vectors, PCI_IRQ_MSIX);
	if (ret != vectors) {
		ENI_ERR(pdev,
			"failed to allocate irq vectors want %d but %d\n",
			vectors, ret);
		return ret;
	}

	eni_vdpa->vectors = vectors;

	for (i = 0; i < queues; i++) {
		snprintf(eni_vdpa->vring[i].msix_name, ENI_MSIX_NAME_SIZE,
			 "eni-vdpa[%s]-%d\n", pci_name(pdev), i);
		irq = pci_irq_vector(pdev, i);
		ret = devm_request_irq(&pdev->dev, irq,
				       eni_vdpa_vq_handler,
				       0, eni_vdpa->vring[i].msix_name,
				       &eni_vdpa->vring[i]);
		if (ret) {
			ENI_ERR(pdev, "failed to request irq for vq %d\n", i);
			goto err;
		}
		vp_legacy_queue_vector(ldev, i, i);
		eni_vdpa->vring[i].irq = irq;
	}

	snprintf(eni_vdpa->msix_name, ENI_MSIX_NAME_SIZE, "eni-vdpa[%s]-config\n",
		 pci_name(pdev));
	irq = pci_irq_vector(pdev, queues);
	ret = devm_request_irq(&pdev->dev, irq, eni_vdpa_config_handler, 0,
			       eni_vdpa->msix_name, eni_vdpa);
	if (ret) {
		ENI_ERR(pdev, "failed to request irq for config vq %d\n", i);
		goto err;
	}
	vp_legacy_config_vector(ldev, queues);
	eni_vdpa->config_irq = irq;

	return 0;
err:
	eni_vdpa_free_irq(eni_vdpa);
	return ret;
}

static void eni_vdpa_set_status(struct vdpa_device *vdpa, u8 status)
{
	struct eni_vdpa *eni_vdpa = vdpa_to_eni(vdpa);
	struct virtio_pci_legacy_device *ldev = &eni_vdpa->ldev;
	u8 s = eni_vdpa_get_status(vdpa);

	if (status & VIRTIO_CONFIG_S_DRIVER_OK &&
	    !(s & VIRTIO_CONFIG_S_DRIVER_OK)) {
		eni_vdpa_request_irq(eni_vdpa);
	}

	vp_legacy_set_status(ldev, status);

	if (!(status & VIRTIO_CONFIG_S_DRIVER_OK) &&
	    (s & VIRTIO_CONFIG_S_DRIVER_OK))
		eni_vdpa_free_irq(eni_vdpa);
}

static int eni_vdpa_reset(struct vdpa_device *vdpa)
{
	struct eni_vdpa *eni_vdpa = vdpa_to_eni(vdpa);
	struct virtio_pci_legacy_device *ldev = &eni_vdpa->ldev;
	u8 s = eni_vdpa_get_status(vdpa);

	vp_legacy_set_status(ldev, 0);

	if (s & VIRTIO_CONFIG_S_DRIVER_OK)
		eni_vdpa_free_irq(eni_vdpa);

	return 0;
}

static u16 eni_vdpa_get_vq_num_max(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return vp_legacy_get_queue_size(ldev, 0);
}

static u16 eni_vdpa_get_vq_num_min(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return vp_legacy_get_queue_size(ldev, 0);
}

static int eni_vdpa_get_vq_state(struct vdpa_device *vdpa, u16 qid,
				struct vdpa_vq_state *state)
{
	return -EOPNOTSUPP;
}

static int eni_vdpa_set_vq_state(struct vdpa_device *vdpa, u16 qid,
				 const struct vdpa_vq_state *state)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);
	const struct vdpa_vq_state_split *split = &state->split;

	/* ENI is build upon virtio-pci specfication which not support
	 * to set state of virtqueue. But if the state is equal to the
	 * device initial state by chance, we can let it go.
	 */
	if (!vp_legacy_get_queue_enable(ldev, qid)
	    && split->avail_index == 0)
		return 0;

	return -EOPNOTSUPP;
}


static void eni_vdpa_set_vq_cb(struct vdpa_device *vdpa, u16 qid,
			       struct vdpa_callback *cb)
{
	struct eni_vdpa *eni_vdpa = vdpa_to_eni(vdpa);

	eni_vdpa->vring[qid].cb = *cb;
}

static void eni_vdpa_set_vq_ready(struct vdpa_device *vdpa, u16 qid,
				  bool ready)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	/* ENI is a legacy virtio-pci device. This is not supported
	 * by specification. But we can disable virtqueue by setting
	 * address to 0.
	 */
	if (!ready)
		vp_legacy_set_queue_address(ldev, qid, 0);
}

static bool eni_vdpa_get_vq_ready(struct vdpa_device *vdpa, u16 qid)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return vp_legacy_get_queue_enable(ldev, qid);
}

static void eni_vdpa_set_vq_num(struct vdpa_device *vdpa, u16 qid,
			       u32 num)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);
	struct pci_dev *pdev = ldev->pci_dev;
	u16 n = vp_legacy_get_queue_size(ldev, qid);

	/* ENI is a legacy virtio-pci device which not allow to change
	 * virtqueue size. Just report a error if someone tries to
	 * change it.
	 */
	if (num != n)
		ENI_ERR(pdev,
			"not support to set vq %u fixed num %u to %u\n",
			qid, n, num);
}

static int eni_vdpa_set_vq_address(struct vdpa_device *vdpa, u16 qid,
				   u64 desc_area, u64 driver_area,
				   u64 device_area)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);
	u32 pfn = desc_area >> VIRTIO_PCI_QUEUE_ADDR_SHIFT;

	vp_legacy_set_queue_address(ldev, qid, pfn);

	return 0;
}

static void eni_vdpa_kick_vq(struct vdpa_device *vdpa, u16 qid)
{
	struct eni_vdpa *eni_vdpa = vdpa_to_eni(vdpa);

	iowrite16(qid, eni_vdpa->vring[qid].notify);
}

static u32 eni_vdpa_get_device_id(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return ldev->id.device;
}

static u32 eni_vdpa_get_vendor_id(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return ldev->id.vendor;
}

static u32 eni_vdpa_get_vq_align(struct vdpa_device *vdpa)
{
	return VIRTIO_PCI_VRING_ALIGN;
}

static size_t eni_vdpa_get_config_size(struct vdpa_device *vdpa)
{
	return sizeof(struct virtio_net_config);
}


static void eni_vdpa_get_config(struct vdpa_device *vdpa,
				unsigned int offset,
				void *buf, unsigned int len)
{
	struct eni_vdpa *eni_vdpa = vdpa_to_eni(vdpa);
	struct virtio_pci_legacy_device *ldev = &eni_vdpa->ldev;
	void __iomem *ioaddr = ldev->ioaddr +
		VIRTIO_PCI_CONFIG_OFF(eni_vdpa->vectors) +
		offset;
	u8 *p = buf;
	int i;

	for (i = 0; i < len; i++)
		*p++ = ioread8(ioaddr + i);
}

static void eni_vdpa_set_config(struct vdpa_device *vdpa,
				unsigned int offset, const void *buf,
				unsigned int len)
{
	struct eni_vdpa *eni_vdpa = vdpa_to_eni(vdpa);
	struct virtio_pci_legacy_device *ldev = &eni_vdpa->ldev;
	void __iomem *ioaddr = ldev->ioaddr +
		VIRTIO_PCI_CONFIG_OFF(eni_vdpa->vectors) +
		offset;
	const u8 *p = buf;
	int i;

	for (i = 0; i < len; i++)
		iowrite8(*p++, ioaddr + i);
}

static void eni_vdpa_set_config_cb(struct vdpa_device *vdpa,
				   struct vdpa_callback *cb)
{
	struct eni_vdpa *eni_vdpa = vdpa_to_eni(vdpa);

	eni_vdpa->config_cb = *cb;
}

static const struct vdpa_config_ops eni_vdpa_ops = {
	.get_device_features = eni_vdpa_get_device_features,
	.set_driver_features = eni_vdpa_set_driver_features,
	.get_driver_features = eni_vdpa_get_driver_features,
	.get_status	= eni_vdpa_get_status,
	.set_status	= eni_vdpa_set_status,
	.reset		= eni_vdpa_reset,
	.get_vq_num_max	= eni_vdpa_get_vq_num_max,
	.get_vq_num_min	= eni_vdpa_get_vq_num_min,
	.get_vq_state	= eni_vdpa_get_vq_state,
	.set_vq_state	= eni_vdpa_set_vq_state,
	.set_vq_cb	= eni_vdpa_set_vq_cb,
	.set_vq_ready	= eni_vdpa_set_vq_ready,
	.get_vq_ready	= eni_vdpa_get_vq_ready,
	.set_vq_num	= eni_vdpa_set_vq_num,
	.set_vq_address	= eni_vdpa_set_vq_address,
	.kick_vq	= eni_vdpa_kick_vq,
	.get_device_id	= eni_vdpa_get_device_id,
	.get_vendor_id	= eni_vdpa_get_vendor_id,
	.get_vq_align	= eni_vdpa_get_vq_align,
	.get_config_size = eni_vdpa_get_config_size,
	.get_config	= eni_vdpa_get_config,
	.set_config	= eni_vdpa_set_config,
	.set_config_cb  = eni_vdpa_set_config_cb,
	.get_vq_irq	= eni_vdpa_get_vq_irq,
};


static u16 eni_vdpa_get_num_queues(struct eni_vdpa *eni_vdpa)
{
	struct virtio_pci_legacy_device *ldev = &eni_vdpa->ldev;
	u32 features = vp_legacy_get_features(ldev);
	u16 num = 2;

	if (features & BIT_ULL(VIRTIO_NET_F_MQ)) {
		__virtio16 max_virtqueue_pairs;

		eni_vdpa_get_config(&eni_vdpa->vdpa,
			offsetof(struct virtio_net_config, max_virtqueue_pairs),
			&max_virtqueue_pairs,
			sizeof(max_virtqueue_pairs));
		num = 2 * __virtio16_to_cpu(virtio_legacy_is_little_endian(),
				max_virtqueue_pairs);
	}

	if (features & BIT_ULL(VIRTIO_NET_F_CTRL_VQ))
		num += 1;

	return num;
}

static int eni_vdpa_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct eni_vdpa *eni_vdpa;
	struct virtio_pci_legacy_device *ldev;
	int ret, i;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	eni_vdpa = vdpa_alloc_device(struct eni_vdpa, vdpa,
				     dev, &eni_vdpa_ops, NULL, false);
	if (IS_ERR(eni_vdpa)) {
		ENI_ERR(pdev, "failed to allocate vDPA structure\n");
		return PTR_ERR(eni_vdpa);
	}

	ldev = &eni_vdpa->ldev;
	ldev->pci_dev = pdev;

	ret = vp_legacy_probe(ldev);
	if (ret) {
		ENI_ERR(pdev, "failed to probe legacy PCI device\n");
		goto err;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, eni_vdpa);

	eni_vdpa->vdpa.dma_dev = &pdev->dev;
	eni_vdpa->queues = eni_vdpa_get_num_queues(eni_vdpa);

	eni_vdpa->vring = devm_kcalloc(&pdev->dev, eni_vdpa->queues,
				      sizeof(*eni_vdpa->vring),
				      GFP_KERNEL);
	if (!eni_vdpa->vring) {
		ret = -ENOMEM;
		ENI_ERR(pdev, "failed to allocate virtqueues\n");
		goto err;
	}

	for (i = 0; i < eni_vdpa->queues; i++) {
		eni_vdpa->vring[i].irq = VIRTIO_MSI_NO_VECTOR;
		eni_vdpa->vring[i].notify = ldev->ioaddr + VIRTIO_PCI_QUEUE_NOTIFY;
	}
	eni_vdpa->config_irq = VIRTIO_MSI_NO_VECTOR;

	ret = vdpa_register_device(&eni_vdpa->vdpa, eni_vdpa->queues);
	if (ret) {
		ENI_ERR(pdev, "failed to register to vdpa bus\n");
		goto err;
	}

	return 0;

err:
	put_device(&eni_vdpa->vdpa.dev);
	return ret;
}

static void eni_vdpa_remove(struct pci_dev *pdev)
{
	struct eni_vdpa *eni_vdpa = pci_get_drvdata(pdev);

	vdpa_unregister_device(&eni_vdpa->vdpa);
	vp_legacy_remove(&eni_vdpa->ldev);
}

static struct pci_device_id eni_pci_ids[] = {
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_REDHAT_QUMRANET,
			 VIRTIO_TRANS_ID_NET,
			 PCI_SUBVENDOR_ID_REDHAT_QUMRANET,
			 VIRTIO_ID_NET) },
	{ 0 },
};

static struct pci_driver eni_vdpa_driver = {
	.name		= "alibaba-eni-vdpa",
	.id_table	= eni_pci_ids,
	.probe		= eni_vdpa_probe,
	.remove		= eni_vdpa_remove,
};

module_pci_driver(eni_vdpa_driver);

MODULE_AUTHOR("Wu Zongyong <wuzongyong@linux.alibaba.com>");
MODULE_DESCRIPTION("Alibaba ENI vDPA driver");
MODULE_LICENSE("GPL v2");
