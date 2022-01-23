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
#include "amdgpu_dm.h"

static bool link_get_psr_caps(struct dc_link *link)
{
	uint8_t psr_dpcd_data[EDP_PSR_RECEIVER_CAP_SIZE];
	uint8_t edp_rev_dpcd_data;



	if (!dm_helpers_dp_read_dpcd(NULL, link, DP_PSR_SUPPORT,
				    psr_dpcd_data, sizeof(psr_dpcd_data)))
		return false;

	if (!dm_helpers_dp_read_dpcd(NULL, link, DP_EDP_DPCD_REV,
				    &edp_rev_dpcd_data, sizeof(edp_rev_dpcd_data)))
		return false;

	link->dpcd_caps.psr_caps.psr_version = psr_dpcd_data[0];
	link->dpcd_caps.psr_caps.edp_revision = edp_rev_dpcd_data;

#ifdef CONFIG_DRM_AMD_DC_DCN
	if (link->dpcd_caps.psr_caps.psr_version > 0x1) {
		uint8_t alpm_dpcd_data;
		uint8_t su_granularity_dpcd_data;

		if (!dm_helpers_dp_read_dpcd(NULL, link, DP_RECEIVER_ALPM_CAP,
						&alpm_dpcd_data, sizeof(alpm_dpcd_data)))
			return false;

		if (!dm_helpers_dp_read_dpcd(NULL, link, DP_PSR2_SU_Y_GRANULARITY,
						&su_granularity_dpcd_data, sizeof(su_granularity_dpcd_data)))
			return false;

		link->dpcd_caps.psr_caps.y_coordinate_required = psr_dpcd_data[1] & DP_PSR2_SU_Y_COORDINATE_REQUIRED;
		link->dpcd_caps.psr_caps.su_granularity_required = psr_dpcd_data[1] & DP_PSR2_SU_GRANULARITY_REQUIRED;

		link->dpcd_caps.psr_caps.alpm_cap = alpm_dpcd_data & DP_ALPM_CAP;
		link->dpcd_caps.psr_caps.standby_support = alpm_dpcd_data & (1 << 1);

		link->dpcd_caps.psr_caps.su_y_granularity = su_granularity_dpcd_data;
	}
#endif
	return true;
}

#ifdef CONFIG_DRM_AMD_DC_DCN
static bool link_supports_psrsu(struct dc_link *link)
{
	struct dc *dc = link->ctx->dc;

	if (!dc->caps.dmcub_support)
		return false;

	if (dc->ctx->dce_version < DCN_VERSION_3_1)
		return false;

	if (!link->dpcd_caps.psr_caps.alpm_cap ||
	    !link->dpcd_caps.psr_caps.y_coordinate_required)
		return false;

	if (link->dpcd_caps.psr_caps.su_granularity_required &&
	    !link->dpcd_caps.psr_caps.su_y_granularity)
		return false;

	return true;
}
#endif

/*
 * amdgpu_dm_set_psr_caps() - set link psr capabilities
 * @link: link
 *
 */
void amdgpu_dm_set_psr_caps(struct dc_link *link)
{
	if (!(link->connector_signal & SIGNAL_TYPE_EDP))
		return;

	if (link->type == dc_connection_none)
		return;

	if (!link_get_psr_caps(link)) {
		DRM_ERROR("amdgpu: Failed to read PSR Caps!\n");
		return;
	}

	if (link->dpcd_caps.psr_caps.psr_version == 0) {
		link->psr_settings.psr_version = DC_PSR_VERSION_UNSUPPORTED;
		link->psr_settings.psr_feature_enabled = false;

	} else {
#ifdef CONFIG_DRM_AMD_DC_DCN
		if (link_supports_psrsu(link))
			link->psr_settings.psr_version = DC_PSR_VERSION_SU_1;
		else
#endif
			link->psr_settings.psr_version = DC_PSR_VERSION_1;

		link->psr_settings.psr_feature_enabled = true;
	}

	DRM_INFO("PSR support:%d\n", link->psr_settings.psr_feature_enabled);

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
	unsigned int power_opt = 0;
	bool psr_enable = true;

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

	power_opt |= psr_power_opt_z10_static_screen;

	return dc_link_set_psr_allow_active(link, &psr_enable, false, false, &power_opt);
}

/*
 * amdgpu_dm_psr_disable() - disable psr f/w
 * @stream:  stream state
 *
 * Return: true if success
 */
bool amdgpu_dm_psr_disable(struct dc_stream_state *stream)
{
	unsigned int power_opt = 0;
	bool psr_enable = false;

	DRM_DEBUG_DRIVER("Disabling psr...\n");

	return dc_link_set_psr_allow_active(stream->link, &psr_enable, true, false, &power_opt);
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

