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

int aie2_query_status(struct amdxdna_dev_hdl *ndev, char __user *buf,
		      u32 size, u32 *cols_filled)
{
	DECLARE_AIE2_MSG(aie_column_info, MSG_OP_QUERY_COL_STATUS);
	struct amdxdna_dev *xdna = ndev->xdna;
	struct amdxdna_client *client;
	struct amdxdna_hwctx *hwctx;
	unsigned long hwctx_id;
	dma_addr_t dma_addr;
	u32 aie_bitmap = 0;
	u8 *buff_addr;
	int ret, idx;

	buff_addr = dma_alloc_noncoherent(xdna->ddev.dev, size, &dma_addr,
					  DMA_FROM_DEVICE, GFP_KERNEL);
	if (!buff_addr)
		return -ENOMEM;

	/* Go through each hardware context and mark the AIE columns that are active */
	list_for_each_entry(client, &xdna->client_list, node) {
		idx = srcu_read_lock(&client->hwctx_srcu);
		amdxdna_for_each_hwctx(client, hwctx_id, hwctx)
			aie_bitmap |= amdxdna_hwctx_col_map(hwctx);
		srcu_read_unlock(&client->hwctx_srcu, idx);
	}

	*cols_filled = 0;
	req.dump_buff_addr = dma_addr;
	req.dump_buff_size = size;
	req.num_cols = hweight32(aie_bitmap);
	req.aie_bitmap = aie_bitmap;

	drm_clflush_virt_range(buff_addr, size); /* device can access */
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret) {
		XDNA_ERR(xdna, "Error during NPU query, status %d", ret);
		goto fail;
	}

	if (resp.status != AIE2_STATUS_SUCCESS) {
		XDNA_ERR(xdna, "Query NPU status failed, status 0x%x", resp.status);
		ret = -EINVAL;
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
	dma_free_noncoherent(xdna->ddev.dev, size, buff_addr, dma_addr, DMA_FROM_DEVICE);
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

int aie2_execbuf(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job,
		 int (*notify_cb)(void *, void __iomem *, size_t))
{
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct amdxdna_gem_obj *cmd_abo = job->cmd_bo;
	union {
		struct execute_buffer_req ebuf;
		struct exec_dpu_req dpu;
	} req;
	struct xdna_mailbox_msg msg;
	u32 payload_len;
	void *payload;
	int cu_idx;
	int ret;
	u32 op;

	if (!chann)
		return -ENODEV;

	payload = amdxdna_cmd_get_payload(cmd_abo, &payload_len);
	if (!payload) {
		XDNA_ERR(xdna, "Invalid command, cannot get payload");
		return -EINVAL;
	}

	cu_idx = amdxdna_cmd_get_cu_idx(cmd_abo);
	if (cu_idx < 0) {
		XDNA_DBG(xdna, "Invalid cu idx");
		return -EINVAL;
	}

	op = amdxdna_cmd_get_op(cmd_abo);
	switch (op) {
	case ERT_START_CU:
		if (unlikely(payload_len > sizeof(req.ebuf.payload)))
			XDNA_DBG(xdna, "Invalid ebuf payload len: %d", payload_len);
		req.ebuf.cu_idx = cu_idx;
		memcpy(req.ebuf.payload, payload, sizeof(req.ebuf.payload));
		msg.send_size = sizeof(req.ebuf);
		msg.opcode = MSG_OP_EXECUTE_BUFFER_CF;
		break;
	case ERT_START_NPU: {
		struct amdxdna_cmd_start_npu *sn = payload;

		if (unlikely(payload_len - sizeof(*sn) > sizeof(req.dpu.payload)))
			XDNA_DBG(xdna, "Invalid dpu payload len: %d", payload_len);
		req.dpu.inst_buf_addr = sn->buffer;
		req.dpu.inst_size = sn->buffer_size;
		req.dpu.inst_prop_cnt = sn->prop_count;
		req.dpu.cu_idx = cu_idx;
		memcpy(req.dpu.payload, sn->prop_args, sizeof(req.dpu.payload));
		msg.send_size = sizeof(req.dpu);
		msg.opcode = MSG_OP_EXEC_DPU;
		break;
	}
	default:
		XDNA_DBG(xdna, "Invalid ERT cmd op code: %d", op);
		return -EINVAL;
	}
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

static int
aie2_cmdlist_fill_one_slot_cf(void *cmd_buf, u32 offset,
			      struct amdxdna_gem_obj *abo, u32 *size)
{
	struct cmd_chain_slot_execbuf_cf *buf = cmd_buf + offset;
	int cu_idx = amdxdna_cmd_get_cu_idx(abo);
	u32 payload_len;
	void *payload;

	if (cu_idx < 0)
		return -EINVAL;

	payload = amdxdna_cmd_get_payload(abo, &payload_len);
	if (!payload)
		return -EINVAL;

	if (!slot_cf_has_space(offset, payload_len))
		return -ENOSPC;

	buf->cu_idx = cu_idx;
	buf->arg_cnt = payload_len / sizeof(u32);
	memcpy(buf->args, payload, payload_len);
	/* Accurate buf size to hint firmware to do necessary copy */
	*size = sizeof(*buf) + payload_len;
	return 0;
}

static int
aie2_cmdlist_fill_one_slot_dpu(void *cmd_buf, u32 offset,
			       struct amdxdna_gem_obj *abo, u32 *size)
{
	struct cmd_chain_slot_dpu *buf = cmd_buf + offset;
	int cu_idx = amdxdna_cmd_get_cu_idx(abo);
	struct amdxdna_cmd_start_npu *sn;
	u32 payload_len;
	void *payload;
	u32 arg_sz;

	if (cu_idx < 0)
		return -EINVAL;

	payload = amdxdna_cmd_get_payload(abo, &payload_len);
	if (!payload)
		return -EINVAL;
	sn = payload;
	arg_sz = payload_len - sizeof(*sn);
	if (payload_len < sizeof(*sn) || arg_sz > MAX_DPU_ARGS_SIZE)
		return -EINVAL;

	if (!slot_dpu_has_space(offset, arg_sz))
		return -ENOSPC;

	buf->inst_buf_addr = sn->buffer;
	buf->inst_size = sn->buffer_size;
	buf->inst_prop_cnt = sn->prop_count;
	buf->cu_idx = cu_idx;
	buf->arg_cnt = arg_sz / sizeof(u32);
	memcpy(buf->args, sn->prop_args, arg_sz);

	/* Accurate buf size to hint firmware to do necessary copy */
	*size += sizeof(*buf) + arg_sz;
	return 0;
}

static int
aie2_cmdlist_fill_one_slot(u32 op, struct amdxdna_gem_obj *cmdbuf_abo, u32 offset,
			   struct amdxdna_gem_obj *abo, u32 *size)
{
	u32 this_op = amdxdna_cmd_get_op(abo);
	void *cmd_buf = cmdbuf_abo->mem.kva;
	int ret;

	if (this_op != op) {
		ret = -EINVAL;
		goto done;
	}

	switch (op) {
	case ERT_START_CU:
		ret = aie2_cmdlist_fill_one_slot_cf(cmd_buf, offset, abo, size);
		break;
	case ERT_START_NPU:
		ret = aie2_cmdlist_fill_one_slot_dpu(cmd_buf, offset, abo, size);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

done:
	if (ret) {
		XDNA_ERR(abo->client->xdna, "Can't fill slot for cmd op %d ret %d",
			 op, ret);
	}
	return ret;
}

static inline struct amdxdna_gem_obj *
aie2_cmdlist_get_cmd_buf(struct amdxdna_sched_job *job)
{
	int idx = get_job_idx(job->seq);

	return job->hwctx->priv->cmd_buf[idx];
}

static void
aie2_cmdlist_prepare_request(struct cmd_chain_req *req,
			     struct amdxdna_gem_obj *cmdbuf_abo, u32 size, u32 cnt)
{
	req->buf_addr = cmdbuf_abo->mem.dev_addr;
	req->buf_size = size;
	req->count = cnt;
	drm_clflush_virt_range(cmdbuf_abo->mem.kva, size);
	XDNA_DBG(cmdbuf_abo->client->xdna, "Command buf addr 0x%llx size 0x%x count %d",
		 req->buf_addr, size, cnt);
}

static inline u32
aie2_cmd_op_to_msg_op(u32 op)
{
	switch (op) {
	case ERT_START_CU:
		return MSG_OP_CHAIN_EXEC_BUFFER_CF;
	case ERT_START_NPU:
		return MSG_OP_CHAIN_EXEC_DPU;
	default:
		return MSG_OP_MAX_OPCODE;
	}
}

int aie2_cmdlist_multi_execbuf(struct amdxdna_hwctx *hwctx,
			       struct amdxdna_sched_job *job,
			       int (*notify_cb)(void *, void __iomem *, size_t))
{
	struct amdxdna_gem_obj *cmdbuf_abo = aie2_cmdlist_get_cmd_buf(job);
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_client *client = hwctx->client;
	struct amdxdna_gem_obj *cmd_abo = job->cmd_bo;
	struct amdxdna_cmd_chain *payload;
	struct xdna_mailbox_msg msg;
	struct cmd_chain_req req;
	u32 payload_len;
	u32 offset = 0;
	u32 size;
	int ret;
	u32 op;
	u32 i;

	op = amdxdna_cmd_get_op(cmd_abo);
	payload = amdxdna_cmd_get_payload(cmd_abo, &payload_len);
	if (op != ERT_CMD_CHAIN || !payload ||
	    payload_len < struct_size(payload, data, payload->command_count))
		return -EINVAL;

	for (i = 0; i < payload->command_count; i++) {
		u32 boh = (u32)(payload->data[i]);
		struct amdxdna_gem_obj *abo;

		abo = amdxdna_gem_get_obj(client, boh, AMDXDNA_BO_CMD);
		if (!abo) {
			XDNA_ERR(client->xdna, "Failed to find cmd BO %d", boh);
			return -ENOENT;
		}

		/* All sub-cmd should have same op, use the first one. */
		if (i == 0)
			op = amdxdna_cmd_get_op(abo);

		ret = aie2_cmdlist_fill_one_slot(op, cmdbuf_abo, offset, abo, &size);
		amdxdna_gem_put_obj(abo);
		if (ret)
			return -EINVAL;

		offset += size;
	}

	/* The offset is the accumulated total size of the cmd buffer */
	aie2_cmdlist_prepare_request(&req, cmdbuf_abo, offset, payload->command_count);

	msg.opcode = aie2_cmd_op_to_msg_op(op);
	if (msg.opcode == MSG_OP_MAX_OPCODE)
		return -EOPNOTSUPP;
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

int aie2_cmdlist_single_execbuf(struct amdxdna_hwctx *hwctx,
				struct amdxdna_sched_job *job,
				int (*notify_cb)(void *, void __iomem *, size_t))
{
	struct amdxdna_gem_obj *cmdbuf_abo = aie2_cmdlist_get_cmd_buf(job);
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_gem_obj *cmd_abo = job->cmd_bo;
	struct xdna_mailbox_msg msg;
	struct cmd_chain_req req;
	u32 size;
	int ret;
	u32 op;

	op = amdxdna_cmd_get_op(cmd_abo);
	ret = aie2_cmdlist_fill_one_slot(op, cmdbuf_abo, 0, cmd_abo, &size);
	if (ret)
		return ret;

	aie2_cmdlist_prepare_request(&req, cmdbuf_abo, size, 1);

	msg.opcode = aie2_cmd_op_to_msg_op(op);
	if (msg.opcode == MSG_OP_MAX_OPCODE)
		return -EOPNOTSUPP;
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
	req.dst_addr = abo->mem.dev_addr - hwctx->client->dev_heap->mem.dev_addr;
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
