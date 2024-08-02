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
#include "dc_dmub_srv.h"
#include "dc.h"
#include "dm_helpers.h"
#include "amdgpu_dm.h"
#include "modules/power/power_helpers.h"

static bool link_supports_psrsu(struct dc_link *link)
{
	struct dc *dc = link->ctx->dc;

	if (!dc->caps.dmcub_support)
		return false;

	if (dc->ctx->dce_version < DCN_VERSION_3_1)
		return false;

	if (!is_psr_su_specific_panel(link))
		return false;

	if (!link->dpcd_caps.alpm_caps.bits.AUX_WAKE_ALPM_CAP ||
	    !link->dpcd_caps.psr_info.psr_dpcd_caps.bits.Y_COORDINATE_REQUIRED)
		return false;

	if (link->dpcd_caps.psr_info.psr_dpcd_caps.bits.SU_GRANULARITY_REQUIRED &&
	    !link->dpcd_caps.psr_info.psr2_su_y_granularity_cap)
		return false;

	if (amdgpu_dc_debug_mask & DC_DISABLE_PSR_SU)
		return false;

	return dc_dmub_check_min_version(dc->ctx->dmub_srv->dmub);
}

/*
 * amdgpu_dm_set_psr_caps() - set link psr capabilities
 * @link: link
 *
 */
void amdgpu_dm_set_psr_caps(struct dc_link *link)
{
	if (!(link->connector_signal & SIGNAL_TYPE_EDP)) {
		link->psr_settings.psr_feature_enabled = false;
		return;
	}

	if (link->type == dc_connection_none) {
		link->psr_settings.psr_feature_enabled = false;
		return;
	}

	if (link->dpcd_caps.psr_info.psr_version == 0) {
		link->psr_settings.psr_version = DC_PSR_VERSION_UNSUPPORTED;
		link->psr_settings.psr_feature_enabled = false;

	} else {
		if (link_supports_psrsu(link))
			link->psr_settings.psr_version = DC_PSR_VERSION_SU_1;
		else
			link->psr_settings.psr_version = DC_PSR_VERSION_1;

		link->psr_settings.psr_feature_enabled = true;
	}

	DRM_INFO("PSR support %d, DC PSR ver %d, sink PSR ver %d DPCD caps 0x%x su_y_granularity %d\n",
		link->psr_settings.psr_feature_enabled,
		link->psr_settings.psr_version,
		link->dpcd_caps.psr_info.psr_version,
		link->dpcd_caps.psr_info.psr_dpcd_caps.raw,
		link->dpcd_caps.psr_info.psr2_su_y_granularity_cap);

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
	struct dc *dc = NULL;
	bool ret = false;

	if (stream == NULL)
		return false;

	link = stream->link;
	dc = link->ctx->dc;

	if (link->psr_settings.psr_version != DC_PSR_VERSION_UNSUPPORTED) {
		mod_power_calc_psr_configs(&psr_config, link, stream);

		/* linux DM specific updating for psr config fields */
		psr_config.allow_smu_optimizations =
			(amdgpu_dc_feature_mask & DC_PSR_ALLOW_SMU_OPT) &&
			mod_power_only_edp(dc->current_state, stream);
		psr_config.allow_multi_disp_optimizations =
			(amdgpu_dc_feature_mask & DC_PSR_ALLOW_MULTI_DISP_OPT);

		if (!psr_su_set_dsc_slice_height(dc, link, stream, &psr_config))
			return false;

		ret = dc_link_setup_psr(link, stream, &psr_config, &psr_context);

	}
	DRM_DEBUG_DRIVER("PSR link: %d\n",	link->psr_settings.psr_feature_enabled);

	return ret;
}

/*
 * amdgpu_dm_psr_enable() - enable psr f/w
 * @stream: stream state
 *
 */
void amdgpu_dm_psr_enable(struct dc_stream_state *stream)
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
			stream->timing.pix_clk_100hz * (uint64_t)100),
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

	/*
	 * Only enable static-screen optimizations for PSR1. For PSR SU, this
	 * causes vstartup interrupt issues, used by amdgpu_dm to send vblank
	 * events.
	 */
	if (link->psr_settings.psr_version < DC_PSR_VERSION_SU_1)
		power_opt |= psr_power_opt_z10_static_screen;

	dc_link_set_psr_allow_active(link, &psr_enable, false, false, &power_opt);

	if (link->ctx->dc->caps.ips_support)
		dc_allow_idle_optimizations(link->ctx->dc, true);
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
 * amdgpu_dm_psr_disable_all() - disable psr f/w for all streams
 * if psr is enabled on any stream
 *
 * Return: true if success
 */
bool amdgpu_dm_psr_disable_all(struct amdgpu_display_manager *dm)
{
	DRM_DEBUG_DRIVER("Disabling psr if psr is enabled on any stream\n");
	return dc_set_psr_allow_active(dm->dc, false);
}

/*
 * amdgpu_dm_psr_is_active_allowed() - check if psr is allowed on any stream
 * @dm:  pointer to amdgpu_display_manager
 *
 * Return: true if allowed
 */

bool amdgpu_dm_psr_is_active_allowed(struct amdgpu_display_manager *dm)
{
	unsigned int i;
	bool allow_active = false;

	for (i = 0; i < dm->dc->current_state->stream_count ; i++) {
		struct dc_link *link;
		struct dc_stream_state *stream = dm->dc->current_state->streams[i];

		link = stream->link;
		if (!link)
			continue;
		if (link->psr_settings.psr_feature_enabled &&
		    link->psr_settings.psr_allow_active) {
			allow_active = true;
			break;
		}
	}

	return allow_active;
}
