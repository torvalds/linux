// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include "display/intel_hdcp_gsc.h"
#include "gem/i915_gem_region.h"
#include "gt/uc/intel_gsc_uc_heci_cmd_submit.h"
#include "i915_drv.h"
#include "i915_utils.h"

/*This function helps allocate memory for the command that we will send to gsc cs */
static int intel_hdcp_gsc_initialize_message(struct drm_i915_private *i915,
					     struct intel_hdcp_gsc_message *hdcp_message)
{
	struct intel_gt *gt = i915->media_gt;
	struct drm_i915_gem_object *obj = NULL;
	struct i915_vma *vma = NULL;
	void *cmd;
	int err;

	/* allocate object of one page for HDCP command memory and store it */
	obj = i915_gem_object_create_shmem(i915, PAGE_SIZE);

	if (IS_ERR(obj)) {
		drm_err(&i915->drm, "Failed to allocate HDCP streaming command!\n");
		return PTR_ERR(obj);
	}

	cmd = i915_gem_object_pin_map_unlocked(obj, i915_coherent_map_type(i915, obj, true));
	if (IS_ERR(cmd)) {
		drm_err(&i915->drm, "Failed to map gsc message page!\n");
		err = PTR_ERR(cmd);
		goto out_unpin;
	}

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_unmap;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err)
		goto out_unmap;

	memset(cmd, 0, obj->base.size);

	hdcp_message->hdcp_cmd = cmd;
	hdcp_message->vma = vma;

	return 0;

out_unmap:
	i915_gem_object_unpin_map(obj);
out_unpin:
	i915_gem_object_put(obj);
	return err;
}

int intel_hdcp_gsc_hdcp2_init(struct drm_i915_private *i915)
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
	i915->display.hdcp.hdcp_message = hdcp_message;
	ret = intel_hdcp_gsc_initialize_message(i915, hdcp_message);

	if (ret)
		drm_err(&i915->drm, "Could not initialize hdcp_message\n");

	return ret;
}

void intel_hdcp_gsc_free_message(struct drm_i915_private *i915)
{
	struct intel_hdcp_gsc_message *hdcp_message =
					i915->display.hdcp.hdcp_message;

	i915_vma_unpin_and_release(&hdcp_message->vma, I915_VMA_RELEASE_MAP);
	kfree(hdcp_message);
}

static int intel_gsc_send_sync(struct drm_i915_private *i915,
			       struct intel_gsc_mtl_header *header, u64 addr,
			       size_t msg_out_len)
{
	struct intel_gt *gt = i915->media_gt;
	int ret;

	header->flags = 0;
	ret = intel_gsc_uc_heci_cmd_submit_packet(&gt->uc.gsc, addr,
						  header->message_size,
						  addr,
						  msg_out_len + sizeof(*header));
	if (ret) {
		drm_err(&i915->drm, "failed to send gsc HDCP msg (%d)\n", ret);
		return ret;
	}

	/*
	 * Checking validity marker for memory sanity
	 */
	if (header->validity_marker != GSC_HECI_VALIDITY_MARKER) {
		drm_err(&i915->drm, "invalid validity marker\n");
		return -EINVAL;
	}

	if (header->status != 0) {
		drm_err(&i915->drm, "header status indicates error %d\n",
			header->status);
		return -EINVAL;
	}

	if (header->flags & GSC_OUTFLAG_MSG_PENDING)
		return -EAGAIN;

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
				size_t msg_in_len, u8 *msg_out, size_t msg_out_len)
{
	struct intel_gt *gt = i915->media_gt;
	struct intel_gsc_mtl_header *header;
	const size_t max_msg_size = PAGE_SIZE - sizeof(*header);
	struct intel_hdcp_gsc_message *hdcp_message;
	u64 addr, host_session_id;
	u32 reply_size, msg_size;
	int ret, tries = 0;

	if (!intel_uc_uses_gsc_uc(&gt->uc))
		return -ENODEV;

	if (msg_in_len > max_msg_size || msg_out_len > max_msg_size)
		return -ENOSPC;

	hdcp_message = i915->display.hdcp.hdcp_message;
	header = hdcp_message->hdcp_cmd;
	addr = i915_ggtt_offset(hdcp_message->vma);

	msg_size = msg_in_len + sizeof(*header);
	memset(header, 0, msg_size);
	get_random_bytes(&host_session_id, sizeof(u64));
	intel_gsc_uc_heci_cmd_emit_mtl_header(header, HECI_MEADDRESS_HDCP,
					      msg_size, host_session_id);
	memcpy(hdcp_message->hdcp_cmd + sizeof(*header), msg_in, msg_in_len);

	/*
	 * Keep sending request in case the pending bit is set no need to add
	 * message handle as we are using same address hence loc. of header is
	 * same and it will contain the message handle. we will send the message
	 * 20 times each message 50 ms apart
	 */
	do {
		ret = intel_gsc_send_sync(i915, header, addr, msg_out_len);

		/* Only try again if gsc says so */
		if (ret != -EAGAIN)
			break;

		msleep(50);

	} while (++tries < 20);

	if (ret)
		goto err;

	/* we use the same mem for the reply, so header is in the same loc */
	reply_size = header->message_size - sizeof(*header);
	if (reply_size > msg_out_len) {
		drm_warn(&i915->drm, "caller with insufficient HDCP reply size %u (%d)\n",
			 reply_size, (u32)msg_out_len);
		reply_size = msg_out_len;
	} else if (reply_size != msg_out_len) {
		drm_dbg_kms(&i915->drm, "caller unexpected HCDP reply size %u (%d)\n",
			    reply_size, (u32)msg_out_len);
	}

	memcpy(msg_out, hdcp_message->hdcp_cmd + sizeof(*header), msg_out_len);

err:
	return ret;
}
