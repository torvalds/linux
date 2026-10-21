// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#include <drm/drm_atomic.h>
#include <drm/drm_modes.h>
#include <drm/drm_vblank.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_crtc.h"
#include "amdgpu_dm_psr.h"
#include "amdgpu_dm_replay.h"
#include "amdgpu_dm_freesync.h"
#include "dm_helpers.h"
#include "modules/inc/mod_freesync.h"

bool amdgpu_dm_is_dc_timing_adjust_needed(struct dm_crtc_state *old_state,
					  struct dm_crtc_state *new_state)
{
	if (new_state->stream->adjust.timing_adjust_pending)
		return true;
	if (new_state->freesync_config.state ==  VRR_STATE_ACTIVE_FIXED)
		return true;
	else if (amdgpu_dm_crtc_vrr_active(old_state) != amdgpu_dm_crtc_vrr_active(new_state))
		return true;
	else
		return false;
}
EXPORT_IF_KUNIT(amdgpu_dm_is_dc_timing_adjust_needed);

bool
amdgpu_dm_is_timing_unchanged_for_freesync(struct drm_crtc_state *old_crtc_state,
					   struct drm_crtc_state *new_crtc_state)
{
	const struct drm_display_mode *old_mode, *new_mode;

	if (!old_crtc_state || !new_crtc_state)
		return false;

	old_mode = &old_crtc_state->mode;
	new_mode = &new_crtc_state->mode;

	if (old_mode->clock       == new_mode->clock &&
	    old_mode->hdisplay    == new_mode->hdisplay &&
	    old_mode->vdisplay    == new_mode->vdisplay &&
	    old_mode->htotal      == new_mode->htotal &&
	    old_mode->vtotal      != new_mode->vtotal &&
	    old_mode->hsync_start == new_mode->hsync_start &&
	    old_mode->vsync_start != new_mode->vsync_start &&
	    old_mode->hsync_end   == new_mode->hsync_end &&
	    old_mode->vsync_end   != new_mode->vsync_end &&
	    old_mode->hskew       == new_mode->hskew &&
	    old_mode->vscan       == new_mode->vscan &&
	    (old_mode->vsync_end - old_mode->vsync_start) ==
	    (new_mode->vsync_end - new_mode->vsync_start))
		return true;

	return false;
}
EXPORT_IF_KUNIT(amdgpu_dm_is_timing_unchanged_for_freesync);

void amdgpu_dm_set_freesync_fixed_config(struct dm_crtc_state *dm_new_crtc_state)
{
	u64 num, den, res;
	struct drm_crtc_state *new_crtc_state = &dm_new_crtc_state->base;

	dm_new_crtc_state->freesync_config.state = VRR_STATE_ACTIVE_FIXED;

	num = (unsigned long long)new_crtc_state->mode.clock * 1000 * 1000000;
	den = (unsigned long long)new_crtc_state->mode.htotal *
	      (unsigned long long)new_crtc_state->mode.vtotal;

	res = div_u64(num, den);
	dm_new_crtc_state->freesync_config.fixed_refresh_in_uhz = res;
}
EXPORT_IF_KUNIT(amdgpu_dm_set_freesync_fixed_config);

void amdgpu_dm_reset_freesync_config_for_crtc(
	struct dm_crtc_state *new_crtc_state)
{
	new_crtc_state->vrr_supported = false;

	memset(&new_crtc_state->vrr_infopacket, 0,
	       sizeof(new_crtc_state->vrr_infopacket));
}
EXPORT_IF_KUNIT(amdgpu_dm_reset_freesync_config_for_crtc);

void amdgpu_dm_get_freesync_config_for_crtc(
	struct dm_crtc_state *new_crtc_state,
	struct dm_connector_state *new_con_state)
{
	struct mod_freesync_config config = {0};
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode *mode = &new_crtc_state->base.mode;
	int vrefresh = drm_mode_vrefresh(mode);
	bool fs_vid_mode = false;

	if (new_con_state->base.connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
		return;

	aconnector = to_amdgpu_dm_connector(new_con_state->base.connector);

	new_crtc_state->vrr_supported = new_con_state->freesync_capable &&
					vrefresh >= aconnector->min_vfreq &&
					vrefresh <= aconnector->max_vfreq;

	if (new_crtc_state->vrr_supported) {
		new_crtc_state->stream->ignore_msa_timing_param = true;
		fs_vid_mode = new_crtc_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED;

		config.min_refresh_in_uhz = aconnector->min_vfreq * 1000000;
		config.max_refresh_in_uhz = aconnector->max_vfreq * 1000000;
		config.vsif_supported = true;
		config.btr = true;

		if (fs_vid_mode) {
			config.state = VRR_STATE_ACTIVE_FIXED;
			config.fixed_refresh_in_uhz = new_crtc_state->freesync_config.fixed_refresh_in_uhz;
			goto out;
		} else if (new_crtc_state->base.vrr_enabled) {
			config.state = VRR_STATE_ACTIVE_VARIABLE;
		} else {
			config.state = VRR_STATE_INACTIVE;
		}
	} else {
		config.state = VRR_STATE_UNSUPPORTED;
	}
out:
	new_crtc_state->freesync_config = config;
}
EXPORT_IF_KUNIT(amdgpu_dm_get_freesync_config_for_crtc);

void amdgpu_dm_update_freesync_state_on_stream(
	struct amdgpu_display_manager *dm,
	struct dm_crtc_state *new_crtc_state,
	struct dc_stream_state *new_stream,
	struct dc_plane_state *surface,
	u32 flip_timestamp_in_us)
{
	struct mod_vrr_params vrr_params;
	struct dc_info_packet vrr_infopacket = {0};
	struct amdgpu_device *adev = dm->adev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(new_crtc_state->base.crtc);
	unsigned long flags;
	bool pack_sdp_v1_3 = false;
	struct amdgpu_dm_connector *aconn;
	enum vrr_packet_type packet_type = PACKET_TYPE_VRR;

	if (!new_stream)
		return;

	/*
	 * TODO: Determine why min/max totals and vrefresh can be 0 here.
	 * For now it's sufficient to just guard against these conditions.
	 */

	if (!new_stream->timing.h_total || !new_stream->timing.v_total)
		return;

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
	vrr_params = acrtc->dm_irq_params.vrr_params;

	if (surface) {
		mod_freesync_handle_preflip(
			dm->freesync_module,
			surface,
			new_stream,
			flip_timestamp_in_us,
			&vrr_params);

		if (adev->family < AMDGPU_FAMILY_AI &&
		    amdgpu_dm_crtc_vrr_active(new_crtc_state)) {
			mod_freesync_handle_v_update(dm->freesync_module,
						     new_stream, &vrr_params);

			/* Need to call this before the frame ends. */
			dc_stream_adjust_vmin_vmax(dm->dc,
						   new_crtc_state->stream,
						   &vrr_params.adjust);
		}
	}

	aconn = (struct amdgpu_dm_connector *)new_stream->dm_stream_context;

	if (aconn && (aconn->as_type == FREESYNC_TYPE_PCON_IN_WHITELIST || aconn->vsdb_info.replay_mode)) {
		pack_sdp_v1_3 = aconn->pack_sdp_v1_3;

		if (aconn->vsdb_info.amd_vsdb_version == 1)
			packet_type = PACKET_TYPE_FS_V1;
		else if (aconn->vsdb_info.amd_vsdb_version == 2)
			packet_type = PACKET_TYPE_FS_V2;
		else if (aconn->vsdb_info.amd_vsdb_version == 3)
			packet_type = PACKET_TYPE_FS_V3;

		mod_build_adaptive_sync_infopacket(new_stream, aconn->as_type, NULL,
					&new_stream->adaptive_sync_infopacket);
	}

	mod_freesync_build_vrr_infopacket(
		dm->freesync_module,
		new_stream,
		&vrr_params,
		packet_type,
		TRANSFER_FUNC_UNKNOWN,
		&vrr_infopacket,
		pack_sdp_v1_3);

	new_crtc_state->freesync_vrr_info_changed |=
		(memcmp(&new_crtc_state->vrr_infopacket,
			&vrr_infopacket,
			sizeof(vrr_infopacket)) != 0);

	acrtc->dm_irq_params.vrr_params = vrr_params;
	new_crtc_state->vrr_infopacket = vrr_infopacket;

	new_stream->vrr_infopacket = vrr_infopacket;
	new_stream->allow_freesync = mod_freesync_get_freesync_enabled(&vrr_params);

	if (new_crtc_state->freesync_vrr_info_changed)
		drm_dbg_kms(adev_to_drm(adev), "VRR packet update: crtc=%u enabled=%d state=%d",
			      new_crtc_state->base.crtc->base.id,
			      (int)new_crtc_state->base.vrr_enabled,
			      (int)vrr_params.state);

	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
}

void amdgpu_dm_update_stream_irq_parameters(
	struct amdgpu_display_manager *dm,
	struct dm_crtc_state *new_crtc_state)
{
	struct dc_stream_state *new_stream = new_crtc_state->stream;
	struct mod_vrr_params vrr_params;
	struct mod_freesync_config config = new_crtc_state->freesync_config;
	struct amdgpu_device *adev = dm->adev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(new_crtc_state->base.crtc);
	unsigned long flags;

	if (!new_stream)
		return;

	/*
	 * TODO: Determine why min/max totals and vrefresh can be 0 here.
	 * For now it's sufficient to just guard against these conditions.
	 */
	if (!new_stream->timing.h_total || !new_stream->timing.v_total)
		return;

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
	vrr_params = acrtc->dm_irq_params.vrr_params;

	if (new_crtc_state->vrr_supported &&
	    config.min_refresh_in_uhz &&
	    config.max_refresh_in_uhz) {
		/*
		 * if freesync compatible mode was set, config.state will be set
		 * in atomic check
		 */
		if (config.state == VRR_STATE_ACTIVE_FIXED && config.fixed_refresh_in_uhz &&
		    (!drm_atomic_crtc_needs_modeset(&new_crtc_state->base) ||
		     new_crtc_state->freesync_config.state == VRR_STATE_ACTIVE_FIXED)) {
			vrr_params.max_refresh_in_uhz = config.max_refresh_in_uhz;
			vrr_params.min_refresh_in_uhz = config.min_refresh_in_uhz;
			vrr_params.fixed_refresh_in_uhz = config.fixed_refresh_in_uhz;
			vrr_params.state = VRR_STATE_ACTIVE_FIXED;
		} else {
			config.state = new_crtc_state->base.vrr_enabled ?
						     VRR_STATE_ACTIVE_VARIABLE :
						     VRR_STATE_INACTIVE;
		}
	} else {
		config.state = VRR_STATE_UNSUPPORTED;
	}

	mod_freesync_build_vrr_params(dm->freesync_module,
				      new_stream,
				      &config, &vrr_params);

	new_crtc_state->freesync_config = config;
	/* Copy state for access from DM IRQ handler */
	acrtc->dm_irq_params.freesync_config = config;
	acrtc->dm_irq_params.active_planes = new_crtc_state->active_planes;
	acrtc->dm_irq_params.vrr_params = vrr_params;
	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
}

void amdgpu_dm_handle_vrr_transition(struct amdgpu_display_manager *dm,
				     struct dm_crtc_state *old_state,
				     struct dm_crtc_state *new_state)
{
	struct amdgpu_device *adev = dm->adev;
	bool old_vrr_active = amdgpu_dm_crtc_vrr_active(old_state);
	bool new_vrr_active = amdgpu_dm_crtc_vrr_active(new_state);

	/* Only DCE gates vupdate on VRR, keep it enabled for DCN */
	bool vrr_gates_vupdate = amdgpu_ip_version(adev, DCE_HWIP, 0) == 0;

	if (!old_vrr_active && new_vrr_active) {
		/* Transition VRR inactive -> active:
		 * While VRR is active, we must not disable vblank irq, as a
		 * reenable after disable would compute bogus vblank/pflip
		 * timestamps if it likely happened inside display front-porch.
		 *
		 * We also need vupdate irq for the actual core vblank handling
		 * at end of vblank.
		 */
		if (vrr_gates_vupdate)
			WARN_ON(amdgpu_dm_crtc_set_vupdate_irq(new_state->base.crtc, true) != 0);
		WARN_ON(drm_crtc_vblank_get(new_state->base.crtc) != 0);
		drm_dbg_driver(new_state->base.crtc->dev, "%s: crtc=%u VRR off->on: Get vblank ref\n",
				 __func__, new_state->base.crtc->base.id);

		scoped_guard(mutex, &dm->dc_lock) {
			dc_exit_ips_for_hw_access(dm->dc);
			amdgpu_dm_psr_set_event(dm, new_state->stream, true,
				psr_event_vrr_transition, true);
			amdgpu_dm_replay_set_event(dm, new_state->stream, true,
				replay_event_vrr, true);
		}
	} else if (old_vrr_active && !new_vrr_active) {
		/* Transition VRR active -> inactive:
		 * Allow vblank irq disable again for fixed refresh rate.
		 */
		if (vrr_gates_vupdate)
			WARN_ON(amdgpu_dm_crtc_set_vupdate_irq(new_state->base.crtc, false) != 0);
		drm_crtc_vblank_put(new_state->base.crtc);
		drm_dbg_driver(new_state->base.crtc->dev, "%s: crtc=%u VRR on->off: Drop vblank ref\n",
				 __func__, new_state->base.crtc->base.id);

		scoped_guard(mutex, &dm->dc_lock) {
			dc_exit_ips_for_hw_access(dm->dc);
			amdgpu_dm_psr_set_event(dm, new_state->stream, false,
				psr_event_vrr_transition, false);
			amdgpu_dm_replay_set_event(dm, new_state->stream, false,
				replay_event_vrr, false);
		}
	}
}
