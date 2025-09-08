// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include <drm/drm_print.h>
#include <drm/intel/i915_hdcp_interface.h>
#include <linux/delay.h>

#include "abi/gsc_command_header_abi.h"
#include "intel_hdcp_gsc.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_force_wake.h"
#include "xe_gsc_proxy.h"
#include "xe_gsc_submit.h"
#include "xe_map.h"
#include "xe_pm.h"
#include "xe_uc_fw.h"

#define HECI_MEADDRESS_HDCP 18

struct intel_hdcp_gsc_context {
	struct xe_device *xe;
	struct xe_bo *hdcp_bo;
	u64 hdcp_cmd_in;
	u64 hdcp_cmd_out;
};

#define HDCP_GSC_HEADER_SIZE sizeof(struct intel_gsc_mtl_header)

bool intel_hdcp_gsc_check_status(struct drm_device *drm)
{
	struct xe_device *xe = to_xe_device(drm);
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->media_gt;
	struct xe_gsc *gsc = &gt->uc.gsc;
	bool ret = true;
	unsigned int fw_ref;

	if (!gsc || !xe_uc_fw_is_enabled(&gsc->fw)) {
		drm_dbg_kms(&xe->drm,
			    "GSC Components not ready for HDCP2.x\n");
		return false;
	}

	xe_pm_runtime_get(xe);
	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GSC);
	if (!fw_ref) {
		drm_dbg_kms(&xe->drm,
			    "failed to get forcewake to check proxy status\n");
		ret = false;
		goto out;
	}

	if (!xe_gsc_proxy_init_done(gsc))
		ret = false;

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
out:
	xe_pm_runtime_put(xe);
	return ret;
}

/*This function helps allocate memory for the command that we will send to gsc cs */
static int intel_hdcp_gsc_initialize_message(struct xe_device *xe,
					     struct intel_hdcp_gsc_context *gsc_context)
{
	struct xe_bo *bo = NULL;
	u64 cmd_in, cmd_out;
	int ret = 0;

	/* allocate object of two page for HDCP command memory and store it */
	bo = xe_bo_create_pin_map_novm(xe, xe_device_get_root_tile(xe), PAGE_SIZE * 2,
				       ttm_bo_type_kernel,
				       XE_BO_FLAG_SYSTEM |
				       XE_BO_FLAG_GGTT, false);

	if (IS_ERR(bo)) {
		drm_err(&xe->drm, "Failed to allocate bo for HDCP streaming command!\n");
		ret = PTR_ERR(bo);
		goto out;
	}

	cmd_in = xe_bo_ggtt_addr(bo);
	cmd_out = cmd_in + PAGE_SIZE;
	xe_map_memset(xe, &bo->vmap, 0, 0, xe_bo_size(bo));

	gsc_context->hdcp_bo = bo;
	gsc_context->hdcp_cmd_in = cmd_in;
	gsc_context->hdcp_cmd_out = cmd_out;
	gsc_context->xe = xe;

out:
	return ret;
}

struct intel_hdcp_gsc_context *intel_hdcp_gsc_context_alloc(struct drm_device *drm)
{
	struct xe_device *xe = to_xe_device(drm);
	struct intel_hdcp_gsc_context *gsc_context;
	int ret;

	gsc_context = kzalloc(sizeof(*gsc_context), GFP_KERNEL);
	if (!gsc_context)
		return ERR_PTR(-ENOMEM);

	/*
	 * NOTE: No need to lock the comp mutex here as it is already
	 * going to be taken before this function called
	 */
	ret = intel_hdcp_gsc_initialize_message(xe, gsc_context);
	if (ret) {
		drm_err(&xe->drm, "Could not initialize gsc_context\n");
		kfree(gsc_context);
		gsc_context = ERR_PTR(ret);
	}

	return gsc_context;
}

void intel_hdcp_gsc_context_free(struct intel_hdcp_gsc_context *gsc_context)
{
	if (!gsc_context)
		return;

	xe_bo_unpin_map_no_vm(gsc_context->hdcp_bo);
	kfree(gsc_context);
}

static int xe_gsc_send_sync(struct xe_device *xe,
			    struct intel_hdcp_gsc_context *gsc_context,
			    u32 msg_size_in, u32 msg_size_out,
			    u32 addr_out_off)
{
	struct xe_gt *gt = gsc_context->hdcp_bo->tile->media_gt;
	struct iosys_map *map = &gsc_context->hdcp_bo->vmap;
	struct xe_gsc *gsc = &gt->uc.gsc;
	int ret;

	ret = xe_gsc_pkt_submit_kernel(gsc, gsc_context->hdcp_cmd_in, msg_size_in,
				       gsc_context->hdcp_cmd_out, msg_size_out);
	if (ret) {
		drm_err(&xe->drm, "failed to send gsc HDCP msg (%d)\n", ret);
		return ret;
	}

	if (xe_gsc_check_and_update_pending(xe, map, 0, map, addr_out_off))
		return -EAGAIN;

	ret = xe_gsc_read_out_header(xe, map, addr_out_off,
				     sizeof(struct hdcp_cmd_header), NULL);

	return ret;
}

ssize_t intel_hdcp_gsc_msg_send(struct intel_hdcp_gsc_context *gsc_context,
				void *msg_in, size_t msg_in_len,
				void *msg_out, size_t msg_out_len)
{
	struct xe_device *xe = gsc_context->xe;
	const size_t max_msg_size = PAGE_SIZE - HDCP_GSC_HEADER_SIZE;
	u64 host_session_id;
	u32 msg_size_in, msg_size_out;
	u32 addr_out_off, addr_in_wr_off = 0;
	int ret, tries = 0;

	if (msg_in_len > max_msg_size || msg_out_len > max_msg_size) {
		ret = -ENOSPC;
		goto out;
	}

	msg_size_in = msg_in_len + HDCP_GSC_HEADER_SIZE;
	msg_size_out = msg_out_len + HDCP_GSC_HEADER_SIZE;
	addr_out_off = PAGE_SIZE;

	host_session_id = xe_gsc_create_host_session_id();
	xe_pm_runtime_get_noresume(xe);
	addr_in_wr_off = xe_gsc_emit_header(xe, &gsc_context->hdcp_bo->vmap,
					    addr_in_wr_off, HECI_MEADDRESS_HDCP,
					    host_session_id, msg_in_len);
	xe_map_memcpy_to(xe, &gsc_context->hdcp_bo->vmap, addr_in_wr_off,
			 msg_in, msg_in_len);
	/*
	 * Keep sending request in case the pending bit is set no need to add
	 * message handle as we are using same address hence loc. of header is
	 * same and it will contain the message handle. we will send the message
	 * 20 times each message 50 ms apart
	 */
	do {
		ret = xe_gsc_send_sync(xe, gsc_context, msg_size_in, msg_size_out,
				       addr_out_off);

		/* Only try again if gsc says so */
		if (ret != -EAGAIN)
			break;

		msleep(50);

	} while (++tries < 20);

	if (ret)
		goto out;

	xe_map_memcpy_from(xe, msg_out, &gsc_context->hdcp_bo->vmap,
			   addr_out_off + HDCP_GSC_HEADER_SIZE,
			   msg_out_len);

out:
	xe_pm_runtime_put(xe);
	return ret;
}
