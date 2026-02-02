// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_cache.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_print.h>
#include <drm/gpu_scheduler.h>
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/xarray.h>

#include "aie2_msg_priv.h"
#include "aie2_pci.h"
#include "amdxdna_ctx.h"
#include "amdxdna_gem.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_mailbox_helper.h"
#include "amdxdna_pci_drv.h"

#define DECLARE_AIE2_MSG(name, op) \
	DECLARE_XDNA_MSG_COMMON(name, op, MAX_AIE2_STATUS_CODE)

#define EXEC_MSG_OPS(xdna)	((xdna)->dev_handle->exec_msg_ops)

static int aie2_send_mgmt_msg_wait(struct amdxdna_dev_hdl *ndev,
				   struct xdna_mailbox_msg *msg)
{
	struct amdxdna_dev *xdna = ndev->xdna;
	struct xdna_notify *hdl = msg->handle;
	int ret;

	if (!ndev->mgmt_chann)
		return -ENODEV;

	ret = xdna_send_msg_wait(xdna, ndev->mgmt_chann, msg);
	if (ret == -ETIME) {
		xdna_mailbox_stop_channel(ndev->mgmt_chann);
		xdna_mailbox_destroy_channel(ndev->mgmt_chann);
		ndev->mgmt_chann = NULL;
	}

	if (!ret && *hdl->status != AIE2_STATUS_SUCCESS) {
		XDNA_ERR(xdna, "command opcode 0x%x failed, status 0x%x",
			 msg->opcode, *hdl->data);
		ret = -EINVAL;
	}

	return ret;
}

void *aie2_alloc_msg_buffer(struct amdxdna_dev_hdl *ndev, u32 *size,
			    dma_addr_t *dma_addr)
{
	struct amdxdna_dev *xdna = ndev->xdna;
	int order;

	*size = max(*size, SZ_8K);
	order = get_order(*size);
	if (order > MAX_PAGE_ORDER)
		return NULL;
	*size = PAGE_SIZE << order;

	return dma_alloc_noncoherent(xdna->ddev.dev, *size, dma_addr,
				     DMA_FROM_DEVICE, GFP_KERNEL);
}

int aie2_suspend_fw(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE2_MSG(suspend, MSG_OP_SUSPEND);
	int ret;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Failed to suspend fw, ret %d", ret);
		return ret;
	}

	return aie2_psp_waitmode_poll(ndev->psp_hdl);
}

int aie2_resume_fw(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE2_MSG(suspend, MSG_OP_RESUME);

	return aie2_send_mgmt_msg_wait(ndev, &msg);
}

int aie2_set_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 value)
{
	DECLARE_AIE2_MSG(set_runtime_cfg, MSG_OP_SET_RUNTIME_CONFIG);
	int ret;

	req.type = type;
	req.value = value;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Failed to set runtime config, ret %d", ret);
		return ret;
	}

	return 0;
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

static int aie2_destroy_context_req(struct amdxdna_dev_hdl *ndev, u32 id)
{
	DECLARE_AIE2_MSG(destroy_ctx, MSG_OP_DESTROY_CONTEXT);
	struct amdxdna_dev *xdna = ndev->xdna;
	int ret;

	req.context_id = id;
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		XDNA_WARN(xdna, "Destroy context failed, ret %d", ret);

	return ret;
}

static u32 aie2_get_context_priority(struct amdxdna_dev_hdl *ndev,
				     struct amdxdna_hwctx *hwctx)
{
	if (!AIE2_FEATURE_ON(ndev, AIE2_PREEMPT))
		return PRIORITY_HIGH;

	switch (hwctx->qos.priority) {
	case AMDXDNA_QOS_REALTIME_PRIORITY:
		return PRIORITY_REALTIME;
	case AMDXDNA_QOS_HIGH_PRIORITY:
		return PRIORITY_HIGH;
	case AMDXDNA_QOS_NORMAL_PRIORITY:
		return PRIORITY_NORMAL;
	case AMDXDNA_QOS_LOW_PRIORITY:
		return PRIORITY_LOW;
	default:
		return PRIORITY_HIGH;
	}
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
	req.num_unused_col = hwctx->num_unused_col;
	req.num_cq_pairs_requested = 1;
	req.pasid = hwctx->client->pasid;
	req.context_priority = aie2_get_context_priority(ndev, hwctx);

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	hwctx->fw_ctx_id = resp.context_id;
	if (WARN_ON_ONCE(hwctx->fw_ctx_id == -1))
		return -EINVAL;

	if (ndev->force_preempt_enabled) {
		ret = aie2_runtime_cfg(ndev, AIE2_RT_CFG_FORCE_PREEMPT, &hwctx->fw_ctx_id);
		if (ret) {
			XDNA_ERR(xdna, "failed to enable force preempt %d", ret);
			goto del_ctx_req;
		}
	}

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
		XDNA_ERR(xdna, "Alloc IRQ failed %d", ret);
		goto del_ctx_req;
	}

	intr_reg = i2x.mb_head_ptr_reg + 4;
	hwctx->priv->mbox_chann = xdna_mailbox_create_channel(ndev->mbox, &x2i, &i2x,
							      intr_reg, ret);
	if (!hwctx->priv->mbox_chann) {
		XDNA_ERR(xdna, "Not able to create channel");
		ret = -EINVAL;
		goto del_ctx_req;
	}
	ndev->hwctx_num++;

	XDNA_DBG(xdna, "Mailbox channel irq: %d, msix_id: %d", ret, resp.msix_id);
	XDNA_DBG(xdna, "Created fw ctx %d pasid %d", hwctx->fw_ctx_id, hwctx->client->pasid);

	return 0;

del_ctx_req:
	aie2_destroy_context_req(ndev, hwctx->fw_ctx_id);
	return ret;
}

int aie2_destroy_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_dev *xdna = ndev->xdna;
	int ret;

	xdna_mailbox_stop_channel(hwctx->priv->mbox_chann);
	ret = aie2_destroy_context_req(ndev, hwctx->fw_ctx_id);
	xdna_mailbox_destroy_channel(hwctx->priv->mbox_chann);
	XDNA_DBG(xdna, "Destroyed fw ctx %d", hwctx->fw_ctx_id);
	hwctx->priv->mbox_chann = NULL;
	hwctx->fw_ctx_id = -1;
	ndev->hwctx_num--;

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

static int amdxdna_hwctx_col_map(struct amdxdna_hwctx *hwctx, void *arg)
{
	u32 *bitmap = arg;

	*bitmap |= GENMASK(hwctx->start_col + hwctx->num_col - 1, hwctx->start_col);

	return 0;
}

int aie2_query_status(struct amdxdna_dev_hdl *ndev, char __user *buf,
		      u32 size, u32 *cols_filled)
{
	DECLARE_AIE2_MSG(aie_column_info, MSG_OP_QUERY_COL_STATUS);
	struct amdxdna_dev *xdna = ndev->xdna;
	u32 buf_sz = size, aie_bitmap = 0;
	struct amdxdna_client *client;
	dma_addr_t dma_addr;
	u8 *buff_addr;
	int ret;

	buff_addr = aie2_alloc_msg_buffer(ndev, &buf_sz, &dma_addr);
	if (!buff_addr)
		return -ENOMEM;

	/* Go through each hardware context and mark the AIE columns that are active */
	list_for_each_entry(client, &xdna->client_list, node)
		amdxdna_hwctx_walk(client, &aie_bitmap, amdxdna_hwctx_col_map);

	*cols_filled = 0;
	req.dump_buff_addr = dma_addr;
	req.dump_buff_size = buf_sz;
	req.num_cols = hweight32(aie_bitmap);
	req.aie_bitmap = aie_bitmap;

	drm_clflush_virt_range(buff_addr, size); /* device can access */
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret) {
		XDNA_ERR(xdna, "Error during NPU query, status %d", ret);
		goto fail;
	}

	XDNA_DBG(xdna, "Query NPU status completed");

	if (size < resp.size) {
		ret = -EINVAL;
		XDNA_ERR(xdna, "Bad buffer size. Available: %u. Needs: %u", size, resp.size);
		goto fail;
	}

	if (copy_to_user(buf, buff_addr, resp.size)) {
		ret = -EFAULT;
		XDNA_ERR(xdna, "Failed to copy NPU status to user space");
		goto fail;
	}

	*cols_filled = aie_bitmap;

fail:
	aie2_free_msg_buffer(ndev, buf_sz, buff_addr, dma_addr);
	return ret;
}

int aie2_query_telemetry(struct amdxdna_dev_hdl *ndev,
			 char __user *buf, u32 size,
			 struct amdxdna_drm_query_telemetry_header *header)
{
	DECLARE_AIE2_MSG(get_telemetry, MSG_OP_GET_TELEMETRY);
	struct amdxdna_dev *xdna = ndev->xdna;
	dma_addr_t dma_addr;
	u32 buf_sz = size;
	u8 *addr;
	int ret;

	if (header->type >= MAX_TELEMETRY_TYPE)
		return -EINVAL;

	addr = aie2_alloc_msg_buffer(ndev, &buf_sz, &dma_addr);
	if (!addr)
		return -ENOMEM;

	req.buf_addr = dma_addr;
	req.buf_size = buf_sz;
	req.type = header->type;

	drm_clflush_virt_range(addr, size); /* device can access */
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret) {
		XDNA_ERR(xdna, "Query telemetry failed, status %d", ret);
		goto free_buf;
	}

	if (size < resp.size) {
		ret = -EINVAL;
		XDNA_ERR(xdna, "Bad buffer size. Available: %u. Needs: %u", size, resp.size);
		goto free_buf;
	}

	if (copy_to_user(buf, addr, resp.size)) {
		ret = -EFAULT;
		XDNA_ERR(xdna, "Failed to copy telemetry to user space");
		goto free_buf;
	}

	header->major = resp.major;
	header->minor = resp.minor;

free_buf:
	aie2_free_msg_buffer(ndev, buf_sz, addr, dma_addr);
	return ret;
}

int aie2_register_asyn_event_msg(struct amdxdna_dev_hdl *ndev, dma_addr_t addr, u32 size,
				 void *handle, int (*cb)(void*, void __iomem *, size_t))
{
	struct async_event_msg_req req = { 0 };
	struct xdna_mailbox_msg msg = {
		.send_data = (u8 *)&req,
		.send_size = sizeof(req),
		.handle = handle,
		.opcode = MSG_OP_REGISTER_ASYNC_EVENT_MSG,
		.notify_cb = cb,
	};

	req.buf_addr = addr;
	req.buf_size = size;

	XDNA_DBG(ndev->xdna, "Register addr 0x%llx size 0x%x", addr, size);
	return xdna_mailbox_send_msg(ndev->mgmt_chann, &msg, TX_TIMEOUT);
}

int aie2_config_cu(struct amdxdna_hwctx *hwctx,
		   int (*notify_cb)(void *, void __iomem *, size_t))
{
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	u32 shift = xdna->dev_info->dev_mem_buf_shift;
	struct config_cu_req req = { 0 };
	struct xdna_mailbox_msg msg;
	struct drm_gem_object *gobj;
	struct amdxdna_gem_obj *abo;
	int i;

	if (!chann)
		return -ENODEV;

	if (!hwctx->cus)
		return 0;

	if (hwctx->cus->num_cus > MAX_NUM_CUS) {
		XDNA_DBG(xdna, "Exceed maximum CU %d", MAX_NUM_CUS);
		return -EINVAL;
	}

	for (i = 0; i < hwctx->cus->num_cus; i++) {
		struct amdxdna_cu_config *cu = &hwctx->cus->cu_configs[i];

		if (XDNA_MBZ_DBG(xdna, cu->pad, sizeof(cu->pad)))
			return -EINVAL;

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

	msg.send_data = (u8 *)&req;
	msg.send_size = sizeof(req);
	msg.handle = hwctx;
	msg.opcode = MSG_OP_CONFIG_CU;
	msg.notify_cb = notify_cb;
	return xdna_mailbox_send_msg(chann, &msg, TX_TIMEOUT);
}

static int aie2_init_exec_cu_req(struct amdxdna_gem_obj *cmd_bo, void *req,
				 size_t *size, u32 *msg_op)
{
	struct execute_buffer_req *cu_req = req;
	u32 cmd_len;
	void *cmd;

	cmd = amdxdna_cmd_get_payload(cmd_bo, &cmd_len);
	if (cmd_len > sizeof(cu_req->payload))
		return -EINVAL;

	cu_req->cu_idx = amdxdna_cmd_get_cu_idx(cmd_bo);
	if (cu_req->cu_idx == INVALID_CU_IDX)
		return -EINVAL;

	memcpy(cu_req->payload, cmd, cmd_len);

	*size = sizeof(*cu_req);
	*msg_op = MSG_OP_EXECUTE_BUFFER_CF;
	return 0;
}

static int aie2_init_exec_dpu_req(struct amdxdna_gem_obj *cmd_bo, void *req,
				  size_t *size, u32 *msg_op)
{
	struct exec_dpu_req *dpu_req = req;
	struct amdxdna_cmd_start_npu *sn;
	u32 cmd_len;

	sn = amdxdna_cmd_get_payload(cmd_bo, &cmd_len);
	if (cmd_len - sizeof(*sn) > sizeof(dpu_req->payload))
		return -EINVAL;

	dpu_req->cu_idx = amdxdna_cmd_get_cu_idx(cmd_bo);
	if (dpu_req->cu_idx == INVALID_CU_IDX)
		return -EINVAL;

	dpu_req->inst_buf_addr = sn->buffer;
	dpu_req->inst_size = sn->buffer_size;
	dpu_req->inst_prop_cnt = sn->prop_count;
	memcpy(dpu_req->payload, sn->prop_args, cmd_len - sizeof(*sn));

	*size = sizeof(*dpu_req);
	*msg_op = MSG_OP_EXEC_DPU;
	return 0;
}

static void aie2_init_exec_chain_req(void *req, u64 slot_addr, size_t size, u32 cmd_cnt)
{
	struct cmd_chain_req *chain_req = req;

	chain_req->buf_addr = slot_addr;
	chain_req->buf_size = size;
	chain_req->count = cmd_cnt;
}

static void aie2_init_npu_chain_req(void *req, u64 slot_addr, size_t size, u32 cmd_cnt)
{
	struct cmd_chain_npu_req *npu_chain_req = req;

	npu_chain_req->flags = 0;
	npu_chain_req->reserved = 0;
	npu_chain_req->buf_addr = slot_addr;
	npu_chain_req->buf_size = size;
	npu_chain_req->count = cmd_cnt;
}

static int
aie2_cmdlist_fill_cf(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size)
{
	struct cmd_chain_slot_execbuf_cf *cf_slot = slot;
	u32 cmd_len;
	void *cmd;

	cmd = amdxdna_cmd_get_payload(cmd_bo, &cmd_len);
	if (*size < sizeof(*cf_slot) + cmd_len)
		return -EINVAL;

	cf_slot->cu_idx = amdxdna_cmd_get_cu_idx(cmd_bo);
	if (cf_slot->cu_idx == INVALID_CU_IDX)
		return -EINVAL;

	cf_slot->arg_cnt = cmd_len / sizeof(u32);
	memcpy(cf_slot->args, cmd, cmd_len);
	/* Accurate slot size to hint firmware to do necessary copy */
	*size = sizeof(*cf_slot) + cmd_len;
	return 0;
}

static int
aie2_cmdlist_fill_dpu(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size)
{
	struct cmd_chain_slot_dpu *dpu_slot = slot;
	struct amdxdna_cmd_start_npu *sn;
	u32 cmd_len;
	u32 arg_sz;

	sn = amdxdna_cmd_get_payload(cmd_bo, &cmd_len);
	arg_sz = cmd_len - sizeof(*sn);
	if (cmd_len < sizeof(*sn) || arg_sz > MAX_DPU_ARGS_SIZE)
		return -EINVAL;

	if (*size < sizeof(*dpu_slot) + arg_sz)
		return -EINVAL;

	dpu_slot->cu_idx = amdxdna_cmd_get_cu_idx(cmd_bo);
	if (dpu_slot->cu_idx == INVALID_CU_IDX)
		return -EINVAL;

	dpu_slot->inst_buf_addr = sn->buffer;
	dpu_slot->inst_size = sn->buffer_size;
	dpu_slot->inst_prop_cnt = sn->prop_count;
	dpu_slot->arg_cnt = arg_sz / sizeof(u32);
	memcpy(dpu_slot->args, sn->prop_args, arg_sz);

	/* Accurate slot size to hint firmware to do necessary copy */
	*size = sizeof(*dpu_slot) + arg_sz;
	return 0;
}

static int aie2_cmdlist_unsupp(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size)
{
	return -EOPNOTSUPP;
}

static u32 aie2_get_chain_msg_op(u32 cmd_op)
{
	switch (cmd_op) {
	case ERT_START_CU:
		return MSG_OP_CHAIN_EXEC_BUFFER_CF;
	case ERT_START_NPU:
		return MSG_OP_CHAIN_EXEC_DPU;
	default:
		break;
	}

	return MSG_OP_MAX_OPCODE;
}

static struct aie2_exec_msg_ops legacy_exec_message_ops = {
	.init_cu_req = aie2_init_exec_cu_req,
	.init_dpu_req = aie2_init_exec_dpu_req,
	.init_chain_req = aie2_init_exec_chain_req,
	.fill_cf_slot = aie2_cmdlist_fill_cf,
	.fill_dpu_slot = aie2_cmdlist_fill_dpu,
	.fill_preempt_slot = aie2_cmdlist_unsupp,
	.fill_elf_slot = aie2_cmdlist_unsupp,
	.get_chain_msg_op = aie2_get_chain_msg_op,
};

static int
aie2_cmdlist_fill_npu_cf(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size)
{
	struct cmd_chain_slot_npu *npu_slot = slot;
	u32 cmd_len;
	void *cmd;

	memset(npu_slot, 0, sizeof(*npu_slot));
	cmd = amdxdna_cmd_get_payload(cmd_bo, &cmd_len);
	if (*size < sizeof(*npu_slot) + cmd_len)
		return -EINVAL;

	npu_slot->cu_idx = amdxdna_cmd_get_cu_idx(cmd_bo);
	if (npu_slot->cu_idx == INVALID_CU_IDX)
		return -EINVAL;

	npu_slot->type = EXEC_NPU_TYPE_NON_ELF;
	npu_slot->arg_cnt = cmd_len / sizeof(u32);
	memcpy(npu_slot->args, cmd, cmd_len);

	*size = sizeof(*npu_slot) + cmd_len;
	return 0;
}

static int
aie2_cmdlist_fill_npu_dpu(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size)
{
	struct cmd_chain_slot_npu *npu_slot = slot;
	struct amdxdna_cmd_start_npu *sn;
	u32 cmd_len;
	u32 arg_sz;

	memset(npu_slot, 0, sizeof(*npu_slot));
	sn = amdxdna_cmd_get_payload(cmd_bo, &cmd_len);
	arg_sz = cmd_len - sizeof(*sn);
	if (cmd_len < sizeof(*sn) || arg_sz > MAX_NPU_ARGS_SIZE)
		return -EINVAL;

	if (*size < sizeof(*npu_slot) + arg_sz)
		return -EINVAL;

	npu_slot->cu_idx = amdxdna_cmd_get_cu_idx(cmd_bo);
	if (npu_slot->cu_idx == INVALID_CU_IDX)
		return -EINVAL;

	npu_slot->type = EXEC_NPU_TYPE_PARTIAL_ELF;
	npu_slot->inst_buf_addr = sn->buffer;
	npu_slot->inst_size = sn->buffer_size;
	npu_slot->inst_prop_cnt = sn->prop_count;
	npu_slot->arg_cnt = arg_sz / sizeof(u32);
	memcpy(npu_slot->args, sn->prop_args, arg_sz);

	*size = sizeof(*npu_slot) + arg_sz;
	return 0;
}

static int
aie2_cmdlist_fill_npu_preempt(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size)
{
	struct cmd_chain_slot_npu *npu_slot = slot;
	struct amdxdna_cmd_preempt_data *pd;
	u32 cmd_len;
	u32 arg_sz;

	memset(npu_slot, 0, sizeof(*npu_slot));
	pd = amdxdna_cmd_get_payload(cmd_bo, &cmd_len);
	arg_sz = cmd_len - sizeof(*pd);
	if (cmd_len < sizeof(*pd) || arg_sz > MAX_NPU_ARGS_SIZE)
		return -EINVAL;

	if (*size < sizeof(*npu_slot) + arg_sz)
		return -EINVAL;

	npu_slot->cu_idx = amdxdna_cmd_get_cu_idx(cmd_bo);
	if (npu_slot->cu_idx == INVALID_CU_IDX)
		return -EINVAL;

	npu_slot->type = EXEC_NPU_TYPE_PREEMPT;
	npu_slot->inst_buf_addr = pd->inst_buf;
	npu_slot->save_buf_addr = pd->save_buf;
	npu_slot->restore_buf_addr = pd->restore_buf;
	npu_slot->inst_size = pd->inst_size;
	npu_slot->save_size = pd->save_size;
	npu_slot->restore_size = pd->restore_size;
	npu_slot->inst_prop_cnt = pd->inst_prop_cnt;
	npu_slot->arg_cnt = arg_sz / sizeof(u32);
	memcpy(npu_slot->args, pd->prop_args, arg_sz);

	*size = sizeof(*npu_slot) + arg_sz;
	return 0;
}

static int
aie2_cmdlist_fill_npu_elf(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size)
{
	struct cmd_chain_slot_npu *npu_slot = slot;
	struct amdxdna_cmd_preempt_data *pd;
	u32 cmd_len;
	u32 arg_sz;

	memset(npu_slot, 0, sizeof(*npu_slot));
	pd = amdxdna_cmd_get_payload(cmd_bo, &cmd_len);
	arg_sz = cmd_len - sizeof(*pd);
	if (cmd_len < sizeof(*pd) || arg_sz > MAX_NPU_ARGS_SIZE)
		return -EINVAL;

	if (*size < sizeof(*npu_slot) + arg_sz)
		return -EINVAL;

	npu_slot->type = EXEC_NPU_TYPE_ELF;
	npu_slot->inst_buf_addr = pd->inst_buf;
	npu_slot->save_buf_addr = pd->save_buf;
	npu_slot->restore_buf_addr = pd->restore_buf;
	npu_slot->inst_size = pd->inst_size;
	npu_slot->save_size = pd->save_size;
	npu_slot->restore_size = pd->restore_size;
	npu_slot->inst_prop_cnt = pd->inst_prop_cnt;
	npu_slot->arg_cnt = 1;
	npu_slot->args[0] = AIE2_EXEC_BUFFER_KERNEL_OP_TXN;

	*size = struct_size(npu_slot, args, npu_slot->arg_cnt);
	return 0;
}

static u32 aie2_get_npu_chain_msg_op(u32 cmd_op)
{
	return MSG_OP_CHAIN_EXEC_NPU;
}

static struct aie2_exec_msg_ops npu_exec_message_ops = {
	.init_cu_req = aie2_init_exec_cu_req,
	.init_dpu_req = aie2_init_exec_dpu_req,
	.init_chain_req = aie2_init_npu_chain_req,
	.fill_cf_slot = aie2_cmdlist_fill_npu_cf,
	.fill_dpu_slot = aie2_cmdlist_fill_npu_dpu,
	.fill_preempt_slot = aie2_cmdlist_fill_npu_preempt,
	.fill_elf_slot = aie2_cmdlist_fill_npu_elf,
	.get_chain_msg_op = aie2_get_npu_chain_msg_op,
};

static int aie2_init_exec_req(void *req, struct amdxdna_gem_obj *cmd_abo,
			      size_t *size, u32 *msg_op)
{
	struct amdxdna_dev *xdna = cmd_abo->client->xdna;
	int ret;
	u32 op;


	op = amdxdna_cmd_get_op(cmd_abo);
	switch (op) {
	case ERT_START_CU:
		ret = EXEC_MSG_OPS(xdna)->init_cu_req(cmd_abo, req, size, msg_op);
		if (ret) {
			XDNA_DBG(xdna, "Init CU req failed ret %d", ret);
			return ret;
		}
		break;
	case ERT_START_NPU:
		ret = EXEC_MSG_OPS(xdna)->init_dpu_req(cmd_abo, req, size, msg_op);
		if (ret) {
			XDNA_DBG(xdna, "Init DPU req failed ret %d", ret);
			return ret;
		}

		break;
	default:
		XDNA_ERR(xdna, "Unsupported op %d", op);
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int
aie2_cmdlist_fill_slot(void *slot, struct amdxdna_gem_obj *cmd_abo,
		       size_t *size, u32 *cmd_op)
{
	struct amdxdna_dev *xdna = cmd_abo->client->xdna;
	int ret;
	u32 op;

	op = amdxdna_cmd_get_op(cmd_abo);
	if (*cmd_op == ERT_INVALID_CMD)
		*cmd_op = op;
	else if (op != *cmd_op)
		return -EINVAL;

	switch (op) {
	case ERT_START_CU:
		ret = EXEC_MSG_OPS(xdna)->fill_cf_slot(cmd_abo, slot, size);
		break;
	case ERT_START_NPU:
		ret = EXEC_MSG_OPS(xdna)->fill_dpu_slot(cmd_abo, slot, size);
		break;
	case ERT_START_NPU_PREEMPT:
		if (!AIE2_FEATURE_ON(xdna->dev_handle, AIE2_PREEMPT))
			return -EOPNOTSUPP;
		ret = EXEC_MSG_OPS(xdna)->fill_preempt_slot(cmd_abo, slot, size);
		break;
	case ERT_START_NPU_PREEMPT_ELF:
		if (!AIE2_FEATURE_ON(xdna->dev_handle, AIE2_PREEMPT))
			return -EOPNOTSUPP;
		ret = EXEC_MSG_OPS(xdna)->fill_elf_slot(cmd_abo, slot, size);
		break;
	default:
		XDNA_INFO(xdna, "Unsupported op %d", op);
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

void aie2_msg_init(struct amdxdna_dev_hdl *ndev)
{
	if (AIE2_FEATURE_ON(ndev, AIE2_NPU_COMMAND))
		ndev->exec_msg_ops = &npu_exec_message_ops;
	else
		ndev->exec_msg_ops = &legacy_exec_message_ops;
}

static inline struct amdxdna_gem_obj *
aie2_cmdlist_get_cmd_buf(struct amdxdna_sched_job *job)
{
	int idx = get_job_idx(job->seq);

	return job->hwctx->priv->cmd_buf[idx];
}

int aie2_execbuf(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job,
		 int (*notify_cb)(void *, void __iomem *, size_t))
{
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct amdxdna_gem_obj *cmd_abo = job->cmd_bo;
	struct xdna_mailbox_msg msg;
	union exec_req req;
	int ret;

	if (!chann)
		return -ENODEV;

	ret = aie2_init_exec_req(&req, cmd_abo, &msg.send_size, &msg.opcode);
	if (ret)
		return ret;

	msg.handle = job;
	msg.notify_cb = notify_cb;
	msg.send_data = (u8 *)&req;
	print_hex_dump_debug("cmd: ", DUMP_PREFIX_OFFSET, 16, 4, &req,
			     0x40, false);

	ret = xdna_mailbox_send_msg(chann, &msg, TX_TIMEOUT);
	if (ret) {
		XDNA_ERR(xdna, "Send message failed");
		return ret;
	}

	return 0;
}

int aie2_cmdlist_multi_execbuf(struct amdxdna_hwctx *hwctx,
			       struct amdxdna_sched_job *job,
			       int (*notify_cb)(void *, void __iomem *, size_t))
{
	struct amdxdna_gem_obj *cmdbuf_abo = aie2_cmdlist_get_cmd_buf(job);
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_client *client = hwctx->client;
	struct amdxdna_gem_obj *cmd_abo = job->cmd_bo;
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_cmd_chain *payload;
	struct xdna_mailbox_msg msg;
	union exec_chain_req req;
	u32 payload_len;
	u32 offset = 0;
	size_t size;
	int ret;
	u32 op;
	u32 i;

	op = amdxdna_cmd_get_op(cmd_abo);
	payload = amdxdna_cmd_get_payload(cmd_abo, &payload_len);
	if (op != ERT_CMD_CHAIN || !payload ||
	    payload_len < struct_size(payload, data, payload->command_count))
		return -EINVAL;

	op = ERT_INVALID_CMD;
	for (i = 0; i < payload->command_count; i++) {
		u32 boh = (u32)(payload->data[i]);
		struct amdxdna_gem_obj *abo;

		abo = amdxdna_gem_get_obj(client, boh, AMDXDNA_BO_CMD);
		if (!abo) {
			XDNA_ERR(xdna, "Failed to find cmd BO %d", boh);
			return -ENOENT;
		}

		size = cmdbuf_abo->mem.size - offset;
		ret = aie2_cmdlist_fill_slot(cmdbuf_abo->mem.kva + offset,
					     abo, &size, &op);
		amdxdna_gem_put_obj(abo);
		if (ret)
			return ret;

		offset += size;
	}
	msg.opcode = EXEC_MSG_OPS(xdna)->get_chain_msg_op(op);
	if (msg.opcode == MSG_OP_MAX_OPCODE)
		return -EOPNOTSUPP;

	/* The offset is the accumulated total size of the cmd buffer */
	EXEC_MSG_OPS(xdna)->init_chain_req(&req, cmdbuf_abo->mem.dev_addr,
					   offset, payload->command_count);
	drm_clflush_virt_range(cmdbuf_abo->mem.kva, offset);

	msg.handle = job;
	msg.notify_cb = notify_cb;
	msg.send_data = (u8 *)&req;
	msg.send_size = sizeof(req);
	ret = xdna_mailbox_send_msg(chann, &msg, TX_TIMEOUT);
	if (ret) {
		XDNA_ERR(xdna, "Send message failed");
		return ret;
	}

	return 0;
}

int aie2_cmdlist_single_execbuf(struct amdxdna_hwctx *hwctx,
				struct amdxdna_sched_job *job,
				int (*notify_cb)(void *, void __iomem *, size_t))
{
	struct amdxdna_gem_obj *cmdbuf_abo = aie2_cmdlist_get_cmd_buf(job);
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct amdxdna_gem_obj *cmd_abo = job->cmd_bo;
	struct xdna_mailbox_msg msg;
	union exec_chain_req req;
	u32 op = ERT_INVALID_CMD;
	size_t size;
	int ret;

	size = cmdbuf_abo->mem.size;
	ret = aie2_cmdlist_fill_slot(cmdbuf_abo->mem.kva, cmd_abo, &size, &op);
	if (ret)
		return ret;

	msg.opcode = EXEC_MSG_OPS(xdna)->get_chain_msg_op(op);
	if (msg.opcode == MSG_OP_MAX_OPCODE)
		return -EOPNOTSUPP;

	EXEC_MSG_OPS(xdna)->init_chain_req(&req, cmdbuf_abo->mem.dev_addr,
					   size, 1);
	drm_clflush_virt_range(cmdbuf_abo->mem.kva, size);

	msg.handle = job;
	msg.notify_cb = notify_cb;
	msg.send_data = (u8 *)&req;
	msg.send_size = sizeof(req);
	ret = xdna_mailbox_send_msg(chann, &msg, TX_TIMEOUT);
	if (ret) {
		XDNA_ERR(hwctx->client->xdna, "Send message failed");
		return ret;
	}

	return 0;
}

int aie2_sync_bo(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job,
		 int (*notify_cb)(void *, void __iomem *, size_t))
{
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_gem_obj *abo = to_xdna_obj(job->bos[0]);
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct xdna_mailbox_msg msg;
	struct sync_bo_req req;
	int ret = 0;

	req.src_addr = 0;
	req.dst_addr = amdxdna_dev_bo_offset(abo);
	req.size = abo->mem.size;

	/* Device to Host */
	req.type = FIELD_PREP(AIE2_MSG_SYNC_BO_SRC_TYPE, SYNC_BO_DEV_MEM) |
		FIELD_PREP(AIE2_MSG_SYNC_BO_DST_TYPE, SYNC_BO_HOST_MEM);

	XDNA_DBG(xdna, "sync %d bytes src(0x%llx) to dst(0x%llx) completed",
		 req.size, req.src_addr, req.dst_addr);

	msg.handle = job;
	msg.notify_cb = notify_cb;
	msg.send_data = (u8 *)&req;
	msg.send_size = sizeof(req);
	msg.opcode = MSG_OP_SYNC_BO;

	ret = xdna_mailbox_send_msg(chann, &msg, TX_TIMEOUT);
	if (ret) {
		XDNA_ERR(xdna, "Send message failed");
		return ret;
	}

	return 0;
}

int aie2_config_debug_bo(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job,
			 int (*notify_cb)(void *, void __iomem *, size_t))
{
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_gem_obj *abo = to_xdna_obj(job->bos[0]);
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct config_debug_bo_req req;
	struct xdna_mailbox_msg msg;

	if (job->drv_cmd->opcode == ATTACH_DEBUG_BO)
		req.config = DEBUG_BO_REGISTER;
	else
		req.config = DEBUG_BO_UNREGISTER;

	req.offset = amdxdna_dev_bo_offset(abo);
	req.size = abo->mem.size;

	XDNA_DBG(xdna, "offset 0x%llx size 0x%llx config %d",
		 req.offset, req.size, req.config);

	msg.handle = job;
	msg.notify_cb = notify_cb;
	msg.send_data = (u8 *)&req;
	msg.send_size = sizeof(req);
	msg.opcode = MSG_OP_CONFIG_DEBUG_BO;

	return xdna_mailbox_send_msg(chann, &msg, TX_TIMEOUT);
}
