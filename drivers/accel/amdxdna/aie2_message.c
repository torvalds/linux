// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_print.h>
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "aie2_msg_priv.h"
#include "aie2_pci.h"
#include "amdxdna_ctx.h"
#include "amdxdna_gem.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_mailbox_helper.h"
#include "amdxdna_pci_drv.h"

#define DECLARE_AIE2_MSG(name, op) \
	DECLARE_XDNA_MSG_COMMON(name, op, MAX_AIE2_STATUS_CODE)

static int aie2_send_mgmt_msg_wait(struct amdxdna_dev_hdl *ndev,
				   struct xdna_mailbox_msg *msg)
{
	struct amdxdna_dev *xdna = ndev->xdna;
	struct xdna_notify *hdl = msg->handle;
	int ret;

	if (!ndev->mgmt_chann)
		return -ENODEV;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));
	ret = xdna_send_msg_wait(xdna, ndev->mgmt_chann, msg);
	if (ret == -ETIME) {
		xdna_mailbox_stop_channel(ndev->mgmt_chann);
		xdna_mailbox_destroy_channel(ndev->mgmt_chann);
		ndev->mgmt_chann = NULL;
	}

	if (!ret && *hdl->data != AIE2_STATUS_SUCCESS) {
		XDNA_ERR(xdna, "command opcode 0x%x failed, status 0x%x",
			 msg->opcode, *hdl->data);
		ret = -EINVAL;
	}

	return ret;
}

int aie2_suspend_fw(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE2_MSG(suspend, MSG_OP_SUSPEND);

	return aie2_send_mgmt_msg_wait(ndev, &msg);
}

int aie2_resume_fw(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE2_MSG(suspend, MSG_OP_RESUME);

	return aie2_send_mgmt_msg_wait(ndev, &msg);
}

int aie2_set_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 value)
{
	DECLARE_AIE2_MSG(set_runtime_cfg, MSG_OP_SET_RUNTIME_CONFIG);

	req.type = type;
	req.value = value;

	return aie2_send_mgmt_msg_wait(ndev, &msg);
}

int aie2_get_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 *value)
{
	DECLARE_AIE2_MSG(get_runtime_cfg, MSG_OP_GET_RUNTIME_CONFIG);
	int ret;

	req.type = type;
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Failed to get runtime config, ret %d", ret);
		return ret;
	}

	*value = resp.value;
	return 0;
}

int aie2_check_protocol_version(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE2_MSG(protocol_version, MSG_OP_GET_PROTOCOL_VERSION);
	struct amdxdna_dev *xdna = ndev->xdna;
	int ret;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret) {
		XDNA_ERR(xdna, "Failed to get protocol version, ret %d", ret);
		return ret;
	}

	if (resp.major != ndev->priv->protocol_major) {
		XDNA_ERR(xdna, "Incompatible firmware protocol version major %d minor %d",
			 resp.major, resp.minor);
		return -EINVAL;
	}

	if (resp.minor < ndev->priv->protocol_minor) {
		XDNA_ERR(xdna, "Firmware minor version smaller than supported");
		return -EINVAL;
	}

	return 0;
}

int aie2_assign_mgmt_pasid(struct amdxdna_dev_hdl *ndev, u16 pasid)
{
	DECLARE_AIE2_MSG(assign_mgmt_pasid, MSG_OP_ASSIGN_MGMT_PASID);

	req.pasid = pasid;

	return aie2_send_mgmt_msg_wait(ndev, &msg);
}

int aie2_query_aie_version(struct amdxdna_dev_hdl *ndev, struct aie_version *version)
{
	DECLARE_AIE2_MSG(aie_version_info, MSG_OP_QUERY_AIE_VERSION);
	struct amdxdna_dev *xdna = ndev->xdna;
	int ret;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	XDNA_DBG(xdna, "Query AIE version - major: %u minor: %u completed",
		 resp.major, resp.minor);

	version->major = resp.major;
	version->minor = resp.minor;

	return 0;
}

int aie2_query_aie_metadata(struct amdxdna_dev_hdl *ndev, struct aie_metadata *metadata)
{
	DECLARE_AIE2_MSG(aie_tile_info, MSG_OP_QUERY_AIE_TILE_INFO);
	int ret;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	metadata->size = resp.info.size;
	metadata->cols = resp.info.cols;
	metadata->rows = resp.info.rows;

	metadata->version.major = resp.info.major;
	metadata->version.minor = resp.info.minor;

	metadata->core.row_count = resp.info.core_rows;
	metadata->core.row_start = resp.info.core_row_start;
	metadata->core.dma_channel_count = resp.info.core_dma_channels;
	metadata->core.lock_count = resp.info.core_locks;
	metadata->core.event_reg_count = resp.info.core_events;

	metadata->mem.row_count = resp.info.mem_rows;
	metadata->mem.row_start = resp.info.mem_row_start;
	metadata->mem.dma_channel_count = resp.info.mem_dma_channels;
	metadata->mem.lock_count = resp.info.mem_locks;
	metadata->mem.event_reg_count = resp.info.mem_events;

	metadata->shim.row_count = resp.info.shim_rows;
	metadata->shim.row_start = resp.info.shim_row_start;
	metadata->shim.dma_channel_count = resp.info.shim_dma_channels;
	metadata->shim.lock_count = resp.info.shim_locks;
	metadata->shim.event_reg_count = resp.info.shim_events;

	return 0;
}

int aie2_query_firmware_version(struct amdxdna_dev_hdl *ndev,
				struct amdxdna_fw_ver *fw_ver)
{
	DECLARE_AIE2_MSG(firmware_version, MSG_OP_GET_FIRMWARE_VERSION);
	int ret;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	fw_ver->major = resp.major;
	fw_ver->minor = resp.minor;
	fw_ver->sub = resp.sub;
	fw_ver->build = resp.build;

	return 0;
}

int aie2_create_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx)
{
	DECLARE_AIE2_MSG(create_ctx, MSG_OP_CREATE_CONTEXT);
	struct amdxdna_dev *xdna = ndev->xdna;
	struct xdna_mailbox_chann_res x2i;
	struct xdna_mailbox_chann_res i2x;
	struct cq_pair *cq_pair;
	u32 intr_reg;
	int ret;

	req.aie_type = 1;
	req.start_col = hwctx->start_col;
	req.num_col = hwctx->num_col;
	req.num_cq_pairs_requested = 1;
	req.pasid = hwctx->client->pasid;
	req.context_priority = 2;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	hwctx->fw_ctx_id = resp.context_id;
	WARN_ONCE(hwctx->fw_ctx_id == -1, "Unexpected context id");

	cq_pair = &resp.cq_pair[0];
	x2i.mb_head_ptr_reg = AIE2_MBOX_OFF(ndev, cq_pair->x2i_q.head_addr);
	x2i.mb_tail_ptr_reg = AIE2_MBOX_OFF(ndev, cq_pair->x2i_q.tail_addr);
	x2i.rb_start_addr   = AIE2_SRAM_OFF(ndev, cq_pair->x2i_q.buf_addr);
	x2i.rb_size	    = cq_pair->x2i_q.buf_size;

	i2x.mb_head_ptr_reg = AIE2_MBOX_OFF(ndev, cq_pair->i2x_q.head_addr);
	i2x.mb_tail_ptr_reg = AIE2_MBOX_OFF(ndev, cq_pair->i2x_q.tail_addr);
	i2x.rb_start_addr   = AIE2_SRAM_OFF(ndev, cq_pair->i2x_q.buf_addr);
	i2x.rb_size	    = cq_pair->i2x_q.buf_size;

	ret = pci_irq_vector(to_pci_dev(xdna->ddev.dev), resp.msix_id);
	if (ret == -EINVAL) {
		XDNA_ERR(xdna, "not able to create channel");
		goto out_destroy_context;
	}

	intr_reg = i2x.mb_head_ptr_reg + 4;
	hwctx->priv->mbox_chann = xdna_mailbox_create_channel(ndev->mbox, &x2i, &i2x,
							      intr_reg, ret);
	if (!hwctx->priv->mbox_chann) {
		XDNA_ERR(xdna, "not able to create channel");
		ret = -EINVAL;
		goto out_destroy_context;
	}

	XDNA_DBG(xdna, "%s mailbox channel irq: %d, msix_id: %d",
		 hwctx->name, ret, resp.msix_id);
	XDNA_DBG(xdna, "%s created fw ctx %d pasid %d", hwctx->name,
		 hwctx->fw_ctx_id, hwctx->client->pasid);

	return 0;

out_destroy_context:
	aie2_destroy_context(ndev, hwctx);
	return ret;
}

int aie2_destroy_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx)
{
	DECLARE_AIE2_MSG(destroy_ctx, MSG_OP_DESTROY_CONTEXT);
	struct amdxdna_dev *xdna = ndev->xdna;
	int ret;

	if (hwctx->fw_ctx_id == -1)
		return 0;

	xdna_mailbox_stop_channel(hwctx->priv->mbox_chann);

	req.context_id = hwctx->fw_ctx_id;
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		XDNA_WARN(xdna, "%s destroy context failed, ret %d", hwctx->name, ret);

	xdna_mailbox_destroy_channel(hwctx->priv->mbox_chann);
	XDNA_DBG(xdna, "%s destroyed fw ctx %d", hwctx->name,
		 hwctx->fw_ctx_id);
	hwctx->priv->mbox_chann = NULL;
	hwctx->fw_ctx_id = -1;

	return ret;
}

int aie2_map_host_buf(struct amdxdna_dev_hdl *ndev, u32 context_id, u64 addr, u64 size)
{
	DECLARE_AIE2_MSG(map_host_buffer, MSG_OP_MAP_HOST_BUFFER);
	struct amdxdna_dev *xdna = ndev->xdna;
	int ret;

	req.context_id = context_id;
	req.buf_addr = addr;
	req.buf_size = size;
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	XDNA_DBG(xdna, "fw ctx %d map host buf addr 0x%llx size 0x%llx",
		 context_id, addr, size);

	return 0;
}

int aie2_config_cu(struct amdxdna_hwctx *hwctx)
{
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	u32 shift = xdna->dev_info->dev_mem_buf_shift;
	DECLARE_AIE2_MSG(config_cu, MSG_OP_CONFIG_CU);
	struct drm_gem_object *gobj;
	struct amdxdna_gem_obj *abo;
	int ret, i;

	if (!chann)
		return -ENODEV;

	if (hwctx->cus->num_cus > MAX_NUM_CUS) {
		XDNA_DBG(xdna, "Exceed maximum CU %d", MAX_NUM_CUS);
		return -EINVAL;
	}

	for (i = 0; i < hwctx->cus->num_cus; i++) {
		struct amdxdna_cu_config *cu = &hwctx->cus->cu_configs[i];

		gobj = drm_gem_object_lookup(hwctx->client->filp, cu->cu_bo);
		if (!gobj) {
			XDNA_ERR(xdna, "Lookup GEM object failed");
			return -EINVAL;
		}
		abo = to_xdna_obj(gobj);

		if (abo->type != AMDXDNA_BO_DEV) {
			drm_gem_object_put(gobj);
			XDNA_ERR(xdna, "Invalid BO type");
			return -EINVAL;
		}

		req.cfgs[i] = FIELD_PREP(AIE2_MSG_CFG_CU_PDI_ADDR,
					 abo->mem.dev_addr >> shift);
		req.cfgs[i] |= FIELD_PREP(AIE2_MSG_CFG_CU_FUNC, cu->cu_func);
		XDNA_DBG(xdna, "CU %d full addr 0x%llx, cfg 0x%x", i,
			 abo->mem.dev_addr, req.cfgs[i]);
		drm_gem_object_put(gobj);
	}
	req.num_cus = hwctx->cus->num_cus;

	ret = xdna_send_msg_wait(xdna, chann, &msg);
	if (ret == -ETIME)
		aie2_destroy_context(xdna->dev_handle, hwctx);

	if (resp.status == AIE2_STATUS_SUCCESS) {
		XDNA_DBG(xdna, "Configure %d CUs, ret %d", req.num_cus, ret);
		return 0;
	}

	XDNA_ERR(xdna, "Command opcode 0x%x failed, status 0x%x ret %d",
		 msg.opcode, resp.status, ret);
	return ret;
}
