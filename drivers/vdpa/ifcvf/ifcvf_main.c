// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel IFC VF NIC driver for virtio dataplane offloading
 *
 * Copyright (C) 2020 Intel Corporation.
 *
 * Author: Zhu Lingshan <lingshan.zhu@intel.com>
 *
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sysfs.h>
#include "ifcvf_base.h"

#define VERSION_STRING  "0.1"
#define DRIVER_AUTHOR   "Intel Corporation"
#define IFCVF_DRIVER_NAME       "ifcvf"

static irqreturn_t ifcvf_config_changed(int irq, void *arg)
{
	struct ifcvf_hw *vf = arg;

	if (vf->config_cb.callback)
		return vf->config_cb.callback(vf->config_cb.private);

	return IRQ_HANDLED;
}

static irqreturn_t ifcvf_intr_handler(int irq, void *arg)
{
	struct vring_info *vring = arg;

	if (vring->cb.callback)
		return vring->cb.callback(vring->cb.private);

	return IRQ_HANDLED;
}

static void ifcvf_free_irq_vectors(void *data)
{
	pci_free_irq_vectors(data);
}

static void ifcvf_free_irq(struct ifcvf_adapter *adapter, int queues)
{
	struct pci_dev *pdev = adapter->pdev;
	struct ifcvf_hw *vf = &adapter->vf;
	int i;


	for (i = 0; i < queues; i++) {
		devm_free_irq(&pdev->dev, vf->vring[i].irq, &vf->vring[i]);
		vf->vring[i].irq = -EINVAL;
	}

	devm_free_irq(&pdev->dev, vf->config_irq, vf);
	ifcvf_free_irq_vectors(pdev);
}

static int ifcvf_request_irq(struct ifcvf_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct ifcvf_hw *vf = &adapter->vf;
	int vector, i, ret, irq;

	ret = pci_alloc_irq_vectors(pdev, IFCVF_MAX_INTR,
				    IFCVF_MAX_INTR, PCI_IRQ_MSIX);
	if (ret < 0) {
		IFCVF_ERR(pdev, "Failed to alloc IRQ vectors\n");
		return ret;
	}

	snprintf(vf->config_msix_name, 256, "ifcvf[%s]-config\n",
		 pci_name(pdev));
	vector = 0;
	vf->config_irq = pci_irq_vector(pdev, vector);
	ret = devm_request_irq(&pdev->dev, vf->config_irq,
			       ifcvf_config_changed, 0,
			       vf->config_msix_name, vf);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to request config irq\n");
		return ret;
	}

	for (i = 0; i < IFCVF_MAX_QUEUE_PAIRS * 2; i++) {
		snprintf(vf->vring[i].msix_name, 256, "ifcvf[%s]-%d\n",
			 pci_name(pdev), i);
		vector = i + IFCVF_MSI_QUEUE_OFF;
		irq = pci_irq_vector(pdev, vector);
		ret = devm_request_irq(&pdev->dev, irq,
				       ifcvf_intr_handler, 0,
				       vf->vring[i].msix_name,
				       &vf->vring[i]);
		if (ret) {
			IFCVF_ERR(pdev,
				  "Failed to request irq for vq %d\n", i);
			ifcvf_free_irq(adapter, i);

			return ret;
		}

		vf->vring[i].irq = irq;
	}

	return 0;
}

static int ifcvf_start_datapath(void *private)
{
	struct ifcvf_hw *vf = ifcvf_private_to_vf(private);
	u8 status;
	int ret;

	vf->nr_vring = IFCVF_MAX_QUEUE_PAIRS * 2;
	ret = ifcvf_start_hw(vf);
	if (ret < 0) {
		status = ifcvf_get_status(vf);
		status |= VIRTIO_CONFIG_S_FAILED;
		ifcvf_set_status(vf, status);
	}

	return ret;
}

static int ifcvf_stop_datapath(void *private)
{
	struct ifcvf_hw *vf = ifcvf_private_to_vf(private);
	int i;

	for (i = 0; i < IFCVF_MAX_QUEUE_PAIRS * 2; i++)
		vf->vring[i].cb.callback = NULL;

	ifcvf_stop_hw(vf);

	return 0;
}

static void ifcvf_reset_vring(struct ifcvf_adapter *adapter)
{
	struct ifcvf_hw *vf = ifcvf_private_to_vf(adapter);
	int i;

	for (i = 0; i < IFCVF_MAX_QUEUE_PAIRS * 2; i++) {
		vf->vring[i].last_avail_idx = 0;
		vf->vring[i].desc = 0;
		vf->vring[i].avail = 0;
		vf->vring[i].used = 0;
		vf->vring[i].ready = 0;
		vf->vring[i].cb.callback = NULL;
		vf->vring[i].cb.private = NULL;
	}

	ifcvf_reset(vf);
}

static struct ifcvf_adapter *vdpa_to_adapter(struct vdpa_device *vdpa_dev)
{
	return container_of(vdpa_dev, struct ifcvf_adapter, vdpa);
}

static struct ifcvf_hw *vdpa_to_vf(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_adapter *adapter = vdpa_to_adapter(vdpa_dev);

	return &adapter->vf;
}

static u64 ifcvf_vdpa_get_features(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);
	u64 features;

	features = ifcvf_get_features(vf) & IFCVF_SUPPORTED_FEATURES;

	return features;
}

static int ifcvf_vdpa_set_features(struct vdpa_device *vdpa_dev, u64 features)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	vf->req_features = features;

	return 0;
}

static u8 ifcvf_vdpa_get_status(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return ifcvf_get_status(vf);
}

static void ifcvf_vdpa_set_status(struct vdpa_device *vdpa_dev, u8 status)
{
	struct ifcvf_adapter *adapter;
	struct ifcvf_hw *vf;
	u8 status_old;
	int ret;

	vf  = vdpa_to_vf(vdpa_dev);
	adapter = dev_get_drvdata(vdpa_dev->dev.parent);
	status_old = ifcvf_get_status(vf);

	if (status_old == status)
		return;

	if ((status_old & VIRTIO_CONFIG_S_DRIVER_OK) &&
	    !(status & VIRTIO_CONFIG_S_DRIVER_OK)) {
		ifcvf_stop_datapath(adapter);
		ifcvf_free_irq(adapter, IFCVF_MAX_QUEUE_PAIRS * 2);
	}

	if (status == 0) {
		ifcvf_reset_vring(adapter);
		return;
	}

	if ((status & VIRTIO_CONFIG_S_DRIVER_OK) &&
	    !(status_old & VIRTIO_CONFIG_S_DRIVER_OK)) {
		ret = ifcvf_request_irq(adapter);
		if (ret) {
			status = ifcvf_get_status(vf);
			status |= VIRTIO_CONFIG_S_FAILED;
			ifcvf_set_status(vf, status);
			return;
		}

		if (ifcvf_start_datapath(adapter) < 0)
			IFCVF_ERR(adapter->pdev,
				  "Failed to set ifcvf vdpa  status %u\n",
				  status);
	}

	ifcvf_set_status(vf, status);
}

static u16 ifcvf_vdpa_get_vq_num_max(struct vdpa_device *vdpa_dev)
{
	return IFCVF_QUEUE_MAX;
}

static int ifcvf_vdpa_get_vq_state(struct vdpa_device *vdpa_dev, u16 qid,
				   struct vdpa_vq_state *state)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	state->avail_index = ifcvf_get_vq_state(vf, qid);
	return 0;
}

static int ifcvf_vdpa_set_vq_state(struct vdpa_device *vdpa_dev, u16 qid,
				   const struct vdpa_vq_state *state)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return ifcvf_set_vq_state(vf, qid, state->avail_index);
}

static void ifcvf_vdpa_set_vq_cb(struct vdpa_device *vdpa_dev, u16 qid,
				 struct vdpa_callback *cb)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	vf->vring[qid].cb = *cb;
}

static void ifcvf_vdpa_set_vq_ready(struct vdpa_device *vdpa_dev,
				    u16 qid, bool ready)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	vf->vring[qid].ready = ready;
}

static bool ifcvf_vdpa_get_vq_ready(struct vdpa_device *vdpa_dev, u16 qid)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return vf->vring[qid].ready;
}

static void ifcvf_vdpa_set_vq_num(struct vdpa_device *vdpa_dev, u16 qid,
				  u32 num)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	vf->vring[qid].size = num;
}

static int ifcvf_vdpa_set_vq_address(struct vdpa_device *vdpa_dev, u16 qid,
				     u64 desc_area, u64 driver_area,
				     u64 device_area)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	vf->vring[qid].desc = desc_area;
	vf->vring[qid].avail = driver_area;
	vf->vring[qid].used = device_area;

	return 0;
}

static void ifcvf_vdpa_kick_vq(struct vdpa_device *vdpa_dev, u16 qid)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	ifcvf_notify_queue(vf, qid);
}

static u32 ifcvf_vdpa_get_generation(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return ioread8(&vf->common_cfg->config_generation);
}

static u32 ifcvf_vdpa_get_device_id(struct vdpa_device *vdpa_dev)
{
	return VIRTIO_ID_NET;
}

static u32 ifcvf_vdpa_get_vendor_id(struct vdpa_device *vdpa_dev)
{
	return IFCVF_SUBSYS_VENDOR_ID;
}

static u32 ifcvf_vdpa_get_vq_align(struct vdpa_device *vdpa_dev)
{
	return IFCVF_QUEUE_ALIGNMENT;
}

static void ifcvf_vdpa_get_config(struct vdpa_device *vdpa_dev,
				  unsigned int offset,
				  void *buf, unsigned int len)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	WARN_ON(offset + len > sizeof(struct virtio_net_config));
	ifcvf_read_net_config(vf, offset, buf, len);
}

static void ifcvf_vdpa_set_config(struct vdpa_device *vdpa_dev,
				  unsigned int offset, const void *buf,
				  unsigned int len)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	WARN_ON(offset + len > sizeof(struct virtio_net_config));
	ifcvf_write_net_config(vf, offset, buf, len);
}

static void ifcvf_vdpa_set_config_cb(struct vdpa_device *vdpa_dev,
				     struct vdpa_callback *cb)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	vf->config_cb.callback = cb->callback;
	vf->config_cb.private = cb->private;
}

static int ifcvf_vdpa_get_vq_irq(struct vdpa_device *vdpa_dev,
				 u16 qid)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return vf->vring[qid].irq;
}

/*
 * IFCVF currently does't have on-chip IOMMU, so not
 * implemented set_map()/dma_map()/dma_unmap()
 */
static const struct vdpa_config_ops ifc_vdpa_ops = {
	.get_features	= ifcvf_vdpa_get_features,
	.set_features	= ifcvf_vdpa_set_features,
	.get_status	= ifcvf_vdpa_get_status,
	.set_status	= ifcvf_vdpa_set_status,
	.get_vq_num_max	= ifcvf_vdpa_get_vq_num_max,
	.get_vq_state	= ifcvf_vdpa_get_vq_state,
	.set_vq_state	= ifcvf_vdpa_set_vq_state,
	.set_vq_cb	= ifcvf_vdpa_set_vq_cb,
	.set_vq_ready	= ifcvf_vdpa_set_vq_ready,
	.get_vq_ready	= ifcvf_vdpa_get_vq_ready,
	.set_vq_num	= ifcvf_vdpa_set_vq_num,
	.set_vq_address	= ifcvf_vdpa_set_vq_address,
	.get_vq_irq	= ifcvf_vdpa_get_vq_irq,
	.kick_vq	= ifcvf_vdpa_kick_vq,
	.get_generation	= ifcvf_vdpa_get_generation,
	.get_device_id	= ifcvf_vdpa_get_device_id,
	.get_vendor_id	= ifcvf_vdpa_get_vendor_id,
	.get_vq_align	= ifcvf_vdpa_get_vq_align,
	.get_config	= ifcvf_vdpa_get_config,
	.set_config	= ifcvf_vdpa_set_config,
	.set_config_cb  = ifcvf_vdpa_set_config_cb,
};

static int ifcvf_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct ifcvf_adapter *adapter;
	struct ifcvf_hw *vf;
	int ret, i;

	ret = pcim_enable_device(pdev);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to enable device\n");
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2) | BIT(4),
				 IFCVF_DRIVER_NAME);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to request MMIO region\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		IFCVF_ERR(pdev, "No usable DMA configuration\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, ifcvf_free_irq_vectors, pdev);
	if (ret) {
		IFCVF_ERR(pdev,
			  "Failed for adding devres for freeing irq vectors\n");
		return ret;
	}

	adapter = vdpa_alloc_device(struct ifcvf_adapter, vdpa,
				    dev, &ifc_vdpa_ops,
				    IFCVF_MAX_QUEUE_PAIRS * 2, NULL);
	if (adapter == NULL) {
		IFCVF_ERR(pdev, "Failed to allocate vDPA structure");
		return -ENOMEM;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, adapter);

	vf = &adapter->vf;
	vf->base = pcim_iomap_table(pdev);

	adapter->pdev = pdev;
	adapter->vdpa.dma_dev = &pdev->dev;

	ret = ifcvf_init_hw(vf, pdev);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to init IFCVF hw\n");
		goto err;
	}

	for (i = 0; i < IFCVF_MAX_QUEUE_PAIRS * 2; i++)
		vf->vring[i].irq = -EINVAL;

	ret = vdpa_register_device(&adapter->vdpa);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to register ifcvf to vdpa bus");
		goto err;
	}

	return 0;

err:
	put_device(&adapter->vdpa.dev);
	return ret;
}

static void ifcvf_remove(struct pci_dev *pdev)
{
	struct ifcvf_adapter *adapter = pci_get_drvdata(pdev);

	vdpa_unregister_device(&adapter->vdpa);
}

static struct pci_device_id ifcvf_pci_ids[] = {
	{ PCI_DEVICE_SUB(IFCVF_VENDOR_ID,
		IFCVF_DEVICE_ID,
		IFCVF_SUBSYS_VENDOR_ID,
		IFCVF_SUBSYS_DEVICE_ID) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, ifcvf_pci_ids);

static struct pci_driver ifcvf_driver = {
	.name     = IFCVF_DRIVER_NAME,
	.id_table = ifcvf_pci_ids,
	.probe    = ifcvf_probe,
	.remove   = ifcvf_remove,
};

module_pci_driver(ifcvf_driver);

MODULE_LICENSE("GPL v2");
MODULE_VERSION(VERSION_STRING);
