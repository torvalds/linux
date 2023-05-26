/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel IFC VF NIC driver for virtio dataplane offloading
 *
 * Copyright (C) 2020 Intel Corporation.
 *
 * Author: Zhu Lingshan <lingshan.zhu@intel.com>
 *
 */

#ifndef _IFCVF_H_
#define _IFCVF_H_

#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/vdpa.h>
#include <linux/virtio_pci_modern.h>
#include <uapi/linux/virtio_net.h>
#include <uapi/linux/virtio_blk.h>
#include <uapi/linux/virtio_config.h>
#include <uapi/linux/virtio_pci.h>
#include <uapi/linux/vdpa.h>

#define N3000_DEVICE_ID		0x1041
#define N3000_SUBSYS_DEVICE_ID	0x001A

/* Max 8 data queue pairs(16 queues) and one control vq for now. */
#define IFCVF_MAX_QUEUES	17

#define IFCVF_QUEUE_ALIGNMENT	PAGE_SIZE
#define IFCVF_QUEUE_MAX		32768
#define IFCVF_PCI_MAX_RESOURCE	6

#define IFCVF_LM_CFG_SIZE		0x40
#define IFCVF_LM_RING_STATE_OFFSET	0x20
#define IFCVF_LM_BAR			4

#define IFCVF_ERR(pdev, fmt, ...)	dev_err(&pdev->dev, fmt, ##__VA_ARGS__)
#define IFCVF_DBG(pdev, fmt, ...)	dev_dbg(&pdev->dev, fmt, ##__VA_ARGS__)
#define IFCVF_INFO(pdev, fmt, ...)	dev_info(&pdev->dev, fmt, ##__VA_ARGS__)

/* all vqs and config interrupt has its own vector */
#define MSIX_VECTOR_PER_VQ_AND_CONFIG		1
/* all vqs share a vector, and config interrupt has a separate vector */
#define MSIX_VECTOR_SHARED_VQ_AND_CONFIG	2
/* all vqs and config interrupt share a vector */
#define MSIX_VECTOR_DEV_SHARED			3

struct vring_info {
	u16 last_avail_idx;
	void __iomem *notify_addr;
	phys_addr_t notify_pa;
	u32 irq;
	struct vdpa_callback cb;
	char msix_name[256];
};

struct ifcvf_hw {
	u8 __iomem *isr;
	/* Live migration */
	u8 __iomem *lm_cfg;
	/* Notification bar number */
	u8 notify_bar;
	u8 msix_vector_status;
	/* virtio-net or virtio-blk device config size */
	u32 config_size;
	/* Notificaiton bar address */
	void __iomem *notify_base;
	phys_addr_t notify_base_pa;
	u32 notify_off_multiplier;
	u32 dev_type;
	u64 hw_features;
	/* provisioned device features */
	u64 dev_features;
	struct virtio_pci_common_cfg __iomem *common_cfg;
	void __iomem *dev_cfg;
	struct vring_info vring[IFCVF_MAX_QUEUES];
	void __iomem * const *base;
	char config_msix_name[256];
	struct vdpa_callback config_cb;
	int config_irq;
	int vqs_reused_irq;
	u16 nr_vring;
	/* VIRTIO_PCI_CAP_DEVICE_CFG size */
	u32 cap_dev_config_size;
	struct pci_dev *pdev;
};

struct ifcvf_adapter {
	struct vdpa_device vdpa;
	struct pci_dev *pdev;
	struct ifcvf_hw *vf;
};

struct ifcvf_vring_lm_cfg {
	u32 idx_addr[2];
	u8 reserved[IFCVF_LM_CFG_SIZE - 8];
};

struct ifcvf_lm_cfg {
	u8 reserved[IFCVF_LM_RING_STATE_OFFSET];
	struct ifcvf_vring_lm_cfg vring_lm_cfg[IFCVF_MAX_QUEUES];
};

struct ifcvf_vdpa_mgmt_dev {
	struct vdpa_mgmt_dev mdev;
	struct ifcvf_hw vf;
	struct ifcvf_adapter *adapter;
	struct pci_dev *pdev;
};

int ifcvf_init_hw(struct ifcvf_hw *hw, struct pci_dev *dev);
void ifcvf_stop_hw(struct ifcvf_hw *hw);
void ifcvf_notify_queue(struct ifcvf_hw *hw, u16 qid);
void ifcvf_read_dev_config(struct ifcvf_hw *hw, u64 offset,
			   void *dst, int length);
void ifcvf_write_dev_config(struct ifcvf_hw *hw, u64 offset,
			    const void *src, int length);
u8 ifcvf_get_status(struct ifcvf_hw *hw);
void ifcvf_set_status(struct ifcvf_hw *hw, u8 status);
void io_write64_twopart(u64 val, u32 *lo, u32 *hi);
void ifcvf_reset(struct ifcvf_hw *hw);
u64 ifcvf_get_dev_features(struct ifcvf_hw *hw);
u64 ifcvf_get_hw_features(struct ifcvf_hw *hw);
int ifcvf_verify_min_features(struct ifcvf_hw *hw, u64 features);
u16 ifcvf_get_vq_state(struct ifcvf_hw *hw, u16 qid);
int ifcvf_set_vq_state(struct ifcvf_hw *hw, u16 qid, u16 num);
struct ifcvf_adapter *vf_to_adapter(struct ifcvf_hw *hw);
int ifcvf_probed_virtio_net(struct ifcvf_hw *hw);
u32 ifcvf_get_config_size(struct ifcvf_hw *hw);
u16 ifcvf_set_vq_vector(struct ifcvf_hw *hw, u16 qid, int vector);
u16 ifcvf_set_config_vector(struct ifcvf_hw *hw, int vector);
void ifcvf_set_vq_num(struct ifcvf_hw *hw, u16 qid, u32 num);
int ifcvf_set_vq_address(struct ifcvf_hw *hw, u16 qid, u64 desc_area,
			 u64 driver_area, u64 device_area);
bool ifcvf_get_vq_ready(struct ifcvf_hw *hw, u16 qid);
void ifcvf_set_vq_ready(struct ifcvf_hw *hw, u16 qid, bool ready);
void ifcvf_set_driver_features(struct ifcvf_hw *hw, u64 features);
u64 ifcvf_get_driver_features(struct ifcvf_hw *hw);
#endif /* _IFCVF_H_ */
