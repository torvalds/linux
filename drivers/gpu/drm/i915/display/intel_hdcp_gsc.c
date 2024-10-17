// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include <drm/intel/i915_hdcp_interface.h>

#include "gem/i915_gem_region.h"
#include "gt/intel_gt.h"
#include "gt/uc/intel_gsc_uc_heci_cmd_submit.h"
#include "i915_drv.h"
#include "i915_utils.h"
#include "intel_hdcp_gsc.h"
#include "intel_hdcp_gsc_message.h"

struct intel_hdcp_gsc_message {
	struct i915_vma *vma;
	void *hdcp_cmd_in;
	void *hdcp_cmd_out;
};

bool intel_hdcp_gsc_cs_required(struct intel_display *display)
{
	return DISPLAY_VER(display) >= 14;
}

bool intel_hdcp_gsc_check_status(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct intel_gt *gt = i915->media_gt;
	struct intel_gsc_uc *gsc = gt ? &gt->uc.gsc : NULL;

	if (!gsc || !intel_uc_fw_is_running(&gsc->fw)) {
		drm_dbg_kms(display->drm,
			    "GSC components required for HDCP2.2 are not ready\n");
		return false;
	}

	return true;
}

/*This function helps allocate memory for the command that we will send to gsc cs */
static int intel_hdcp_gsc_initialize_message(struct drm_i915_private *i915,
					     struct intel_hdcp_gsc_message *hdcp_message)
{
	struct intel_gt *gt = i915->media_gt;
	struct drm_i915_gem_object *obj = NULL;
	struct i915_vma *vma = NULL;
	void *cmd_in, *cmd_out;
	int err;

	/* allocate object of two page for HDCP command memory and store it */
	obj = i915_gem_object_create_shmem(i915, 2 * PAGE_SIZE);

	if (IS_ERR(obj)) {
		drm_err(&i915->drm, "Failed to allocate HDCP streaming command!\n");
		return PTR_ERR(obj);
	}

	cmd_in = i915_gem_object_pin_map_unlocked(obj, intel_gt_coherent_map_type(gt, obj, true));
	if (IS_ERR(cmd_in)) {
		drm_err(&i915->drm, "Failed to map gsc message page!\n");
		err = PTR_ERR(cmd_in);
		goto out_unpin;
	}

	cmd_out = cmd_in + PAGE_SIZE;

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_unmap;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL | PIN_HIGH);
	if (err)
		goto out_unmap;

	memset(cmd_in, 0, obj->base.size);

	hdcp_message->hdcp_cmd_in = cmd_in;
	hdcp_message->hdcp_cmd_out = cmd_out;
	hdcp_message->vma = vma;

	return 0;

out_unmap:
	i915_gem_object_unpin_map(obj);
out_unpin:
	i915_gem_object_put(obj);
	return err;
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

static int intel_hdcp_gsc_hdcp2_init(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct intel_hdcp_gsc_message *hdcp_message;
	int ret;

	hdcp_message = kzalloc(sizeof(*hdcp_message), GFP_KERNEL);

	if (!hdcp_message)
		return -ENOMEM;

	/*
	 * NOTE: No need to lock the comp mutex here as it is already
	 * going to be taken before this function called
	 */
	display->hdcp.hdcp_message = hdcp_message;
	ret = intel_hdcp_gsc_initialize_message(i915, hdcp_message);

	if (ret)
		drm_err(display->drm, "Could not initialize hdcp_message\n");

	return ret;
}

static void intel_hdcp_gsc_free_message(struct intel_display *display)
{
	struct intel_hdcp_gsc_message *hdcp_message =
					display->hdcp.hdcp_message;

	hdcp_message->hdcp_cmd_in = NULL;
	hdcp_message->hdcp_cmd_out = NULL;
	i915_vma_unpin_and_release(&hdcp_message->vma, I915_VMA_RELEASE_MAP);
	kfree(hdcp_message);
}

int intel_hdcp_gsc_init(struct intel_display *display)
{
	struct i915_hdcp_arbiter *data;
	int ret;

	data = kzalloc(sizeof(struct i915_hdcp_arbiter), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_lock(&display->hdcp.hdcp_mutex);
	display->hdcp.arbiter = data;
	display->hdcp.arbiter->hdcp_dev = display->drm->dev;
	display->hdcp.arbiter->ops = &gsc_hdcp_ops;
	ret = intel_hdcp_gsc_hdcp2_init(display);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

void intel_hdcp_gsc_fini(struct intel_display *display)
{
	intel_hdcp_gsc_free_message(display);
	kfree(display->hdcp.arbiter);
}

static int intel_gsc_send_sync(struct drm_i915_private *i915,
			       struct intel_gsc_mtl_header *header_in,
			       struct intel_gsc_mtl_header *header_out,
			       u64 addr_in, u64 addr_out,
			       size_t msg_out_len)
{
	struct intel_gt *gt = i915->media_gt;
	int ret;

	ret = intel_gsc_uc_heci_cmd_submit_packet(&gt->uc.gsc, addr_in,
						  header_in->message_size,
						  addr_out,
						  msg_out_len + sizeof(*header_out));
	if (ret) {
		drm_err(&i915->drm, "failed to send gsc HDCP msg (%d)\n", ret);
		return ret;
	}

	/*
	 * Checking validity marker and header status to see if some error has
	 * blocked us from sending message to gsc cs
	 */
	if (header_out->validity_marker != GSC_HECI_VALIDITY_MARKER) {
		drm_err(&i915->drm, "invalid validity marker\n");
		return -EINVAL;
	}

	if (header_out->status != 0) {
		drm_err(&i915->drm, "header status indicates error %d\n",
			header_out->status);
		return -EINVAL;
	}

	if (header_out->flags & GSC_OUTFLAG_MSG_PENDING) {
		header_in->gsc_message_handle = header_out->gsc_message_handle;
		return -EAGAIN;
	}

	return 0;
}

/*
 * This function can now be used for sending requests and will also handle
 * receipt of reply messages hence no different function of message retrieval
 * is required. We will initialize intel_hdcp_gsc_message structure then add
 * gsc cs memory header as stated in specs after which the normal HDCP payload
 * will follow
 */
ssize_t intel_hdcp_gsc_msg_send(struct drm_i915_private *i915, u8 *msg_in,
				size_t msg_in_len, u8 *msg_out,
				size_t msg_out_len)
{
	struct intel_gt *gt = i915->media_gt;
	struct intel_gsc_mtl_header *header_in, *header_out;
	const size_t max_msg_size = PAGE_SIZE - sizeof(*header_in);
	struct intel_hdcp_gsc_message *hdcp_message;
	u64 addr_in, addr_out, host_session_id;
	u32 reply_size, msg_size_in, msg_size_out;
	int ret, tries = 0;

	if (!intel_uc_uses_gsc_uc(&gt->uc))
		return -ENODEV;

	if (msg_in_len > max_msg_size || msg_out_len > max_msg_size)
		return -ENOSPC;

	msg_size_in = msg_in_len + sizeof(*header_in);
	msg_size_out = msg_out_len + sizeof(*header_out);
	hdcp_message = i915->display.hdcp.hdcp_message;
	header_in = hdcp_message->hdcp_cmd_in;
	header_out = hdcp_message->hdcp_cmd_out;
	addr_in = i915_ggtt_offset(hdcp_message->vma);
	addr_out = addr_in + PAGE_SIZE;

	memset(header_in, 0, msg_size_in);
	memset(header_out, 0, msg_size_out);
	get_random_bytes(&host_session_id, sizeof(u64));
	intel_gsc_uc_heci_cmd_emit_mtl_header(header_in, HECI_MEADDRESS_HDCP,
					      msg_size_in, host_session_id);
	memcpy(hdcp_message->hdcp_cmd_in + sizeof(*header_in), msg_in, msg_in_len);

	/*
	 * Keep sending request in case the pending bit is set no need to add
	 * message handle as we are using same address hence loc. of header is
	 * same and it will contain the message handle. we will send the message
	 * 20 times each message 50 ms apart
	 */
	do {
		ret = intel_gsc_send_sync(i915, header_in, header_out, addr_in,
					  addr_out, msg_out_len);

		/* Only try again if gsc says so */
		if (ret != -EAGAIN)
			break;

		msleep(50);

	} while (++tries < 20);

	if (ret)
		goto err;

	/* we use the same mem for the reply, so header is in the same loc */
	reply_size = header_out->message_size - sizeof(*header_out);
	if (reply_size > msg_out_len) {
		drm_warn(&i915->drm, "caller with insufficient HDCP reply size %u (%d)\n",
			 reply_size, (u32)msg_out_len);
		reply_size = msg_out_len;
	} else if (reply_size != msg_out_len) {
		drm_dbg_kms(&i915->drm, "caller unexpected HCDP reply size %u (%d)\n",
			    reply_size, (u32)msg_out_len);
	}

	memcpy(msg_out, hdcp_message->hdcp_cmd_out + sizeof(*header_out), msg_out_len);

err:
	return ret;
}
