// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2023 Intel Corporation.
 */

#include "gem/i915_gem_internal.h"

#include "gt/intel_context.h"
#include "gt/uc/intel_gsc_fw.h"
#include "gt/uc/intel_gsc_uc_heci_cmd_submit.h"

#include "i915_drv.h"
#include "intel_pxp.h"
#include "intel_pxp_cmd_interface_42.h"
#include "intel_pxp_cmd_interface_43.h"
#include "intel_pxp_gsccs.h"
#include "intel_pxp_types.h"

static bool
is_fw_err_platform_config(u32 type)
{
	switch (type) {
	case PXP_STATUS_ERROR_API_VERSION:
	case PXP_STATUS_PLATFCONFIG_KF1_NOVERIF:
	case PXP_STATUS_PLATFCONFIG_KF1_BAD:
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
		return "ERR_PLATFORM_CONFIG";
	default:
		break;
	}
	return NULL;
}

static int
gsccs_send_message(struct intel_pxp *pxp,
		   void *msg_in, size_t msg_in_size,
		   void *msg_out, size_t msg_out_size_max,
		   size_t *msg_out_len,
		   u64 *gsc_msg_handle_retry)
{
	struct intel_gt *gt = pxp->ctrl_gt;
	struct drm_i915_private *i915 = gt->i915;
	struct gsccs_session_resources *exec_res =  &pxp->gsccs_res;
	struct intel_gsc_mtl_header *header = exec_res->pkt_vaddr;
	struct intel_gsc_heci_non_priv_pkt pkt;
	size_t max_msg_size;
	u32 reply_size;
	int ret;

	if (!exec_res->ce)
		return -ENODEV;

	max_msg_size = PXP43_MAX_HECI_INOUT_SIZE - sizeof(*header);

	if (msg_in_size > max_msg_size || msg_out_size_max > max_msg_size)
		return -ENOSPC;

	if (!exec_res->pkt_vma || !exec_res->bb_vma)
		return -ENOENT;

	GEM_BUG_ON(exec_res->pkt_vma->size < (2 * PXP43_MAX_HECI_INOUT_SIZE));

	mutex_lock(&pxp->tee_mutex);

	memset(header, 0, sizeof(*header));
	intel_gsc_uc_heci_cmd_emit_mtl_header(header, HECI_MEADDRESS_PXP,
					      msg_in_size + sizeof(*header),
					      exec_res->host_session_handle);

	/* check if this is a host-session-handle cleanup call (empty packet) */
	if (!msg_in && !msg_out)
		header->flags |= GSC_INFLAG_MSG_CLEANUP;

	/* copy caller provided gsc message handle if this is polling for a prior msg completion */
	header->gsc_message_handle = *gsc_msg_handle_retry;

	/* NOTE: zero size packets are used for session-cleanups */
	if (msg_in && msg_in_size)
		memcpy(exec_res->pkt_vaddr + sizeof(*header), msg_in, msg_in_size);

	pkt.addr_in = i915_vma_offset(exec_res->pkt_vma);
	pkt.size_in = header->message_size;
	pkt.addr_out = pkt.addr_in + PXP43_MAX_HECI_INOUT_SIZE;
	pkt.size_out = msg_out_size_max + sizeof(*header);
	pkt.heci_pkt_vma = exec_res->pkt_vma;
	pkt.bb_vma = exec_res->bb_vma;

	/*
	 * Before submitting, let's clear-out the validity marker on the reply offset.
	 * We use offset PXP43_MAX_HECI_INOUT_SIZE for reply location so point header there.
	 */
	header = exec_res->pkt_vaddr + PXP43_MAX_HECI_INOUT_SIZE;
	header->validity_marker = 0;

	ret = intel_gsc_uc_heci_cmd_submit_nonpriv(&gt->uc.gsc,
						   exec_res->ce, &pkt, exec_res->bb_vaddr,
						   GSC_REPLY_LATENCY_MS);
	if (ret) {
		drm_err(&i915->drm, "failed to send gsc PXP msg (%d)\n", ret);
		goto unlock;
	}

	/* Response validity marker, status and busyness */
	if (header->validity_marker != GSC_HECI_VALIDITY_MARKER) {
		drm_err(&i915->drm, "gsc PXP reply with invalid validity marker\n");
		ret = -EINVAL;
		goto unlock;
	}
	if (header->status != 0) {
		drm_dbg(&i915->drm, "gsc PXP reply status has error = 0x%08x\n",
			header->status);
		ret = -EINVAL;
		goto unlock;
	}
	if (header->flags & GSC_OUTFLAG_MSG_PENDING) {
		drm_dbg(&i915->drm, "gsc PXP reply is busy\n");
		/*
		 * When the GSC firmware replies with pending bit, it means that the requested
		 * operation has begun but the completion is pending and the caller needs
		 * to re-request with the gsc_message_handle that was returned by the firmware.
		 * until the pending bit is turned off.
		 */
		*gsc_msg_handle_retry = header->gsc_message_handle;
		ret = -EAGAIN;
		goto unlock;
	}

	reply_size = header->message_size - sizeof(*header);
	if (reply_size > msg_out_size_max) {
		drm_warn(&i915->drm, "caller with insufficient PXP reply size %u (%zu)\n",
			 reply_size, msg_out_size_max);
		reply_size = msg_out_size_max;
	}

	if (msg_out)
		memcpy(msg_out, exec_res->pkt_vaddr + PXP43_MAX_HECI_INOUT_SIZE + sizeof(*header),
		       reply_size);
	if (msg_out_len)
		*msg_out_len = reply_size;

unlock:
	mutex_unlock(&pxp->tee_mutex);
	return ret;
}

static int
gsccs_send_message_retry_complete(struct intel_pxp *pxp,
				  void *msg_in, size_t msg_in_size,
				  void *msg_out, size_t msg_out_size_max,
				  size_t *msg_out_len)
{
	u64 gsc_session_retry = 0;
	int ret, tries = 0;

	/*
	 * Keep sending request if GSC firmware was busy. Based on fw specs +
	 * sw overhead (and testing) we expect a worst case pending-bit delay of
	 * GSC_PENDING_RETRY_MAXCOUNT x GSC_PENDING_RETRY_PAUSE_MS millisecs.
	 */
	do {
		ret = gsccs_send_message(pxp, msg_in, msg_in_size, msg_out, msg_out_size_max,
					 msg_out_len, &gsc_session_retry);
		/* Only try again if gsc says so */
		if (ret != -EAGAIN)
			break;

		msleep(GSC_PENDING_RETRY_PAUSE_MS);
	} while (++tries < GSC_PENDING_RETRY_MAXCOUNT);

	return ret;
}

bool intel_pxp_gsccs_is_ready_for_sessions(struct intel_pxp *pxp)
{
	/*
	 * GSC-fw loading, HuC-fw loading, HuC-fw authentication and
	 * GSC-proxy init flow (requiring an mei component driver)
	 * must all occur first before we can start requesting for PXP
	 * sessions. Checking for completion on HuC authentication and
	 * gsc-proxy init flow (the last set of dependencies that
	 * are out of order) will suffice.
	 */
	if (intel_huc_is_authenticated(&pxp->ctrl_gt->uc.huc, INTEL_HUC_AUTH_BY_GSC) &&
	    intel_gsc_uc_fw_proxy_init_done(&pxp->ctrl_gt->uc.gsc))
		return true;

	return false;
}

int intel_pxp_gsccs_create_session(struct intel_pxp *pxp,
				   int arb_session_id)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct pxp43_create_arb_in msg_in = {0};
	struct pxp43_create_arb_out msg_out = {0};
	int ret;

	msg_in.header.api_version = PXP_APIVER(4, 3);
	msg_in.header.command_id = PXP43_CMDID_INIT_SESSION;
	msg_in.header.stream_id = (FIELD_PREP(PXP43_INIT_SESSION_APPID, arb_session_id) |
				   FIELD_PREP(PXP43_INIT_SESSION_VALID, 1) |
				   FIELD_PREP(PXP43_INIT_SESSION_APPTYPE, 0));
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);
	msg_in.protection_mode = PXP43_INIT_SESSION_PROTECTION_ARB;

	ret = gsccs_send_message_retry_complete(pxp,
						&msg_in, sizeof(msg_in),
						&msg_out, sizeof(msg_out), NULL);
	if (ret) {
		drm_err(&i915->drm, "Failed to init session %d, ret=[%d]\n", arb_session_id, ret);
	} else if (msg_out.header.status != 0) {
		if (is_fw_err_platform_config(msg_out.header.status)) {
			drm_info_once(&i915->drm,
				      "PXP init-session-%d failed due to BIOS/SOC:0x%08x:%s\n",
				      arb_session_id, msg_out.header.status,
				      fw_err_to_string(msg_out.header.status));
		} else {
			drm_dbg(&i915->drm, "PXP init-session-%d failed 0x%08x:%st:\n",
				arb_session_id, msg_out.header.status,
				fw_err_to_string(msg_out.header.status));
			drm_dbg(&i915->drm, "     cmd-detail: ID=[0x%08x],API-Ver-[0x%08x]\n",
				msg_in.header.command_id, msg_in.header.api_version);
		}
	}

	return ret;
}

void intel_pxp_gsccs_end_arb_fw_session(struct intel_pxp *pxp, u32 session_id)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct pxp42_inv_stream_key_in msg_in = {0};
	struct pxp42_inv_stream_key_out msg_out = {0};
	int ret = 0;

	/*
	 * Stream key invalidation reuses the same version 4.2 input/output
	 * command format but firmware requires 4.3 API interaction
	 */
	msg_in.header.api_version = PXP_APIVER(4, 3);
	msg_in.header.command_id = PXP42_CMDID_INVALIDATE_STREAM_KEY;
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);

	msg_in.header.stream_id = FIELD_PREP(PXP_CMDHDR_EXTDATA_SESSION_VALID, 1);
	msg_in.header.stream_id |= FIELD_PREP(PXP_CMDHDR_EXTDATA_APP_TYPE, 0);
	msg_in.header.stream_id |= FIELD_PREP(PXP_CMDHDR_EXTDATA_SESSION_ID, session_id);

	ret = gsccs_send_message_retry_complete(pxp,
						&msg_in, sizeof(msg_in),
						&msg_out, sizeof(msg_out), NULL);
	if (ret) {
		drm_err(&i915->drm, "Failed to inv-stream-key-%u, ret=[%d]\n",
			session_id, ret);
	} else if (msg_out.header.status != 0) {
		if (is_fw_err_platform_config(msg_out.header.status)) {
			drm_info_once(&i915->drm,
				      "PXP inv-stream-key-%u failed due to BIOS/SOC :0x%08x:%s\n",
				      session_id, msg_out.header.status,
				      fw_err_to_string(msg_out.header.status));
		} else {
			drm_dbg(&i915->drm, "PXP inv-stream-key-%u failed 0x%08x:%s:\n",
				session_id, msg_out.header.status,
				fw_err_to_string(msg_out.header.status));
			drm_dbg(&i915->drm, "     cmd-detail: ID=[0x%08x],API-Ver-[0x%08x]\n",
				msg_in.header.command_id, msg_in.header.api_version);
		}
	}
}

static void
gsccs_cleanup_fw_host_session_handle(struct intel_pxp *pxp)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	int ret;

	ret = gsccs_send_message_retry_complete(pxp, NULL, 0, NULL, 0, NULL);
	if (ret)
		drm_dbg(&i915->drm, "Failed to send gsccs msg host-session-cleanup: ret=[%d]\n",
			ret);
}

static void
gsccs_destroy_execution_resource(struct intel_pxp *pxp)
{
	struct gsccs_session_resources *exec_res = &pxp->gsccs_res;

	if (exec_res->host_session_handle)
		gsccs_cleanup_fw_host_session_handle(pxp);
	if (exec_res->ce)
		intel_context_put(exec_res->ce);
	if (exec_res->bb_vma)
		i915_vma_unpin_and_release(&exec_res->bb_vma, I915_VMA_RELEASE_MAP);
	if (exec_res->pkt_vma)
		i915_vma_unpin_and_release(&exec_res->pkt_vma, I915_VMA_RELEASE_MAP);

	memset(exec_res, 0, sizeof(*exec_res));
}

static int
gsccs_create_buffer(struct intel_gt *gt,
		    const char *bufname, size_t size,
		    struct i915_vma **vma, void **map)
{
	struct drm_i915_private *i915 = gt->i915;
	struct drm_i915_gem_object *obj;
	int err = 0;

	obj = i915_gem_object_create_internal(i915, size);
	if (IS_ERR(obj)) {
		drm_err(&i915->drm, "Failed to allocate gsccs backend %s.\n", bufname);
		err = PTR_ERR(obj);
		goto out_none;
	}

	*vma = i915_vma_instance(obj, gt->vm, NULL);
	if (IS_ERR(*vma)) {
		drm_err(&i915->drm, "Failed to vma-instance gsccs backend %s.\n", bufname);
		err = PTR_ERR(*vma);
		goto out_put;
	}

	/* return a virtual pointer */
	*map = i915_gem_object_pin_map_unlocked(obj, i915_coherent_map_type(i915, obj, true));
	if (IS_ERR(*map)) {
		drm_err(&i915->drm, "Failed to map gsccs backend %s.\n", bufname);
		err = PTR_ERR(*map);
		goto out_put;
	}

	/* all PXP sessions commands are treated as non-privileged */
	err = i915_vma_pin(*vma, 0, 0, PIN_USER);
	if (err) {
		drm_err(&i915->drm, "Failed to vma-pin gsccs backend %s.\n", bufname);
		goto out_unmap;
	}

	return 0;

out_unmap:
	i915_gem_object_unpin_map(obj);
out_put:
	i915_gem_object_put(obj);
out_none:
	*vma = NULL;
	*map = NULL;

	return err;
}

static int
gsccs_allocate_execution_resource(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp->ctrl_gt;
	struct gsccs_session_resources *exec_res = &pxp->gsccs_res;
	struct intel_engine_cs *engine = gt->engine[GSC0];
	struct intel_context *ce;
	int err = 0;

	/*
	 * First, ensure the GSC engine is present.
	 * NOTE: Backend would only be called with the correct gt.
	 */
	if (!engine)
		return -ENODEV;

	/*
	 * Now, allocate, pin and map two objects, one for the heci message packet
	 * and another for the batch buffer we submit into GSC engine (that includes the packet).
	 * NOTE: GSC-CS backend is currently only supported on MTL, so we allocate shmem.
	 */
	err = gsccs_create_buffer(pxp->ctrl_gt, "Heci Packet",
				  2 * PXP43_MAX_HECI_INOUT_SIZE,
				  &exec_res->pkt_vma, &exec_res->pkt_vaddr);
	if (err)
		return err;

	err = gsccs_create_buffer(pxp->ctrl_gt, "Batch Buffer", PAGE_SIZE,
				  &exec_res->bb_vma, &exec_res->bb_vaddr);
	if (err)
		goto free_pkt;

	/* Finally, create an intel_context to be used during the submission */
	ce = intel_context_create(engine);
	if (IS_ERR(ce)) {
		drm_err(&gt->i915->drm, "Failed creating gsccs backend ctx\n");
		err = PTR_ERR(ce);
		goto free_batch;
	}

	i915_vm_put(ce->vm);
	ce->vm = i915_vm_get(pxp->ctrl_gt->vm);
	exec_res->ce = ce;

	/* initialize host-session-handle (for all i915-to-gsc-firmware PXP cmds) */
	get_random_bytes(&exec_res->host_session_handle, sizeof(exec_res->host_session_handle));

	return 0;

free_batch:
	i915_vma_unpin_and_release(&exec_res->bb_vma, I915_VMA_RELEASE_MAP);
free_pkt:
	i915_vma_unpin_and_release(&exec_res->pkt_vma, I915_VMA_RELEASE_MAP);
	memset(exec_res, 0, sizeof(*exec_res));

	return err;
}

void intel_pxp_gsccs_fini(struct intel_pxp *pxp)
{
	intel_wakeref_t wakeref;

	gsccs_destroy_execution_resource(pxp);
	with_intel_runtime_pm(&pxp->ctrl_gt->i915->runtime_pm, wakeref)
		intel_pxp_fini_hw(pxp);
}

int intel_pxp_gsccs_init(struct intel_pxp *pxp)
{
	int ret;
	intel_wakeref_t wakeref;

	ret = gsccs_allocate_execution_resource(pxp);
	if (!ret) {
		with_intel_runtime_pm(&pxp->ctrl_gt->i915->runtime_pm, wakeref)
			intel_pxp_init_hw(pxp);
	}
	return ret;
}
