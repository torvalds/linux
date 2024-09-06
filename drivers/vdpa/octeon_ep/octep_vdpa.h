/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024 Marvell.
 */
#ifndef __OCTEP_VDPA_H__
#define __OCTEP_VDPA_H__

#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/vdpa.h>
#include <linux/virtio_pci_modern.h>
#include <uapi/linux/virtio_net.h>
#include <uapi/linux/virtio_blk.h>
#include <uapi/linux/virtio_config.h>
#include <uapi/linux/virtio_pci.h>
#include <uapi/linux/vdpa.h>

#define OCTEP_VDPA_DEVID_CN106K_PF 0xb900
#define OCTEP_VDPA_DEVID_CN106K_VF 0xb903
#define OCTEP_VDPA_DEVID_CN105K_PF 0xba00
#define OCTEP_VDPA_DEVID_CN105K_VF 0xba03
#define OCTEP_VDPA_DEVID_CN103K_PF 0xbd00
#define OCTEP_VDPA_DEVID_CN103K_VF 0xbd03

#define OCTEP_HW_MBOX_BAR 0
#define OCTEP_HW_CAPS_BAR 4

#define OCTEP_DEV_READY_SIGNATURE 0xBABABABA

#define OCTEP_EPF_RINFO(x) (0x000209f0 | ((x) << 25))
#define OCTEP_VF_MBOX_DATA(x) (0x00010210 | ((x) << 17))
#define OCTEP_PF_MBOX_DATA(x) (0x00022000 | ((x) << 4))

#define OCTEP_EPF_RINFO_RPVF(val) (((val) >> 32) & 0xF)
#define OCTEP_EPF_RINFO_NVFS(val) (((val) >> 48) & 0x7F)

#define OCTEP_FW_READY_SIGNATURE0  0xFEEDFEED
#define OCTEP_FW_READY_SIGNATURE1  0x3355ffaa

enum octep_vdpa_dev_status {
	OCTEP_VDPA_DEV_STATUS_INVALID,
	OCTEP_VDPA_DEV_STATUS_ALLOC,
	OCTEP_VDPA_DEV_STATUS_WAIT_FOR_BAR_INIT,
	OCTEP_VDPA_DEV_STATUS_INIT,
	OCTEP_VDPA_DEV_STATUS_READY,
	OCTEP_VDPA_DEV_STATUS_UNINIT
};

struct octep_vring_info {
	struct vdpa_callback cb;
	void __iomem *notify_addr;
	u32 __iomem *cb_notify_addr;
	phys_addr_t notify_pa;
	char msix_name[256];
};

struct octep_hw {
	struct pci_dev *pdev;
	u8 __iomem *base[PCI_STD_NUM_BARS];
	struct virtio_pci_common_cfg __iomem *common_cfg;
	u8 __iomem *dev_cfg;
	u8 __iomem *isr;
	void __iomem *notify_base;
	phys_addr_t notify_base_pa;
	u32 notify_off_multiplier;
	u8 notify_bar;
	struct octep_vring_info *vqs;
	struct vdpa_callback config_cb;
	u64 features;
	u16 nr_vring;
	u32 config_size;
	int irq;
};

u8 octep_hw_get_status(struct octep_hw *oct_hw);
void octep_hw_set_status(struct octep_hw *dev, uint8_t status);
void octep_hw_reset(struct octep_hw *oct_hw);
void octep_write_queue_select(struct octep_hw *oct_hw, u16 queue_id);
void octep_notify_queue(struct octep_hw *oct_hw, u16 qid);
void octep_read_dev_config(struct octep_hw *oct_hw, u64 offset, void *dst, int length);
int octep_set_vq_address(struct octep_hw *oct_hw, u16 qid, u64 desc_area, u64 driver_area,
			 u64 device_area);
void octep_set_vq_num(struct octep_hw *oct_hw, u16 qid, u32 num);
void octep_set_vq_ready(struct octep_hw *oct_hw, u16 qid, bool ready);
bool octep_get_vq_ready(struct octep_hw *oct_hw, u16 qid);
int octep_set_vq_state(struct octep_hw *oct_hw, u16 qid, const struct vdpa_vq_state *state);
int octep_get_vq_state(struct octep_hw *oct_hw, u16 qid, struct vdpa_vq_state *state);
u16 octep_get_vq_size(struct octep_hw *oct_hw);
int octep_hw_caps_read(struct octep_hw *oct_hw, struct pci_dev *pdev);
u64 octep_hw_get_dev_features(struct octep_hw *oct_hw);
void octep_hw_set_drv_features(struct octep_hw *oct_hw, u64 features);
u64 octep_hw_get_drv_features(struct octep_hw *oct_hw);
int octep_verify_features(u64 features);

#endif /* __OCTEP_VDPA_H__ */
