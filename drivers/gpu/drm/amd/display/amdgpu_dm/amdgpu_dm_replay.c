/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "amdgpu_dm_replay.h"
#include "dc_dmub_srv.h"
#include "dc.h"
#include "dm_helpers.h"
#include "amdgpu_dm.h"
#include "modules/power/power_helpers.h"
#include "dmub/inc/dmub_cmd.h"
#include "dc/inc/link.h"

/*
 * amdgpu_dm_link_supports_replay() - check if the link supports replay
 * @link: link
 * @aconnector: aconnector
 *
 */
bool amdgpu_dm_link_supports_replay(struct dc_link *link, struct amdgpu_dm_connector *aconnector)
{
	struct dm_connector_state *state = to_dm_connector_state(aconnector->base.state);
	struct dpcd_caps *dpcd_caps = &link->dpcd_caps;
	struct adaptive_sync_caps *as_caps = &link->dpcd_caps.adaptive_sync_caps;

	if (!state->freesync_capable)
		return false;

	if (!aconnector->vsdb_info.replay_mode)
		return false;

	// Check the eDP version
	if (dpcd_caps->edp_rev < EDP_REVISION_13)
		return false;

	if (!dpcd_caps->alpm_caps.bits.AUX_WAKE_ALPM_CAP)
		return false;

	// Check adaptive sync support cap
	if (!as_caps->dp_adap_sync_caps.bits.ADAPTIVE_SYNC_SDP_SUPPORT)
		return false;

	// Sink shall populate line deviation information
	if (dpcd_caps->pr_info.pixel_deviation_per_line == 0 ||
		dpcd_caps->pr_info.max_deviation_line == 0)
		return false;

	return true;
}

/*
 * amdgpu_dm_set_replay_caps() - setup Replay capabilities
 * @link: link
 * @aconnector: aconnector
 *
 */
bool amdgpu_dm_set_replay_caps(struct dc_link *link, struct amdgpu_dm_connector *aconnector)
{
	struct replay_config pr_config = { 0 };
	union replay_debug_flags *debug_flags = NULL;
	struct dc *dc = link->ctx->dc;

	// If Replay is already set to support, return true to skip checks
	if (link->replay_settings.config.replay_supported)
		return true;

	if (!dc_is_embedded_signal(link->connector_signal))
		return false;

	if (link->panel_config.psr.disallow_replay)
		return false;

	if (!amdgpu_dm_link_supports_replay(link, aconnector))
		return false;

	if (!dc->ctx->dmub_srv || !dc->ctx->dmub_srv->dmub ||
		!dc->ctx->dmub_srv->dmub->feature_caps.replay_supported)
		return false;

	// Mark Replay is supported in pr_config
	pr_config.replay_supported = true;

	debug_flags = (union replay_debug_flags *)&pr_config.debug_flags;
	debug_flags->u32All = 0;
	debug_flags->bitfields.visual_confirm =
		link->ctx->dc->debug.visual_confirm == VISUAL_CONFIRM_REPLAY;

	init_replay_config(link, &pr_config);

	return true;
}

/*
 * amdgpu_dm_link_setup_replay() - configure replay link
 * @link: link
 * @aconnector: aconnector
 *
 */
bool amdgpu_dm_link_setup_replay(struct dc_link *link, struct amdgpu_dm_connector *aconnector)
{
	struct replay_config *pr_config;

	if (link == NULL || aconnector == NULL)
		return false;

	pr_config = &link->replay_settings.config;

	if (!pr_config->replay_supported)
		return false;

	pr_config->replay_power_opt_supported = 0x11;
	pr_config->replay_smu_opt_supported = false;
	pr_config->replay_enable_option |= pr_enable_option_static_screen;
	pr_config->replay_support_fast_resync_in_ultra_sleep_mode = aconnector->max_vfreq >= 2 * aconnector->min_vfreq;
	pr_config->replay_timing_sync_supported = false;

	if (!pr_config->replay_timing_sync_supported)
		pr_config->replay_enable_option &= ~pr_enable_option_general_ui;

	link->replay_settings.replay_feature_enabled = true;

	return true;
}

/*
 * amdgpu_dm_replay_enable() - enable replay f/w
 * @stream: stream state
 *
 * Return: true if success
 */
bool amdgpu_dm_replay_enable(struct dc_stream_state *stream, bool wait)
{
	bool replay_active = true;
	struct dc_link *link = NULL;

	if (stream == NULL)
		return false;

	link = stream->link;

	if (link) {
		link->dc->link_srv->edp_setup_replay(link, stream);
		link->dc->link_srv->edp_set_coasting_vtotal(link, stream->timing.v_total);
		DRM_DEBUG_DRIVER("Enabling replay...\n");
		link->dc->link_srv->edp_set_replay_allow_active(link, &replay_active, wait, false, NULL);
		return true;
	}

	return false;
}

/*
 * amdgpu_dm_replay_disable() - disable replay f/w
 * @stream:  stream state
 *
 * Return: true if success
 */
bool amdgpu_dm_replay_disable(struct dc_stream_state *stream)
{
	bool replay_active = false;
	struct dc_link *link = NULL;

	if (stream == NULL)
		return false;

	link = stream->link;

	if (link) {
		DRM_DEBUG_DRIVER("Disabling replay...\n");
		link->dc->link_srv->edp_set_replay_allow_active(stream->link, &replay_active, true, false, NULL);
		return true;
	}

	return false;
}

/*
 * amdgpu_dm_replay_disable_all() - disable replay f/w
 * if replay is enabled on any stream
 *
 * Return: true if success
 */
bool amdgpu_dm_replay_disable_all(struct amdgpu_display_manager *dm)
{
	DRM_DEBUG_DRIVER("Disabling replay if replay is enabled on any stream\n");
	return dc_set_replay_allow_active(dm->dc, false);
}
