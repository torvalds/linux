// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation.
 */

#include "xe_pxp_submit.h"

#include <linux/delay.h>
#include <uapi/drm/xe_drm.h>

#include "xe_device_types.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_exec_queue.h"
#include "xe_gsc_submit.h"
#include "xe_gt.h"
#include "xe_lrc.h"
#include "xe_map.h"
#include "xe_pxp.h"
#include "xe_pxp_types.h"
#include "xe_sched_job.h"
#include "xe_vm.h"
#include "abi/gsc_command_header_abi.h"
#include "abi/gsc_pxp_commands_abi.h"
#include "instructions/xe_gsc_commands.h"
#include "instructions/xe_mfx_commands.h"
#include "instructions/xe_mi_commands.h"

/*
 * The VCS is used for kernel-owned GGTT submissions to issue key termination.
 * Terminations are serialized, so we only need a single queue and a single
 * batch.
 */
static int allocate_vcs_execution_resources(struct xe_pxp *pxp)
{
	struct xe_gt *gt = pxp->gt;
	struct xe_device *xe = pxp->xe;
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_hw_engine *hwe;
	struct xe_exec_queue *q;
	struct xe_bo *bo;
	int err;

	hwe = xe_gt_hw_engine(gt, XE_ENGINE_CLASS_VIDEO_DECODE, 0, true);
	if (!hwe)
		return -ENODEV;

	q = xe_exec_queue_create(xe, NULL, BIT(hwe->logical_instance), 1, hwe,
				 EXEC_QUEUE_FLAG_KERNEL | EXEC_QUEUE_FLAG_PERMANENT, 0);
	if (IS_ERR(q))
		return PTR_ERR(q);

	/*
	 * Each termination is 16 DWORDS, so 4K is enough to contain a
	 * termination for each sessions.
	 */
	bo = xe_bo_create_pin_map_novm(xe, tile, SZ_4K, ttm_bo_type_kernel,
				       XE_BO_FLAG_SYSTEM | XE_BO_FLAG_PINNED | XE_BO_FLAG_GGTT,
				       false);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		goto out_queue;
	}

	pxp->vcs_exec.q = q;
	pxp->vcs_exec.bo = bo;

	return 0;

out_queue:
	xe_exec_queue_put(q);
	return err;
}

static void destroy_vcs_execution_resources(struct xe_pxp *pxp)
{
	if (pxp->vcs_exec.bo)
		xe_bo_unpin_map_no_vm(pxp->vcs_exec.bo);

	if (pxp->vcs_exec.q)
		xe_exec_queue_put(pxp->vcs_exec.q);
}

#define PXP_BB_SIZE		XE_PAGE_SIZE
static int allocate_gsc_client_resources(struct xe_gt *gt,
					 struct xe_pxp_gsc_client_resources *gsc_res,
					 size_t inout_size)
{
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_validation_ctx ctx;
	struct xe_hw_engine *hwe;
	struct drm_exec exec;
	struct xe_vm *vm;
	struct xe_bo *bo;
	struct xe_exec_queue *q;
	struct dma_fence *fence;
	long timeout;
	int err = 0;

	hwe = xe_gt_hw_engine(gt, XE_ENGINE_CLASS_OTHER, 0, true);

	/* we shouldn't reach here if the GSC engine is not available */
	xe_assert(xe, hwe);

	/* PXP instructions must be issued from PPGTT */
	vm = xe_vm_create(xe, XE_VM_FLAG_GSC, NULL);
	if (IS_ERR(vm))
		return PTR_ERR(vm);

	/* We allocate a single object for the batch and the in/out memory */

	xe_validation_guard(&ctx, &xe->val, &exec, (struct xe_val_flags){}, err) {
		err = xe_vm_drm_exec_lock(vm, &exec);
		drm_exec_retry_on_contention(&exec);
		if (err)
			break;

		bo = xe_bo_create_pin_map(xe, tile, vm, PXP_BB_SIZE + inout_size * 2,
					  ttm_bo_type_kernel,
					  XE_BO_FLAG_SYSTEM | XE_BO_FLAG_PINNED |
					  XE_BO_FLAG_NEEDS_UC, &exec);
		drm_exec_retry_on_contention(&exec);
		if (IS_ERR(bo)) {
			err = PTR_ERR(bo);
			xe_validation_retry_on_oom(&ctx, &err);
			break;
		}
	}
	if (err)
		goto vm_out;

	fence = xe_vm_bind_kernel_bo(vm, bo, NULL, 0, XE_CACHE_WB);
	if (IS_ERR(fence)) {
		err = PTR_ERR(fence);
		goto bo_out;
	}

	timeout = dma_fence_wait_timeout(fence, false, HZ);
	dma_fence_put(fence);
	if (timeout <= 0) {
		err = timeout ?: -ETIME;
		goto bo_out;
	}

	q = xe_exec_queue_create(xe, vm, BIT(hwe->logical_instance), 1, hwe,
				 EXEC_QUEUE_FLAG_KERNEL |
				 EXEC_QUEUE_FLAG_PERMANENT, 0);
	if (IS_ERR(q)) {
		err = PTR_ERR(q);
		goto bo_out;
	}

	gsc_res->vm = vm;
	gsc_res->bo = bo;
	gsc_res->inout_size = inout_size;
	gsc_res->batch = IOSYS_MAP_INIT_OFFSET(&bo->vmap, 0);
	gsc_res->msg_in = IOSYS_MAP_INIT_OFFSET(&bo->vmap, PXP_BB_SIZE);
	gsc_res->msg_out = IOSYS_MAP_INIT_OFFSET(&bo->vmap, PXP_BB_SIZE + inout_size);
	gsc_res->q = q;

	/* initialize host-session-handle (for all Xe-to-gsc-firmware PXP cmds) */
	gsc_res->host_session_handle = xe_gsc_create_host_session_id();

	return 0;

bo_out:
	xe_bo_unpin_map_no_vm(bo);
vm_out:
	xe_vm_close_and_put(vm);

	return err;
}

static void destroy_gsc_client_resources(struct xe_pxp_gsc_client_resources *gsc_res)
{
	if (!gsc_res->q)
		return;

	xe_exec_queue_put(gsc_res->q);
	xe_bo_unpin_map_no_vm(gsc_res->bo);
	xe_vm_close_and_put(gsc_res->vm);
}

/**
 * xe_pxp_allocate_execution_resources - Allocate PXP submission objects
 * @pxp: the xe_pxp structure
 *
 * Allocates exec_queues objects for VCS and GSCCS submission. The GSCCS
 * submissions are done via PPGTT, so this function allocates a VM for it and
 * maps the object into it.
 *
 * Returns 0 if the allocation and mapping is successful, an errno value
 * otherwise.
 */
int xe_pxp_allocate_execution_resources(struct xe_pxp *pxp)
{
	int err;

	err = allocate_vcs_execution_resources(pxp);
	if (err)
		return err;

	/*
	 * PXP commands can require a lot of BO space (see PXP_MAX_PACKET_SIZE),
	 * but we currently only support a subset of commands that are small
	 * (< 20 dwords), so a single page is enough for now.
	 */
	err = allocate_gsc_client_resources(pxp->gt, &pxp->gsc_res, XE_PAGE_SIZE);
	if (err)
		goto destroy_vcs_context;

	return 0;

destroy_vcs_context:
	destroy_vcs_execution_resources(pxp);
	return err;
}

void xe_pxp_destroy_execution_resources(struct xe_pxp *pxp)
{
	destroy_gsc_client_resources(&pxp->gsc_res);
	destroy_vcs_execution_resources(pxp);
}

#define emit_cmd(xe_, map_, offset_, val_) \
	xe_map_wr(xe_, map_, (offset_) * sizeof(u32), u32, val_)

/* stall until prior PXP and MFX/HCP/HUC objects are completed */
#define MFX_WAIT_PXP (MFX_WAIT | \
		      MFX_WAIT_DW0_PXP_SYNC_CONTROL_FLAG | \
		      MFX_WAIT_DW0_MFX_SYNC_CONTROL_FLAG)
static u32 pxp_emit_wait(struct xe_device *xe, struct iosys_map *batch, u32 offset)
{
	/* wait for cmds to go through */
	emit_cmd(xe, batch, offset++, MFX_WAIT_PXP);
	emit_cmd(xe, batch, offset++, 0);

	return offset;
}

static u32 pxp_emit_session_selection(struct xe_device *xe, struct iosys_map *batch,
				      u32 offset, u32 idx)
{
	offset = pxp_emit_wait(xe, batch, offset);

	/* pxp off */
	emit_cmd(xe, batch, offset++, MI_FLUSH_DW | MI_FLUSH_IMM_DW);
	emit_cmd(xe, batch, offset++, 0);
	emit_cmd(xe, batch, offset++, 0);
	emit_cmd(xe, batch, offset++, 0);

	/* select session */
	emit_cmd(xe, batch, offset++, MI_SET_APPID | MI_SET_APPID_SESSION_ID(idx));
	emit_cmd(xe, batch, offset++, 0);

	offset = pxp_emit_wait(xe, batch, offset);

	/* pxp on */
	emit_cmd(xe, batch, offset++, MI_FLUSH_DW |
				      MI_FLUSH_DW_PROTECTED_MEM_EN |
				      MI_FLUSH_DW_OP_STOREDW | MI_FLUSH_DW_STORE_INDEX |
				      MI_FLUSH_IMM_DW);
	emit_cmd(xe, batch, offset++, LRC_PPHWSP_PXP_INVAL_SCRATCH_ADDR |
				      MI_FLUSH_DW_USE_GTT);
	emit_cmd(xe, batch, offset++, 0);
	emit_cmd(xe, batch, offset++, 0);

	offset = pxp_emit_wait(xe, batch, offset);

	return offset;
}

static u32 pxp_emit_inline_termination(struct xe_device *xe,
				       struct iosys_map *batch, u32 offset)
{
	/* session inline termination */
	emit_cmd(xe, batch, offset++, CRYPTO_KEY_EXCHANGE);
	emit_cmd(xe, batch, offset++, 0);

	return offset;
}

static u32 pxp_emit_session_termination(struct xe_device *xe, struct iosys_map *batch,
					u32 offset, u32 idx)
{
	offset = pxp_emit_session_selection(xe, batch, offset, idx);
	offset = pxp_emit_inline_termination(xe, batch, offset);

	return offset;
}

/**
 * xe_pxp_submit_session_termination - submits a PXP inline termination
 * @pxp: the xe_pxp structure
 * @id: the session to terminate
 *
 * Emit an inline termination via the VCS engine to terminate a session.
 *
 * Returns 0 if the submission is successful, an errno value otherwise.
 */
int xe_pxp_submit_session_termination(struct xe_pxp *pxp, u32 id)
{
	struct xe_sched_job *job;
	struct dma_fence *fence;
	long timeout;
	u32 offset = 0;
	u64 addr = xe_bo_ggtt_addr(pxp->vcs_exec.bo);

	offset = pxp_emit_session_termination(pxp->xe, &pxp->vcs_exec.bo->vmap, offset, id);
	offset = pxp_emit_wait(pxp->xe, &pxp->vcs_exec.bo->vmap, offset);
	emit_cmd(pxp->xe, &pxp->vcs_exec.bo->vmap, offset, MI_BATCH_BUFFER_END);

	job = xe_sched_job_create(pxp->vcs_exec.q, &addr);
	if (IS_ERR(job))
		return PTR_ERR(job);

	xe_sched_job_arm(job);
	fence = dma_fence_get(&job->drm.s_fence->finished);
	xe_sched_job_push(job);

	timeout = dma_fence_wait_timeout(fence, false, HZ);

	dma_fence_put(fence);

	if (!timeout)
		return -ETIMEDOUT;
	else if (timeout < 0)
		return timeout;

	return 0;
}

static bool
is_fw_err_platform_config(u32 type)
{
	switch (type) {
	case PXP_STATUS_ERROR_API_VERSION:
	case PXP_STATUS_PLATFCONFIG_KF1_NOVERIF:
	case PXP_STATUS_PLATFCONFIG_KF1_BAD:
	case PXP_STATUS_PLATFCONFIG_FIXED_KF1_NOT_SUPPORTED:
		return true;
	default:
		break;
	}
	return false;
}

static const char *
fw_err_to_string(u32 type)
{
	switch (type) {
	case PXP_STATUS_ERROR_API_VERSION:
		return "ERR_API_VERSION";
	case PXP_STATUS_NOT_READY:
		return "ERR_NOT_READY";
	case PXP_STATUS_PLATFCONFIG_KF1_NOVERIF:
	case PXP_STATUS_PLATFCONFIG_KF1_BAD:
	case PXP_STATUS_PLATFCONFIG_FIXED_KF1_NOT_SUPPORTED:
		return "ERR_PLATFORM_CONFIG";
	default:
		break;
	}
	return NULL;
}

static int pxp_pkt_submit(struct xe_exec_queue *q, u64 batch_addr)
{
	struct xe_gt *gt = q->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_sched_job *job;
	struct dma_fence *fence;
	long timeout;

	xe_assert(xe, q->hwe->engine_id == XE_HW_ENGINE_GSCCS0);

	job = xe_sched_job_create(q, &batch_addr);
	if (IS_ERR(job))
		return PTR_ERR(job);

	xe_sched_job_arm(job);
	fence = dma_fence_get(&job->drm.s_fence->finished);
	xe_sched_job_push(job);

	timeout = dma_fence_wait_timeout(fence, false, HZ);
	dma_fence_put(fence);
	if (timeout < 0)
		return timeout;
	else if (!timeout)
		return -ETIME;

	return 0;
}

static void emit_pxp_heci_cmd(struct xe_device *xe, struct iosys_map *batch,
			      u64 addr_in, u32 size_in, u64 addr_out, u32 size_out)
{
	u32 len = 0;

	xe_map_wr(xe, batch, len++ * sizeof(u32), u32, GSC_HECI_CMD_PKT);
	xe_map_wr(xe, batch, len++ * sizeof(u32), u32, lower_32_bits(addr_in));
	xe_map_wr(xe, batch, len++ * sizeof(u32), u32, upper_32_bits(addr_in));
	xe_map_wr(xe, batch, len++ * sizeof(u32), u32, size_in);
	xe_map_wr(xe, batch, len++ * sizeof(u32), u32, lower_32_bits(addr_out));
	xe_map_wr(xe, batch, len++ * sizeof(u32), u32, upper_32_bits(addr_out));
	xe_map_wr(xe, batch, len++ * sizeof(u32), u32, size_out);
	xe_map_wr(xe, batch, len++ * sizeof(u32), u32, 0);
	xe_map_wr(xe, batch, len++ * sizeof(u32), u32, MI_BATCH_BUFFER_END);
}

#define GSC_PENDING_RETRY_MAXCOUNT 40
#define GSC_PENDING_RETRY_PAUSE_MS 50
static int gsccs_send_message(struct xe_pxp_gsc_client_resources *gsc_res,
			      void *msg_in, size_t msg_in_size,
			      void *msg_out, size_t msg_out_size_max)
{
	struct xe_device *xe = gsc_res->vm->xe;
	const size_t max_msg_size = gsc_res->inout_size - sizeof(struct intel_gsc_mtl_header);
	u32 wr_offset;
	u32 rd_offset;
	u32 reply_size;
	u32 min_reply_size = 0;
	int ret;
	int retry = GSC_PENDING_RETRY_MAXCOUNT;

	if (msg_in_size > max_msg_size || msg_out_size_max > max_msg_size)
		return -ENOSPC;

	wr_offset = xe_gsc_emit_header(xe, &gsc_res->msg_in, 0,
				       HECI_MEADDRESS_PXP,
				       gsc_res->host_session_handle,
				       msg_in_size);

	/* NOTE: zero size packets are used for session-cleanups */
	if (msg_in && msg_in_size) {
		xe_map_memcpy_to(xe, &gsc_res->msg_in, wr_offset,
				 msg_in, msg_in_size);
		min_reply_size = sizeof(struct pxp_cmd_header);
	}

	/* Make sure the reply header does not contain stale data */
	xe_gsc_poison_header(xe, &gsc_res->msg_out, 0);

	/*
	 * The BO is mapped at address 0 of the PPGTT, so no need to add its
	 * base offset when calculating the in/out addresses.
	 */
	emit_pxp_heci_cmd(xe, &gsc_res->batch, PXP_BB_SIZE,
			  wr_offset + msg_in_size, PXP_BB_SIZE + gsc_res->inout_size,
			  wr_offset + msg_out_size_max);

	xe_device_wmb(xe);

	/*
	 * If the GSC needs to communicate with CSME to complete our request,
	 * it'll set the "pending" flag in the return header. In this scenario
	 * we're expected to wait 50ms to give some time to the proxy code to
	 * handle the GSC<->CSME communication and then try again. Note that,
	 * although in most case the 50ms window is enough, the proxy flow is
	 * not actually guaranteed to complete within that time period, so we
	 * might have to try multiple times, up to a worst case of 2 seconds,
	 * after which the request is considered aborted.
	 */
	do {
		ret = pxp_pkt_submit(gsc_res->q, 0);
		if (ret)
			break;

		if (xe_gsc_check_and_update_pending(xe, &gsc_res->msg_in, 0,
						    &gsc_res->msg_out, 0)) {
			ret = -EAGAIN;
			msleep(GSC_PENDING_RETRY_PAUSE_MS);
		}
	} while (--retry && ret == -EAGAIN);

	if (ret) {
		drm_err(&xe->drm, "failed to submit GSC PXP message (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	ret = xe_gsc_read_out_header(xe, &gsc_res->msg_out, 0,
				     min_reply_size, &rd_offset);
	if (ret) {
		drm_err(&xe->drm, "invalid GSC reply for PXP (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	if (msg_out && min_reply_size) {
		reply_size = xe_map_rd_field(xe, &gsc_res->msg_out, rd_offset,
					     struct pxp_cmd_header, buffer_len);
		reply_size += sizeof(struct pxp_cmd_header);

		if (reply_size > msg_out_size_max) {
			drm_warn(&xe->drm, "PXP reply size overflow: %u (%zu)\n",
				 reply_size, msg_out_size_max);
			reply_size = msg_out_size_max;
		}

		xe_map_memcpy_from(xe, msg_out, &gsc_res->msg_out,
				   rd_offset, reply_size);
	}

	xe_gsc_poison_header(xe, &gsc_res->msg_in, 0);

	return ret;
}

/**
 * xe_pxp_submit_session_init - submits a PXP GSC session initialization
 * @gsc_res: the pxp client resources
 * @id: the session to initialize
 *
 * Submit a message to the GSC FW to initialize (i.e. start) a PXP session.
 *
 * Returns 0 if the submission is successful, an errno value otherwise.
 */
int xe_pxp_submit_session_init(struct xe_pxp_gsc_client_resources *gsc_res, u32 id)
{
	struct xe_device *xe = gsc_res->vm->xe;
	struct pxp43_create_arb_in msg_in = {0};
	struct pxp43_create_arb_out msg_out = {0};
	int ret;

	msg_in.header.api_version = PXP_APIVER(4, 3);
	msg_in.header.command_id = PXP43_CMDID_INIT_SESSION;
	msg_in.header.stream_id = (FIELD_PREP(PXP43_INIT_SESSION_APPID, id) |
				   FIELD_PREP(PXP43_INIT_SESSION_VALID, 1) |
				   FIELD_PREP(PXP43_INIT_SESSION_APPTYPE, 0));
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);

	if (id == DRM_XE_PXP_HWDRM_DEFAULT_SESSION)
		msg_in.protection_mode = PXP43_INIT_SESSION_PROTECTION_ARB;

	ret = gsccs_send_message(gsc_res, &msg_in, sizeof(msg_in),
				 &msg_out, sizeof(msg_out));
	if (ret) {
		drm_err(&xe->drm, "Failed to init PXP session %u (%pe)\n", id, ERR_PTR(ret));
	} else if (msg_out.header.status != 0) {
		ret = -EIO;

		if (is_fw_err_platform_config(msg_out.header.status))
			drm_info_once(&xe->drm,
				      "Failed to init PXP session %u due to BIOS/SOC, s=0x%x(%s)\n",
				      id, msg_out.header.status,
				      fw_err_to_string(msg_out.header.status));
		else
			drm_dbg(&xe->drm, "Failed to init PXP session %u, s=0x%x\n",
				id, msg_out.header.status);
	}

	return ret;
}

/**
 * xe_pxp_submit_session_invalidation - submits a PXP GSC invalidation
 * @gsc_res: the pxp client resources
 * @id: the session to invalidate
 *
 * Submit a message to the GSC FW to notify it that a session has been
 * terminated and is therefore invalid.
 *
 * Returns 0 if the submission is successful, an errno value otherwise.
 */
int xe_pxp_submit_session_invalidation(struct xe_pxp_gsc_client_resources *gsc_res, u32 id)
{
	struct xe_device *xe = gsc_res->vm->xe;
	struct pxp43_inv_stream_key_in msg_in = {0};
	struct pxp43_inv_stream_key_out msg_out = {0};
	int ret = 0;

	/*
	 * Stream key invalidation reuses the same version 4.2 input/output
	 * command format but firmware requires 4.3 API interaction
	 */
	msg_in.header.api_version = PXP_APIVER(4, 3);
	msg_in.header.command_id = PXP43_CMDID_INVALIDATE_STREAM_KEY;
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);

	msg_in.header.stream_id = FIELD_PREP(PXP_CMDHDR_EXTDATA_SESSION_VALID, 1);
	msg_in.header.stream_id |= FIELD_PREP(PXP_CMDHDR_EXTDATA_APP_TYPE, 0);
	msg_in.header.stream_id |= FIELD_PREP(PXP_CMDHDR_EXTDATA_SESSION_ID, id);

	ret = gsccs_send_message(gsc_res, &msg_in, sizeof(msg_in),
				 &msg_out, sizeof(msg_out));
	if (ret) {
		drm_err(&xe->drm, "Failed to invalidate PXP stream-key %u (%pe)\n",
			id, ERR_PTR(ret));
	} else if (msg_out.header.status != 0) {
		ret = -EIO;

		if (is_fw_err_platform_config(msg_out.header.status))
			drm_info_once(&xe->drm,
				      "Failed to invalidate PXP stream-key %u: BIOS/SOC 0x%08x(%s)\n",
				      id, msg_out.header.status,
				      fw_err_to_string(msg_out.header.status));
		else
			drm_dbg(&xe->drm, "Failed to invalidate stream-key %u, s=0x%08x\n",
				id, msg_out.header.status);
	}

	return ret;
}
