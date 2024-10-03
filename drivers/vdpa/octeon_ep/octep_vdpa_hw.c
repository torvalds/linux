// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Marvell. */

#include <linux/iopoll.h>

#include "octep_vdpa.h"

enum octep_mbox_ids {
	OCTEP_MBOX_MSG_SET_VQ_STATE = 1,
	OCTEP_MBOX_MSG_GET_VQ_STATE,
};

#define OCTEP_HW_TIMEOUT       10000000

#define MBOX_OFFSET            64
#define MBOX_RSP_MASK          0x00000001
#define MBOX_RC_MASK           0x0000FFFE

#define MBOX_RSP_TO_ERR(val)   (-(((val) & MBOX_RC_MASK) >> 2))
#define MBOX_AVAIL(val)        (((val) & MBOX_RSP_MASK))
#define MBOX_RSP(val)          ((val) & (MBOX_RC_MASK | MBOX_RSP_MASK))

#define DEV_RST_ACK_BIT        7
#define FEATURE_SEL_ACK_BIT    15
#define QUEUE_SEL_ACK_BIT      15

struct octep_mbox_hdr {
	u8 ver;
	u8 rsvd1;
	u16 id;
	u16 rsvd2;
#define MBOX_REQ_SIG (0xdead)
#define MBOX_RSP_SIG (0xbeef)
	u16 sig;
};

struct octep_mbox_sts {
	u16 rsp:1;
	u16 rc:15;
	u16 rsvd;
};

struct octep_mbox {
	struct octep_mbox_hdr hdr;
	struct octep_mbox_sts sts;
	u64 rsvd;
	u32 data[];
};

static inline struct octep_mbox __iomem *octep_get_mbox(struct octep_hw *oct_hw)
{
	return (struct octep_mbox __iomem *)(oct_hw->dev_cfg + MBOX_OFFSET);
}

static inline int octep_wait_for_mbox_avail(struct octep_mbox __iomem *mbox)
{
	u32 val;

	return readx_poll_timeout(ioread32, &mbox->sts, val, MBOX_AVAIL(val), 10,
				  OCTEP_HW_TIMEOUT);
}

static inline int octep_wait_for_mbox_rsp(struct octep_mbox __iomem *mbox)
{
	u32 val;

	return readx_poll_timeout(ioread32, &mbox->sts, val, MBOX_RSP(val), 10,
				  OCTEP_HW_TIMEOUT);
}

static inline void octep_write_hdr(struct octep_mbox __iomem *mbox, u16 id, u16 sig)
{
	iowrite16(id, &mbox->hdr.id);
	iowrite16(sig, &mbox->hdr.sig);
}

static inline u32 octep_read_sig(struct octep_mbox __iomem *mbox)
{
	return ioread16(&mbox->hdr.sig);
}

static inline void octep_write_sts(struct octep_mbox __iomem *mbox, u32 sts)
{
	iowrite32(sts, &mbox->sts);
}

static inline u32 octep_read_sts(struct octep_mbox __iomem *mbox)
{
	return ioread32(&mbox->sts);
}

static inline u32 octep_read32_word(struct octep_mbox __iomem *mbox, u16 word_idx)
{
	return ioread32(&mbox->data[word_idx]);
}

static inline void octep_write32_word(struct octep_mbox __iomem *mbox, u16 word_idx, u32 word)
{
	return iowrite32(word, &mbox->data[word_idx]);
}

static int octep_process_mbox(struct octep_hw *oct_hw, u16 id, u16 qid, void *buffer,
			      u32 buf_size, bool write)
{
	struct octep_mbox __iomem *mbox = octep_get_mbox(oct_hw);
	struct pci_dev *pdev = oct_hw->pdev;
	u32 *p = (u32 *)buffer;
	u16 data_wds;
	int ret, i;
	u32 val;

	if (!IS_ALIGNED(buf_size, 4))
		return -EINVAL;

	/* Make sure mbox space is available */
	ret = octep_wait_for_mbox_avail(mbox);
	if (ret) {
		dev_warn(&pdev->dev, "Timeout waiting for previous mbox data to be consumed\n");
		return ret;
	}
	data_wds = buf_size / 4;

	if (write) {
		for (i = 1; i <= data_wds; i++) {
			octep_write32_word(mbox, i, *p);
			p++;
		}
	}
	octep_write32_word(mbox, 0, (u32)qid);
	octep_write_sts(mbox, 0);

	octep_write_hdr(mbox, id, MBOX_REQ_SIG);

	ret = octep_wait_for_mbox_rsp(mbox);
	if (ret) {
		dev_warn(&pdev->dev, "Timeout waiting for mbox : %d response\n", id);
		return ret;
	}

	val = octep_read_sig(mbox);
	if ((val & 0xFFFF) != MBOX_RSP_SIG) {
		dev_warn(&pdev->dev, "Invalid Signature from mbox : %d response\n", id);
		return -EINVAL;
	}

	val = octep_read_sts(mbox);
	if (val & MBOX_RC_MASK) {
		ret = MBOX_RSP_TO_ERR(val);
		dev_warn(&pdev->dev, "Error while processing mbox : %d, err %d\n", id, ret);
		return ret;
	}

	if (!write)
		for (i = 1; i <= data_wds; i++)
			*p++ = octep_read32_word(mbox, i);

	return 0;
}

static void octep_mbox_init(struct octep_mbox __iomem *mbox)
{
	iowrite32(1, &mbox->sts);
}

int octep_verify_features(u64 features)
{
	/* Minimum features to expect */
	if (!(features & BIT_ULL(VIRTIO_F_VERSION_1)))
		return -EOPNOTSUPP;

	if (!(features & BIT_ULL(VIRTIO_F_NOTIFICATION_DATA)))
		return -EOPNOTSUPP;

	if (!(features & BIT_ULL(VIRTIO_F_RING_PACKED)))
		return -EOPNOTSUPP;

	return 0;
}

u8 octep_hw_get_status(struct octep_hw *oct_hw)
{
	return ioread8(&oct_hw->common_cfg->device_status);
}

void octep_hw_set_status(struct octep_hw *oct_hw, u8 status)
{
	iowrite8(status, &oct_hw->common_cfg->device_status);
}

void octep_hw_reset(struct octep_hw *oct_hw)
{
	u8 val;

	octep_hw_set_status(oct_hw, 0 | BIT(DEV_RST_ACK_BIT));
	if (readx_poll_timeout(ioread8, &oct_hw->common_cfg->device_status, val, !val, 10,
			       OCTEP_HW_TIMEOUT)) {
		dev_warn(&oct_hw->pdev->dev, "Octeon device reset timeout\n");
		return;
	}
}

static int feature_sel_write_with_timeout(struct octep_hw *oct_hw, u32 select, void __iomem *addr)
{
	u32 val;

	iowrite32(select | BIT(FEATURE_SEL_ACK_BIT), addr);

	if (readx_poll_timeout(ioread32, addr, val, val == select, 10, OCTEP_HW_TIMEOUT)) {
		dev_warn(&oct_hw->pdev->dev, "Feature select%d write timeout\n", select);
		return -1;
	}
	return 0;
}

u64 octep_hw_get_dev_features(struct octep_hw *oct_hw)
{
	u32 features_lo, features_hi;

	if (feature_sel_write_with_timeout(oct_hw, 0, &oct_hw->common_cfg->device_feature_select))
		return 0;

	features_lo = ioread32(&oct_hw->common_cfg->device_feature);

	if (feature_sel_write_with_timeout(oct_hw, 1, &oct_hw->common_cfg->device_feature_select))
		return 0;

	features_hi = ioread32(&oct_hw->common_cfg->device_feature);

	return ((u64)features_hi << 32) | features_lo;
}

u64 octep_hw_get_drv_features(struct octep_hw *oct_hw)
{
	u32 features_lo, features_hi;

	if (feature_sel_write_with_timeout(oct_hw, 0, &oct_hw->common_cfg->guest_feature_select))
		return 0;

	features_lo = ioread32(&oct_hw->common_cfg->guest_feature);

	if (feature_sel_write_with_timeout(oct_hw, 1, &oct_hw->common_cfg->guest_feature_select))
		return 0;

	features_hi = ioread32(&oct_hw->common_cfg->guest_feature);

	return ((u64)features_hi << 32) | features_lo;
}

void octep_hw_set_drv_features(struct octep_hw *oct_hw, u64 features)
{
	if (feature_sel_write_with_timeout(oct_hw, 0, &oct_hw->common_cfg->guest_feature_select))
		return;

	iowrite32(features & (BIT_ULL(32) - 1), &oct_hw->common_cfg->guest_feature);

	if (feature_sel_write_with_timeout(oct_hw, 1, &oct_hw->common_cfg->guest_feature_select))
		return;

	iowrite32(features >> 32, &oct_hw->common_cfg->guest_feature);
}

void octep_write_queue_select(struct octep_hw *oct_hw, u16 queue_id)
{
	u16 val;

	iowrite16(queue_id | BIT(QUEUE_SEL_ACK_BIT), &oct_hw->common_cfg->queue_select);

	if (readx_poll_timeout(ioread16, &oct_hw->common_cfg->queue_select, val, val == queue_id,
			       10, OCTEP_HW_TIMEOUT)) {
		dev_warn(&oct_hw->pdev->dev, "Queue select write timeout\n");
		return;
	}
}

void octep_notify_queue(struct octep_hw *oct_hw, u16 qid)
{
	iowrite16(qid, oct_hw->vqs[qid].notify_addr);
}

void octep_read_dev_config(struct octep_hw *oct_hw, u64 offset, void *dst, int length)
{
	u8 old_gen, new_gen, *p;
	int i;

	if (WARN_ON(offset + length > oct_hw->config_size))
		return;

	do {
		old_gen = ioread8(&oct_hw->common_cfg->config_generation);
		p = dst;
		for (i = 0; i < length; i++)
			*p++ = ioread8(oct_hw->dev_cfg + offset + i);

		new_gen = ioread8(&oct_hw->common_cfg->config_generation);
	} while (old_gen != new_gen);
}

int octep_set_vq_address(struct octep_hw *oct_hw, u16 qid, u64 desc_area, u64 driver_area,
			 u64 device_area)
{
	struct virtio_pci_common_cfg __iomem *cfg = oct_hw->common_cfg;

	octep_write_queue_select(oct_hw, qid);
	vp_iowrite64_twopart(desc_area, &cfg->queue_desc_lo,
			     &cfg->queue_desc_hi);
	vp_iowrite64_twopart(driver_area, &cfg->queue_avail_lo,
			     &cfg->queue_avail_hi);
	vp_iowrite64_twopart(device_area, &cfg->queue_used_lo,
			     &cfg->queue_used_hi);

	return 0;
}

int octep_get_vq_state(struct octep_hw *oct_hw, u16 qid, struct vdpa_vq_state *state)
{
	return octep_process_mbox(oct_hw, OCTEP_MBOX_MSG_GET_VQ_STATE, qid, state,
				  sizeof(*state), 0);
}

int octep_set_vq_state(struct octep_hw *oct_hw, u16 qid, const struct vdpa_vq_state *state)
{
	struct vdpa_vq_state q_state;

	memcpy(&q_state, state, sizeof(struct vdpa_vq_state));
	return octep_process_mbox(oct_hw, OCTEP_MBOX_MSG_SET_VQ_STATE, qid, &q_state,
				  sizeof(*state), 1);
}

void octep_set_vq_num(struct octep_hw *oct_hw, u16 qid, u32 num)
{
	struct virtio_pci_common_cfg __iomem *cfg = oct_hw->common_cfg;

	octep_write_queue_select(oct_hw, qid);
	iowrite16(num, &cfg->queue_size);
}

void octep_set_vq_ready(struct octep_hw *oct_hw, u16 qid, bool ready)
{
	struct virtio_pci_common_cfg __iomem *cfg = oct_hw->common_cfg;

	octep_write_queue_select(oct_hw, qid);
	iowrite16(ready, &cfg->queue_enable);
}

bool octep_get_vq_ready(struct octep_hw *oct_hw, u16 qid)
{
	struct virtio_pci_common_cfg __iomem *cfg = oct_hw->common_cfg;

	octep_write_queue_select(oct_hw, qid);
	return ioread16(&cfg->queue_enable);
}

u16 octep_get_vq_size(struct octep_hw *oct_hw)
{
	octep_write_queue_select(oct_hw, 0);
	return ioread16(&oct_hw->common_cfg->queue_size);
}

static u32 octep_get_config_size(struct octep_hw *oct_hw)
{
	return sizeof(struct virtio_net_config);
}

static void __iomem *octep_get_cap_addr(struct octep_hw *oct_hw, struct virtio_pci_cap *cap)
{
	struct device *dev = &oct_hw->pdev->dev;
	u32 length = le32_to_cpu(cap->length);
	u32 offset = le32_to_cpu(cap->offset);
	u8  bar    = cap->bar;
	u32 len;

	if (bar != OCTEP_HW_CAPS_BAR) {
		dev_err(dev, "Invalid bar: %u\n", bar);
		return NULL;
	}
	if (offset + length < offset) {
		dev_err(dev, "offset(%u) + length(%u) overflows\n",
			offset, length);
		return NULL;
	}
	len = pci_resource_len(oct_hw->pdev, bar);
	if (offset + length > len) {
		dev_err(dev, "invalid cap: overflows bar space: %u > %u\n",
			offset + length, len);
		return NULL;
	}
	return oct_hw->base[bar] + offset;
}

/* In Octeon DPU device, the virtio config space is completely
 * emulated by the device's firmware. So, the standard pci config
 * read apis can't be used for reading the virtio capability.
 */
static void octep_pci_caps_read(struct octep_hw *oct_hw, void *buf, size_t len, off_t offset)
{
	u8 __iomem *bar = oct_hw->base[OCTEP_HW_CAPS_BAR];
	u8 *p = buf;
	size_t i;

	for (i = 0; i < len; i++)
		*p++ = ioread8(bar + offset + i);
}

static int octep_pci_signature_verify(struct octep_hw *oct_hw)
{
	u32 signature[2];

	octep_pci_caps_read(oct_hw, &signature, sizeof(signature), 0);

	if (signature[0] != OCTEP_FW_READY_SIGNATURE0)
		return -1;

	if (signature[1] != OCTEP_FW_READY_SIGNATURE1)
		return -1;

	return 0;
}

int octep_hw_caps_read(struct octep_hw *oct_hw, struct pci_dev *pdev)
{
	struct octep_mbox __iomem *mbox;
	struct device *dev = &pdev->dev;
	struct virtio_pci_cap cap;
	u16 notify_off;
	int i, ret;
	u8 pos;

	oct_hw->pdev = pdev;
	ret = octep_pci_signature_verify(oct_hw);
	if (ret) {
		dev_err(dev, "Octeon Virtio FW is not initialized\n");
		return -EIO;
	}

	octep_pci_caps_read(oct_hw, &pos, 1, PCI_CAPABILITY_LIST);

	while (pos) {
		octep_pci_caps_read(oct_hw, &cap, 2, pos);

		if (cap.cap_vndr != PCI_CAP_ID_VNDR) {
			dev_err(dev, "Found invalid capability vndr id: %d\n", cap.cap_vndr);
			break;
		}

		octep_pci_caps_read(oct_hw, &cap, sizeof(cap), pos);

		dev_info(dev, "[%2x] cfg type: %u, bar: %u, offset: %04x, len: %u\n",
			 pos, cap.cfg_type, cap.bar, cap.offset, cap.length);

		switch (cap.cfg_type) {
		case VIRTIO_PCI_CAP_COMMON_CFG:
			oct_hw->common_cfg = octep_get_cap_addr(oct_hw, &cap);
			break;
		case VIRTIO_PCI_CAP_NOTIFY_CFG:
			octep_pci_caps_read(oct_hw, &oct_hw->notify_off_multiplier,
					    4, pos + sizeof(cap));

			oct_hw->notify_base = octep_get_cap_addr(oct_hw, &cap);
			oct_hw->notify_bar = cap.bar;
			oct_hw->notify_base_pa = pci_resource_start(pdev, cap.bar) +
						 le32_to_cpu(cap.offset);
			break;
		case VIRTIO_PCI_CAP_DEVICE_CFG:
			oct_hw->dev_cfg = octep_get_cap_addr(oct_hw, &cap);
			break;
		case VIRTIO_PCI_CAP_ISR_CFG:
			oct_hw->isr = octep_get_cap_addr(oct_hw, &cap);
			break;
		}

		pos = cap.cap_next;
	}
	if (!oct_hw->common_cfg || !oct_hw->notify_base ||
	    !oct_hw->dev_cfg    || !oct_hw->isr) {
		dev_err(dev, "Incomplete PCI capabilities");
		return -EIO;
	}
	dev_info(dev, "common cfg mapped at: %p\n", oct_hw->common_cfg);
	dev_info(dev, "device cfg mapped at: %p\n", oct_hw->dev_cfg);
	dev_info(dev, "isr cfg mapped at: %p\n", oct_hw->isr);
	dev_info(dev, "notify base: %p, notify off multiplier: %u\n",
		 oct_hw->notify_base, oct_hw->notify_off_multiplier);

	oct_hw->config_size = octep_get_config_size(oct_hw);
	oct_hw->features = octep_hw_get_dev_features(oct_hw);

	ret = octep_verify_features(oct_hw->features);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't read features from the device FW\n");
		return ret;
	}
	oct_hw->nr_vring = vp_ioread16(&oct_hw->common_cfg->num_queues);

	oct_hw->vqs = devm_kcalloc(&pdev->dev, oct_hw->nr_vring, sizeof(*oct_hw->vqs), GFP_KERNEL);
	if (!oct_hw->vqs)
		return -ENOMEM;

	oct_hw->irq = -1;

	dev_info(&pdev->dev, "Device features : %llx\n", oct_hw->features);
	dev_info(&pdev->dev, "Maximum queues : %u\n", oct_hw->nr_vring);

	for (i = 0; i < oct_hw->nr_vring; i++) {
		octep_write_queue_select(oct_hw, i);
		notify_off = vp_ioread16(&oct_hw->common_cfg->queue_notify_off);
		oct_hw->vqs[i].notify_addr = oct_hw->notify_base +
			notify_off * oct_hw->notify_off_multiplier;
		oct_hw->vqs[i].cb_notify_addr = (u32 __iomem *)oct_hw->vqs[i].notify_addr + 1;
		oct_hw->vqs[i].notify_pa = oct_hw->notify_base_pa +
			notify_off * oct_hw->notify_off_multiplier;
	}
	mbox = octep_get_mbox(oct_hw);
	octep_mbox_init(mbox);
	dev_info(dev, "mbox mapped at: %p\n", mbox);

	return 0;
}
