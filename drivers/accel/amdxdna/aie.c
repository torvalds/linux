// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include <linux/errno.h>

#include "aie.h"
#include "amdxdna_mailbox_helper.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_pci_drv.h"

void aie_dump_mgmt_chann_debug(struct aie_device *aie)
{
	struct amdxdna_dev *xdna = aie->xdna;

	XDNA_DBG(xdna, "i2x tail    0x%x", aie->mgmt_i2x.mb_tail_ptr_reg);
	XDNA_DBG(xdna, "i2x head    0x%x", aie->mgmt_i2x.mb_head_ptr_reg);
	XDNA_DBG(xdna, "i2x ringbuf 0x%x", aie->mgmt_i2x.rb_start_addr);
	XDNA_DBG(xdna, "i2x rsize   0x%x", aie->mgmt_i2x.rb_size);
	XDNA_DBG(xdna, "x2i tail    0x%x", aie->mgmt_x2i.mb_tail_ptr_reg);
	XDNA_DBG(xdna, "x2i head    0x%x", aie->mgmt_x2i.mb_head_ptr_reg);
	XDNA_DBG(xdna, "x2i ringbuf 0x%x", aie->mgmt_x2i.rb_start_addr);
	XDNA_DBG(xdna, "x2i rsize   0x%x", aie->mgmt_x2i.rb_size);
	XDNA_DBG(xdna, "x2i chann index 0x%x", aie->mgmt_chan_idx);
	XDNA_DBG(xdna, "mailbox protocol major 0x%x", aie->mgmt_prot_major);
	XDNA_DBG(xdna, "mailbox protocol minor 0x%x", aie->mgmt_prot_minor);
}

void aie_destroy_chann(struct aie_device *aie, struct mailbox_channel **chann)
{
	struct amdxdna_dev *xdna = aie->xdna;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	if (!*chann)
		return;

	xdna_mailbox_stop_channel(*chann);
	xdna_mailbox_free_channel(*chann);
	*chann = NULL;
}

int aie_send_mgmt_msg_wait(struct aie_device *aie, struct xdna_mailbox_msg *msg)
{
	struct amdxdna_dev *xdna = aie->xdna;
	struct xdna_notify *hdl = msg->handle;
	int ret;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	if (!aie->mgmt_chann)
		return -ENODEV;

	ret = xdna_send_msg_wait(xdna, aie->mgmt_chann, msg);
	if (ret == -ETIME)
		aie_destroy_chann(aie, &aie->mgmt_chann);

	if (!ret && *hdl->status) {
		XDNA_ERR(xdna, "command opcode 0x%x failed, status 0x%x",
			 msg->opcode, *hdl->data);
		ret = -EINVAL;
	}

	return ret;
}

int aie_check_protocol(struct aie_device *aie, u32 fw_major, u32 fw_minor)
{
	const struct amdxdna_fw_feature_tbl *feature;
	bool found = false;

	for (feature = aie->xdna->dev_info->fw_feature_tbl;
	     feature->major; feature++) {
		if (feature->major != fw_major)
			continue;
		if (fw_minor < feature->min_minor)
			continue;
		if (feature->max_minor > 0 && fw_minor > feature->max_minor)
			continue;

		aie->feature_mask |= feature->features;

		/* firmware version matches one of the driver support entry */
		found = true;
	}

	return found ? 0 : -EOPNOTSUPP;
}

static void amdxdna_update_vbnv(struct amdxdna_dev *xdna,
				const struct amdxdna_rev_vbnv *tbl,
				u32 rev)
{
	int i;

	for (i = 0; tbl[i].vbnv; i++) {
		if (tbl[i].revision == rev) {
			xdna->vbnv = tbl[i].vbnv;
			break;
		}
	}
}

void amdxdna_vbnv_init(struct amdxdna_dev *xdna)
{
	const struct amdxdna_dev_info *info = xdna->dev_info;
	u32 rev;

	xdna->vbnv = info->default_vbnv;

	if (!info->ops->get_dev_revision || !info->rev_vbnv_tbl)
		return;

	if (info->ops->get_dev_revision(xdna, &rev))
		return;

	amdxdna_update_vbnv(xdna, info->rev_vbnv_tbl, rev);
}

int amdxdna_get_metadata(struct aie_device *aie,
			 struct amdxdna_client *client,
			 struct amdxdna_drm_get_info *args)
{
	int ret = 0;
	u32 buf_sz;

	buf_sz = min(args->buffer_size, sizeof(aie->metadata));
	if (copy_to_user(u64_to_user_ptr(args->buffer), &aie->metadata, buf_sz))
		ret = -EFAULT;

	return ret;
}

void *amdxdna_alloc_msg_buffer(struct amdxdna_dev *xdna, u32 *size,
			       dma_addr_t *dma_addr)
{
	void *vaddr;
	int order;

	*size = max_t(u32, *size, SZ_8K);
	order = get_order(*size);
	if (order > MAX_PAGE_ORDER)
		return ERR_PTR(-EINVAL);
	*size = PAGE_SIZE << order;

	if (amdxdna_iova_on(xdna))
		return amdxdna_iommu_alloc(xdna, *size, dma_addr);

	vaddr = dma_alloc_noncoherent(xdna->ddev.dev, *size, dma_addr,
				      DMA_FROM_DEVICE, GFP_KERNEL);
	if (!vaddr)
		return ERR_PTR(-ENOMEM);

	return vaddr;
}

void amdxdna_free_msg_buffer(struct amdxdna_dev *xdna, size_t size,
			     void *cpu_addr, dma_addr_t dma_addr)
{
	if (amdxdna_iova_on(xdna)) {
		amdxdna_iommu_free(xdna, size, cpu_addr, dma_addr);
		return;
	}

	dma_free_noncoherent(xdna->ddev.dev, size, cpu_addr, dma_addr, DMA_FROM_DEVICE);
}
