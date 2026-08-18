// SPDX-License-Identifier: MIT
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
#include "amdgpu.h"
#include "dc_dmub_srv.h"
#include "dc.h"
#include "amdgpu_dm.h"
#include "modules/power/power_helpers.h"
#include "amdgpu_dm_kunit_helpers.h"


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

	/* Temporarily disable PSR-SU to avoid glitches */
	return false;
}

STATIC_IFN_KUNIT
void amdgpu_dm_psr_fill_caps(struct dc_link *link, struct psr_caps *caps)
{
	struct dpcd_caps *dpcd_caps = &link->dpcd_caps;
	unsigned int power_opts = 0;

	if (amdgpu_dc_feature_mask & DC_PSR_ALLOW_SMU_OPT)
		power_opts |= psr_power_opt_smu_opt_static_screen;
	power_opts |= psr_power_opt_z10_static_screen;

	if (link->psr_settings.psr_version == DC_PSR_VERSION_1)
		caps->psr_version = 1;
	else if (link->psr_settings.psr_version == DC_PSR_VERSION_SU_1)
		caps->psr_version = 2;

	caps->psr_rfb_setup_time = (6 - dpcd_caps->psr_info.psr_dpcd_caps.bits.PSR_SETUP_TIME) * 55;
	caps->psr_exit_link_training_required =
		!dpcd_caps->psr_info.psr_dpcd_caps.bits.LINK_TRAINING_ON_EXIT_NOT_REQUIRED;
	caps->edp_revision = dpcd_caps->edp_rev;
	caps->support_ver = dpcd_caps->psr_info.psr_version;
	caps->su_granularity_required =
		dpcd_caps->psr_info.psr_dpcd_caps.bits.SU_GRANULARITY_REQUIRED;
	caps->y_coordinate_required = dpcd_caps->psr_info.psr_dpcd_caps.bits.Y_COORDINATE_REQUIRED;
	caps->su_y_granularity = dpcd_caps->psr_info.psr2_su_y_granularity_cap;
	caps->alpm_cap = dpcd_caps->alpm_caps.bits.AUX_WAKE_ALPM_CAP;
	caps->standby_support = dpcd_caps->alpm_caps.bits.PM_STATE_2A_SUPPORT;
	caps->rate_control_caps = 0; /* TODO: read in rc caps from aux */
	caps->psr_power_opt_flag = power_opts;
}
EXPORT_IF_KUNIT(amdgpu_dm_psr_fill_caps);

/*
 * amdgpu_dm_set_psr_caps() - set link psr capabilities
 * @link: link
 * @aconnector: amdgpu_dm_connector
 */
bool amdgpu_dm_set_psr_caps(struct dc_link *link, struct amdgpu_dm_connector *aconnector)
{
	struct dc *dc;
	unsigned int panel_inst = 0;

	if (!link || !aconnector)
		return false;

	dc = link->ctx->dc;

	/* Reset psr version first */
	link->psr_settings.psr_version = DC_PSR_VERSION_UNSUPPORTED;

	if (!dc->caps.dmub_caps.psr)
		return false;

	if (!(link->connector_signal & SIGNAL_TYPE_EDP))
		return false;

	if (link->type == dc_connection_none)
		return false;

	if (link->dpcd_caps.psr_info.psr_version == 0)
		return false;

	/*disable allow psr/psrsu/replay on eDP1*/
	if (dc_get_edp_link_panel_inst(dc, link, &panel_inst) && panel_inst == 1)
		return false;

	if (link_supports_psrsu(link))
		link->psr_settings.psr_version = DC_PSR_VERSION_SU_1;
	else
		link->psr_settings.psr_version = DC_PSR_VERSION_1;

	amdgpu_dm_psr_fill_caps(link, &aconnector->psr_caps);
	return true;
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

	for (i = 0; i < dm->dc->current_state->stream_count; i++) {
		const struct dc_link *link = dm->dc->current_state->streams[i]->link;

		if (!link)
			continue;

		if (link->psr_settings.psr_feature_enabled && link->psr_settings.psr_allow_active)
			return true;
	}
	return false;
}

/*
 * amdgpu_dm_psr_set_event() - set or clear PSR event for stream
 * @dm: pointer to amdgpu_display_manager
 * @stream: pointer to dc_stream_state
 * @set_event: true to set event, false to clear event
 * @event: PSR event type
 * @wait_for_disable: whether to wait for PSR to be disabled
 *
 * Return: true if successful, false otherwise
 */
bool amdgpu_dm_psr_set_event(struct amdgpu_display_manager *dm, struct dc_stream_state *stream,
		bool set_event, enum psr_event event, bool wait_for_disable)
{
	unsigned int psr_events;

	/* Validate all required parameters */
	if (!stream || !stream->link ||
		!stream->link->psr_settings.psr_feature_enabled)
		return false;

	/* Get current psr events */
	if (!mod_power_get_psr_event(dm->power_module, stream, &psr_events))
		return false;

	/* If all events already in desired state, return true. */
	if ((psr_events & event) == (set_event ? event : 0))
		return true;

	return mod_power_set_psr_event(dm->power_module, stream,
				       set_event, event, wait_for_disable);
}
EXPORT_IF_KUNIT(amdgpu_dm_psr_set_event);
