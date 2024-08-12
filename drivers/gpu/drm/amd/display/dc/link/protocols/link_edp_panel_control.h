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

#ifndef __DC_LINK_EDP_PANEL_CONTROL_H__
#define __DC_LINK_EDP_PANEL_CONTROL_H__
#include "link.h"

enum dp_panel_mode dp_get_panel_mode(struct dc_link *link);
void dp_set_panel_mode(struct dc_link *link, enum dp_panel_mode panel_mode);
bool set_default_brightness_aux(struct dc_link *link);
void edp_panel_backlight_power_on(struct dc_link *link, bool wait_for_hpd);
int edp_get_backlight_level(const struct dc_link *link);
bool edp_get_backlight_level_nits(struct dc_link *link,
		uint32_t *backlight_millinits_avg,
		uint32_t *backlight_millinits_peak);
bool edp_set_backlight_level(const struct dc_link *link,
		uint32_t backlight_pwm_u16_16,
		uint32_t frame_ramp);
bool edp_set_backlight_level_nits(struct dc_link *link,
		bool isHDR,
		uint32_t backlight_millinits,
		uint32_t transition_time_in_ms);
int edp_get_target_backlight_pwm(const struct dc_link *link);
bool edp_get_psr_state(const struct dc_link *link, enum dc_psr_state *state);
bool edp_set_psr_allow_active(struct dc_link *link, const bool *allow_active,
		bool wait, bool force_static, const unsigned int *power_opts);
bool edp_setup_psr(struct dc_link *link,
		const struct dc_stream_state *stream, struct psr_config *psr_config,
		struct psr_context *psr_context);
bool edp_set_sink_vtotal_in_psr_active(const struct dc_link *link,
       uint16_t psr_vtotal_idle, uint16_t psr_vtotal_su);
void edp_get_psr_residency(const struct dc_link *link, uint32_t *residency, enum psr_residency_mode mode);
bool edp_set_replay_allow_active(struct dc_link *dc_link, const bool *enable,
	bool wait, bool force_static, const unsigned int *power_opts);
bool edp_setup_replay(struct dc_link *link,
		const struct dc_stream_state *stream);
bool edp_send_replay_cmd(struct dc_link *link,
			enum replay_FW_Message_type msg,
			union dmub_replay_cmd_set *cmd_data);
bool edp_set_coasting_vtotal(struct dc_link *link, uint32_t coasting_vtotal);
bool edp_replay_residency(const struct dc_link *link,
	unsigned int *residency, const bool is_start, const enum pr_residency_mode mode);
bool edp_get_replay_state(const struct dc_link *link, uint64_t *state);
bool edp_set_replay_power_opt_and_coasting_vtotal(struct dc_link *link,
	const unsigned int *power_opts, uint32_t coasting_vtotal);
bool edp_wait_for_t12(struct dc_link *link);
bool edp_is_ilr_optimization_required(struct dc_link *link,
       struct dc_crtc_timing *crtc_timing);
bool edp_is_ilr_optimization_enabled(struct dc_link *link);
enum dc_link_rate get_max_link_rate_from_ilr_table(struct dc_link *link);
bool edp_backlight_enable_aux(struct dc_link *link, bool enable);
void edp_add_delay_for_T9(struct dc_link *link);
bool edp_receiver_ready_T9(struct dc_link *link);
bool edp_receiver_ready_T7(struct dc_link *link);
bool edp_power_alpm_dpcd_enable(struct dc_link *link, bool enable);
void edp_set_panel_power(struct dc_link *link, bool powerOn);
void edp_set_panel_assr(struct dc_link *link, struct pipe_ctx *pipe_ctx,
		enum dp_panel_mode *panel_mode, bool enable);
#endif /* __DC_LINK_EDP_POWER_CONTROL_H__ */
