// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#include <linux/component.h>

#include <drm/i915_pxp_tee_interface.h>
#include <drm/i915_component.h>

#include "gem/i915_gem_lmem.h"

#include "i915_drv.h"

#include "intel_pxp.h"
#include "intel_pxp_cmd_interface_42.h"
#include "intel_pxp_huc.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"
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

static int intel_pxp_tee_io_message(struct intel_pxp *pxp,
				    void *msg_in, u32 msg_in_size,
				    void *msg_out, u32 msg_out_max_size,
				    u32 *msg_out_rcv_size)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct i915_pxp_component *pxp_component = pxp->pxp_component;
	int ret = 0;

	mutex_lock(&pxp->tee_mutex);

	/*
	 * The binding of the component is asynchronous from i915 probe, so we
	 * can't be sure it has happened.
	 */
	if (!pxp_component) {
		ret = -ENODEV;
		goto unlock;
	}

	ret = pxp_component->ops->send(pxp_component->tee_dev, msg_in, msg_in_size);
	if (ret) {
		drm_err(&i915->drm, "Failed to send PXP TEE message\n");
		goto unlock;
	}

	ret = pxp_component->ops->recv(pxp_component->tee_dev, msg_out, msg_out_max_size);
	if (ret < 0) {
		drm_err(&i915->drm, "Failed to receive PXP TEE message\n");
		goto unlock;
	}

	if (ret > msg_out_max_size) {
		drm_err(&i915->drm,
			"Failed to receive PXP TEE message due to unexpected output size\n");
		ret = -ENOSPC;
		goto unlock;
	}

	if (msg_out_rcv_size)
		*msg_out_rcv_size = ret;

	ret = 0;
unlock:
	mutex_unlock(&pxp->tee_mutex);
	return ret;
}

int intel_pxp_tee_stream_message(struct intel_pxp *pxp,
				 u8 client_id, u32 fence_id,
				 void *msg_in, size_t msg_in_len,
				 void *msg_out, size_t msg_out_len)
{
	/* TODO: for bigger objects we need to use a sg of 4k pages */
	const size_t max_msg_size = PAGE_SIZE;
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct i915_pxp_component *pxp_component = pxp->pxp_component;
	unsigned int offset = 0;
	struct scatterlist *sg;
	int ret;

	if (msg_in_len > max_msg_size || msg_out_len > max_msg_size)
		return -ENOSPC;

	mutex_lock(&pxp->tee_mutex);

	if (unlikely(!pxp_component || !pxp_component->ops->gsc_command)) {
		ret = -ENODEV;
		goto unlock;
	}

	GEM_BUG_ON(!pxp->stream_cmd.obj);

	sg = i915_gem_object_get_sg_dma(pxp->stream_cmd.obj, 0, &offset);

	memcpy(pxp->stream_cmd.vaddr, msg_in, msg_in_len);

	ret = pxp_component->ops->gsc_command(pxp_component->tee_dev, client_id,
					      fence_id, sg, msg_in_len, sg);
	if (ret < 0)
		drm_err(&i915->drm, "Failed to send PXP TEE gsc command\n");
	else
		memcpy(msg_out, pxp->stream_cmd.vaddr, msg_out_len);

unlock:
	mutex_unlock(&pxp->tee_mutex);
	return ret;
}

/**
 * i915_pxp_tee_component_bind - bind function to pass the function pointers to pxp_tee
 * @i915_kdev: pointer to i915 kernel device
 * @tee_kdev: pointer to tee kernel device
 * @data: pointer to pxp_tee_master containing the function pointers
 *
 * This bind function is called during the system boot or resume from system sleep.
 *
 * Return: return 0 if successful.
 */
static int i915_pxp_tee_component_bind(struct device *i915_kdev,
				       struct device *tee_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_pxp *pxp = i915->pxp;
	struct intel_uc *uc = &pxp->ctrl_gt->uc;
	intel_wakeref_t wakeref;
	int ret = 0;

	if (!HAS_HECI_PXP(i915)) {
		pxp->dev_link = device_link_add(i915_kdev, tee_kdev, DL_FLAG_STATELESS);
		if (drm_WARN_ON(&i915->drm, !pxp->dev_link))
			return -ENODEV;
	}

	mutex_lock(&pxp->tee_mutex);
	pxp->pxp_component = data;
	pxp->pxp_component->tee_dev = tee_kdev;
	mutex_unlock(&pxp->tee_mutex);

	if (intel_uc_uses_huc(uc) && intel_huc_is_loaded_by_gsc(&uc->huc)) {
		with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
			/* load huc via pxp */
			ret = intel_huc_fw_load_and_auth_via_gsc(&uc->huc);
			if (ret < 0)
				drm_err(&i915->drm, "failed to load huc via gsc %d\n", ret);
		}
	}

	/* if we are suspended, the HW will be re-initialized on resume */
	wakeref = intel_runtime_pm_get_if_in_use(&i915->runtime_pm);
	if (!wakeref)
		return 0;

	/* the component is required to fully start the PXP HW */
	if (intel_pxp_is_enabled(pxp))
		intel_pxp_init_hw(pxp);

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return ret;
}

static void i915_pxp_tee_component_unbind(struct device *i915_kdev,
					  struct device *tee_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_pxp *pxp = i915->pxp;
	intel_wakeref_t wakeref;

	if (intel_pxp_is_enabled(pxp))
		with_intel_runtime_pm_if_in_use(&i915->runtime_pm, wakeref)
			intel_pxp_fini_hw(pxp);

	mutex_lock(&pxp->tee_mutex);
	pxp->pxp_component = NULL;
	mutex_unlock(&pxp->tee_mutex);

	if (pxp->dev_link) {
		device_link_del(pxp->dev_link);
		pxp->dev_link = NULL;
	}
}

static const struct component_ops i915_pxp_tee_component_ops = {
	.bind   = i915_pxp_tee_component_bind,
	.unbind = i915_pxp_tee_component_unbind,
};

static int alloc_streaming_command(struct intel_pxp *pxp)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct drm_i915_gem_object *obj = NULL;
	void *cmd;
	int err;

	pxp->stream_cmd.obj = NULL;
	pxp->stream_cmd.vaddr = NULL;

	if (!IS_DGFX(i915))
		return 0;

	/* allocate lmem object of one page for PXP command memory and store it */
	obj = i915_gem_object_create_lmem(i915, PAGE_SIZE, I915_BO_ALLOC_CONTIGUOUS);
	if (IS_ERR(obj)) {
		drm_err(&i915->drm, "Failed to allocate pxp streaming command!\n");
		return PTR_ERR(obj);
	}

	err = i915_gem_object_pin_pages_unlocked(obj);
	if (err) {
		drm_err(&i915->drm, "Failed to pin gsc message page!\n");
		goto out_put;
	}

	/* map the lmem into the virtual memory pointer */
	cmd = i915_gem_object_pin_map_unlocked(obj, i915_coherent_map_type(i915, obj, true));
	if (IS_ERR(cmd)) {
		drm_err(&i915->drm, "Failed to map gsc message page!\n");
		err = PTR_ERR(cmd);
		goto out_unpin;
	}

	memset(cmd, 0, obj->base.size);

	pxp->stream_cmd.obj = obj;
	pxp->stream_cmd.vaddr = cmd;

	return 0;

out_unpin:
	i915_gem_object_unpin_pages(obj);
out_put:
	i915_gem_object_put(obj);
	return err;
}

static void free_streaming_command(struct intel_pxp *pxp)
{
	struct drm_i915_gem_object *obj = fetch_and_zero(&pxp->stream_cmd.obj);

	if (!obj)
		return;

	i915_gem_object_unpin_map(obj);
	i915_gem_object_unpin_pages(obj);
	i915_gem_object_put(obj);
}

int intel_pxp_tee_component_init(struct intel_pxp *pxp)
{
	int ret;
	struct intel_gt *gt = pxp->ctrl_gt;
	struct drm_i915_private *i915 = gt->i915;

	ret = alloc_streaming_command(pxp);
	if (ret)
		return ret;

	ret = component_add_typed(i915->drm.dev, &i915_pxp_tee_component_ops,
				  I915_COMPONENT_PXP);
	if (ret < 0) {
		drm_err(&i915->drm, "Failed to add PXP component (%d)\n", ret);
		goto out_free;
	}

	pxp->pxp_component_added = true;

	return 0;

out_free:
	free_streaming_command(pxp);
	return ret;
}

void intel_pxp_tee_component_fini(struct intel_pxp *pxp)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;

	if (!pxp->pxp_component_added)
		return;

	component_del(i915->drm.dev, &i915_pxp_tee_component_ops);
	pxp->pxp_component_added = false;

	free_streaming_command(pxp);
}

int intel_pxp_tee_cmd_create_arb_session(struct intel_pxp *pxp,
					 int arb_session_id)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct pxp42_create_arb_in msg_in = {0};
	struct pxp42_create_arb_out msg_out = {0};
	int ret;

	msg_in.header.api_version = PXP_APIVER(4, 2);
	msg_in.header.command_id = PXP42_CMDID_INIT_SESSION;
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);
	msg_in.protection_mode = PXP42_ARB_SESSION_MODE_HEAVY;
	msg_in.session_id = arb_session_id;

	ret = intel_pxp_tee_io_message(pxp,
				       &msg_in, sizeof(msg_in),
				       &msg_out, sizeof(msg_out),
				       NULL);

	if (ret) {
		drm_err(&i915->drm, "Failed to send tee msg init arb session, ret=[%d]\n", ret);
	} else if (msg_out.header.status != 0) {
		if (is_fw_err_platform_config(msg_out.header.status)) {
			drm_info_once(&i915->drm,
				      "PXP init-arb-session-%d failed due to BIOS/SOC:0x%08x:%s\n",
				      arb_session_id, msg_out.header.status,
				      fw_err_to_string(msg_out.header.status));
		} else {
			drm_dbg(&i915->drm, "PXP init-arb-session--%d failed 0x%08x:%st:\n",
				arb_session_id, msg_out.header.status,
				fw_err_to_string(msg_out.header.status));
			drm_dbg(&i915->drm, "     cmd-detail: ID=[0x%08x],API-Ver-[0x%08x]\n",
				msg_in.header.command_id, msg_in.header.api_version);
		}
	}

	return ret;
}

void intel_pxp_tee_end_arb_fw_session(struct intel_pxp *pxp, u32 session_id)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct pxp42_inv_stream_key_in msg_in = {0};
	struct pxp42_inv_stream_key_out msg_out = {0};
	int ret, trials = 0;

try_again:
	memset(&msg_in, 0, sizeof(msg_in));
	memset(&msg_out, 0, sizeof(msg_out));
	msg_in.header.api_version = PXP_APIVER(4, 2);
	msg_in.header.command_id = PXP42_CMDID_INVALIDATE_STREAM_KEY;
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);

	msg_in.header.stream_id = FIELD_PREP(PXP_CMDHDR_EXTDATA_SESSION_VALID, 1);
	msg_in.header.stream_id |= FIELD_PREP(PXP_CMDHDR_EXTDATA_APP_TYPE, 0);
	msg_in.header.stream_id |= FIELD_PREP(PXP_CMDHDR_EXTDATA_SESSION_ID, session_id);

	ret = intel_pxp_tee_io_message(pxp,
				       &msg_in, sizeof(msg_in),
				       &msg_out, sizeof(msg_out),
				       NULL);

	/* Cleanup coherency between GT and Firmware is critical, so try again if it fails */
	if ((ret || msg_out.header.status != 0x0) && ++trials < 3)
		goto try_again;

	if (ret) {
		drm_err(&i915->drm, "Failed to send tee msg for inv-stream-key-%u, ret=[%d]\n",
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
