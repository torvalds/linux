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

static irqreturn_t ifcvf_vq_intr_handler(int irq, void *arg)
{
	struct vring_info *vring = arg;

	if (vring->cb.callback)
		return vring->cb.callback(vring->cb.private);

	return IRQ_HANDLED;
}

static irqreturn_t ifcvf_vqs_reused_intr_handler(int irq, void *arg)
{
	struct ifcvf_hw *vf = arg;
	struct vring_info *vring;
	int i;

	for (i = 0; i < vf->nr_vring; i++) {
		vring = &vf->vring[i];
		if (vring->cb.callback)
			vring->cb.callback(vring->cb.private);
	}

	return IRQ_HANDLED;
}

static irqreturn_t ifcvf_dev_intr_handler(int irq, void *arg)
{
	struct ifcvf_hw *vf = arg;
	u8 isr;

	isr = vp_ioread8(vf->isr);
	if (isr & VIRTIO_PCI_ISR_CONFIG)
		ifcvf_config_changed(irq, arg);

	return ifcvf_vqs_reused_intr_handler(irq, arg);
}

static void ifcvf_free_irq_vectors(void *data)
{
	pci_free_irq_vectors(data);
}

static void ifcvf_free_per_vq_irq(struct ifcvf_hw *vf)
{
	struct pci_dev *pdev = vf->pdev;
	int i;

	for (i = 0; i < vf->nr_vring; i++) {
		if (vf->vring[i].irq != -EINVAL) {
			devm_free_irq(&pdev->dev, vf->vring[i].irq, &vf->vring[i]);
			vf->vring[i].irq = -EINVAL;
		}
	}
}

static void ifcvf_free_vqs_reused_irq(struct ifcvf_hw *vf)
{
	struct pci_dev *pdev = vf->pdev;

	if (vf->vqs_reused_irq != -EINVAL) {
		devm_free_irq(&pdev->dev, vf->vqs_reused_irq, vf);
		vf->vqs_reused_irq = -EINVAL;
	}

}

static void ifcvf_free_vq_irq(struct ifcvf_hw *vf)
{
	if (vf->msix_vector_status == MSIX_VECTOR_PER_VQ_AND_CONFIG)
		ifcvf_free_per_vq_irq(vf);
	else
		ifcvf_free_vqs_reused_irq(vf);
}

static void ifcvf_free_config_irq(struct ifcvf_hw *vf)
{
	struct pci_dev *pdev = vf->pdev;

	if (vf->config_irq == -EINVAL)
		return;

	/* If the irq is shared by all vqs and the config interrupt,
	 * it is already freed in ifcvf_free_vq_irq, so here only
	 * need to free config irq when msix_vector_status != MSIX_VECTOR_DEV_SHARED
	 */
	if (vf->msix_vector_status != MSIX_VECTOR_DEV_SHARED) {
		devm_free_irq(&pdev->dev, vf->config_irq, vf);
		vf->config_irq = -EINVAL;
	}
}

static void ifcvf_free_irq(struct ifcvf_hw *vf)
{
	struct pci_dev *pdev = vf->pdev;

	ifcvf_free_vq_irq(vf);
	ifcvf_free_config_irq(vf);
	ifcvf_free_irq_vectors(pdev);
	vf->num_msix_vectors = 0;
}

/* ifcvf MSIX vectors allocator, this helper tries to allocate
 * vectors for all virtqueues and the config interrupt.
 * It returns the number of allocated vectors, negative
 * return value when fails.
 */
static int ifcvf_alloc_vectors(struct ifcvf_hw *vf)
{
	struct pci_dev *pdev = vf->pdev;
	int max_intr, ret;

	/* all queues and config interrupt  */
	max_intr = vf->nr_vring + 1;
	ret = pci_alloc_irq_vectors(pdev, 1, max_intr, PCI_IRQ_MSIX | PCI_IRQ_AFFINITY);

	if (ret < 0) {
		IFCVF_ERR(pdev, "Failed to alloc IRQ vectors\n");
		return ret;
	}

	if (ret < max_intr)
		IFCVF_INFO(pdev,
			   "Requested %u vectors, however only %u allocated, lower performance\n",
			   max_intr, ret);

	return ret;
}

static int ifcvf_request_per_vq_irq(struct ifcvf_hw *vf)
{
	struct pci_dev *pdev = vf->pdev;
	int i, vector, ret, irq;

	vf->vqs_reused_irq = -EINVAL;
	for (i = 0; i < vf->nr_vring; i++) {
		snprintf(vf->vring[i].msix_name, 256, "ifcvf[%s]-%d\n", pci_name(pdev), i);
		vector = i;
		irq = pci_irq_vector(pdev, vector);
		ret = devm_request_irq(&pdev->dev, irq,
				       ifcvf_vq_intr_handler, 0,
				       vf->vring[i].msix_name,
				       &vf->vring[i]);
		if (ret) {
			IFCVF_ERR(pdev, "Failed to request irq for vq %d\n", i);
			goto err;
		}

		vf->vring[i].irq = irq;
		ret = ifcvf_set_vq_vector(vf, i, vector);
		if (ret == VIRTIO_MSI_NO_VECTOR) {
			IFCVF_ERR(pdev, "No msix vector for vq %u\n", i);
			goto err;
		}
	}

	return 0;
err:
	ifcvf_free_irq(vf);

	return -EFAULT;
}

static int ifcvf_request_vqs_reused_irq(struct ifcvf_hw *vf)
{
	struct pci_dev *pdev = vf->pdev;
	int i, vector, ret, irq;

	vector = 0;
	snprintf(vf->vring[0].msix_name, 256, "ifcvf[%s]-vqs-reused-irq\n", pci_name(pdev));
	irq = pci_irq_vector(pdev, vector);
	ret = devm_request_irq(&pdev->dev, irq,
			       ifcvf_vqs_reused_intr_handler, 0,
			       vf->vring[0].msix_name, vf);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to request reused irq for the device\n");
		goto err;
	}

	vf->vqs_reused_irq = irq;
	for (i = 0; i < vf->nr_vring; i++) {
		vf->vring[i].irq = -EINVAL;
		ret = ifcvf_set_vq_vector(vf, i, vector);
		if (ret == VIRTIO_MSI_NO_VECTOR) {
			IFCVF_ERR(pdev, "No msix vector for vq %u\n", i);
			goto err;
		}
	}

	return 0;
err:
	ifcvf_free_irq(vf);

	return -EFAULT;
}

static int ifcvf_request_dev_irq(struct ifcvf_hw *vf)
{
	struct pci_dev *pdev = vf->pdev;
	int i, vector, ret, irq;

	vector = 0;
	snprintf(vf->vring[0].msix_name, 256, "ifcvf[%s]-dev-irq\n", pci_name(pdev));
	irq = pci_irq_vector(pdev, vector);
	ret = devm_request_irq(&pdev->dev, irq,
			       ifcvf_dev_intr_handler, 0,
			       vf->vring[0].msix_name, vf);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to request irq for the device\n");
		goto err;
	}

	vf->vqs_reused_irq = irq;
	for (i = 0; i < vf->nr_vring; i++) {
		vf->vring[i].irq = -EINVAL;
		ret = ifcvf_set_vq_vector(vf, i, vector);
		if (ret == VIRTIO_MSI_NO_VECTOR) {
			IFCVF_ERR(pdev, "No msix vector for vq %u\n", i);
			goto err;
		}
	}

	vf->config_irq = irq;
	ret = ifcvf_set_config_vector(vf, vector);
	if (ret == VIRTIO_MSI_NO_VECTOR) {
		IFCVF_ERR(pdev, "No msix vector for device config\n");
		goto err;
	}

	return 0;
err:
	ifcvf_free_irq(vf);

	return -EFAULT;

}

static int ifcvf_request_vq_irq(struct ifcvf_hw *vf)
{
	int ret;

	if (vf->msix_vector_status == MSIX_VECTOR_PER_VQ_AND_CONFIG)
		ret = ifcvf_request_per_vq_irq(vf);
	else
		ret = ifcvf_request_vqs_reused_irq(vf);

	return ret;
}

static int ifcvf_request_config_irq(struct ifcvf_hw *vf)
{
	struct pci_dev *pdev = vf->pdev;
	int config_vector, ret;

	if (vf->msix_vector_status == MSIX_VECTOR_PER_VQ_AND_CONFIG)
		config_vector = vf->nr_vring;
	else if (vf->msix_vector_status ==  MSIX_VECTOR_SHARED_VQ_AND_CONFIG)
		/* vector 0 for vqs and 1 for config interrupt */
		config_vector = 1;
	else if (vf->msix_vector_status == MSIX_VECTOR_DEV_SHARED)
		/* re-use the vqs vector */
		return 0;
	else
		return -EINVAL;

	snprintf(vf->config_msix_name, 256, "ifcvf[%s]-config\n",
		 pci_name(pdev));
	vf->config_irq = pci_irq_vector(pdev, config_vector);
	ret = devm_request_irq(&pdev->dev, vf->config_irq,
			       ifcvf_config_changed, 0,
			       vf->config_msix_name, vf);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to request config irq\n");
		goto err;
	}

	ret = ifcvf_set_config_vector(vf, config_vector);
	if (ret == VIRTIO_MSI_NO_VECTOR) {
		IFCVF_ERR(pdev, "No msix vector for device config\n");
		goto err;
	}

	return 0;
err:
	ifcvf_free_irq(vf);

	return -EFAULT;
}

static int ifcvf_request_irq(struct ifcvf_hw *vf)
{
	int nvectors, ret, max_intr;

	nvectors = ifcvf_alloc_vectors(vf);
	if (nvectors <= 0)
		return -EFAULT;

	vf->msix_vector_status = MSIX_VECTOR_PER_VQ_AND_CONFIG;
	max_intr = vf->nr_vring + 1;
	if (nvectors < max_intr)
		vf->msix_vector_status = MSIX_VECTOR_SHARED_VQ_AND_CONFIG;

	if (nvectors == 1) {
		vf->msix_vector_status = MSIX_VECTOR_DEV_SHARED;
		ret = ifcvf_request_dev_irq(vf);

		return ret;
	}

	ret = ifcvf_request_vq_irq(vf);
	if (ret)
		return ret;

	ret = ifcvf_request_config_irq(vf);

	if (ret)
		return ret;

	vf->num_msix_vectors = nvectors;

	return 0;
}

static struct ifcvf_adapter *vdpa_to_adapter(struct vdpa_device *vdpa_dev)
{
	return container_of(vdpa_dev, struct ifcvf_adapter, vdpa);
}

static struct ifcvf_hw *vdpa_to_vf(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_adapter *adapter = vdpa_to_adapter(vdpa_dev);

	return adapter->vf;
}

static u64 ifcvf_vdpa_get_device_features(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_adapter *adapter = vdpa_to_adapter(vdpa_dev);
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);
	struct pci_dev *pdev = adapter->pdev;
	u32 type = vf->dev_type;
	u64 features;

	if (type == VIRTIO_ID_NET || type == VIRTIO_ID_BLOCK)
		features = ifcvf_get_dev_features(vf);
	else {
		features = 0;
		IFCVF_ERR(pdev, "VIRTIO ID %u not supported\n", vf->dev_type);
	}

	return features;
}

static int ifcvf_vdpa_set_driver_features(struct vdpa_device *vdpa_dev, u64 features)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);
	int ret;

	ret = ifcvf_verify_min_features(vf, features);
	if (ret)
		return ret;

	ifcvf_set_driver_features(vf, features);

	return 0;
}

static u64 ifcvf_vdpa_get_driver_features(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);
	u64 features;

	features = ifcvf_get_driver_features(vf);

	return features;
}

static u8 ifcvf_vdpa_get_status(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return ifcvf_get_status(vf);
}

static void ifcvf_vdpa_set_status(struct vdpa_device *vdpa_dev, u8 status)
{
	struct ifcvf_hw *vf;
	u8 status_old;
	int ret;

	vf  = vdpa_to_vf(vdpa_dev);
	status_old = ifcvf_get_status(vf);

	if (status_old == status)
		return;

	if ((status & VIRTIO_CONFIG_S_DRIVER_OK) &&
	    !(status_old & VIRTIO_CONFIG_S_DRIVER_OK)) {
		ret = ifcvf_request_irq(vf);
		if (ret) {
			IFCVF_ERR(vf->pdev, "failed to request irq with error %d\n", ret);
			return;
		}
	}

	ifcvf_set_status(vf, status);
}

static int ifcvf_vdpa_reset(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);
	u8 status = ifcvf_get_status(vf);

	ifcvf_stop(vf);

	if (status & VIRTIO_CONFIG_S_DRIVER_OK)
		ifcvf_free_irq(vf);

	ifcvf_reset(vf);

	return 0;
}

static u16 ifcvf_vdpa_get_vq_num_max(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return ifcvf_get_max_vq_size(vf);
}

static u16 ifcvf_vdpa_get_vq_num_min(struct vdpa_device *vdpa_dev)
{
	return IFCVF_MIN_VQ_SIZE;
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

	ifcvf_set_vq_ready(vf, qid, ready);
}

static bool ifcvf_vdpa_get_vq_ready(struct vdpa_device *vdpa_dev, u16 qid)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return ifcvf_get_vq_ready(vf, qid);
}

static void ifcvf_vdpa_set_vq_num(struct vdpa_device *vdpa_dev, u16 qid,
				  u32 num)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	ifcvf_set_vq_num(vf, qid, num);
}

static int ifcvf_vdpa_set_vq_address(struct vdpa_device *vdpa_dev, u16 qid,
				     u64 desc_area, u64 driver_area,
				     u64 device_area)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return ifcvf_set_vq_address(vf, qid, desc_area, driver_area, device_area);
}

static void ifcvf_vdpa_kick_vq(struct vdpa_device *vdpa_dev, u16 qid)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	ifcvf_notify_queue(vf, qid);
}

static u32 ifcvf_vdpa_get_generation(struct vdpa_device *vdpa_dev)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return vp_ioread8(&vf->common_cfg->config_generation);
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
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return  vf->config_size;
}

static u32 ifcvf_vdpa_get_vq_group(struct vdpa_device *vdpa, u16 idx)
{
	return 0;
}

static void ifcvf_vdpa_get_config(struct vdpa_device *vdpa_dev,
				  unsigned int offset,
				  void *buf, unsigned int len)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	ifcvf_read_dev_config(vf, offset, buf, len);
}

static void ifcvf_vdpa_set_config(struct vdpa_device *vdpa_dev,
				  unsigned int offset, const void *buf,
				  unsigned int len)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	ifcvf_write_dev_config(vf, offset, buf, len);
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

	if (vf->vqs_reused_irq < 0)
		return vf->vring[qid].irq;
	else
		return -EINVAL;
}

static u16 ifcvf_vdpa_get_vq_size(struct vdpa_device *vdpa_dev,
			     u16 qid)
{
	struct ifcvf_hw *vf = vdpa_to_vf(vdpa_dev);

	return ifcvf_get_vq_size(vf, qid);
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
 * IFCVF currently doesn't have on-chip IOMMU, so not
 * implemented set_map()/dma_map()/dma_unmap()
 */
static const struct vdpa_config_ops ifc_vdpa_ops = {
	.get_device_features = ifcvf_vdpa_get_device_features,
	.set_driver_features = ifcvf_vdpa_set_driver_features,
	.get_driver_features = ifcvf_vdpa_get_driver_features,
	.get_status	= ifcvf_vdpa_get_status,
	.set_status	= ifcvf_vdpa_set_status,
	.reset		= ifcvf_vdpa_reset,
	.get_vq_num_max	= ifcvf_vdpa_get_vq_num_max,
	.get_vq_num_min	= ifcvf_vdpa_get_vq_num_min,
	.get_vq_state	= ifcvf_vdpa_get_vq_state,
	.set_vq_state	= ifcvf_vdpa_set_vq_state,
	.set_vq_cb	= ifcvf_vdpa_set_vq_cb,
	.set_vq_ready	= ifcvf_vdpa_set_vq_ready,
	.get_vq_ready	= ifcvf_vdpa_get_vq_ready,
	.set_vq_num	= ifcvf_vdpa_set_vq_num,
	.set_vq_address	= ifcvf_vdpa_set_vq_address,
	.get_vq_irq	= ifcvf_vdpa_get_vq_irq,
	.get_vq_size	= ifcvf_vdpa_get_vq_size,
	.kick_vq	= ifcvf_vdpa_kick_vq,
	.get_generation	= ifcvf_vdpa_get_generation,
	.get_device_id	= ifcvf_vdpa_get_device_id,
	.get_vendor_id	= ifcvf_vdpa_get_vendor_id,
	.get_vq_align	= ifcvf_vdpa_get_vq_align,
	.get_vq_group	= ifcvf_vdpa_get_vq_group,
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

static int ifcvf_vdpa_dev_add(struct vdpa_mgmt_dev *mdev, const char *name,
			      const struct vdpa_dev_set_config *config)
{
	struct ifcvf_vdpa_mgmt_dev *ifcvf_mgmt_dev;
	struct ifcvf_adapter *adapter;
	struct vdpa_device *vdpa_dev;
	struct pci_dev *pdev;
	struct ifcvf_hw *vf;
	u64 device_features;
	int ret;

	ifcvf_mgmt_dev = container_of(mdev, struct ifcvf_vdpa_mgmt_dev, mdev);
	vf = &ifcvf_mgmt_dev->vf;
	pdev = vf->pdev;
	adapter = vdpa_alloc_device(struct ifcvf_adapter, vdpa,
				    &pdev->dev, &ifc_vdpa_ops, 1, 1, NULL, false);
	if (IS_ERR(adapter)) {
		IFCVF_ERR(pdev, "Failed to allocate vDPA structure");
		return PTR_ERR(adapter);
	}

	ifcvf_mgmt_dev->adapter = adapter;
	adapter->pdev = pdev;
	adapter->vdpa.dma_dev = &pdev->dev;
	adapter->vdpa.mdev = mdev;
	adapter->vf = vf;
	vdpa_dev = &adapter->vdpa;

	device_features = vf->hw_features;
	if (config->mask & BIT_ULL(VDPA_ATTR_DEV_FEATURES)) {
		if (config->device_features & ~device_features) {
			IFCVF_ERR(pdev, "The provisioned features 0x%llx are not supported by this device with features 0x%llx\n",
				  config->device_features, device_features);
			return -EINVAL;
		}
		device_features &= config->device_features;
	}
	vf->dev_features = device_features;

	if (name)
		ret = dev_set_name(&vdpa_dev->dev, "%s", name);
	else
		ret = dev_set_name(&vdpa_dev->dev, "vdpa%u", vdpa_dev->index);

	ret = _vdpa_register_device(&adapter->vdpa, vf->nr_vring);
	if (ret) {
		put_device(&adapter->vdpa.dev);
		IFCVF_ERR(pdev, "Failed to register to vDPA bus");
		return ret;
	}

	return 0;
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
	struct ifcvf_hw *vf;
	u32 dev_type;
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

	pci_set_master(pdev);
	ifcvf_mgmt_dev = kzalloc(sizeof(struct ifcvf_vdpa_mgmt_dev), GFP_KERNEL);
	if (!ifcvf_mgmt_dev) {
		IFCVF_ERR(pdev, "Failed to alloc memory for the vDPA management device\n");
		return -ENOMEM;
	}

	vf = &ifcvf_mgmt_dev->vf;
	vf->dev_type = get_dev_type(pdev);
	vf->base = pcim_iomap_table(pdev);
	vf->pdev = pdev;

	ret = ifcvf_init_hw(vf, pdev);
	if (ret) {
		IFCVF_ERR(pdev, "Failed to init IFCVF hw\n");
		goto err;
	}

	for (i = 0; i < vf->nr_vring; i++)
		vf->vring[i].irq = -EINVAL;

	vf->hw_features = ifcvf_get_hw_features(vf);
	vf->config_size = ifcvf_get_config_size(vf);

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
	ifcvf_mgmt_dev->mdev.max_supported_vqs = vf->nr_vring;
	ifcvf_mgmt_dev->mdev.supported_features = vf->hw_features;
	ifcvf_mgmt_dev->mdev.config_attr_mask = (1 << VDPA_ATTR_DEV_FEATURES);

	ret = vdpa_mgmtdev_register(&ifcvf_mgmt_dev->mdev);
	if (ret) {
		IFCVF_ERR(pdev,
			  "Failed to initialize the management interfaces\n");
		goto err;
	}

	pci_set_drvdata(pdev, ifcvf_mgmt_dev);

	return 0;

err:
	kfree(ifcvf_mgmt_dev->vf.vring);
	kfree(ifcvf_mgmt_dev);
	return ret;
}

static void ifcvf_remove(struct pci_dev *pdev)
{
	struct ifcvf_vdpa_mgmt_dev *ifcvf_mgmt_dev;

	ifcvf_mgmt_dev = pci_get_drvdata(pdev);
	vdpa_mgmtdev_unregister(&ifcvf_mgmt_dev->mdev);
	kfree(ifcvf_mgmt_dev->vf.vring);
	kfree(ifcvf_mgmt_dev);
}

static struct pci_device_id ifcvf_pci_ids[] = {
	/* N3000 network device */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_REDHAT_QUMRANET,
			 N3000_DEVICE_ID,
			 PCI_VENDOR_ID_INTEL,
			 N3000_SUBSYS_DEVICE_ID) },
	/* C5000X-PL network device
	 * F2000X-PL network device
	 */
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
