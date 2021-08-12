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
#include <uapi/linux/virtio_net.h>
#include <uapi/linux/virtio_blk.h>
#include <uapi/linux/virtio_config.h>
#include <uapi/linux/virtio_pci.h>

#define N3000_DEVICE_ID		0x1041
#define N3000_SUBSYS_DEVICE_ID	0x001A

#define IFCVF_NET_SUPPORTED_FEATURES \
		((1ULL << VIRTIO_NET_F_MAC)			| \
		 (1ULL << VIRTIO_F_ANY_LAYOUT)			| \
		 (1ULL << VIRTIO_F_VERSION_1)			| \
		 (1ULL << VIRTIO_NET_F_STATUS)			| \
		 (1ULL << VIRTIO_F_ORDER_PLATFORM)		| \
		 (1ULL << VIRTIO_F_ACCESS_PLATFORM)		| \
		 (1ULL << VIRTIO_NET_F_MRG_RXBUF))

/* Only one queue pair for now. */
#define IFCVF_MAX_QUEUE_PAIRS	1

#define IFCVF_QUEUE_ALIGNMENT	PAGE_SIZE
#define IFCVF_QUEUE_MAX		32768
#define IFCVF_MSI_CONFIG_OFF	0
#define IFCVF_MSI_QUEUE_OFF	1
#define IFCVF_PCI_MAX_RESOURCE	6

#define IFCVF_LM_CFG_SIZE		0x40
#define IFCVF_LM_RING_STATE_OFFSET	0x20
#define IFCVF_LM_BAR			4

#define IFCVF_ERR(pdev, fmt, ...)	dev_err(&pdev->dev, fmt, ##__VA_ARGS__)
#define IFCVF_DBG(pdev, fmt, ...)	dev_dbg(&pdev->dev, fmt, ##__VA_ARGS__)
#define IFCVF_INFO(pdev, fmt, ...)	dev_info(&pdev->dev, fmt, ##__VA_ARGS__)

#define ifcvf_private_to_vf(adapter) \
	(&((struct ifcvf_adapter *)adapter)->vf)

#define IFCVF_MAX_INTR (IFCVF_MAX_QUEUE_PAIRS * 2 + 1)

struct vring_info {
	u64 desc;
	u64 avail;
	u64 used;
	u16 size;
	u16 last_avail_idx;
	bool ready;
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
	u16 nr_vring;
	/* Notification bar number */
	u8 notify_bar;
	/* Notificaiton bar address */
	void __iomem *notify_base;
	phys_addr_t notify_base_pa;
	u32 notify_off_multiplier;
	u64 req_features;
	u64 hw_features;
	u32 dev_type;
	struct virtio_pci_common_cfg __iomem *common_cfg;
	void __iomem *net_cfg;
	struct vring_info vring[IFCVF_MAX_QUEUE_PAIRS * 2];
	void __iomem * const *base;
	char config_msix_name[256];
	struct vdpa_callback config_cb;
	unsigned int config_irq;
};

struct ifcvf_adapter {
	struct vdpa_device vdpa;
	struct pci_dev *pdev;
	struct ifcvf_hw vf;
};

struct ifcvf_vring_lm_cfg {
	u32 idx_addr[2];
	u8 reserved[IFCVF_LM_CFG_SIZE - 8];
};

struct ifcvf_lm_cfg {
	u8 reserved[IFCVF_LM_RING_STATE_OFFSET];
	struct ifcvf_vring_lm_cfg vring_lm_cfg[IFCVF_MAX_QUEUE_PAIRS];
};

struct ifcvf_vdpa_mgmt_dev {
	struct vdpa_mgmt_dev mdev;
	struct ifcvf_adapter *adapter;
	struct pci_dev *pdev;
};

int ifcvf_init_hw(struct ifcvf_hw *hw, struct pci_dev *dev);
int ifcvf_start_hw(struct ifcvf_hw *hw);
void ifcvf_stop_hw(struct ifcvf_hw *hw);
void ifcvf_notify_queue(struct ifcvf_hw *hw, u16 qid);
void ifcvf_read_net_config(struct ifcvf_hw *hw, u64 offset,
			   void *dst, int length);
void ifcvf_write_net_config(struct ifcvf_hw *hw, u64 offset,
			    const void *src, int length);
u8 ifcvf_get_status(struct ifcvf_hw *hw);
void ifcvf_set_status(struct ifcvf_hw *hw, u8 status);
void io_write64_twopart(u64 val, u32 *lo, u32 *hi);
void ifcvf_reset(struct ifcvf_hw *hw);
u64 ifcvf_get_features(struct ifcvf_hw *hw);
u64 ifcvf_get_hw_features(struct ifcvf_hw *hw);
int ifcvf_verify_min_features(struct ifcvf_hw *hw, u64 features);
u16 ifcvf_get_vq_state(struct ifcvf_hw *hw, u16 qid);
int ifcvf_set_vq_state(struct ifcvf_hw *hw, u16 qid, u16 num);
struct ifcvf_adapter *vf_to_adapter(struct ifcvf_hw *hw);
int ifcvf_probed_virtio_net(struct ifcvf_hw *hw);
#endif /* _IFCVF_H_ */
