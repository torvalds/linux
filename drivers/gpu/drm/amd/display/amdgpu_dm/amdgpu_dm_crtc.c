// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
#include <drm/drm_vblank.h>
#include <drm/drm_atomic_helper.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_dm_psr.h"
#include "amdgpu_dm_replay.h"
#include "amdgpu_dm_crtc.h"
#include "amdgpu_dm_plane.h"
#include "amdgpu_dm_trace.h"
#include "amdgpu_dm_debugfs.h"

void amdgpu_dm_crtc_handle_vblank(struct amdgpu_crtc *acrtc)
{
	struct drm_crtc *crtc = &acrtc->base;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	drm_crtc_handle_vblank(crtc);

	spin_lock_irqsave(&dev->event_lock, flags);

	/* Send completion event for cursor-only commits */
	if (acrtc->event && acrtc->pflip_status != AMDGPU_FLIP_SUBMITTED) {
		drm_crtc_send_vblank_event(crtc, acrtc->event);
		drm_crtc_vblank_put(crtc);
		acrtc->event = NULL;
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

bool amdgpu_dm_crtc_modeset_required(struct drm_crtc_state *crtc_state,
			     struct dc_stream_state *new_stream,
			     struct dc_stream_state *old_stream)
{
	return crtc_state->active && drm_atomic_crtc_needs_modeset(crtc_state);
}

bool amdgpu_dm_crtc_vrr_active_irq(struct amdgpu_crtc *acrtc)

{
	return acrtc->dm_irq_params.freesync_config.state ==
		       VRR_STATE_ACTIVE_VARIABLE ||
	       acrtc->dm_irq_params.freesync_config.state ==
		       VRR_STATE_ACTIVE_FIXED;
}

int amdgpu_dm_crtc_set_vupdate_irq(struct drm_crtc *crtc, bool enable)
{
	enum dc_irq_source irq_source;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);
	int rc;

	if (acrtc->otg_inst == -1)
		return 0;

	irq_source = IRQ_TYPE_VUPDATE + acrtc->otg_inst;

	rc = dc_interrupt_set(adev->dm.dc, irq_source, enable) ? 0 : -EBUSY;

	DRM_DEBUG_VBL("crtc %d - vupdate irq %sabling: r=%d\n",
		      acrtc->crtc_id, enable ? "en" : "dis", rc);
	return rc;
}

bool amdgpu_dm_crtc_vrr_active(struct dm_crtc_state *dm_state)
{
	return dm_state->freesync_config.state == VRR_STATE_ACTIVE_VARIABLE ||
	       dm_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED;
}

static void amdgpu_dm_crtc_vblank_control_worker(struct work_struct *work)
{
	struct vblank_control_work *vblank_work =
		container_of(work, struct vblank_control_work, work);
	struct amdgpu_display_manager *dm = vblank_work->dm;

	mutex_lock(&dm->dc_lock);

	if (vblank_work->enable)
		dm->active_vblank_irq_count++;
	else if (dm->active_vblank_irq_count)
		dm->active_vblank_irq_count--;

	dc_allow_idle_optimizations(dm->dc, dm->active_vblank_irq_count == 0);

	DRM_DEBUG_KMS("Allow idle optimizations (MALL): %d\n", dm->active_vblank_irq_count == 0);

	/*
	 * Control PSR based on vblank requirements from OS
	 *
	 * If panel supports PSR SU, there's no need to disable PSR when OS is
	 * submitting fast atomic commits (we infer this by whether the OS
	 * requests vblank events). Fast atomic commits will simply trigger a
	 * full-frame-update (FFU); a specific case of selective-update (SU)
	 * where the SU region is the full hactive*vactive region. See
	 * fill_dc_dirty_rects().
	 */
	if (vblank_work->stream && vblank_work->stream->link) {
		/*
		 * Prioritize replay, instead of psr
		 */
		if (vblank_work->stream->link->replay_settings.replay_feature_enabled)
			amdgpu_dm_replay_enable(vblank_work->stream, false);
		else if (vblank_work->enable) {
			if (vblank_work->stream->link->psr_settings.psr_version < DC_PSR_VERSION_SU_1 &&
			    vblank_work->stream->link->psr_settings.psr_allow_active)
				amdgpu_dm_psr_disable(vblank_work->stream);
		} else if (vblank_work->stream->link->psr_settings.psr_feature_enabled &&
			   !vblank_work->stream->link->psr_settings.psr_allow_active &&
#ifdef CONFIG_DRM_AMD_SECURE_DISPLAY
			   !amdgpu_dm_crc_window_is_activated(&vblank_work->acrtc->base) &&
#endif
			   vblank_work->stream->link->panel_config.psr.disallow_replay &&
			   vblank_work->acrtc->dm_irq_params.allow_psr_entry) {
			amdgpu_dm_psr_enable(vblank_work->stream);
		}
	}

	mutex_unlock(&dm->dc_lock);

	dc_stream_release(vblank_work->stream);

	kfree(vblank_work);
}

static inline int amdgpu_dm_crtc_set_vblank(struct drm_crtc *crtc, bool enable)
{
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);
	struct dm_crtc_state *acrtc_state = to_dm_crtc_state(crtc->state);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct vblank_control_work *work;
	int rc = 0;

	if (acrtc->otg_inst == -1)
		goto skip;

	if (enable) {
		/* vblank irq on -> Only need vupdate irq in vrr mode */
		if (amdgpu_dm_crtc_vrr_active(acrtc_state))
			rc = amdgpu_dm_crtc_set_vupdate_irq(crtc, true);
	} else {
		/* vblank irq off -> vupdate irq off */
		rc = amdgpu_dm_crtc_set_vupdate_irq(crtc, false);
	}

	if (rc)
		return rc;

	rc = (enable)
		? amdgpu_irq_get(adev, &adev->crtc_irq, acrtc->crtc_id)
		: amdgpu_irq_put(adev, &adev->crtc_irq, acrtc->crtc_id);

	if (rc)
		return rc;

skip:
	if (amdgpu_in_reset(adev))
		return 0;

	if (dm->vblank_control_workqueue) {
		work = kzalloc(sizeof(*work), GFP_ATOMIC);
		if (!work)
			return -ENOMEM;

		INIT_WORK(&work->work, amdgpu_dm_crtc_vblank_control_worker);
		work->dm = dm;
		work->acrtc = acrtc;
		work->enable = enable;

		if (acrtc_state->stream) {
			dc_stream_retain(acrtc_state->stream);
			work->stream = acrtc_state->stream;
		}

		queue_work(dm->vblank_control_workqueue, &work->work);
	}

	return 0;
}

int amdgpu_dm_crtc_enable_vblank(struct drm_crtc *crtc)
{
	return amdgpu_dm_crtc_set_vblank(crtc, true);
}

void amdgpu_dm_crtc_disable_vblank(struct drm_crtc *crtc)
{
	amdgpu_dm_crtc_set_vblank(crtc, false);
}

static void amdgpu_dm_crtc_destroy_state(struct drm_crtc *crtc,
				  struct drm_crtc_state *state)
{
	struct dm_crtc_state *cur = to_dm_crtc_state(state);

	/* TODO Destroy dc_stream objects are stream object is flattened */
	if (cur->stream)
		dc_stream_release(cur->stream);


	__drm_atomic_helper_crtc_destroy_state(state);


	kfree(state);
}

static struct drm_crtc_state *amdgpu_dm_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct dm_crtc_state *state, *cur;

	cur = to_dm_crtc_state(crtc->state);

	if (WARN_ON(!crtc->state))
		return NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &state->base);

	if (cur->stream) {
		state->stream = cur->stream;
		dc_stream_retain(state->stream);
	}

	state->active_planes = cur->active_planes;
	state->vrr_infopacket = cur->vrr_infopacket;
	state->abm_level = cur->abm_level;
	state->vrr_supported = cur->vrr_supported;
	state->freesync_config = cur->freesync_config;
	state->cm_has_degamma = cur->cm_has_degamma;
	state->cm_is_degamma_srgb = cur->cm_is_degamma_srgb;
	state->regamma_tf = cur->regamma_tf;
	state->crc_skip_count = cur->crc_skip_count;
	state->mpo_requested = cur->mpo_requested;
	/* TODO Duplicate dc_stream after objects are stream object is flattened */

	return &state->base;
}

static void amdgpu_dm_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static void amdgpu_dm_crtc_reset_state(struct drm_crtc *crtc)
{
	struct dm_crtc_state *state;

	if (crtc->state)
		amdgpu_dm_crtc_destroy_state(crtc, crtc->state);

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (WARN_ON(!state))
		return;

	__drm_atomic_helper_crtc_reset(crtc, &state->base);
}

#ifdef CONFIG_DEBUG_FS
static int amdgpu_dm_crtc_late_register(struct drm_crtc *crtc)
{
	crtc_debugfs_init(crtc);

	return 0;
}
#endif

#ifdef AMD_PRIVATE_COLOR
/**
 * dm_crtc_additional_color_mgmt - enable additional color properties
 * @crtc: DRM CRTC
 *
 * This function lets the driver enable post-blending CRTC regamma transfer
 * function property in addition to DRM CRTC gamma LUT. Default value means
 * linear transfer function, which is the default CRTC gamma LUT behaviour
 * without this property.
 */
static void
dm_crtc_additional_color_mgmt(struct drm_crtc *crtc)
{
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);

	if(adev->dm.dc->caps.color.mpc.ogam_ram)
		drm_object_attach_property(&crtc->base,
					   adev->mode_info.regamma_tf_property,
					   AMDGPU_TRANSFER_FUNCTION_DEFAULT);
}

static int
amdgpu_dm_atomic_crtc_set_property(struct drm_crtc *crtc,
				   struct drm_crtc_state *state,
				   struct drm_property *property,
				   uint64_t val)
{
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);
	struct dm_crtc_state *acrtc_state = to_dm_crtc_state(state);

	if (property == adev->mode_info.regamma_tf_property) {
		if (acrtc_state->regamma_tf != val) {
			acrtc_state->regamma_tf = val;
			acrtc_state->base.color_mgmt_changed |= 1;
		}
	} else {
		drm_dbg_atomic(crtc->dev,
			       "[CRTC:%d:%s] unknown property [PROP:%d:%s]]\n",
			       crtc->base.id, crtc->name,
			       property->base.id, property->name);
		return -EINVAL;
	}

	return 0;
}

static int
amdgpu_dm_atomic_crtc_get_property(struct drm_crtc *crtc,
				   const struct drm_crtc_state *state,
				   struct drm_property *property,
				   uint64_t *val)
{
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);
	struct dm_crtc_state *acrtc_state = to_dm_crtc_state(state);

	if (property == adev->mode_info.regamma_tf_property)
		*val = acrtc_state->regamma_tf;
	else
		return -EINVAL;

	return 0;
}
#endif

/* Implemented only the options currently available for the driver */
static const struct drm_crtc_funcs amdgpu_dm_crtc_funcs = {
	.reset = amdgpu_dm_crtc_reset_state,
	.destroy = amdgpu_dm_crtc_destroy,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = amdgpu_dm_crtc_duplicate_state,
	.atomic_destroy_state = amdgpu_dm_crtc_destroy_state,
	.set_crc_source = amdgpu_dm_crtc_set_crc_source,
	.verify_crc_source = amdgpu_dm_crtc_verify_crc_source,
	.get_crc_sources = amdgpu_dm_crtc_get_crc_sources,
	.get_vblank_counter = amdgpu_get_vblank_counter_kms,
	.enable_vblank = amdgpu_dm_crtc_enable_vblank,
	.disable_vblank = amdgpu_dm_crtc_disable_vblank,
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp,
#if defined(CONFIG_DEBUG_FS)
	.late_register = amdgpu_dm_crtc_late_register,
#endif
#ifdef AMD_PRIVATE_COLOR
	.atomic_set_property = amdgpu_dm_atomic_crtc_set_property,
	.atomic_get_property = amdgpu_dm_atomic_crtc_get_property,
#endif
};

static void amdgpu_dm_crtc_helper_disable(struct drm_crtc *crtc)
{
}

static int amdgpu_dm_crtc_count_crtc_active_planes(struct drm_crtc_state *new_crtc_state)
{
	struct drm_atomic_state *state = new_crtc_state->state;
	struct drm_plane *plane;
	int num_active = 0;

	drm_for_each_plane_mask(plane, state->dev, new_crtc_state->plane_mask) {
		struct drm_plane_state *new_plane_state;

		/* Cursor planes are "fake". */
		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		new_plane_state = drm_atomic_get_new_plane_state(state, plane);

		if (!new_plane_state) {
			/*
			 * The plane is enable on the CRTC and hasn't changed
			 * state. This means that it previously passed
			 * validation and is therefore enabled.
			 */
			num_active += 1;
			continue;
		}

		/* We need a framebuffer to be considered enabled. */
		num_active += (new_plane_state->fb != NULL);
	}

	return num_active;
}

static void amdgpu_dm_crtc_update_crtc_active_planes(struct drm_crtc *crtc,
						     struct drm_crtc_state *new_crtc_state)
{
	struct dm_crtc_state *dm_new_crtc_state =
		to_dm_crtc_state(new_crtc_state);

	dm_new_crtc_state->active_planes = 0;

	if (!dm_new_crtc_state->stream)
		return;

	dm_new_crtc_state->active_planes =
		amdgpu_dm_crtc_count_crtc_active_planes(new_crtc_state);
}

static bool amdgpu_dm_crtc_helper_mode_fixup(struct drm_crtc *crtc,
				      const struct drm_display_mode *mode,
				      struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int amdgpu_dm_crtc_helper_atomic_check(struct drm_crtc *crtc,
					      struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
										crtc);
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);
	struct dc *dc = adev->dm.dc;
	struct dm_crtc_state *dm_crtc_state = to_dm_crtc_state(crtc_state);
	int ret = -EINVAL;

	trace_amdgpu_dm_crtc_atomic_check(crtc_state);

	amdgpu_dm_crtc_update_crtc_active_planes(crtc, crtc_state);

	if (WARN_ON(unlikely(!dm_crtc_state->stream &&
			amdgpu_dm_crtc_modeset_required(crtc_state, NULL, dm_crtc_state->stream)))) {
		return ret;
	}

	/*
	 * We require the primary plane to be enabled whenever the CRTC is, otherwise
	 * drm_mode_cursor_universal may end up trying to enable the cursor plane while all other
	 * planes are disabled, which is not supported by the hardware. And there is legacy
	 * userspace which stops using the HW cursor altogether in response to the resulting EINVAL.
	 */
	if (crtc_state->enable &&
		!(crtc_state->plane_mask & drm_plane_mask(crtc->primary))) {
		DRM_DEBUG_ATOMIC("Can't enable a CRTC without enabling the primary plane\n");
		return -EINVAL;
	}

	/*
	 * Only allow async flips for fast updates that don't change the FB
	 * pitch, the DCC state, rotation, etc.
	 */
	if (crtc_state->async_flip &&
	    dm_crtc_state->update_type != UPDATE_TYPE_FAST) {
		drm_dbg_atomic(crtc->dev,
			       "[CRTC:%d:%s] async flips are only supported for fast updates\n",
			       crtc->base.id, crtc->name);
		return -EINVAL;
	}

	/* In some use cases, like reset, no stream is attached */
	if (!dm_crtc_state->stream)
		return 0;

	if (dc_validate_stream(dc, dm_crtc_state->stream) == DC_OK)
		return 0;

	DRM_DEBUG_ATOMIC("Failed DC stream validation\n");
	return ret;
}

static const struct drm_crtc_helper_funcs amdgpu_dm_crtc_helper_funcs = {
	.disable = amdgpu_dm_crtc_helper_disable,
	.atomic_check = amdgpu_dm_crtc_helper_atomic_check,
	.mode_fixup = amdgpu_dm_crtc_helper_mode_fixup,
	.get_scanout_position = amdgpu_crtc_get_scanout_position,
};

int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			       struct drm_plane *plane,
			       uint32_t crtc_index)
{
	struct amdgpu_crtc *acrtc = NULL;
	struct drm_plane *cursor_plane;
	bool is_dcn;
	int res = -ENOMEM;

	cursor_plane = kzalloc(sizeof(*cursor_plane), GFP_KERNEL);
	if (!cursor_plane)
		goto fail;

	cursor_plane->type = DRM_PLANE_TYPE_CURSOR;
	res = amdgpu_dm_plane_init(dm, cursor_plane, 0, NULL);

	acrtc = kzalloc(sizeof(struct amdgpu_crtc), GFP_KERNEL);
	if (!acrtc)
		goto fail;

	res = drm_crtc_init_with_planes(
			dm->ddev,
			&acrtc->base,
			plane,
			cursor_plane,
			&amdgpu_dm_crtc_funcs, NULL);

	if (res)
		goto fail;

	drm_crtc_helper_add(&acrtc->base, &amdgpu_dm_crtc_helper_funcs);

	/* Create (reset) the plane state */
	if (acrtc->base.funcs->reset)
		acrtc->base.funcs->reset(&acrtc->base);

	acrtc->max_cursor_width = dm->adev->dm.dc->caps.max_cursor_size;
	acrtc->max_cursor_height = dm->adev->dm.dc->caps.max_cursor_size;

	acrtc->crtc_id = crtc_index;
	acrtc->base.enabled = false;
	acrtc->otg_inst = -1;

	dm->adev->mode_info.crtcs[crtc_index] = acrtc;

	/* Don't enable DRM CRTC degamma property for DCE since it doesn't
	 * support programmable degamma anywhere.
	 */
	is_dcn = dm->adev->dm.dc->caps.color.dpp.dcn_arch;
	drm_crtc_enable_color_mgmt(&acrtc->base, is_dcn ? MAX_COLOR_LUT_ENTRIES : 0,
				   true, MAX_COLOR_LUT_ENTRIES);

	drm_mode_crtc_set_gamma_size(&acrtc->base, MAX_COLOR_LEGACY_LUT_ENTRIES);

#ifdef AMD_PRIVATE_COLOR
	dm_crtc_additional_color_mgmt(&acrtc->base);
#endif
	return 0;

fail:
	kfree(acrtc);
	kfree(cursor_plane);
	return res;
}

