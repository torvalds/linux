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
	u16 max_intr;

	/* all queues and config interrupt  */
	max_intr = vf->nr_vring + 1;

	ret = pci_alloc_irq_vectors(pdev, max_intr,
				    max_intr, PCI_IRQ_MSIX);
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

	for (i = 0; i < vf->nr_vring; i++) {
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

	for (i = 0; i < vf->nr_vring; i++)
		vf->vring[i].cb.callback = NULL;

	ifcvf_stop_hw(vf);

	return 0;
}

static void ifcvf_reset_vring(struct ifcvf_adapter *adapter)
{
	struct ifcvf_hw *vf = ifcvf_private_to_vf(adapter);
	int i;

	for (i = 0; i < vf->nr_vring; i++) {
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
	struct ifcvf_adapter *adapter = vdpa_to_adapter(vdpa_dev);
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);
	struct pci_dev *pdev = adapter->pdev;

	u64 features;

	switch (vf->dev_type) {
	case VIRTIO_ID_NET:
		features = ifcvf_get_features(vf) & IFCVF_NET_SUPPORTED_FEATURES;
		break;
	case VIRTIO_ID_BLOCK:
		features = ifcvf_get_features(vf);
		break;
	default:
		features = 0;
		IFCVF_ERR(pdev, "VIRTIO ID %u not supported\n", vf->dev_type);
	}

	return features;
}

static int ifcvf_vdpa_set_features(struct vdpa_device *vdpa_dev, u64 features)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);
	int ret;

	ret = ifcvf_verify_min_features(vf, features);
	if (ret)
		return ret;

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
	adapter = vdpa_to_adapter(vdpa_dev);
	status_old = ifcvf_get_status(vf);

	if (status_old == status)
		return;

	if ((status_old & VIRTIO_CONFIG_S_DRIVER_OK) &&
	    !(status & VIRTIO_CONFIG_S_DRIVER_OK)) {
		ifcvf_stop_datapath(adapter);
		ifcvf_free_irq(adapter, vf->nr_vring);
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

	state->split.avail_index = ifcvf_get_vq_state(vf, qid);
	return 0;
}

static int ifcvf_vdpa_set_vq_state(struct vdpa_device *vdpa_dev, u16 qid,
				   const struct vdpa_vq_state *state)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return ifcvf_set_vq_state(vf, qid, state->split.avail_index);
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
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return vf->dev_type;
}

static u32 ifcvf_vdpa_get_vendor_id(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_adapter *adapter = vdpa_to_adapter(vdpa_dev);
	struct pci_dev *pdev = adapter->pdev;

	return pdev->subsystem_vendor;
}

static u32 ifcvf_vdpa_get_vq_align(struct vdpa_device *vdpa_dev)
{
	return IFCVF_QUEUE_ALIGNMENT;
}

static size_t ifcvf_vdpa_get_config_size(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_adapter *adapter = vdpa_to_adapter(vdpa_dev);
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);
	struct pci_dev *pdev = adapter->pdev;
	size_t size;

	switch (vf->dev_type) {
	case VIRTIO_ID_NET:
		size = sizeof(struct virtio_net_config);
		break;
	case VIRTIO_ID_BLOCK:
		size = sizeof(struct virtio_blk_config);
		break;
	default:
		size = 0;
		IFCVF_ERR(pdev, "VIRTIO ID %u not supported\n", vf->dev_type);
	}

	return size;
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

static struct vdpa_notification_area ifcvf_get_vq_notification(struct vdpa_device *vdpa_dev,
							       u16 idx)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);
	struct vdpa_notification_area area;

	area.addr = vf->vring[idx].notify_pa;
	if (!vf->notify_off_multiplier)
		area.size = PAGE_SIZE;
	else
		area.size = vf->notify_off_multiplier;

	return area;
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
	.get_config_size	= ifcvf_vdpa_get_config_size,
	.get_config	= ifcvf_vdpa_get_config,
	.set_config	= ifcvf_vdpa_set_config,
	.set_config_cb  = ifcvf_vdpa_set_config_cb,
	.get_vq_notification = ifcvf_get_vq_notification,
};

static struct virtio_device_id id_table_net[] = {
	{VIRTIO_ID_NET, VIRTIO_DEV_ANY_ID},
	{0},
};

static struct virtio_device_id id_table_blk[] = {
	{VIRTIO_ID_BLOCK, VIRTIO_DEV_ANY_ID},
	{0},
};

static u32 get_dev_type(struct pci_dev *pdev)
{
	u32 dev_type;

	/* This drirver drives both modern virtio devices and transitional
	 * devices in modern mode.
	 * vDPA requires feature bit VIRTIO_F_ACCESS_PLATFORM,
	 * so legacy devices and transitional devices in legacy
	 * mode will not work for vDPA, this driver will not
	 * drive devices with legacy interface.
	 */

	if (pdev->device < 0x1040)
		dev_type =  pdev->subsystem_device;
	else
		dev_type =  pdev->device - 0x1040;

	return dev_type;
}

static int ifcvf_vdpa_dev_add(struct vdpa_mgmt_dev *mdev, const char *name)
{
	struct ifcvf_vdpa_mgmt_dev *ifcvf_mgmt_dev;
	struct ifcvf_adapter *adapter;
	struct pci_dev *pdev;
	struct ifcvf_hw *vf;
	struct device *dev;
	int ret, i;

	ifcvf_mgmt_dev = container_of(mdev, struct ifcvf_vdpa_mgmt_dev, mdev);
	if (ifcvf_mgmt_dev->adapter)
		return -EOPNOTSUPP;

	pdev = ifcvf_mgmt_dev->pdev;
	dev = &pdev->dev;
	adapter = vdpa_alloc_device(struct ifcvf_adapter, vdpa,
				    dev, &ifc_vdpa_ops, name);
	if (IS_ERR(adapter)) {
		IFCVF_ERR(pdev, "Failed to allocate vDPA structure");
		return PTR_ERR(adapter);
	}

	ifcvf_mgmt_dev->adapter = adapter;
	pci_set_drvdata(pdev, ifcvf_mgmt_dev);

	vf = &adapter->vf;
	vf->dev_type = get_dev_type(pdev);
	vf->base = pcim_iomap_table(pdev);

	adapter->pdev = pdev;
	adapter->vdpa.dma_dev = &pdev->dev;

	ret = ifcvf_init_hw(vf, pdev);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to init IFCVF hw\n");
		goto err;
	}

	for (i = 0; i < vf->nr_vring; i++)
		vf->vring[i].irq = -EINVAL;

	vf->hw_features = ifcvf_get_hw_features(vf);

	adapter->vdpa.mdev = &ifcvf_mgmt_dev->mdev;
	ret = _vdpa_register_device(&adapter->vdpa, vf->nr_vring);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to register to vDPA bus");
		goto err;
	}

	return 0;

err:
	put_device(&adapter->vdpa.dev);
	return ret;
}

static void ifcvf_vdpa_dev_del(struct vdpa_mgmt_dev *mdev, struct vdpa_device *dev)
{
	struct ifcvf_vdpa_mgmt_dev *ifcvf_mgmt_dev;

	ifcvf_mgmt_dev = container_of(mdev, struct ifcvf_vdpa_mgmt_dev, mdev);
	_vdpa_unregister_device(dev);
	ifcvf_mgmt_dev->adapter = NULL;
}

static const struct vdpa_mgmtdev_ops ifcvf_vdpa_mgmt_dev_ops = {
	.dev_add = ifcvf_vdpa_dev_add,
	.dev_del = ifcvf_vdpa_dev_del
};

static int ifcvf_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ifcvf_vdpa_mgmt_dev *ifcvf_mgmt_dev;
	struct device *dev = &pdev->dev;
	u32 dev_type;
	int ret;

	ifcvf_mgmt_dev = kzalloc(sizeof(struct ifcvf_vdpa_mgmt_dev), GFP_KERNEL);
	if (!ifcvf_mgmt_dev) {
		IFCVF_ERR(pdev, "Failed to alloc memory for the vDPA management device\n");
		return -ENOMEM;
	}

	dev_type = get_dev_type(pdev);
	switch (dev_type) {
	case VIRTIO_ID_NET:
		ifcvf_mgmt_dev->mdev.id_table = id_table_net;
		break;
	case VIRTIO_ID_BLOCK:
		ifcvf_mgmt_dev->mdev.id_table = id_table_blk;
		break;
	default:
		IFCVF_ERR(pdev, "VIRTIO ID %u not supported\n", dev_type);
		ret = -EOPNOTSUPP;
		goto err;
	}

	ifcvf_mgmt_dev->mdev.ops = &ifcvf_vdpa_mgmt_dev_ops;
	ifcvf_mgmt_dev->mdev.device = dev;
	ifcvf_mgmt_dev->pdev = pdev;

	ret = pcim_enable_device(pdev);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to enable device\n");
		goto err;
	}

	ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2) | BIT(4),
				 IFCVF_DRIVER_NAME);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to request MMIO region\n");
		goto err;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		IFCVF_ERR(pdev, "No usable DMA configuration\n");
		goto err;
	}

	ret = devm_add_action_or_reset(dev, ifcvf_free_irq_vectors, pdev);
	if (ret) {
		IFCVF_ERR(pdev,
			  "Failed for adding devres for freeing irq vectors\n");
		goto err;
	}

	pci_set_master(pdev);

	ret = vdpa_mgmtdev_register(&ifcvf_mgmt_dev->mdev);
	if (ret) {
		IFCVF_ERR(pdev,
			  "Failed to initialize the management interfaces\n");
		goto err;
	}

	return 0;

err:
	kfree(ifcvf_mgmt_dev);
	return ret;
}

static void ifcvf_remove(struct pci_dev *pdev)
{
	struct ifcvf_vdpa_mgmt_dev *ifcvf_mgmt_dev;

	ifcvf_mgmt_dev = pci_get_drvdata(pdev);
	vdpa_mgmtdev_unregister(&ifcvf_mgmt_dev->mdev);
	kfree(ifcvf_mgmt_dev);
}

static struct pci_device_id ifcvf_pci_ids[] = {
	/* N3000 network device */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_REDHAT_QUMRANET,
			 N3000_DEVICE_ID,
			 PCI_VENDOR_ID_INTEL,
			 N3000_SUBSYS_DEVICE_ID) },
	/* C5000X-PL network device */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_REDHAT_QUMRANET,
			 VIRTIO_TRANS_ID_NET,
			 PCI_VENDOR_ID_INTEL,
			 VIRTIO_ID_NET) },
	/* C5000X-PL block device */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_REDHAT_QUMRANET,
			 VIRTIO_TRANS_ID_BLOCK,
			 PCI_VENDOR_ID_INTEL,
			 VIRTIO_ID_BLOCK) },

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
