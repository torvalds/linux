// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "dc.h"
#include "mod_power.h"
#include "core_types.h"
#include "dmcu.h"
#include "abm.h"
#include "power_helpers.h"
#include "dce/dmub_psr.h"
#include "dal_asic_id.h"
#include "link_service.h"
#include <linux/math.h>

#define DC_TRACE_LEVEL_MESSAGE(...) /* do nothing */
#define DC_TRACE_LEVEL_MESSAGEP(...) /* do nothing */
#include "dc/inc/hw/dmcu.h"
#include "dc/inc/hw/abm.h"
#include "dmub_cmd.h"

#define MOD_POWER_TO_CORE(mod_power)\
		container_of(mod_power, struct core_power, mod_public)

static unsigned int calc_psr_num_static_frames(unsigned int vsync_rate_hz)
{
	/* Initialize fail-safe to 2 static frames. */
	unsigned int num_frames_static = 2;

	/* Calculate number of frames such that at least 30 ms has passed.
	 * Round up to ensure the static period is not shorter than 30 ms.
	 */
	if (vsync_rate_hz != 0)
		num_frames_static = DIV_ROUND_UP(30000 * vsync_rate_hz, 1000000);

	return num_frames_static;
}

bool mod_power_psr_notify_mode_change(struct mod_power *mod_power,
	const struct dc_stream_state *stream,
	struct dc_link *link,
	unsigned int stream_index)
{
	struct core_power *core_power = NULL;
	struct dc *dc = NULL;
	struct psr_config psr_config = {0};
	struct psr_context psr_context = {0};
	int active_psr_events = 0;

	if ((mod_power == NULL) || (stream == NULL) || (link == NULL))
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);
	dc = core_power->dc;

	// NO num_entities check here - already validated by caller
	// stream_index is passed as validated parameter
	active_psr_events = core_power->map[stream_index].psr_events;

	/* Calculate PSR configurations */
	mod_power_calc_psr_configs(&psr_config, link, stream);

	psr_config.psr_exit_link_training_required =
			core_power->map[stream_index].caps->psr_exit_link_training_required;
	if (dc->ctx->asic_id.chip_family >= AMDGPU_FAMILY_GC_11_0_1)
		psr_config.allow_smu_optimizations =
				core_power->psr_smu_optimizations_support && dc_is_embedded_signal(stream->signal);
	else
		psr_config.allow_smu_optimizations =
				core_power->psr_smu_optimizations_support &&
				mod_power_only_edp(dc->current_state, stream);

	psr_config.allow_multi_disp_optimizations = core_power->multi_disp_optimizations_support;

	psr_config.rate_control_caps = core_power->map[stream_index].caps->rate_control_caps;

	if (active_psr_events & psr_event_os_request_force_ffu)
		psr_config.os_request_force_ffu = true;

	/*
	 * DSC support:
	 * DSC slice height value must be 'mod' by su_y_granularity.
	 * According to Panel Vendor, there might be varied conditions to fulfill.
	 * Right now, DSC slice height value must be multiple of su_y_granularity.
	 *
	 * The value of DSC slice height is determined in DSC Driver but it does not
	 * propagated out here, so we need to calculate it as below 'slice_height'.
	 */
	psr_su_set_dsc_slice_height(dc, link,
				(struct dc_stream_state *) stream,
				&psr_config);

	dc_link_setup_psr(link, stream, &psr_config, &psr_context);

	return true;
}

static void mod_power_psr_set_power_opt(struct mod_power *mod_power,
	struct dc_stream_state *stream,
	unsigned int active_psr_events,
	bool psr_enable_request)
{
	(void)psr_enable_request;
	struct core_power *core_power = NULL;
	struct dc_link *link = NULL;
	unsigned int stream_index = 0;
	unsigned int power_opt = 0;

	if (!stream)
		return;

	core_power = MOD_POWER_TO_CORE(mod_power);
	stream_index = map_index_from_stream(core_power, stream);
	if (!core_power->map[stream_index].caps->psr_version)
		return;

	link = dc_stream_get_link(stream);

	if (active_psr_events == 0) {
		/* Static Screen */
		power_opt |= (psr_power_opt_smu_opt_static_screen | psr_power_opt_z10_static_screen |
					psr_power_opt_ds_disable_allow);
	}

	/* psr_power_opt_flag is a configuration parameter into the module that determines
	 * which optimizations to enable during psr
	 */
	power_opt &= core_power->map[stream_index].caps->psr_power_opt_flag;
	if (core_power->map[stream_index].psr_power_opt != power_opt) {
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_VERBOSE,
				WPP_BIT_FLAG_Firmware_PsrState,
				"mod_power set_power_opt: psr_power_opt=0x%04x, power_opt=0x%04x active_psr_events=0x%04x, psr_power_opt_flag=0x%04x",
				core_power->map[stream_index].psr_power_opt,
				power_opt,
				active_psr_events,
				core_power->map[stream_index].caps->psr_power_opt_flag);
		dc_link_set_psr_allow_active(link, NULL, false, false, &power_opt);
		core_power->map[stream_index].psr_power_opt = power_opt;
	}
}

static bool set_psr_enable(struct mod_power *mod_power,
		struct dc_stream_state *stream,
		bool psr_enable,
		bool wait,
		bool force_static)
{
	struct core_power *core_power = NULL;
	enum dc_psr_state state = PSR_STATE0;
	unsigned int retry_count;
	const unsigned int max_retry = 1000;
	struct dc_link *link = NULL;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0) {
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
							WPP_BIT_FLAG_Firmware_PsrState,
							"set psr enable: ERROR: stream=%p num_entities=%u",
							stream,
							core_power->num_entities);
		return false;
	}

	if (psr_enable)	{
		unsigned int vsync_rate_hz;
		struct dc_static_screen_params params = {0};

		vsync_rate_hz = (unsigned int)div_u64(div_u64((
				stream->timing.pix_clk_100hz * 100),
				stream->timing.v_total),
				stream->timing.h_total);

		params.triggers.cursor_update = true;
		params.triggers.overlay_update = true;
		params.triggers.surface_update = true;
		params.num_frames = calc_psr_num_static_frames(vsync_rate_hz);

		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_INFORMATION,
							WPP_BIT_FLAG_Firmware_PsrState,
							"set psr enable: CALCS: pix_clk_100hz=%u v_total=%u h_total=%u vsync_rate_hz=%u num_frames=%u",
							stream->timing.pix_clk_100hz,
							stream->timing.v_total,
							stream->timing.h_total,
							vsync_rate_hz,
							params.num_frames);

		dc_stream_set_static_screen_params(core_power->dc,
						   &stream, 1,
						   &params);
	}

	link = dc_stream_get_link(stream);

	if (!dc_link_set_psr_allow_active(link, &psr_enable, false, force_static, NULL)) {
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
							WPP_BIT_FLAG_Firmware_PsrState,
							"set psr enable: ERROR: stream=%p link=%p psr_enable=%d",
							stream,
							link,
							psr_enable);
		return false;
	}

	if (wait == true) {

		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_INFORMATION,
							WPP_BIT_FLAG_Firmware_PsrState,
							"set psr enable: BEGIN WAIT: psr_enable=%d",
							(int)psr_enable);

		for (retry_count = 0; retry_count <= max_retry; retry_count++) {
			dc_link_get_psr_state(link, &state);
			if (psr_enable) {
				if (state != PSR_STATE0 &&
						(!force_static || state == PSR_STATE3))
					break;
			} else {
				if (state == PSR_STATE0)
					break;
			}
			udelay(500);
		}

		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_INFORMATION,
							WPP_BIT_FLAG_Firmware_PsrState,
							"set psr enable: END WAIT: psr_enable=%d",
							(int)psr_enable);

		/* assert if max retry hit */
		if (retry_count >= max_retry) {
			ASSERT(0);
			DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
								WPP_BIT_FLAG_Firmware_PsrState,
								"set psr enable: ERROR: retry_count=%u: Unexpectedly long wait for PSR state change.",
								retry_count);
		}
	} else {
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_INFORMATION,
							WPP_BIT_FLAG_Firmware_PsrState,
							"set psr enable: PSR state change initiated (wait=false): psr_enable=%d",
							(int)psr_enable);
	}

	return true;
}

bool mod_power_get_psr_event(struct mod_power *mod_power,
			struct dc_stream_state *stream,
			unsigned int *active_psr_events)
{
	struct core_power *core_power = NULL;
	unsigned int stream_index = 0;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return false;

	stream_index = map_index_from_stream(core_power, stream);

	if (!core_power->map[stream_index].caps->psr_version)
		return false;

	*active_psr_events = core_power->map[stream_index].psr_events;

	return true;
}

bool mod_power_set_psr_event(struct mod_power *mod_power,
		struct dc_stream_state *stream, bool set_event,
		enum psr_event event, bool wait)
{
	struct core_power *core_power = NULL;
	unsigned int stream_index = 0;
	unsigned int active_psr_events = 0;
	bool psr_enable_request = false;
	bool force_static = false;

	if (mod_power == NULL || stream == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);
	stream_index = map_index_from_stream(core_power, stream);

	if (core_power->num_entities == 0) {
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
							WPP_BIT_FLAG_Firmware_PsrState,
							"mod_power set_psr_event: ERROR: stream=%p event=%d num_entities=%u",
							stream,
							(int)event,
							core_power->num_entities);
		return false;
	}

	if (!core_power->map[stream_index].caps->psr_version)
		return false;

	if (set_event)
		core_power->map[stream_index].psr_events |= event;
	else
		core_power->map[stream_index].psr_events &= ~event;

	active_psr_events = core_power->map[stream_index].psr_events;

	// ignore other events when we're in forced psr enabled state
	if (active_psr_events & psr_event_dynamic_display_switch &&
			event != psr_event_dynamic_display_switch)
		return false;

	// ignore other events when we're in forced psr enabled state
	if (active_psr_events & psr_event_os_override_hold &&
			event != psr_event_os_override_hold)
		return false;

	// ignore other events when we're in forced psr enabled state
	// dds events need to be processed while in dynamic_link_rate_control
	if (active_psr_events & psr_event_dynamic_link_rate_control &&
			event != psr_event_dynamic_link_rate_control &&
			event != psr_event_dds_defer_stream_enable &&
			event != psr_event_dynamic_display_switch)
		return false;

	if (active_psr_events & (psr_event_test_harness_disable_psr | psr_event_os_request_disable))
		psr_enable_request = false;
	else if (active_psr_events & psr_event_pause)
		psr_enable_request = false;
	else if (active_psr_events & psr_event_test_harness_enable_psr)
		psr_enable_request = true;
	else if (active_psr_events & psr_event_dynamic_display_switch) {
		psr_enable_request = true;
		force_static = true;
	} else if (active_psr_events & psr_event_dynamic_link_rate_control) {
		psr_enable_request = true;
		force_static = true;
	} else if (active_psr_events & psr_event_edp_panel_off_disable_psr)
		psr_enable_request = false;
	else if (active_psr_events & (psr_event_hw_programming |
			psr_event_defer_enable |
			psr_event_dds_defer_stream_enable |
			psr_event_vrr_transition |
			psr_event_immediate_flip))
		psr_enable_request = false;
	else if (active_psr_events & psr_event_big_screen_video)
		psr_enable_request = true;
	else if (active_psr_events & psr_event_full_screen)
		psr_enable_request = false;
	else if (active_psr_events & psr_event_mpo_video_selective_update)
		psr_enable_request = true;
	else if (active_psr_events & psr_event_vsync)
		psr_enable_request = false;
	else if (active_psr_events & psr_event_crc_window_active)
		psr_enable_request = false;
	else
		psr_enable_request = true;

	DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_VERBOSE,
						WPP_BIT_FLAG_Firmware_PsrState,
						"mod_power set_psr_event: before: psr_enabled=%d -> request: set_event=%d event=0x%04x -> result: psr_events=0x%04x psr_enable_request=%d",
						(int)core_power->map[stream_index].psr_enabled,
						(int)set_event,
						(unsigned int)event,
						(unsigned int)core_power->map[stream_index].psr_events,
						(int)psr_enable_request);
	mod_power_psr_set_power_opt(mod_power, stream, active_psr_events, psr_enable_request);

	if (core_power->map[stream_index].psr_enabled != psr_enable_request || force_static) {
		if (set_psr_enable(mod_power, stream, psr_enable_request, wait, force_static))
			core_power->map[stream_index].psr_enabled = psr_enable_request;
	}

	return true;
}

bool mod_power_get_psr_state(struct mod_power *mod_power,
		const struct dc_stream_state *stream,
		enum dc_psr_state *state)
{
	struct core_power *core_power = NULL;
	const struct dc_link *link = NULL;

	if (!stream)
		return false;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return false;

	link = dc_stream_get_link(stream);
	return dc_link_get_psr_state(link, state);
}

bool mod_power_get_psr_enabled_status(struct mod_power *mod_power,
		const struct dc_stream_state *stream,
		bool *psr_enabled)
{
	struct core_power *core_power = NULL;
	unsigned int stream_index = 0;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return false;

	stream_index = map_index_from_stream(core_power, stream);

	if (!core_power->map[stream_index].caps->psr_version)
		return false;

	*psr_enabled = core_power->map[stream_index].psr_enabled;

	return true;
}

void mod_power_psr_residency(struct mod_power *mod_power,
		const struct dc_stream_state *stream,
		unsigned int *residency,
		const uint8_t mode)
{
	struct core_power *core_power = NULL;
	const struct dc_link *link = NULL;

	if (!stream)
		return;

	if (mod_power == NULL)
		return;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return;

	link = dc_stream_get_link(stream);

	if (link != NULL)
		link->dc->link_srv->edp_get_psr_residency(link, residency, mode);
}
bool mod_power_psr_get_active_psr_events(struct mod_power *mod_power,
		const struct dc_stream_state *stream, unsigned int *active_psr_events)
{
	struct core_power *core_power = NULL;
	unsigned int stream_index = 0;

	if (!stream)
		return false;

	if (mod_power == NULL)
		return false;

	if (active_psr_events == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return false;

	stream_index = map_index_from_stream(core_power, stream);

	*active_psr_events = core_power->map[stream_index].psr_events;
	return true;
}

bool mod_power_psr_set_sink_vtotal_in_psr_active(struct mod_power *mod_power,
		const struct dc_stream_state *stream,
		uint16_t psr_vtotal_idle,
		uint16_t psr_vtotal_su)
{
	struct core_power *core_power = NULL;
	unsigned int stream_index = 0;
	const struct dc_link *link = NULL;

	if (!stream)
		return false;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return false;

	stream_index = map_index_from_stream(core_power, stream);

	if (!core_power->map[stream_index].caps->psr_version)
		return false;

	link = dc_stream_get_link(stream);

	return link->dc->link_srv->edp_set_sink_vtotal_in_psr_active(
			link, psr_vtotal_idle, psr_vtotal_su);
}
/*
 * is_psr_su_specific_panel() - check if sink is AMD vendor-specific PSR-SU
 * supported eDP device.
 *
 * @link: dc link pointer
 *
 * Return: true if AMDGPU vendor specific PSR-SU eDP panel
 */
bool is_psr_su_specific_panel(struct dc_link *link)
{
	bool isPSRSUSupported = false;
	struct dpcd_caps *dpcd_caps = &link->dpcd_caps;

	if (dpcd_caps->edp_rev >= DP_EDP_14) {
		if (dpcd_caps->psr_info.psr_version >= DP_PSR2_WITH_Y_COORD_ET_SUPPORTED)
			isPSRSUSupported = true;
		/*
		 * Some panels will report PSR capabilities over additional DPCD bits.
		 * Such panels are approved despite reporting only PSR v3, as long as
		 * the additional bits are reported.
		 */
		if (dpcd_caps->sink_dev_id == DP_BRANCH_DEVICE_ID_001CF8) {
			/*
			 * This is the temporary workaround to disable PSRSU when system turned on
			 * DSC function on the sepcific sink.
			 */
			if (dpcd_caps->psr_info.psr_version < DP_PSR2_WITH_Y_COORD_IS_SUPPORTED)
				isPSRSUSupported = false;
			else if (dpcd_caps->dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_SUPPORT &&
				((dpcd_caps->sink_dev_id_str[1] == 0x08 && dpcd_caps->sink_dev_id_str[0] == 0x08) ||
				(dpcd_caps->sink_dev_id_str[1] == 0x08 && dpcd_caps->sink_dev_id_str[0] == 0x07)))
				isPSRSUSupported = false;
			else if (dpcd_caps->sink_dev_id_str[1] == 0x08 && dpcd_caps->sink_dev_id_str[0] == 0x03)
				isPSRSUSupported = false;
			else if (dpcd_caps->sink_dev_id_str[1] == 0x08 && dpcd_caps->sink_dev_id_str[0] == 0x01)
				isPSRSUSupported = false;
			else if (dpcd_caps->psr_info.force_psrsu_cap == 0x1)
				isPSRSUSupported = true;
		}
	}

	return isPSRSUSupported;
}

/**
 * mod_power_calc_psr_configs() - calculate/update generic psr configuration fields.
 * @psr_config: [output], psr configuration structure to be updated
 * @link: [input] dc link pointer
 * @stream: [input] dc stream state pointer
 *
 * calculate and update the psr configuration fields that are not DM specific, i.e. such
 * fields which are based on DPCD caps or timing information. To setup PSR in DMUB FW,
 * this helper is assumed to be called before the call of the DC helper dc_link_setup_psr().
 *
 * PSR config fields to be updated within the helper:
 * - psr_rfb_setup_time
 * - psr_sdp_transmit_line_num_deadline
 * - line_time_in_us
 * - su_y_granularity
 * - su_granularity_required
 * - psr_frame_capture_indication_req
 * - psr_exit_link_training_required
 *
 * PSR config fields that are DM specific and NOT updated within the helper:
 * - allow_smu_optimizations
 * - allow_multi_disp_optimizations
 */
void mod_power_calc_psr_configs(struct psr_config *psr_config,
		struct dc_link *link,
		const struct dc_stream_state *stream)
{
	unsigned int num_vblank_lines = 0;
	unsigned int vblank_time_in_us = 0;
	unsigned int sdp_tx_deadline_in_us = 0;
	unsigned int line_time_in_us = 0;
	struct dpcd_caps *dpcd_caps = &link->dpcd_caps;
	const int psr_setup_time_step_in_us = 55;	/* refer to eDP spec DPCD 0x071h */

	/* timing parameters */
	num_vblank_lines = stream->timing.v_total -
			 stream->timing.v_addressable -
			 stream->timing.v_border_top -
			 stream->timing.v_border_bottom;

	vblank_time_in_us = (stream->timing.h_total * num_vblank_lines * 1000) / (stream->timing.pix_clk_100hz / 10);

	line_time_in_us = ((stream->timing.h_total * 1000) / (stream->timing.pix_clk_100hz / 10)) + 1;

	/**
	 * psr configuration fields
	 *
	 * as per eDP 1.5 pg. 377 of 459, DPCD 0x071h bits [3:1], psr setup time bits interpreted as below
	 * 000b <--> 330 us (default)
	 * 001b <--> 275 us
	 * 010b <--> 220 us
	 * 011b <--> 165 us
	 * 100b <--> 110 us
	 * 101b <--> 055 us
	 * 110b <--> 000 us
	 */
	psr_config->psr_rfb_setup_time =
		(6 - dpcd_caps->psr_info.psr_dpcd_caps.bits.PSR_SETUP_TIME) * psr_setup_time_step_in_us;

	if (psr_config->psr_rfb_setup_time > vblank_time_in_us) {
		link->psr_settings.psr_frame_capture_indication_req = true;
		link->psr_settings.psr_sdp_transmit_line_num_deadline = num_vblank_lines;
	} else {
		sdp_tx_deadline_in_us = vblank_time_in_us - psr_config->psr_rfb_setup_time;

		/* Set the last possible line SDP may be transmitted without violating the RFB setup time */
		link->psr_settings.psr_frame_capture_indication_req = false;
		link->psr_settings.psr_sdp_transmit_line_num_deadline = sdp_tx_deadline_in_us / line_time_in_us;
	}

	psr_config->psr_sdp_transmit_line_num_deadline = link->psr_settings.psr_sdp_transmit_line_num_deadline;
	psr_config->line_time_in_us = line_time_in_us;
	psr_config->su_y_granularity = dpcd_caps->psr_info.psr2_su_y_granularity_cap;
	psr_config->su_granularity_required = dpcd_caps->psr_info.psr_dpcd_caps.bits.SU_GRANULARITY_REQUIRED;
	psr_config->psr_frame_capture_indication_req = link->psr_settings.psr_frame_capture_indication_req;
	psr_config->psr_exit_link_training_required =
		!link->dpcd_caps.psr_info.psr_dpcd_caps.bits.LINK_TRAINING_ON_EXIT_NOT_REQUIRED;
}

bool psr_su_set_dsc_slice_height(struct dc *dc, struct dc_link *link,
			      struct dc_stream_state *stream,
			      struct psr_config *config)
{
	uint32_t pic_height;
	uint32_t slice_height;

	config->dsc_slice_height = 0;
	if (!(link->connector_signal & SIGNAL_TYPE_EDP) ||
	    !dc->caps.edp_dsc_support ||
	    link->panel_config.dsc.disable_dsc_edp ||
	    !link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_SUPPORT ||
	    !stream->timing.dsc_cfg.num_slices_v)
		return true;

	pic_height = stream->timing.v_addressable +
		stream->timing.v_border_top + stream->timing.v_border_bottom;

	if (stream->timing.dsc_cfg.num_slices_v == 0)
		return false;

	slice_height = pic_height / stream->timing.dsc_cfg.num_slices_v;
	config->dsc_slice_height = (uint16_t)slice_height;

	if (slice_height) {
		if (config->su_y_granularity &&
		    (slice_height % config->su_y_granularity)) {
			ASSERT(0);
			return false;
		}
	}

	return true;
}
