/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "amdgpu_dm_psr.h"
#include "dc.h"
#include "dm_helpers.h"

/*
 * amdgpu_dm_set_psr_caps() - set link psr capabilities
 * @link: link
 *
 */
void amdgpu_dm_set_psr_caps(struct dc_link *link)
{
	uint8_t dpcd_data[EDP_PSR_RECEIVER_CAP_SIZE];

	if (!(link->connector_signal & SIGNAL_TYPE_EDP)) {
		link->psr_settings.psr_feature_enabled = false;
		return;
	}
	if (link->type == dc_connection_none) {
		link->psr_settings.psr_feature_enabled = false;
		return;
	}
	if (dm_helpers_dp_read_dpcd(NULL, link, DP_PSR_SUPPORT,
					dpcd_data, sizeof(dpcd_data))) {
		link->dpcd_caps.psr_caps.psr_version = dpcd_data[0];

		if (dpcd_data[0] == 0) {
			link->psr_settings.psr_version = DC_PSR_VERSION_UNSUPPORTED;
			link->psr_settings.psr_feature_enabled = false;
		} else {
			link->psr_settings.psr_version = DC_PSR_VERSION_1;
			link->psr_settings.psr_feature_enabled = true;
		}

		DRM_INFO("PSR support:%d\n", link->psr_settings.psr_feature_enabled);
	}
}

/*
 * amdgpu_dm_link_setup_psr() - configure psr link
 * @stream: stream state
 *
 * Return: true if success
 */
bool amdgpu_dm_link_setup_psr(struct dc_stream_state *stream)
{
	struct dc_link *link = NULL;
	struct psr_config psr_config = {0};
	struct psr_context psr_context = {0};
	bool ret = false;

	if (stream == NULL)
		return false;

	link = stream->link;

	if (link->psr_settings.psr_version != DC_PSR_VERSION_UNSUPPORTED) {
		psr_config.psr_version = link->psr_settings.psr_version;
		psr_config.psr_frame_capture_indication_req = 0;
		psr_config.psr_rfb_setup_time = 0x37;
		psr_config.psr_sdp_transmit_line_num_deadline = 0x20;
		psr_config.allow_smu_optimizations = 0x0;

		ret = dc_link_setup_psr(link, stream, &psr_config, &psr_context);

	}
	DRM_DEBUG_DRIVER("PSR link: %d\n",	link->psr_settings.psr_feature_enabled);

	return ret;
}

/*
 * amdgpu_dm_psr_enable() - enable psr f/w
 * @stream: stream state
 *
 * Return: true if success
 */
bool amdgpu_dm_psr_enable(struct dc_stream_state *stream)
{
	struct dc_link *link = stream->link;
	unsigned int vsync_rate_hz = 0;
	struct dc_static_screen_params params = {0};
	/* Calculate number of static frames before generating interrupt to
	 * enter PSR.
	 */
	// Init fail safe of 2 frames static
	unsigned int num_frames_static = 2;

	DRM_DEBUG_DRIVER("Enabling psr...\n");

	vsync_rate_hz = div64_u64(div64_u64((
			stream->timing.pix_clk_100hz * 100),
			stream->timing.v_total),
			stream->timing.h_total);

	/* Round up
	 * Calculate number of frames such that at least 30 ms of time has
	 * passed.
	 */
	if (vsync_rate_hz != 0) {
		unsigned int frame_time_microsec = 1000000 / vsync_rate_hz;
		num_frames_static = (30000 / frame_time_microsec) + 1;
	}

	params.triggers.cursor_update = true;
	params.triggers.overlay_update = true;
	params.triggers.surface_update = true;
	params.num_frames = num_frames_static;

	dc_stream_set_static_screen_params(link->ctx->dc,
					   &stream, 1,
					   &params);

	return dc_link_set_psr_allow_active(link, true, false, false);
}

/*
 * amdgpu_dm_psr_disable() - disable psr f/w
 * @stream:  stream state
 *
 * Return: true if success
 */
bool amdgpu_dm_psr_disable(struct dc_stream_state *stream)
{

	DRM_DEBUG_DRIVER("Disabling psr...\n");

	return dc_link_set_psr_allow_active(stream->link, false, true, false);
}

/*
 * amdgpu_dm_psr_disable() - disable psr f/w
 * if psr is enabled on any stream
 *
 * Return: true if success
 */
bool amdgpu_dm_psr_disable_all(struct amdgpu_display_manager *dm)
{
	DRM_DEBUG_DRIVER("Disabling psr if psr is enabled on any stream\n");
	return dc_set_psr_allow_active(dm->dc, false);
}

