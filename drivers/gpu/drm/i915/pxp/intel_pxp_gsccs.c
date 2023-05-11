// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2023 Intel Corporation.
 */

#include "gem/i915_gem_internal.h"

#include "gt/intel_context.h"
#include "gt/uc/intel_gsc_uc_heci_cmd_submit.h"

#include "i915_drv.h"
#include "intel_pxp_cmd_interface_43.h"
#include "intel_pxp_gsccs.h"
#include "intel_pxp_types.h"

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
		drm_warn(&i915->drm, "caller with insufficient PXP reply size %u (%ld)\n",
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
	gsccs_destroy_execution_resource(pxp);
}

int intel_pxp_gsccs_init(struct intel_pxp *pxp)
{
	return gsccs_allocate_execution_resource(pxp);
}
