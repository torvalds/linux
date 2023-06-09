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

/* FILE POLICY AND INTENDED USAGE:
 * This file implements retrieval and configuration of eDP panel features such
 * as PSR and ABM and it also manages specs defined eDP panel power sequences.
 */

#include "link_edp_panel_control.h"
#include "link_dpcd.h"
#include "link_dp_capability.h"
#include "dm_helpers.h"
#include "dal_asic_id.h"
#include "dce/dmub_psr.h"
#include "abm.h"
#define DC_LOGGER_INIT(logger)

/* Travis */
static const uint8_t DP_VGA_LVDS_CONVERTER_ID_2[] = "sivarT";
/* Nutmeg */
static const uint8_t DP_VGA_LVDS_CONVERTER_ID_3[] = "dnomlA";

void dp_set_panel_mode(struct dc_link *link, enum dp_panel_mode panel_mode)
{
	union dpcd_edp_config edp_config_set;
	bool panel_mode_edp = false;

	memset(&edp_config_set, '\0', sizeof(union dpcd_edp_config));

	if (panel_mode != DP_PANEL_MODE_DEFAULT) {

		switch (panel_mode) {
		case DP_PANEL_MODE_EDP:
		case DP_PANEL_MODE_SPECIAL:
			panel_mode_edp = true;
			break;

		default:
				break;
		}

		/*set edp panel mode in receiver*/
		core_link_read_dpcd(
			link,
			DP_EDP_CONFIGURATION_SET,
			&edp_config_set.raw,
			sizeof(edp_config_set.raw));

		if (edp_config_set.bits.PANEL_MODE_EDP
			!= panel_mode_edp) {
			enum dc_status result;

			edp_config_set.bits.PANEL_MODE_EDP =
			panel_mode_edp;
			result = core_link_write_dpcd(
				link,
				DP_EDP_CONFIGURATION_SET,
				&edp_config_set.raw,
				sizeof(edp_config_set.raw));

			ASSERT(result == DC_OK);
		}
	}
	link->panel_mode = panel_mode;
	DC_LOG_DETECTION_DP_CAPS("Link: %d eDP panel mode supported: %d "
		 "eDP panel mode enabled: %d \n",
		 link->link_index,
		 link->dpcd_caps.panel_mode_edp,
		 panel_mode_edp);
}

enum dp_panel_mode dp_get_panel_mode(struct dc_link *link)
{
	/* We need to explicitly check that connector
	 * is not DP. Some Travis_VGA get reported
	 * by video bios as DP.
	 */
	if (link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT) {

		switch (link->dpcd_caps.branch_dev_id) {
		case DP_BRANCH_DEVICE_ID_0022B9:
			/* alternate scrambler reset is required for Travis
			 * for the case when external chip does not
			 * provide sink device id, alternate scrambler
			 * scheme will  be overriden later by querying
			 * Encoder features
			 */
			if (strncmp(
				link->dpcd_caps.branch_dev_name,
				DP_VGA_LVDS_CONVERTER_ID_2,
				sizeof(
				link->dpcd_caps.
				branch_dev_name)) == 0) {
					return DP_PANEL_MODE_SPECIAL;
			}
			break;
		case DP_BRANCH_DEVICE_ID_00001A:
			/* alternate scrambler reset is required for Travis
			 * for the case when external chip does not provide
			 * sink device id, alternate scrambler scheme will
			 * be overriden later by querying Encoder feature
			 */
			if (strncmp(link->dpcd_caps.branch_dev_name,
				DP_VGA_LVDS_CONVERTER_ID_3,
				sizeof(
				link->dpcd_caps.
				branch_dev_name)) == 0) {
					return DP_PANEL_MODE_SPECIAL;
			}
			break;
		default:
			break;
		}
	}

	if (link->dpcd_caps.panel_mode_edp &&
		(link->connector_signal == SIGNAL_TYPE_EDP ||
		 (link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT &&
		  link->is_internal_display))) {
		return DP_PANEL_MODE_EDP;
	}

	return DP_PANEL_MODE_DEFAULT;
}

bool edp_set_backlight_level_nits(struct dc_link *link,
		bool isHDR,
		uint32_t backlight_millinits,
		uint32_t transition_time_in_ms)
{
	struct dpcd_source_backlight_set dpcd_backlight_set;
	uint8_t backlight_control = isHDR ? 1 : 0;

	if (!link || (link->connector_signal != SIGNAL_TYPE_EDP &&
			link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT))
		return false;

	// OLEDs have no PWM, they can only use AUX
	if (link->dpcd_sink_ext_caps.bits.oled == 1)
		backlight_control = 1;

	*(uint32_t *)&dpcd_backlight_set.backlight_level_millinits = backlight_millinits;
	*(uint16_t *)&dpcd_backlight_set.backlight_transition_time_ms = (uint16_t)transition_time_in_ms;


	if (core_link_write_dpcd(link, DP_SOURCE_BACKLIGHT_LEVEL,
			(uint8_t *)(&dpcd_backlight_set),
			sizeof(dpcd_backlight_set)) != DC_OK)
		return false;

	if (core_link_write_dpcd(link, DP_SOURCE_BACKLIGHT_CONTROL,
			&backlight_control, 1) != DC_OK)
		return false;

	return true;
}

bool edp_get_backlight_level_nits(struct dc_link *link,
		uint32_t *backlight_millinits_avg,
		uint32_t *backlight_millinits_peak)
{
	union dpcd_source_backlight_get dpcd_backlight_get;

	memset(&dpcd_backlight_get, 0, sizeof(union dpcd_source_backlight_get));

	if (!link || (link->connector_signal != SIGNAL_TYPE_EDP &&
			link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT))
		return false;

	if (!core_link_read_dpcd(link, DP_SOURCE_BACKLIGHT_CURRENT_PEAK,
			dpcd_backlight_get.raw,
			sizeof(union dpcd_source_backlight_get)))
		return false;

	*backlight_millinits_avg =
		dpcd_backlight_get.bytes.backlight_millinits_avg;
	*backlight_millinits_peak =
		dpcd_backlight_get.bytes.backlight_millinits_peak;

	/* On non-supported panels dpcd_read usually succeeds with 0 returned */
	if (*backlight_millinits_avg == 0 ||
			*backlight_millinits_avg > *backlight_millinits_peak)
		return false;

	return true;
}

bool edp_backlight_enable_aux(struct dc_link *link, bool enable)
{
	uint8_t backlight_enable = enable ? 1 : 0;

	if (!link || (link->connector_signal != SIGNAL_TYPE_EDP &&
		link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT))
		return false;

	if (core_link_write_dpcd(link, DP_SOURCE_BACKLIGHT_ENABLE,
		&backlight_enable, 1) != DC_OK)
		return false;

	return true;
}

// we read default from 0x320 because we expect BIOS wrote it there
// regular get_backlight_nit reads from panel set at 0x326
static bool read_default_bl_aux(struct dc_link *link, uint32_t *backlight_millinits)
{
	if (!link || (link->connector_signal != SIGNAL_TYPE_EDP &&
		link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT))
		return false;

	if (!core_link_read_dpcd(link, DP_SOURCE_BACKLIGHT_LEVEL,
		(uint8_t *) backlight_millinits,
		sizeof(uint32_t)))
		return false;

	return true;
}

bool set_default_brightness_aux(struct dc_link *link)
{
	uint32_t default_backlight;

	if (link && link->dpcd_sink_ext_caps.bits.oled == 1) {
		if (!read_default_bl_aux(link, &default_backlight))
			default_backlight = 150000;
		// if < 5 nits or > 5000, it might be wrong readback
		if (default_backlight < 5000 || default_backlight > 5000000)
			default_backlight = 150000; //

		return edp_set_backlight_level_nits(link, true,
				default_backlight, 0);
	}
	return false;
}

bool edp_is_ilr_optimization_required(struct dc_link *link,
		struct dc_crtc_timing *crtc_timing)
{
	struct dc_link_settings link_setting;
	uint8_t link_bw_set;
	uint8_t link_rate_set;
	uint32_t req_bw;
	union lane_count_set lane_count_set = {0};

	ASSERT(link || crtc_timing); // invalid input

	if (link->dpcd_caps.edp_supported_link_rates_count == 0 ||
			!link->panel_config.ilr.optimize_edp_link_rate)
		return false;


	// Read DPCD 00100h to find if standard link rates are set
	core_link_read_dpcd(link, DP_LINK_BW_SET,
				&link_bw_set, sizeof(link_bw_set));

	if (link_bw_set) {
		DC_LOG_EVENT_LINK_TRAINING("eDP ILR: Optimization required, VBIOS used link_bw_set\n");
		return true;
	}

	// Read DPCD 00115h to find the edp link rate set used
	core_link_read_dpcd(link, DP_LINK_RATE_SET,
			    &link_rate_set, sizeof(link_rate_set));

	// Read DPCD 00101h to find out the number of lanes currently set
	core_link_read_dpcd(link, DP_LANE_COUNT_SET,
				&lane_count_set.raw, sizeof(lane_count_set));

	req_bw = dc_bandwidth_in_kbps_from_timing(crtc_timing);

	if (!crtc_timing->flags.DSC)
		edp_decide_link_settings(link, &link_setting, req_bw);
	else
		decide_edp_link_settings_with_dsc(link, &link_setting, req_bw, LINK_RATE_UNKNOWN);

	if (link->dpcd_caps.edp_supported_link_rates[link_rate_set] != link_setting.link_rate ||
			lane_count_set.bits.LANE_COUNT_SET != link_setting.lane_count) {
		DC_LOG_EVENT_LINK_TRAINING("eDP ILR: Optimization required, VBIOS link_rate_set not optimal\n");
		return true;
	}

	DC_LOG_EVENT_LINK_TRAINING("eDP ILR: No optimization required, VBIOS set optimal link_rate_set\n");
	return false;
}

void edp_panel_backlight_power_on(struct dc_link *link, bool wait_for_hpd)
{
	if (link->connector_signal != SIGNAL_TYPE_EDP)
		return;

	link->dc->hwss.edp_power_control(link, true);
	if (wait_for_hpd)
		link->dc->hwss.edp_wait_for_hpd_ready(link, true);
	if (link->dc->hwss.edp_backlight_control)
		link->dc->hwss.edp_backlight_control(link, true);
}

bool edp_wait_for_t12(struct dc_link *link)
{
	if (link->connector_signal == SIGNAL_TYPE_EDP && link->dc->hwss.edp_wait_for_T12) {
		link->dc->hwss.edp_wait_for_T12(link);

		return true;
	}

	return false;
}

void edp_add_delay_for_T9(struct dc_link *link)
{
	if (link && link->panel_config.pps.extra_delay_backlight_off > 0)
		fsleep(link->panel_config.pps.extra_delay_backlight_off * 1000);
}

bool edp_receiver_ready_T9(struct dc_link *link)
{
	unsigned int tries = 0;
	unsigned char sinkstatus = 0;
	unsigned char edpRev = 0;
	enum dc_status result = DC_OK;

	result = core_link_read_dpcd(link, DP_EDP_DPCD_REV, &edpRev, sizeof(edpRev));

	/* start from eDP version 1.2, SINK_STAUS indicate the sink is ready.*/
	if (result == DC_OK && edpRev >= DP_EDP_12) {
		do {
			sinkstatus = 1;
			result = core_link_read_dpcd(link, DP_SINK_STATUS, &sinkstatus, sizeof(sinkstatus));
			if (sinkstatus == 0)
				break;
			if (result != DC_OK)
				break;
			udelay(100); //MAx T9
		} while (++tries < 50);
	}

	return result;
}

bool edp_receiver_ready_T7(struct dc_link *link)
{
	unsigned char sinkstatus = 0;
	unsigned char edpRev = 0;
	enum dc_status result = DC_OK;

	/* use absolute time stamp to constrain max T7*/
	unsigned long long enter_timestamp = 0;
	unsigned long long finish_timestamp = 0;
	unsigned long long time_taken_in_ns = 0;

	result = core_link_read_dpcd(link, DP_EDP_DPCD_REV, &edpRev, sizeof(edpRev));

	if (result == DC_OK && edpRev >= DP_EDP_12) {
		/* start from eDP version 1.2, SINK_STAUS indicate the sink is ready.*/
		enter_timestamp = dm_get_timestamp(link->ctx);
		do {
			sinkstatus = 0;
			result = core_link_read_dpcd(link, DP_SINK_STATUS, &sinkstatus, sizeof(sinkstatus));
			if (sinkstatus == 1)
				break;
			if (result != DC_OK)
				break;
			udelay(25);
			finish_timestamp = dm_get_timestamp(link->ctx);
			time_taken_in_ns = dm_get_elapse_time_in_ns(link->ctx, finish_timestamp, enter_timestamp);
		} while (time_taken_in_ns < 50 * 1000000); //MAx T7 is 50ms
	}

	if (link && link->panel_config.pps.extra_t7_ms > 0)
		fsleep(link->panel_config.pps.extra_t7_ms * 1000);

	return result;
}

bool edp_power_alpm_dpcd_enable(struct dc_link *link, bool enable)
{
	bool ret = false;
	union dpcd_alpm_configuration alpm_config;

	if (link->psr_settings.psr_version == DC_PSR_VERSION_SU_1) {
		memset(&alpm_config, 0, sizeof(alpm_config));

		alpm_config.bits.ENABLE = (enable ? true : false);
		ret = dm_helpers_dp_write_dpcd(link->ctx, link,
				DP_RECEIVER_ALPM_CONFIG, &alpm_config.raw,
				sizeof(alpm_config.raw));
	}
	return ret;
}

static struct pipe_ctx *get_pipe_from_link(const struct dc_link *link)
{
	int i;
	struct dc *dc = link->ctx->dc;
	struct pipe_ctx *pipe_ctx = NULL;

	for (i = 0; i < MAX_PIPES; i++) {
		if (dc->current_state->res_ctx.pipe_ctx[i].stream) {
			if (dc->current_state->res_ctx.pipe_ctx[i].stream->link == link) {
				pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];
				break;
			}
		}
	}

	return pipe_ctx;
}

bool edp_set_backlight_level(const struct dc_link *link,
		uint32_t backlight_pwm_u16_16,
		uint32_t frame_ramp)
{
	struct dc  *dc = link->ctx->dc;

	DC_LOGGER_INIT(link->ctx->logger);
	DC_LOG_BACKLIGHT("New Backlight level: %d (0x%X)\n",
			backlight_pwm_u16_16, backlight_pwm_u16_16);

	if (dc_is_embedded_signal(link->connector_signal)) {
		struct pipe_ctx *pipe_ctx = get_pipe_from_link(link);

		if (pipe_ctx) {
			/* Disable brightness ramping when the display is blanked
			 * as it can hang the DMCU
			 */
			if (pipe_ctx->plane_state == NULL)
				frame_ramp = 0;
		} else {
			return false;
		}

		dc->hwss.set_backlight_level(
				pipe_ctx,
				backlight_pwm_u16_16,
				frame_ramp);
	}
	return true;
}

bool edp_set_psr_allow_active(struct dc_link *link, const bool *allow_active,
		bool wait, bool force_static, const unsigned int *power_opts)
{
	struct dc  *dc = link->ctx->dc;
	struct dmcu *dmcu = dc->res_pool->dmcu;
	struct dmub_psr *psr = dc->res_pool->psr;
	unsigned int panel_inst;

	if (psr == NULL && force_static)
		return false;

	if (!dc_get_edp_link_panel_inst(dc, link, &panel_inst))
		return false;

	if ((allow_active != NULL) && (*allow_active == true) && (link->type == dc_connection_none)) {
		// Don't enter PSR if panel is not connected
		return false;
	}

	/* Set power optimization flag */
	if (power_opts && link->psr_settings.psr_power_opt != *power_opts) {
		link->psr_settings.psr_power_opt = *power_opts;

		if (psr != NULL && link->psr_settings.psr_feature_enabled && psr->funcs->psr_set_power_opt)
			psr->funcs->psr_set_power_opt(psr, link->psr_settings.psr_power_opt, panel_inst);
	}

	if (psr != NULL && link->psr_settings.psr_feature_enabled &&
			force_static && psr->funcs->psr_force_static)
		psr->funcs->psr_force_static(psr, panel_inst);

	/* Enable or Disable PSR */
	if (allow_active && link->psr_settings.psr_allow_active != *allow_active) {
		link->psr_settings.psr_allow_active = *allow_active;

		if (!link->psr_settings.psr_allow_active)
			dc_z10_restore(dc);

		if (psr != NULL && link->psr_settings.psr_feature_enabled) {
			psr->funcs->psr_enable(psr, link->psr_settings.psr_allow_active, wait, panel_inst);
		} else if ((dmcu != NULL && dmcu->funcs->is_dmcu_initialized(dmcu)) &&
			link->psr_settings.psr_feature_enabled)
			dmcu->funcs->set_psr_enable(dmcu, link->psr_settings.psr_allow_active, wait);
		else
			return false;
	}
	return true;
}

bool edp_get_psr_state(const struct dc_link *link, enum dc_psr_state *state)
{
	struct dc  *dc = link->ctx->dc;
	struct dmcu *dmcu = dc->res_pool->dmcu;
	struct dmub_psr *psr = dc->res_pool->psr;
	unsigned int panel_inst;

	if (!dc_get_edp_link_panel_inst(dc, link, &panel_inst))
		return false;

	if (psr != NULL && link->psr_settings.psr_feature_enabled)
		psr->funcs->psr_get_state(psr, state, panel_inst);
	else if (dmcu != NULL && link->psr_settings.psr_feature_enabled)
		dmcu->funcs->get_psr_state(dmcu, state);

	return true;
}

static inline enum physical_phy_id
transmitter_to_phy_id(struct dc_link *link)
{
	struct dc_context *dc_ctx = link->ctx;
	enum transmitter transmitter_value = link->link_enc->transmitter;

	switch (transmitter_value) {
	case TRANSMITTER_UNIPHY_A:
		return PHYLD_0;
	case TRANSMITTER_UNIPHY_B:
		return PHYLD_1;
	case TRANSMITTER_UNIPHY_C:
		return PHYLD_2;
	case TRANSMITTER_UNIPHY_D:
		return PHYLD_3;
	case TRANSMITTER_UNIPHY_E:
		return PHYLD_4;
	case TRANSMITTER_UNIPHY_F:
		return PHYLD_5;
	case TRANSMITTER_NUTMEG_CRT:
		return PHYLD_6;
	case TRANSMITTER_TRAVIS_CRT:
		return PHYLD_7;
	case TRANSMITTER_TRAVIS_LCD:
		return PHYLD_8;
	case TRANSMITTER_UNIPHY_G:
		return PHYLD_9;
	case TRANSMITTER_COUNT:
		return PHYLD_COUNT;
	case TRANSMITTER_UNKNOWN:
		return PHYLD_UNKNOWN;
	default:
		DC_ERROR("Unknown transmitter value %d\n", transmitter_value);
		return PHYLD_UNKNOWN;
	}
}

bool edp_setup_psr(struct dc_link *link,
		const struct dc_stream_state *stream, struct psr_config *psr_config,
		struct psr_context *psr_context)
{
	struct dc *dc;
	struct dmcu *dmcu;
	struct dmub_psr *psr;
	int i;
	unsigned int panel_inst;
	/* updateSinkPsrDpcdConfig*/
	union dpcd_psr_configuration psr_configuration;
	union dpcd_sink_active_vtotal_control_mode vtotal_control = {0};

	psr_context->controllerId = CONTROLLER_ID_UNDEFINED;

	if (!link)
		return false;

	dc = link->ctx->dc;
	dmcu = dc->res_pool->dmcu;
	psr = dc->res_pool->psr;

	if (!dmcu && !psr)
		return false;

	if (!dc_get_edp_link_panel_inst(dc, link, &panel_inst))
		return false;


	memset(&psr_configuration, 0, sizeof(psr_configuration));

	psr_configuration.bits.ENABLE                    = 1;
	psr_configuration.bits.CRC_VERIFICATION          = 1;
	psr_configuration.bits.FRAME_CAPTURE_INDICATION  =
			psr_config->psr_frame_capture_indication_req;

	/* Check for PSR v2*/
	if (link->psr_settings.psr_version == DC_PSR_VERSION_SU_1) {
		/* For PSR v2 selective update.
		 * Indicates whether sink should start capturing
		 * immediately following active scan line,
		 * or starting with the 2nd active scan line.
		 */
		psr_configuration.bits.LINE_CAPTURE_INDICATION = 0;
		/*For PSR v2, determines whether Sink should generate
		 * IRQ_HPD when CRC mismatch is detected.
		 */
		psr_configuration.bits.IRQ_HPD_WITH_CRC_ERROR    = 1;
		/* For PSR v2, set the bit when the Source device will
		 * be enabling PSR2 operation.
		 */
		psr_configuration.bits.ENABLE_PSR2    = 1;
		/* For PSR v2, the Sink device must be able to receive
		 * SU region updates early in the frame time.
		 */
		psr_configuration.bits.EARLY_TRANSPORT_ENABLE    = 1;
	}

	dm_helpers_dp_write_dpcd(
		link->ctx,
		link,
		368,
		&psr_configuration.raw,
		sizeof(psr_configuration.raw));

	if (link->psr_settings.psr_version == DC_PSR_VERSION_SU_1) {
		edp_power_alpm_dpcd_enable(link, true);
		psr_context->su_granularity_required =
			psr_config->su_granularity_required;
		psr_context->su_y_granularity =
			psr_config->su_y_granularity;
		psr_context->line_time_in_us = psr_config->line_time_in_us;

		/* linux must be able to expose AMD Source DPCD definition
		 * in order to support FreeSync PSR
		 */
		if (link->psr_settings.psr_vtotal_control_support) {
			psr_context->rate_control_caps = psr_config->rate_control_caps;
			vtotal_control.bits.ENABLE = true;
			core_link_write_dpcd(link, DP_SINK_PSR_ACTIVE_VTOTAL_CONTROL_MODE,
							&vtotal_control.raw, sizeof(vtotal_control.raw));
		}
	}

	psr_context->channel = link->ddc->ddc_pin->hw_info.ddc_channel;
	psr_context->transmitterId = link->link_enc->transmitter;
	psr_context->engineId = link->link_enc->preferred_engine;

	for (i = 0; i < MAX_PIPES; i++) {
		if (dc->current_state->res_ctx.pipe_ctx[i].stream
				== stream) {
			/* dmcu -1 for all controller id values,
			 * therefore +1 here
			 */
			psr_context->controllerId =
				dc->current_state->res_ctx.
				pipe_ctx[i].stream_res.tg->inst + 1;
			break;
		}
	}

	/* Hardcoded for now.  Can be Pcie or Uniphy (or Unknown)*/
	psr_context->phyType = PHY_TYPE_UNIPHY;
	/*PhyId is associated with the transmitter id*/
	psr_context->smuPhyId = transmitter_to_phy_id(link);

	psr_context->crtcTimingVerticalTotal = stream->timing.v_total;
	psr_context->vsync_rate_hz = div64_u64(div64_u64((stream->
					timing.pix_clk_100hz * 100),
					stream->timing.v_total),
					stream->timing.h_total);

	psr_context->psrSupportedDisplayConfig = true;
	psr_context->psrExitLinkTrainingRequired =
		psr_config->psr_exit_link_training_required;
	psr_context->sdpTransmitLineNumDeadline =
		psr_config->psr_sdp_transmit_line_num_deadline;
	psr_context->psrFrameCaptureIndicationReq =
		psr_config->psr_frame_capture_indication_req;

	psr_context->skipPsrWaitForPllLock = 0; /* only = 1 in KV */

	psr_context->numberOfControllers =
			link->dc->res_pool->timing_generator_count;

	psr_context->rfb_update_auto_en = true;

	/* 2 frames before enter PSR. */
	psr_context->timehyst_frames = 2;
	/* half a frame
	 * (units in 100 lines, i.e. a value of 1 represents 100 lines)
	 */
	psr_context->hyst_lines = stream->timing.v_total / 2 / 100;
	psr_context->aux_repeats = 10;

	psr_context->psr_level.u32all = 0;

	/*skip power down the single pipe since it blocks the cstate*/
	if (link->ctx->asic_id.chip_family >= FAMILY_RV) {
		switch (link->ctx->asic_id.chip_family) {
		case FAMILY_YELLOW_CARP:
		case AMDGPU_FAMILY_GC_10_3_6:
		case AMDGPU_FAMILY_GC_11_0_1:
			if (dc->debug.disable_z10 || dc->debug.psr_skip_crtc_disable)
				psr_context->psr_level.bits.SKIP_CRTC_DISABLE = true;
			break;
		default:
			psr_context->psr_level.bits.SKIP_CRTC_DISABLE = true;
			break;
		}
	}

	/* SMU will perform additional powerdown sequence.
	 * For unsupported ASICs, set psr_level flag to skip PSR
	 *  static screen notification to SMU.
	 *  (Always set for DAL2, did not check ASIC)
	 */
	psr_context->allow_smu_optimizations = psr_config->allow_smu_optimizations;
	psr_context->allow_multi_disp_optimizations = psr_config->allow_multi_disp_optimizations;

	/* Complete PSR entry before aborting to prevent intermittent
	 * freezes on certain eDPs
	 */
	psr_context->psr_level.bits.DISABLE_PSR_ENTRY_ABORT = 1;

	/* Disable ALPM first for compatible non-ALPM panel now */
	psr_context->psr_level.bits.DISABLE_ALPM = 0;
	psr_context->psr_level.bits.ALPM_DEFAULT_PD_MODE = 1;

	/* Controls additional delay after remote frame capture before
	 * continuing power down, default = 0
	 */
	psr_context->frame_delay = 0;

	psr_context->dsc_slice_height = psr_config->dsc_slice_height;

	if (psr) {
		link->psr_settings.psr_feature_enabled = psr->funcs->psr_copy_settings(psr,
			link, psr_context, panel_inst);
		link->psr_settings.psr_power_opt = 0;
		link->psr_settings.psr_allow_active = 0;
	} else {
		link->psr_settings.psr_feature_enabled = dmcu->funcs->setup_psr(dmcu, link, psr_context);
	}

	/* psr_enabled == 0 indicates setup_psr did not succeed, but this
	 * should not happen since firmware should be running at this point
	 */
	if (link->psr_settings.psr_feature_enabled == 0)
		ASSERT(0);

	return true;

}

void edp_get_psr_residency(const struct dc_link *link, uint32_t *residency)
{
	struct dc  *dc = link->ctx->dc;
	struct dmub_psr *psr = dc->res_pool->psr;
	unsigned int panel_inst;

	if (!dc_get_edp_link_panel_inst(dc, link, &panel_inst))
		return;

	// PSR residency measurements only supported on DMCUB
	if (psr != NULL && link->psr_settings.psr_feature_enabled)
		psr->funcs->psr_get_residency(psr, residency, panel_inst);
	else
		*residency = 0;
}
bool edp_set_sink_vtotal_in_psr_active(const struct dc_link *link, uint16_t psr_vtotal_idle, uint16_t psr_vtotal_su)
{
	struct dc *dc = link->ctx->dc;
	struct dmub_psr *psr = dc->res_pool->psr;

	if (psr == NULL || !link->psr_settings.psr_feature_enabled || !link->psr_settings.psr_vtotal_control_support)
		return false;

	psr->funcs->psr_set_sink_vtotal_in_psr_active(psr, psr_vtotal_idle, psr_vtotal_su);

	return true;
}

static struct abm *get_abm_from_stream_res(const struct dc_link *link)
{
	int i;
	struct dc *dc = link->ctx->dc;
	struct abm *abm = NULL;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx pipe_ctx = dc->current_state->res_ctx.pipe_ctx[i];
		struct dc_stream_state *stream = pipe_ctx.stream;

		if (stream && stream->link == link) {
			abm = pipe_ctx.stream_res.abm;
			break;
		}
	}
	return abm;
}

int edp_get_backlight_level(const struct dc_link *link)
{
	struct abm *abm = get_abm_from_stream_res(link);
	struct panel_cntl *panel_cntl = link->panel_cntl;
	struct dc  *dc = link->ctx->dc;
	struct dmcu *dmcu = dc->res_pool->dmcu;
	bool fw_set_brightness = true;

	if (dmcu)
		fw_set_brightness = dmcu->funcs->is_dmcu_initialized(dmcu);

	if (!fw_set_brightness && panel_cntl->funcs->get_current_backlight)
		return panel_cntl->funcs->get_current_backlight(panel_cntl);
	else if (abm != NULL && abm->funcs->get_current_backlight != NULL)
		return (int) abm->funcs->get_current_backlight(abm);
	else
		return DC_ERROR_UNEXPECTED;
}

int edp_get_target_backlight_pwm(const struct dc_link *link)
{
	struct abm *abm = get_abm_from_stream_res(link);

	if (abm == NULL || abm->funcs->get_target_backlight == NULL)
		return DC_ERROR_UNEXPECTED;

	return (int) abm->funcs->get_target_backlight(abm);
}
