// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include <drm/drm_print.h>
#include <drm/i915_hdcp_interface.h>
#include <linux/delay.h>

#include "abi/gsc_command_header_abi.h"
#include "intel_hdcp_gsc.h"
#include "intel_hdcp_gsc_message.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_gsc_proxy.h"
#include "xe_gsc_submit.h"
#include "xe_gt.h"
#include "xe_map.h"
#include "xe_pm.h"
#include "xe_uc_fw.h"

#define HECI_MEADDRESS_HDCP 18

struct intel_hdcp_gsc_message {
	struct xe_bo *hdcp_bo;
	u64 hdcp_cmd_in;
	u64 hdcp_cmd_out;
};

#define HDCP_GSC_HEADER_SIZE sizeof(struct intel_gsc_mtl_header)

bool intel_hdcp_gsc_cs_required(struct xe_device *xe)
{
	return DISPLAY_VER(xe) >= 14;
}

bool intel_hdcp_gsc_check_status(struct xe_device *xe)
{
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->media_gt;
	bool ret = true;

	if (!xe_uc_fw_is_enabled(&gt->uc.gsc.fw))
		return false;

	xe_pm_runtime_get(xe);
	if (xe_force_wake_get(gt_to_fw(gt), XE_FW_GSC)) {
		drm_dbg_kms(&xe->drm,
			    "failed to get forcewake to check proxy status\n");
		ret = false;
		goto out;
	}

	if (!xe_gsc_proxy_init_done(&gt->uc.gsc))
		ret = false;

	xe_force_wake_put(gt_to_fw(gt), XE_FW_GSC);
out:
	xe_pm_runtime_put(xe);
	return ret;
}

/*This function helps allocate memory for the command that we will send to gsc cs */
static int intel_hdcp_gsc_initialize_message(struct xe_device *xe,
					     struct intel_hdcp_gsc_message *hdcp_message)
{
	struct xe_bo *bo = NULL;
	u64 cmd_in, cmd_out;
	int ret = 0;

	/* allocate object of two page for HDCP command memory and store it */
	bo = xe_bo_create_pin_map(xe, xe_device_get_root_tile(xe), NULL, PAGE_SIZE * 2,
				  ttm_bo_type_kernel,
				  XE_BO_FLAG_SYSTEM |
				  XE_BO_FLAG_GGTT);

	if (IS_ERR(bo)) {
		drm_err(&xe->drm, "Failed to allocate bo for HDCP streaming command!\n");
		ret = PTR_ERR(bo);
		goto out;
	}

	cmd_in = xe_bo_ggtt_addr(bo);
	cmd_out = cmd_in + PAGE_SIZE;
	xe_map_memset(xe, &bo->vmap, 0, 0, bo->size);

	hdcp_message->hdcp_bo = bo;
	hdcp_message->hdcp_cmd_in = cmd_in;
	hdcp_message->hdcp_cmd_out = cmd_out;
out:
	return ret;
}

static int intel_hdcp_gsc_hdcp2_init(struct xe_device *xe)
{
	struct intel_hdcp_gsc_message *hdcp_message;
	int ret;

	hdcp_message = kzalloc(sizeof(*hdcp_message), GFP_KERNEL);

	if (!hdcp_message)
		return -ENOMEM;

	/*
	 * NOTE: No need to lock the comp mutex here as it is already
	 * going to be taken before this function called
	 */
	ret = intel_hdcp_gsc_initialize_message(xe, hdcp_message);
	if (ret) {
		drm_err(&xe->drm, "Could not initialize hdcp_message\n");
		kfree(hdcp_message);
		return ret;
	}

	xe->display.hdcp.hdcp_message = hdcp_message;
	return ret;
}

static const struct i915_hdcp_ops gsc_hdcp_ops = {
	.initiate_hdcp2_session = intel_hdcp_gsc_initiate_session,
	.verify_receiver_cert_prepare_km =
				intel_hdcp_gsc_verify_receiver_cert_prepare_km,
	.verify_hprime = intel_hdcp_gsc_verify_hprime,
	.store_pairing_info = intel_hdcp_gsc_store_pairing_info,
	.initiate_locality_check = intel_hdcp_gsc_initiate_locality_check,
	.verify_lprime = intel_hdcp_gsc_verify_lprime,
	.get_session_key = intel_hdcp_gsc_get_session_key,
	.repeater_check_flow_prepare_ack =
				intel_hdcp_gsc_repeater_check_flow_prepare_ack,
	.verify_mprime = intel_hdcp_gsc_verify_mprime,
	.enable_hdcp_authentication = intel_hdcp_gsc_enable_authentication,
	.close_hdcp_session = intel_hdcp_gsc_close_session,
};

int intel_hdcp_gsc_init(struct xe_device *xe)
{
	struct i915_hdcp_arbiter *data;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_lock(&xe->display.hdcp.hdcp_mutex);
	xe->display.hdcp.arbiter = data;
	xe->display.hdcp.arbiter->hdcp_dev = xe->drm.dev;
	xe->display.hdcp.arbiter->ops = &gsc_hdcp_ops;
	ret = intel_hdcp_gsc_hdcp2_init(xe);
	if (ret)
		kfree(data);

	mutex_unlock(&xe->display.hdcp.hdcp_mutex);

	return ret;
}

void intel_hdcp_gsc_fini(struct xe_device *xe)
{
	struct intel_hdcp_gsc_message *hdcp_message =
					xe->display.hdcp.hdcp_message;

	if (!hdcp_message)
		return;

	xe_bo_unpin_map_no_vm(hdcp_message->hdcp_bo);
	kfree(hdcp_message);
}

static int xe_gsc_send_sync(struct xe_device *xe,
			    struct intel_hdcp_gsc_message *hdcp_message,
			    u32 msg_size_in, u32 msg_size_out,
			    u32 addr_out_off)
{
	struct xe_gt *gt = hdcp_message->hdcp_bo->tile->media_gt;
	struct iosys_map *map = &hdcp_message->hdcp_bo->vmap;
	struct xe_gsc *gsc = &gt->uc.gsc;
	int ret;

	ret = xe_gsc_pkt_submit_kernel(gsc, hdcp_message->hdcp_cmd_in, msg_size_in,
				       hdcp_message->hdcp_cmd_out, msg_size_out);
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

ssize_t intel_hdcp_gsc_msg_send(struct xe_device *xe, u8 *msg_in,
				size_t msg_in_len, u8 *msg_out,
				size_t msg_out_len)
{
	const size_t max_msg_size = PAGE_SIZE - HDCP_GSC_HEADER_SIZE;
	struct intel_hdcp_gsc_message *hdcp_message;
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
	hdcp_message = xe->display.hdcp.hdcp_message;
	addr_out_off = PAGE_SIZE;

	host_session_id = xe_gsc_create_host_session_id();
	xe_pm_runtime_get_noresume(xe);
	addr_in_wr_off = xe_gsc_emit_header(xe, &hdcp_message->hdcp_bo->vmap,
					    addr_in_wr_off, HECI_MEADDRESS_HDCP,
					    host_session_id, msg_in_len);
	xe_map_memcpy_to(xe, &hdcp_message->hdcp_bo->vmap, addr_in_wr_off,
			 msg_in, msg_in_len);
	/*
	 * Keep sending request in case the pending bit is set no need to add
	 * message handle as we are using same address hence loc. of header is
	 * same and it will contain the message handle. we will send the message
	 * 20 times each message 50 ms apart
	 */
	do {
		ret = xe_gsc_send_sync(xe, hdcp_message, msg_size_in, msg_size_out,
				       addr_out_off);

		/* Only try again if gsc says so */
		if (ret != -EAGAIN)
			break;

		msleep(50);

	} while (++tries < 20);

	if (ret)
		goto out;

	xe_map_memcpy_from(xe, msg_out, &hdcp_message->hdcp_bo->vmap,
			   addr_out_off + HDCP_GSC_HEADER_SIZE,
			   msg_out_len);

out:
	xe_pm_runtime_put(xe);
	return ret;
}
