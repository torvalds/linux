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
#include "dc.h"
#include "dm_helpers.h"
#include "amdgpu_dm.h"
#include "modules/power/power_helpers.h"
#include "dmub/inc/dmub_cmd.h"
#include "dc/inc/link.h"

/*
 * link_supports_replay() - check if the link supports replay
 * @link: link
 * @aconnector: aconnector
 *
 */
static bool link_supports_replay(struct dc_link *link, struct amdgpu_dm_connector *aconnector)
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

	return true;
}

/*
 * amdgpu_dm_setup_replay() - setup replay configuration
 * @link: link
 * @aconnector: aconnector
 *
 */
bool amdgpu_dm_setup_replay(struct dc_link *link, struct amdgpu_dm_connector *aconnector)
{
	struct replay_config pr_config;
	union replay_debug_flags *debug_flags = NULL;

	// For eDP, if Replay is supported, return true to skip checks
	if (link->replay_settings.config.replay_supported)
		return true;

	if (!dc_is_embedded_signal(link->connector_signal))
		return false;

	if (link->panel_config.psr.disallow_replay)
		return false;

	if (!link_supports_replay(link, aconnector))
		return false;

	// Mark Replay is supported in link and update related attributes
	pr_config.replay_supported = true;
	pr_config.replay_power_opt_supported = 0;
	pr_config.replay_enable_option |= pr_enable_option_static_screen;
	pr_config.replay_timing_sync_supported = aconnector->max_vfreq >= 2 * aconnector->min_vfreq;

	if (!pr_config.replay_timing_sync_supported)
		pr_config.replay_enable_option &= ~pr_enable_option_general_ui;

	debug_flags = (union replay_debug_flags *)&pr_config.debug_flags;
	debug_flags->u32All = 0;
	debug_flags->bitfields.visual_confirm =
		link->ctx->dc->debug.visual_confirm == VISUAL_CONFIRM_REPLAY;

	link->replay_settings.replay_feature_enabled = true;

	init_replay_config(link, &pr_config);

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
	uint64_t state;
	unsigned int retry_count;
	bool replay_active = true;
	const unsigned int max_retry = 1000;
	bool force_static = true;
	struct dc_link *link = NULL;


	if (stream == NULL)
		return false;

	link = stream->link;

	if (link == NULL)
		return false;

	link->dc->link_srv->edp_setup_replay(link, stream);

	link->dc->link_srv->edp_set_replay_allow_active(link, NULL, false, false, NULL);

	link->dc->link_srv->edp_set_replay_allow_active(link, &replay_active, false, true, NULL);

	if (wait == true) {

		for (retry_count = 0; retry_count <= max_retry; retry_count++) {
			dc_link_get_replay_state(link, &state);
			if (replay_active) {
				if (state != REPLAY_STATE_0 &&
					(!force_static || state == REPLAY_STATE_3))
					break;
			} else {
				if (state == REPLAY_STATE_0)
					break;
			}
			udelay(500);
		}

		/* assert if max retry hit */
		if (retry_count >= max_retry)
			ASSERT(0);
	} else {
		/* To-do: Add trace log */
	}

	return true;
}

/*
 * amdgpu_dm_replay_disable() - disable replay f/w
 * @stream:  stream state
 *
 * Return: true if success
 */
bool amdgpu_dm_replay_disable(struct dc_stream_state *stream)
{

	if (stream->link) {
		DRM_DEBUG_DRIVER("Disabling replay...\n");
		stream->link->dc->link_srv->edp_set_replay_allow_active(stream->link, NULL, false, false, NULL);
		return true;
	}

	return false;
}
