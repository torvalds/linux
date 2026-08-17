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
#include "power_helpers.h"
#include "dc/inc/hw/dmcu.h"
#include "dc/inc/hw/abm.h"
#include "dc.h"
#include "core_types.h"
#include "dmub_cmd.h"

#define MOD_POWER_TO_CORE(mod_power)\
		container_of(mod_power, struct core_power, mod_public)

#define LOW_REFRESH_RATE_DURATION_US_UPPER_BOUND 25000

static bool mod_power_set_replay_active(struct dc_stream_state *stream,
	bool replay_active,
	bool wait,
	bool force_static)
{
	uint64_t state;
	unsigned int retry_count;
	const unsigned int max_retry = 1000;
	struct dc_link *link = NULL;

	if (!stream)
		return false;

	link = dc_stream_get_link(stream);

	if (!link)
		return false;

	if (!dc_link_set_replay_allow_active(link, &replay_active, false, force_static, NULL))
		return false;

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

static unsigned int mod_power_replay_setup_power_opt(struct dc_link *link,
	unsigned int active_replay_events, bool is_ultra_sleep_mode)
{
	unsigned int power_opt = 0;

	if (is_ultra_sleep_mode) {
		/* Static Screen */
		power_opt |= (replay_power_opt_smu_opt_static_screen | replay_power_opt_z10_static_screen);
	} else if (active_replay_events & replay_event_test_harness_ultra_sleep) {
		power_opt |= replay_power_opt_z10_static_screen;
	}

	/* replay_power_opt_flag is a configuration parameter into the module that determines
	 * which optimizations to enable during replay
	 */
	power_opt &= link->replay_settings.config.replay_power_opt_supported;

	return power_opt;
}

static bool mod_power_replay_set_power_opt(struct mod_power *mod_power,
	struct dc_stream_state *stream,
	unsigned int active_replay_events,
	bool is_ultra_sleep_mode)
{
	(void)mod_power;
	struct dc_link *link = NULL;
	unsigned int power_opt = 0;

	if (!stream)
		return false;

	link = dc_stream_get_link(stream);

	if (!link || !link->replay_settings.replay_feature_enabled)
		return false;

	power_opt = mod_power_replay_setup_power_opt(link, active_replay_events, is_ultra_sleep_mode);

	if (!dc_link_set_replay_allow_active(link, NULL, false, false, &power_opt))
		return false;

	return true;
}

bool mod_power_get_replay_event(struct mod_power *mod_power,
	struct dc_stream_state *stream,
	unsigned int *active_replay_events)
{
	struct core_power *core_power = NULL;
	unsigned int stream_index = 0;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return false;

	stream_index = map_index_from_stream(core_power, stream);

	*active_replay_events = core_power->map[stream_index].replay_events;

	return true;
}

static bool mod_power_update_replay_active_status(unsigned int active_replay_events,
	struct dc_link *link, uint32_t *coasting_vtotal, bool *is_full_screen_video,
	bool *is_ultra_sleep_mode, uint16_t *frame_skip_number, bool *is_video_playback)
{
	if (!link || !coasting_vtotal || !is_full_screen_video || !is_video_playback)
		return false;

	// Check coasting_vtotal_table has been updated.
	if (!link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_STATIC]
		|| !link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_NOM])
		return false;

	unsigned int replay_enable_option =
		link->replay_settings.config.replay_enable_option;

	/* TODO: To support test harness and DDS event */

	*coasting_vtotal = link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_NOM];
	ASSERT(link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_NOM] <= 0xFFFF);
	*frame_skip_number = (uint16_t)link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_NOM];

	link->replay_settings.config.replay_timing_sync_supported = false;

	*is_full_screen_video = false;

	*is_ultra_sleep_mode = false;

	*is_video_playback = false;

	/* DSAT test scenario */
	if (active_replay_events & replay_event_test_harness_mode) {
		if (link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_TEST_HARNESS])
			*coasting_vtotal =
				link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_TEST_HARNESS];
		if (link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_TEST_HARNESS]) {
			ASSERT(link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_TEST_HARNESS] <= 0xFFFF);
			*frame_skip_number =
				(uint16_t)link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_TEST_HARNESS];
		}

		/* During the ultra sleep mode testing, disable the timing sync in short vblank mode */
		if (active_replay_events & (replay_event_test_harness_enable_replay)) {
			if ((active_replay_events & replay_event_test_harness_ultra_sleep) &&
				  !link->replay_settings.config.replay_support_fast_resync_in_ultra_sleep_mode)
				link->replay_settings.config.replay_timing_sync_supported = false;
			return true;
		} else
			return false;
	} else if (active_replay_events & (replay_event_test_harness_enable_replay)) {
		if (link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_TEST_HARNESS])
			*coasting_vtotal = link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_TEST_HARNESS];
		if (link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_TEST_HARNESS]) {
			uint32_t frame_skip_val =
				link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_TEST_HARNESS];

			ASSERT(frame_skip_val <= 0xFFFF);
			*frame_skip_number = (uint16_t)frame_skip_val;
		}

		/* During the ultra sleep mode testing, disable the timing sync in short vblank mode */
		if ((active_replay_events & replay_event_test_harness_ultra_sleep) &&
			  !link->replay_settings.config.replay_support_fast_resync_in_ultra_sleep_mode) {
			link->replay_settings.config.replay_timing_sync_supported = false;
		}
		return true;
	} else if (active_replay_events &
			(replay_event_test_harness_disable_replay | replay_event_os_request_disable)) {
		// set last set coasting vtotal
		if (link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_TEST_HARNESS])
			*coasting_vtotal = link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_TEST_HARNESS];
		if (link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_TEST_HARNESS]) {
			uint32_t frame_skip_val =
				link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_TEST_HARNESS];

			ASSERT(frame_skip_val <= 0xFFFF);
			*frame_skip_number = (uint16_t)frame_skip_val;
		}
		return false;
	}

	/* Inactive conditions */
	if (active_replay_events & (replay_event_edp_panel_off_disable_psr |
			replay_event_hw_programming |
			replay_event_vrr |
			replay_event_immediate_flip |
			replay_event_prepare_vtotal |
			replay_event_vrr_transition |
			replay_event_pause |
			replay_event_disable_replay_while_DPMS |
			replay_event_sleep_resume |
			replay_event_disable_in_AC |
			replay_event_disable_replay_while_detect_display |
			replay_event_infopacket |
			replay_event_crc_window_active))
		return false;

	// Full screen scenario
	if (active_replay_events & replay_event_full_screen) {
		if (!(replay_enable_option & pr_enable_option_full_screen))
			return false;
	}

	/* Full screen video scenario */
	if (active_replay_events & replay_event_big_screen_video) {

		link->replay_settings.config.replay_timing_sync_supported = false;

		if (replay_enable_option & pr_enable_option_full_screen_video_coasting) {
			unsigned int fsn_vid =
				link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_FULL_SCREEN_VIDEO];

			*coasting_vtotal =
				link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_FULL_SCREEN_VIDEO];
			ASSERT(fsn_vid <= 0xFFFF);
			*frame_skip_number = (uint16_t)fsn_vid;
		}

		*is_video_playback = true;

		if ((replay_enable_option & pr_enable_option_full_screen_video) &&
			(replay_enable_option & pr_enable_option_full_screen_video_coasting)) {
			*is_full_screen_video = true;
			return true;
		} else
			return false;
	}

	/* MPO video scenario
	 * Some of the cases may contain a full screen UI layer in MPO video scenario which is
	 * not the expected case to enable Replay.
	 */
	if ((active_replay_events & replay_event_mpo_video_selective_update) &&
		!(active_replay_events & replay_event_full_screen)) {

		link->replay_settings.config.replay_timing_sync_supported = false;

		if (replay_enable_option & pr_enable_option_mpo_video_coasting) {
			*coasting_vtotal = link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_NOM];
			{
				uint32_t frame_skip_val =
					link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_NOM];

				ASSERT(frame_skip_val <= 0xFFFF);
				*frame_skip_number = (uint16_t)frame_skip_val;
			}
		}

		*is_video_playback = true;

		if (replay_enable_option & pr_enable_option_mpo_video)
			return true;
		else
			return false;
	}

	/* Static screen scenario */
	if (!(active_replay_events & replay_event_vsync)) {

		if (replay_enable_option & pr_enable_option_static_screen_coasting) {
			// Do not adjust eDP refresh rate if static screen + normal sleep mode
			if ((!(link->replay_settings.config.replay_power_opt_supported &
				replay_power_opt_z10_static_screen)) ||
				(active_replay_events & replay_event_cursor_updating)) {
				// normal sleep mode
				*coasting_vtotal =
					link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_NOM];
				{
					uint32_t frame_skip_val =
						link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_NOM];

					ASSERT(frame_skip_val <= 0xFFFF);
					*frame_skip_number = (uint16_t)frame_skip_val;
				}
			} else {
				// ultra sleep mode
				*coasting_vtotal =
					link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_STATIC];
				{
					uint32_t frame_skip_val =
						link->replay_settings.frame_skip_number_table[PR_COASTING_TYPE_STATIC];

					ASSERT(frame_skip_val <= 0xFFFF);
					*frame_skip_number = (uint16_t)frame_skip_val;
				}
				*is_ultra_sleep_mode = true;
			}
		}

		if (replay_enable_option & pr_enable_option_static_screen) {
			if (!link->replay_settings.config.replay_support_fast_resync_in_ultra_sleep_mode)
				link->replay_settings.config.replay_timing_sync_supported = false;
			return true;
		} else
			return false;
	}

	/* General UI scenario */
	if (active_replay_events & replay_event_general_ui) {
		if (replay_enable_option & pr_enable_option_general_ui)
			return true;
		else
			return false;
	}

	return false;
}

bool mod_power_replay_set_coasting_vtotal(struct mod_power *mod_power,
	const struct dc_stream_state *stream,
	uint32_t coasting_vtotal,
	uint16_t frame_skip_number)
{
	struct core_power *core_power = NULL;
	struct dc_link *link = NULL;

	if (!stream)
		return false;

	link = dc_stream_get_link(stream);
	if (!link || !link->replay_settings.replay_feature_enabled)
		return false;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return false;

	return link->dc->link_srv->edp_set_coasting_vtotal(link, coasting_vtotal, frame_skip_number);
}

void mod_power_replay_set_timing_sync_supported(struct mod_power *mod_power,
	const struct dc_stream_state *stream)
{
	struct core_power *core_power = NULL;
	struct dc_link *link = NULL;
	unsigned int stream_index = 0;
	union dmub_replay_cmd_set cmd_data = { 0 };

	if (!stream || mod_power == NULL)
		return;

	core_power = MOD_POWER_TO_CORE(mod_power);
	if (core_power->num_entities == 0)
		return;

	stream_index = map_index_from_stream(core_power, stream);
	if (stream_index > core_power->num_entities) //invalid index
		return;

	link = dc_stream_get_link(stream);
	if (!link || !link->replay_settings.replay_feature_enabled)
		return;

	cmd_data.sync_data.timing_sync_supported = link->replay_settings.config.replay_timing_sync_supported;

	link->dc->link_srv->edp_send_replay_cmd(link, Replay_Set_Timing_Sync_Supported,
		&cmd_data);
}

void mod_power_replay_disabled_adaptive_sync_sdp(struct mod_power *mod_power,
	const struct dc_stream_state *stream, bool force_disabled)
{
	struct core_power *core_power = NULL;
	struct dc_link *link = NULL;
	unsigned int stream_index = 0;
	union dmub_replay_cmd_set cmd_data = { 0 };

	if (!stream || mod_power == NULL)
		return;

	core_power = MOD_POWER_TO_CORE(mod_power);
	if (core_power->num_entities == 0)
		return;

	stream_index = map_index_from_stream(core_power, stream);
	if (stream_index > core_power->num_entities) //invalid index
		return;

	link = dc_stream_get_link(stream);
	if (!link || !link->replay_settings.replay_feature_enabled)
		return;

	cmd_data.disabled_adaptive_sync_sdp_data.force_disabled = force_disabled;

	link->dc->link_srv->edp_send_replay_cmd(link, Replay_Disabled_Adaptive_Sync_SDP,
		&cmd_data);
}

static void mod_power_replay_set_general_cmd(struct mod_power *mod_power,
	const struct dc_stream_state *stream,
	const enum dmub_cmd_replay_general_subtype general_cmd_type,
	const uint32_t param1, const uint32_t param2)
{
	struct core_power *core_power = NULL;
	struct dc_link *link = NULL;
	unsigned int stream_index = 0;
	union dmub_replay_cmd_set cmd_data = { 0 };

	if (!stream || mod_power == NULL)
		return;

	core_power = MOD_POWER_TO_CORE(mod_power);
	if (core_power->num_entities == 0)
		return;

	stream_index = map_index_from_stream(core_power, stream);
	if (stream_index > core_power->num_entities) //invalid index
		return;

	link = dc_stream_get_link(stream);
	if (!link || !link->replay_settings.replay_feature_enabled)
		return;

	cmd_data.set_general_cmd_data.subtype = general_cmd_type;
	cmd_data.set_general_cmd_data.param1 = param1;
	cmd_data.set_general_cmd_data.param2 = param2;
	link->dc->link_srv->edp_send_replay_cmd(link, Replay_Set_General_Cmd,
		&cmd_data);
}

void mod_power_replay_disabled_desync_error_detection(struct mod_power *mod_power,
	const struct dc_stream_state *stream,  bool force_disabled)
{
	mod_power_replay_set_general_cmd(mod_power, stream,
			REPLAY_GENERAL_CMD_DISABLED_DESYNC_ERROR_DETECTION,
			force_disabled, 0);
}

static void mod_power_replay_set_pseudo_vtotal(struct mod_power *mod_power,
	const struct dc_stream_state *stream, uint16_t vtotal)
{
	struct core_power *core_power = NULL;
	struct dc_link *link = NULL;
	unsigned int stream_index = 0;
	union dmub_replay_cmd_set cmd_data = { 0 };

	if (!stream || mod_power == NULL)
		return;

	core_power = MOD_POWER_TO_CORE(mod_power);
	if (core_power->num_entities == 0)
		return;

	stream_index = map_index_from_stream(core_power, stream);
	if (stream_index > core_power->num_entities) //invalid index
		return;

	link = dc_stream_get_link(stream);
	if (!link || !link->replay_settings.replay_feature_enabled)
		return;

	cmd_data.pseudo_vtotal_data.vtotal = vtotal;

	if (link->replay_settings.last_pseudo_vtotal != vtotal) {
		link->replay_settings.last_pseudo_vtotal = vtotal;
		link->dc->link_srv->edp_send_replay_cmd(link, Replay_Set_Pseudo_VTotal, &cmd_data);
	}
}

static void mod_power_update_error_status(struct mod_power *mod_power,
	const struct dc_stream_state *stream)
{
	struct dc_link *link = NULL;
	union replay_debug_flags *pDebug = NULL;

	if (mod_power == NULL || stream == NULL)
		return;

	link = dc_stream_get_link(stream);

	if (!link)
		return;

	pDebug = (union replay_debug_flags *)&link->replay_settings.config.debug_flags;

	if (pDebug->bitfields.enable_visual_confirm_debug == 0)
		return;

	mod_power_replay_set_general_cmd(mod_power, stream,
		REPLAY_GENERAL_CMD_UPDATE_ERROR_STATUS,
		link->replay_settings.config.replay_error_status.raw, 0);
}

void mod_power_set_low_rr_activate(struct mod_power *mod_power,
	const struct dc_stream_state *stream, bool low_rr_supported)
{
	struct dc_link *link = NULL;

	if (mod_power == NULL || stream == NULL)
		return;

	link = dc_stream_get_link(stream);

	if (!link)
		return;

	mod_power_replay_set_general_cmd(mod_power, stream,
		REPLAY_GENERAL_CMD_SET_LOW_RR_ACTIVATE,
		low_rr_supported, 0);
}

void mod_power_set_video_conferencing_activate(struct mod_power *mod_power,
	const struct dc_stream_state *stream, bool video_conferencing_activate)
{
	struct dc_link *link = NULL;

	if (mod_power == NULL || stream == NULL)
		return;

	link = dc_stream_get_link(stream);
	if (!link || !link->replay_settings.replay_feature_enabled)
		return;

	mod_power_replay_set_general_cmd(mod_power, stream,
		REPLAY_GENERAL_CMD_VIDEO_CONFERENCING,
		video_conferencing_activate, 0);
}

void mod_power_set_coasting_vtotal_without_frame_update(struct mod_power *mod_power,
	const struct dc_stream_state *stream, uint32_t coasting_vtotal)
{
	struct dc_link *link = NULL;

	if (mod_power == NULL || stream == NULL)
		return;

	link = dc_stream_get_link(stream);
	if (!link || !link->replay_settings.replay_feature_enabled)
		return;

	mod_power_replay_set_general_cmd(mod_power, stream,
		REPLAY_GENERAL_CMD_SET_COASTING_VTOTAL_WITHOUT_FRAME_UPDATE,
		coasting_vtotal, 0);
}

void mod_power_set_replay_continuously_resync(struct mod_power *mod_power,
	const struct dc_stream_state *stream, bool enable)
{
	struct dc_link *link = NULL;

	if (mod_power == NULL || stream == NULL)
		return;

	link = dc_stream_get_link(stream);
	if (!link || !link->replay_settings.replay_feature_enabled)
		return;

	mod_power_replay_set_general_cmd(mod_power, stream,
		REPLAY_GENERAL_CMD_SET_CONTINUOUSLY_RESYNC,
		enable, 0);
}

void mod_power_set_live_capture_with_cvt_activate(struct mod_power *mod_power,
	const struct dc_stream_state *stream, bool live_capture_with_cvt_activate)
{
	struct dc_link *link = NULL;

	if (mod_power == NULL || stream == NULL)
		return;

	link = dc_stream_get_link(stream);
	if (!link || !link->replay_settings.replay_feature_enabled)
		return;

	// Check if LIVE_CAPTURE_WITH_CVT bit is enabled in replay optimization config
	if (!link->replay_settings.config.replay_optimization.bits.LIVE_CAPTURE_WITH_CVT)
		return;

	if (link->replay_settings.config.live_capture_with_cvt_activated != live_capture_with_cvt_activate) {
		link->replay_settings.config.live_capture_with_cvt_activated = live_capture_with_cvt_activate;
		mod_power_replay_set_general_cmd(mod_power, stream,
			REPLAY_GENERAL_CMD_LIVE_CAPTURE_WITH_CVT,
			live_capture_with_cvt_activate, 0);
	}
}

bool mod_power_set_replay_event(struct mod_power *mod_power,
	struct dc_stream_state *stream, bool set_event,
	enum replay_event event, bool wait_for_disable)
{
	struct core_power *core_power = NULL;
	struct dc_link *link = NULL;
	unsigned int stream_index = 0;
	unsigned int active_replay_events = 0;
	bool replay_active_request = false;
	bool force_static = false;
	uint32_t coasting_vtotal = 0;
	bool current_timing_sync_status = false;
	bool is_full_screen_video = false;
	bool is_ultra_sleep_mode = false;
	unsigned int sink_duration_us = 0;
	bool low_rr_active = false;
	uint16_t frame_skip_number = 0;
	bool is_video_playback = false;

	if (!stream)
		return false;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return false;

	stream_index = map_index_from_stream(core_power, stream);

	if (set_event)
		core_power->map[stream_index].replay_events |= event;
	else
		core_power->map[stream_index].replay_events &= ~event;

	link = dc_stream_get_link(stream);
	if (!link || !link->replay_settings.replay_feature_enabled)
		return false;

	if ((core_power->map[stream_index].replay_events & replay_event_disable_replay_while_switching_mux) != 0)
		return false;

	if ((core_power->map[stream_index].replay_events & replay_event_os_override_hold) != 0)
		return false;

	active_replay_events = core_power->map[stream_index].replay_events;

	current_timing_sync_status =
		link->replay_settings.config.replay_timing_sync_supported;

	replay_active_request = mod_power_update_replay_active_status(active_replay_events,
		link, &coasting_vtotal, &is_full_screen_video, &is_ultra_sleep_mode, &frame_skip_number, &is_video_playback);

	if (is_full_screen_video)
		mod_power_replay_set_pseudo_vtotal(mod_power, stream,
			link->replay_settings.low_rr_full_screen_video_pseudo_vtotal);
	else
		mod_power_replay_set_pseudo_vtotal(mod_power, stream, 0);

	//If timing_sync_status change, then re-enabled set timing_sync_supported value and re-enabled replay
	if (current_timing_sync_status != link->replay_settings.config.replay_timing_sync_supported)
		mod_power_replay_set_timing_sync_supported(mod_power, stream);

	if (link->replay_settings.config.low_rr_supported) {
		sink_duration_us =
			(unsigned int)(div_u64(((unsigned long long)(coasting_vtotal)
				* 10000) * stream->timing.h_total,
					stream->timing.pix_clk_100hz));
		low_rr_active = sink_duration_us < LOW_REFRESH_RATE_DURATION_US_UPPER_BOUND ? false : true;
		if (low_rr_active != link->replay_settings.config.low_rr_activated) {
			mod_power_set_low_rr_activate(mod_power, stream, low_rr_active);
			link->replay_settings.config.low_rr_activated = low_rr_active;
		}
	}

	// The function return fail when
	// 1. DMUB function is not support (for backward compatible).
	// 2. active_replay_events or coasting_vtotal is not updated in the same time
	if (!mod_power_replay_set_power_opt_and_coasting_vtotal(mod_power,
		stream, active_replay_events, coasting_vtotal, is_ultra_sleep_mode, frame_skip_number)) {
		if (!mod_power_replay_set_power_opt(mod_power, stream, active_replay_events, is_ultra_sleep_mode))
			return false;

		if (!mod_power_replay_set_coasting_vtotal(mod_power, stream, coasting_vtotal, frame_skip_number))
			return false;
	}

	mod_power_set_live_capture_with_cvt_activate(mod_power, stream, is_video_playback);

	mod_power_update_error_status(mod_power, stream);

	// If Replay is going to be enable (No matter is disable -> enable or enable -> enable), we don't need to wait.
	// If Replay is going to be disable
	//     if disable -> disable
	//         -> Replay DMUB state should be state 0.
	//            So no matter wait_for_disable is true or not, it should makes no difference.
	//     if enable -> disable -> We should wait if wait_for_disable is true.
	if (replay_active_request)
		wait_for_disable = false;

	if (!mod_power_set_replay_active(stream, replay_active_request, wait_for_disable, force_static))
		return false;

	return true;
}

bool mod_power_get_replay_active_status(const struct dc_stream_state *stream,
	bool *replay_active)
{
	const struct dc_link *link = NULL;

	if (!stream)
		return false;

	link = dc_stream_get_link(stream);
	*replay_active = link->replay_settings.replay_allow_active;

	return true;
}

void mod_power_replay_residency(const struct dc_stream_state *stream,
	unsigned int *residency, const bool is_start, const bool is_alpm)
{
	const struct dc_link *link = NULL;
	enum pr_residency_mode mode;

	if (!stream)
		return;

	link = dc_stream_get_link(stream);

	if (is_alpm)
		mode = PR_RESIDENCY_MODE_ALPM;
	else
		mode = PR_RESIDENCY_MODE_PHY;

	if (link && link->dc && link->dc->link_srv)
		link->dc->link_srv->edp_replay_residency(link, residency, is_start, mode);
}

bool mod_power_replay_set_power_opt_and_coasting_vtotal(struct mod_power *mod_power,
	const struct dc_stream_state *stream, unsigned int active_replay_events, uint32_t coasting_vtotal,
	bool is_ultra_sleep_mode, uint16_t frame_skip_number)
{
	struct core_power *core_power = NULL;
	struct dc_link *link = NULL;
	unsigned int power_opt = 0;

	if (!stream)
		return false;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return false;

	link = dc_stream_get_link(stream);

	if (!link || !link->replay_settings.replay_feature_enabled)
		return false;

	power_opt = mod_power_replay_setup_power_opt(link, active_replay_events, is_ultra_sleep_mode);

	return link->dc->link_srv->edp_set_replay_power_opt_and_coasting_vtotal(link, &power_opt, coasting_vtotal, frame_skip_number);
}

void mod_power_replay_notify_mode_change(struct mod_power *mod_power,
	struct dc *dc,
	struct dc_link *link,
	const struct dc_stream_state *stream,
	unsigned int stream_index)
{
	struct core_power *core_power = NULL;
	int active_replay_events = 0;

	if (!mod_power || !dc || !link || !stream)
		return;

	core_power = MOD_POWER_TO_CORE(mod_power);
	active_replay_events = core_power->map[stream_index].replay_events;

	link->replay_settings.replay_smu_opt_enable =
		(link->replay_settings.config.replay_smu_opt_supported &&
		mod_power_only_edp(dc->current_state, stream));

	if (active_replay_events & replay_event_os_request_force_ffu) {
		link->replay_settings.config.os_request_force_ffu = true;
	}

	if (dc_is_embedded_signal(stream->signal))
		dc->link_srv->dp_setup_replay(link, stream);
}

void init_replay_config(struct dc_link *link, struct replay_config *pr_config)
{
	link->replay_settings.config = *pr_config;
}

void set_replay_frame_skip_number(struct dc_link *link,
	enum replay_coasting_vtotal_type type,
	uint32_t coasting_vtotal_refresh_rate_uhz,
	uint32_t flicker_free_refresh_rate_uhz,
	bool is_defer)
{
	uint32_t *frame_skip_number_array = NULL;
	uint32_t frame_skip_number = 0;

	if (link == NULL)
		return;

	if (link->replay_settings.config.frame_skip_supported == false)
		return;

	if (flicker_free_refresh_rate_uhz == 0 || coasting_vtotal_refresh_rate_uhz == 0)
		return;

	if (is_defer)
		frame_skip_number_array = link->replay_settings.defer_frame_skip_number_table;
	else
		frame_skip_number_array = link->replay_settings.frame_skip_number_table;

	if (frame_skip_number_array == NULL)
		return;

	frame_skip_number = (coasting_vtotal_refresh_rate_uhz + 500000) / flicker_free_refresh_rate_uhz;

	if (frame_skip_number >= 1)
		frame_skip_number_array[type] = frame_skip_number - 1;
	else
		frame_skip_number_array[type] = 0;
}

void set_replay_defer_update_coasting_vtotal(struct dc_link *link,
	enum replay_coasting_vtotal_type type,
	uint32_t vtotal)
{
	link->replay_settings.defer_update_coasting_vtotal_table[type] = vtotal;
}

void update_replay_coasting_vtotal_from_defer(struct dc_link *link,
	enum replay_coasting_vtotal_type type)
{
	link->replay_settings.coasting_vtotal_table[type] =
		link->replay_settings.defer_update_coasting_vtotal_table[type];
	link->replay_settings.frame_skip_number_table[type] =
		link->replay_settings.defer_frame_skip_number_table[type];
}

void set_replay_coasting_vtotal(struct dc_link *link,
	enum replay_coasting_vtotal_type type,
	uint32_t vtotal)
{
	link->replay_settings.coasting_vtotal_table[type] = vtotal;
}

void set_replay_low_rr_full_screen_video_src_vtotal(struct dc_link *link, uint16_t vtotal)
{
	link->replay_settings.low_rr_full_screen_video_pseudo_vtotal = vtotal;
}

void calculate_replay_link_off_frame_count(struct dc_link *link,
	uint16_t vtotal, uint16_t htotal)
{
	uint32_t max_link_off_frame_count = 0;
	uint16_t max_deviation_line = 0,  pixel_deviation_per_line = 0;

	if (!link || link->replay_settings.config.replay_version != DC_FREESYNC_REPLAY)
		return;

	max_deviation_line = link->dpcd_caps.pr_info.max_deviation_line;
	pixel_deviation_per_line = link->dpcd_caps.pr_info.pixel_deviation_per_line;

	if (htotal != 0 && vtotal != 0 && pixel_deviation_per_line != 0)
		max_link_off_frame_count = htotal * max_deviation_line / (pixel_deviation_per_line * vtotal);
	else
		ASSERT(0);

	link->replay_settings.link_off_frame_count = max_link_off_frame_count;
}

void reset_replay_dsync_error_count(struct dc_link *link)
{
	link->replay_settings.replay_desync_error_fail_count = 0;
}
