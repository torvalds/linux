/*
* Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef __DC_HWSS_DCN10_H__
#define __DC_HWSS_DCN10_H__

#include "core_types.h"
#include "hw_sequencer_private.h"

struct dc;

void dcn10_hw_sequencer_construct(struct dc *dc);

int dcn10_get_vupdate_offset_from_vsync(struct pipe_ctx *pipe_ctx);
void dcn10_calc_vupdate_position(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		uint32_t *start_line,
		uint32_t *end_line);
void dcn10_setup_vupdate_interrupt(struct dc *dc, struct pipe_ctx *pipe_ctx);
enum dc_status dcn10_enable_stream_timing(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc);
void dcn10_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context);
void dcn10_prepare_bandwidth(
		struct dc *dc,
		struct dc_state *context);
void dcn10_pipe_control_lock(
	struct dc *dc,
	struct pipe_ctx *pipe,
	bool lock);
void dcn10_cursor_lock(struct dc *dc, struct pipe_ctx *pipe, bool lock);
void dcn10_blank_pixel_data(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool blank);
void dcn10_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings);
void dcn10_program_output_csc(struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix,
		int opp_id);
bool dcn10_set_output_transfer_func(struct dc *dc, struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream);
bool dcn10_set_input_transfer_func(struct dc *dc, struct pipe_ctx *pipe_ctx,
			const struct dc_plane_state *plane_state);
void dcn10_update_plane_addr(const struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn10_update_mpcc(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn10_reset_hw_ctx_wrap(
		struct dc *dc,
		struct dc_state *context);
void dcn10_disable_plane(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn10_lock_all_pipes(
		struct dc *dc,
		struct dc_state *context,
		bool lock);
void dcn10_apply_ctx_for_surface(
		struct dc *dc,
		const struct dc_stream_state *stream,
		int num_planes,
		struct dc_state *context);
void dcn10_post_unlock_program_front_end(
		struct dc *dc,
		struct dc_state *context);
void dcn10_hubp_pg_control(
		struct dce_hwseq *hws,
		unsigned int hubp_inst,
		bool power_on);
void dcn10_dpp_pg_control(
		struct dce_hwseq *hws,
		unsigned int dpp_inst,
		bool power_on);
void dcn10_enable_power_gating_plane(
	struct dce_hwseq *hws,
	bool enable);
void dcn10_plane_atomic_disable(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn10_disable_vga(
	struct dce_hwseq *hws);
void dcn10_program_pipe(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context);
void dcn10_program_gamut_remap(struct pipe_ctx *pipe_ctx);
void dcn10_init_hw(struct dc *dc);
void dcn10_init_pipes(struct dc *dc, struct dc_state *context);
void dcn10_power_down_on_boot(struct dc *dc);
enum dc_status dce110_apply_ctx_to_hw(
		struct dc *dc,
		struct dc_state *context);
void dcn10_plane_atomic_disconnect(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn10_update_dchub(struct dce_hwseq *hws, struct dchub_init_data *dh_data);
void dcn10_update_pending_status(struct pipe_ctx *pipe_ctx);
void dce110_power_down(struct dc *dc);
void dce110_enable_accelerated_mode(struct dc *dc, struct dc_state *context);
void dcn10_enable_timing_synchronization(
		struct dc *dc,
		int group_index,
		int group_size,
		struct pipe_ctx *grouped_pipes[]);
void dcn10_enable_per_frame_crtc_position_reset(
		struct dc *dc,
		int group_size,
		struct pipe_ctx *grouped_pipes[]);
void dce110_update_info_frame(struct pipe_ctx *pipe_ctx);
void dcn10_send_immediate_sdp_message(struct pipe_ctx *pipe_ctx,
		const uint8_t *custom_sdp_message,
		unsigned int sdp_message_size);
void dce110_blank_stream(struct pipe_ctx *pipe_ctx);
void dce110_enable_audio_stream(struct pipe_ctx *pipe_ctx);
void dce110_disable_audio_stream(struct pipe_ctx *pipe_ctx);
bool dcn10_dummy_display_power_gating(
		struct dc *dc,
		uint8_t controller_id,
		struct dc_bios *dcb,
		enum pipe_gating_control power_gating);
void dcn10_set_drr(struct pipe_ctx **pipe_ctx,
		int num_pipes, unsigned int vmin, unsigned int vmax,
		unsigned int vmid, unsigned int vmid_frame_number);
void dcn10_get_position(struct pipe_ctx **pipe_ctx,
		int num_pipes,
		struct crtc_position *position);
void dcn10_set_static_screen_control(struct pipe_ctx **pipe_ctx,
		int num_pipes, const struct dc_static_screen_params *params);
void dcn10_setup_stereo(struct pipe_ctx *pipe_ctx, struct dc *dc);
void dce110_set_avmute(struct pipe_ctx *pipe_ctx, bool enable);
void dcn10_log_hw_state(struct dc *dc,
		struct dc_log_buffer_ctx *log_ctx);
void dcn10_get_hw_state(struct dc *dc,
		char *pBuf,
		unsigned int bufSize,
		unsigned int mask);
void dcn10_clear_status_bits(struct dc *dc, unsigned int mask);
void dcn10_wait_for_mpcc_disconnect(
		struct dc *dc,
		struct resource_pool *res_pool,
		struct pipe_ctx *pipe_ctx);
void dce110_edp_backlight_control(
		struct dc_link *link,
		bool enable);
void dce110_edp_power_control(
		struct dc_link *link,
		bool power_up);
void dce110_edp_wait_for_hpd_ready(
		struct dc_link *link,
		bool power_up);
void dcn10_set_cursor_position(struct pipe_ctx *pipe_ctx);
void dcn10_set_cursor_attribute(struct pipe_ctx *pipe_ctx);
void dcn10_set_cursor_sdr_white_level(struct pipe_ctx *pipe_ctx);
void dcn10_setup_periodic_interrupt(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum vline_select vline);
enum dc_status dcn10_set_clock(struct dc *dc,
		enum dc_clock_type clock_type,
		uint32_t clk_khz,
		uint32_t stepping);
void dcn10_get_clock(struct dc *dc,
		enum dc_clock_type clock_type,
		struct dc_clock_config *clock_cfg);
bool dcn10_did_underflow_occur(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn10_bios_golden_init(struct dc *dc);
void dcn10_plane_atomic_power_down(struct dc *dc,
		struct dpp *dpp,
		struct hubp *hubp);
void dcn10_get_surface_visual_confirm_color(
		const struct pipe_ctx *pipe_ctx,
		struct tg_color *color);
void dcn10_get_hdr_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);
bool dcn10_disconnect_pipes(
		struct dc *dc,
		struct dc_state *context);

void dcn10_wait_for_pending_cleared(struct dc *dc,
		struct dc_state *context);
void dcn10_set_hdr_multiplier(struct pipe_ctx *pipe_ctx);
void dcn10_verify_allow_pstate_change_high(struct dc *dc);

#endif /* __DC_HWSS_DCN10_H__ */
