/* Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved. */

#ifndef MODULES_INC_MOD_POWER_H_
#define MODULES_INC_MOD_POWER_H_

#include "dm_services.h"

struct core_power;

struct mod_power_init_params {

	bool disable_fractional_pwm;

	/* Use nits based brightness instead of brightness percentage
	 */
	bool use_nits_based_brightness;
	unsigned int panel_min_millinits;
	unsigned int panel_max_millinits;

	unsigned int min_backlight_pwm;
	unsigned int max_backlight_pwm;

	unsigned int min_abm_backlight;
	unsigned int num_backlight_levels;
	bool backlight_ramping_override;
	unsigned int backlight_ramping_reduction;
	unsigned int backlight_ramping_start;
	bool def_varibright_enable;
	unsigned int def_varibright_level;
	unsigned int varibright_level;
	unsigned int abm_config_setting;

	bool allow_psr_smu_optimizations;

	bool allow_psr_multi_disp_optimizations;

	bool use_custom_backlight_caps;
	unsigned int custom_backlight_caps_config_no;
	bool use_linear_backlight_curve;
};

struct mod_power {
	int dummy;
};

/* VariBright settings structure */
struct varibright_info {
	unsigned int level;
	bool enable;
	bool activate;
};

struct mod_power_psr_context {
    /* ddc line */
    unsigned int channel;
    /* Transmitter id */
    unsigned int transmitter_id;
    /* Engine Id is used for Dig Be source select */
    unsigned int engine_id;
    /* Controller Id used for Dig Fe source select */
    unsigned int controller_id;
    /* Pcie or Uniphy */
    unsigned int phy_type;
    /* Physical PHY Id used by SMU interpretation */
    unsigned int smu_phy_id;
    /* Vertical total pixels from crtc timing.
     * This is used for static screen detection.
     * ie. If we want to detect half a frame,
     * we use this to determine the hyst lines.
     */
    unsigned int crtc_timing_vertical_total;
    /* PSR supported from panel capabilities and
     * current display configuration
     */
    bool psr_supported_display_config;
    /* Whether fast link training is supported by the panel */
    bool psr_exit_link_training_required;
    /* If RFB setup time is greater than the total VBLANK time,
     * it is not possible for the sink to capture the video frame
     * in the same frame the SDP is sent. In this case,
     * the frame capture indication bit should be set and an extra
     * static frame should be transmitted to the sink.
     */
    bool psr_frame_capture_indication_req;
    /* Set the last possible line SDP may be transmitted without violating
     * the RFB setup time or entering the active video frame.
     */
    unsigned int sdp_transmit_line_num_deadline;
    /* The VSync rate in Hz used to calculate the
     * step size for smooth brightness feature
     */
    unsigned int vsync_rate_hz;
    unsigned int skip_psr_wait_for_pll_lock;
    unsigned int number_of_controllers;
    /* Unused, for future use. To indicate that first changed frame from
     * state3 shouldn't result in psr_inactive, but rather to perform
     * an automatic single frame rfb_update.
     */
    bool rfb_update_auto_en;
    /* Number of frame before entering static screen */
    unsigned int timehyst_frames;
    /* Partial frames before entering static screen */
    unsigned int hyst_lines;
    /* # of repeated AUX transaction attempts to make before
     * indicating failure to the driver
     */
    unsigned int aux_repeats;
    /* Controls hw blocks to power down during PSR active state */
    unsigned int psr_level;
    /* Controls additional delay after remote frame capture before
     * continuing powerd own
     */
	unsigned int frame_delay;
	bool allow_smu_optimizations;
	bool allow_multi_disp_optimizations;
	unsigned int line_time_in_us;
	/* Panel self refresh 2 selective update granularity required */
	bool su_granularity_required;
	/* psr2 selective update y granularity capability */
	uint8_t su_y_granularity;
	uint8_t rate_control_caps;
	bool os_request_force_ffu;
};

enum psr_event {
	psr_event_invalid = 0x0,
	psr_event_vsync = 0x1,
	psr_event_full_screen = 0x2,
	psr_event_defer_enable = 0x4,
	psr_event_hw_programming = 0x8,
	psr_event_test_harness_enable_psr = 0x10,
	psr_event_test_harness_disable_psr = 0x20,
	psr_event_mpo_video_selective_update = 0x40,
	psr_event_edp_panel_off_disable_psr = 0x80,
	psr_event_dynamic_display_switch = 0x100,
	psr_event_big_screen_video = 0x200,
	psr_event_dds_defer_stream_enable = 0x800,
	psr_event_dynamic_link_rate_control = 0x1000,
	psr_event_vrr_transition = 0x2000,
	psr_event_pause = 0x4000,
	psr_event_immediate_flip = 0x8000,
	psr_event_os_request_disable = 0x10000,
	psr_event_os_request_force_ffu = 0x20000,
	psr_event_os_override_hold = 0x40000,
	psr_event_crc_window_active = 0x80000,
};

enum replay_event {
	replay_event_invalid = 0x0,
	replay_event_vsync = 0x1,
	replay_event_full_screen = 0x2,
	replay_event_mpo_video_selective_update = 0x4,
	replay_event_big_screen_video = 0x8,
	replay_event_hw_programming = 0x10,
	replay_event_edp_panel_off_disable_psr = 0x20,
	replay_event_general_ui = 0x40,
	replay_event_vrr = 0x80,
	replay_event_prepare_vtotal = 0x100,
	replay_event_test_harness_enable_replay = 0x200,
	replay_event_test_harness_disable_replay = 0x400,
	replay_event_test_harness_ultra_sleep = 0x800,
	replay_event_immediate_flip = 0x1000,
	replay_event_vrr_transition = 0x2000,
	replay_event_pause = 0x4000,
	replay_event_disable_replay_while_DPMS = 0x8000,
	replay_event_test_harness_mode = 0x10000,
	replay_event_cursor_updating = 0x20000,
	replay_event_sleep_resume = 0x40000,
	replay_event_disable_in_AC = 0x80000,
	replay_event_disable_replay_while_detect_display = 0x100000,
	replay_event_disable_replay_while_switching_mux = 0x400000,
	replay_event_infopacket = 0x800000,
	replay_event_os_request_disable = 0x1000000,
	replay_event_os_request_force_ffu = 0x2000000,
	replay_event_os_override_hold = 0x4000000,
	replay_event_crc_window_active = 0x8000000,
};

enum replay_enable_option {
	pr_enable_option_static_screen = 0x1,
	pr_enable_option_mpo_video = 0x2,
	pr_enable_option_full_screen_video = 0x4,
	pr_enable_option_general_ui = 0x8,
	pr_enable_option_full_screen = 0x10,
	pr_enable_option_static_screen_coasting = 0x10000,
	pr_enable_option_mpo_video_coasting = 0x20000,
	pr_enable_option_full_screen_video_coasting = 0x40000,
	pr_enable_option_full_screen_coasting = 0x100000,
};

struct mod_power *mod_power_create(struct dc *dc,
		struct mod_power_init_params *init_params,
		unsigned int edp_num);

void mod_power_destroy(struct mod_power *mod_power);

bool mod_power_hw_init(struct mod_power *mod_power);

bool mod_power_add_stream(struct mod_power *mod_power,
		struct dc_stream_state *stream, struct psr_caps *caps);

bool mod_power_remove_stream(struct mod_power *mod_power,
		const struct dc_stream_state *stream);

bool mod_power_replace_stream(struct mod_power *mod_power,
		const struct dc_stream_state *current_stream,
		struct dc_stream_state *new_stream,
		struct psr_caps *new_caps);

bool mod_power_set_backlight_nits(struct mod_power *mod_power,
		struct dc_stream_state *streams,
		unsigned int backlight_millinit,
		unsigned int transition_time_millisec,
		bool skip_aux,
		bool is_hdr);

bool mod_power_set_backlight_percent(struct mod_power *mod_power,
		struct dc_stream_state *stream,
		unsigned int backlight_millipercent,
		unsigned int transition_time_millisec,
		bool is_hdr);

void mod_power_update_backlight(struct mod_power *mod_power,
		struct dc_stream_state *stream,
		unsigned int backlight_millipercent);

void mod_power_update_backlight_nits(struct mod_power *mod_power,
		struct dc_stream_state *stream,
		unsigned int backlight_millinit);

bool mod_power_get_backlight_pwm(struct mod_power *mod_power,
		unsigned int *backlight_pwm,
		unsigned int inst);

bool mod_power_get_backlight_nits(struct mod_power *mod_power,
		unsigned int *backlight_millinit,
		unsigned int inst);

bool mod_power_get_backlight_percent(struct mod_power *mod_power,
		unsigned int *backlight_millipercent,
		unsigned int inst);

bool mod_power_get_hw_target_backlight_pwm_nits(
		struct mod_power *mod_power,
		const struct dc_link *link,
		unsigned int *backlight_millinit,
		unsigned int inst);

bool mod_power_get_hw_target_backlight_pwm_percent(
		struct mod_power *mod_power,
		const struct dc_link *link,
		unsigned int *backlight_millipercent,
		unsigned int inst);

bool mod_power_get_hw_target_backlight_pwm(
		struct mod_power *mod_power,
		const struct dc_link *link,
		unsigned int *backlight_u16_16);

bool mod_power_get_hw_backlight_pwm(
		struct mod_power *mod_power,
		const struct dc_link *link,
		unsigned int *backlight);

bool mod_power_get_hw_backlight_pwm_nits(
		struct mod_power *mod_power,
		const struct dc_link *link,
		unsigned int *backlight_millinit,
		unsigned int inst);

bool mod_power_get_hw_backlight_aux_nits(
		struct mod_power *mod_power,
		struct dc_stream_state **streams, int num_streams,
		unsigned int *backlight_millinit_avg,
		unsigned int *backlight_millinit_peak);

bool mod_power_get_hw_backlight_pwm_percent(
		struct mod_power *mod_power,
		const struct dc_link *link,
		unsigned int *backlight_millipercent,
		unsigned int inst);

void mod_power_initialize_backlight_caps
		(struct mod_power *mod_power);

bool mod_power_get_panel_backlight_boundaries
				(struct mod_power *mod_power,
				unsigned int *out_min_backlight,
				unsigned int *out_max_backlight,
				unsigned int *out_ac_backlight_percent,
				unsigned int *out_dc_backlight_percent,
				unsigned int inst);

bool mod_power_set_smooth_brightness(struct mod_power *mod_power,
		bool enable_brightness,
		unsigned int inst);

bool mod_power_notify_mode_change(struct mod_power *mod_power,
		const struct dc_stream_state *stream,
		bool is_hdr);

bool mod_power_get_varibright_level(struct mod_power *mod_power,
		unsigned int *varibright_level);

bool mod_power_get_varibright_hw_level(struct mod_power *mod_power,
		unsigned int *varibright_level);

bool mod_power_get_varibright_default_level(struct mod_power *mod_power,
		unsigned int *varibright_level);

bool mod_power_get_varibright_enable(struct mod_power *mod_power,
		bool *varibright_enable);

bool mod_power_varibright_activate(struct mod_power	*mod_power,
		bool activate, struct dc_stream_update *stream_update);

bool mod_power_varibright_feature_enable(struct mod_power *mod_power,
		bool enable, struct dc_stream_update *stream_update);


bool mod_power_varibright_set_level(struct mod_power *mod_power,
		unsigned int level, struct dc_stream_update *stream_update);

bool mod_power_varibright_set_hw_level(struct mod_power *mod_power,
		unsigned int level,	struct dc_stream_update *stream_update);

bool mod_power_is_abm_active(struct mod_power *mod_power,
		const struct dc_link *link,
		unsigned int inst);


bool mod_power_set_psr_event(struct mod_power *mod_power,
		struct dc_stream_state *stream, bool set_event,
		enum psr_event event, bool wait);

bool mod_power_get_psr_event(struct mod_power *mod_power,
			struct dc_stream_state *stream,
			unsigned int *active_psr_events);

bool mod_power_get_psr_state(struct mod_power *mod_power,
		const struct dc_stream_state *stream,
		enum dc_psr_state *state);

bool mod_power_get_psr_enabled_status(struct mod_power *mod_power,
		const struct dc_stream_state *stream,
		bool *psr_enabled);

bool mod_power_set_replay_event(struct mod_power *mod_power,
	struct dc_stream_state *stream, bool set_event,
	enum replay_event event, bool wait_for_disable);

bool mod_power_get_replay_event(struct mod_power *mod_power,
	struct dc_stream_state *stream,
	unsigned int *active_replay_events);

bool mod_power_get_replay_active_status(const struct dc_stream_state *stream,
	bool *replay_active);

bool mod_power_replay_set_coasting_vtotal(struct mod_power *mod_power,
	const struct dc_stream_state *stream,
	uint32_t coasting_vtotal, uint16_t frame_skip_number);

void mod_power_replay_residency(const struct dc_stream_state *stream,
	unsigned int *residency, const bool is_start, const bool is_alpm);

bool mod_power_replay_set_power_opt_and_coasting_vtotal(struct mod_power *mod_power,
	const struct dc_stream_state *stream, unsigned int active_replay_events, uint32_t coasting_vtotal,
	bool is_ultra_sleep_mode, uint16_t frame_skip_number);

void mod_power_replay_set_timing_sync_supported(struct mod_power *mod_power,
	const struct dc_stream_state *stream);

void mod_power_replay_disabled_adaptive_sync_sdp(struct mod_power *mod_power,
	const struct dc_stream_state *stream, bool force_disabled);

void mod_power_replay_disabled_desync_error_detection(struct mod_power *mod_power,
	const struct dc_stream_state *stream,  bool force_disabled);
void mod_power_set_low_rr_activate(struct mod_power *mod_power,
	const struct dc_stream_state *stream, bool low_rr_supported);

void mod_power_set_video_conferencing_activate(struct mod_power *mod_power,
	const struct dc_stream_state *stream, bool video_conferencing_activate);

void mod_power_set_live_capture_with_cvt_activate(struct mod_power *mod_power,
	const struct dc_stream_state *stream, bool live_capture_with_cvt_activate);

void mod_power_set_replay_continuously_resync(struct mod_power *mod_power,
	const struct dc_stream_state *stream, bool enable);

void mod_power_set_coasting_vtotal_without_frame_update(struct mod_power *mod_power,
	const struct dc_stream_state *stream, uint32_t coasting_vtotal);



void mod_power_psr_residency(struct mod_power *mod_power,
		const struct dc_stream_state *stream,
		unsigned int *residency,
		const uint8_t mode);
bool mod_power_psr_get_active_psr_events(struct mod_power *mod_power,
		const struct dc_stream_state *stream, unsigned int *active_psr_events);
bool mod_power_psr_set_sink_vtotal_in_psr_active(struct mod_power *mod_power,
		const struct dc_stream_state *stream,
		uint16_t psr_vtotal_idle,
		uint16_t psr_vtotal_su);



bool mod_power_backlight_percent_to_nits(struct mod_power *mod_power,
		struct dc_stream_state *stream,
		unsigned int backlight_millipercent,
		unsigned int *backlight_millinit);
bool mod_power_backlight_nits_to_percent(struct mod_power *mod_power,
		struct dc_stream_state *stream,
		unsigned int backlight_millinit,
		unsigned int *backlight_millipercent);

#endif /* MODULES_INC_MOD_POWER_H_ */
