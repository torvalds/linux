/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef __DC_HW_SEQUENCER_H__
#define __DC_HW_SEQUENCER_H__
#include "dc_types.h"
#include "clock_source.h"
#include "inc/hw/timing_generator.h"
#include "inc/hw/opp.h"
#include "inc/hw/link_encoder.h"
#include "core_status.h"

#define EDP_BACKLIGHT_RAMP_DISABLE_LEVEL 0xFFFFFFFF

enum pipe_gating_control {
	PIPE_GATING_CONTROL_DISABLE = 0,
	PIPE_GATING_CONTROL_ENABLE,
	PIPE_GATING_CONTROL_INIT
};

struct dce_hwseq_wa {
	bool blnd_crtc_trigger;
	bool DEGVIDCN10_253;
	bool false_optc_underflow;
};

struct hwseq_wa_state {
	bool DEGVIDCN10_253_applied;
};

struct dce_hwseq {
	struct dc_context *ctx;
	const struct dce_hwseq_registers *regs;
	const struct dce_hwseq_shift *shifts;
	const struct dce_hwseq_mask *masks;
	struct dce_hwseq_wa wa;
	struct hwseq_wa_state wa_state;
};

struct pipe_ctx;
struct dc_state;
struct dchub_init_data;
struct dc_static_screen_events;
struct resource_pool;
struct resource_context;

struct hw_sequencer_funcs {

	void (*init_hw)(struct dc *dc);

	enum dc_status (*apply_ctx_to_hw)(
			struct dc *dc, struct dc_state *context);

	void (*reset_hw_ctx_wrap)(
			struct dc *dc, struct dc_state *context);

	void (*apply_ctx_for_surface)(
			struct dc *dc,
			const struct dc_stream_state *stream,
			int num_planes,
			struct dc_state *context);

	void (*set_plane_config)(
			const struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			struct resource_context *res_ctx);

	void (*program_gamut_remap)(
			struct pipe_ctx *pipe_ctx);

	void (*program_csc_matrix)(
			struct pipe_ctx *pipe_ctx,
			enum dc_color_space colorspace,
			uint16_t *matrix);

	void (*update_plane_addr)(
		const struct dc *dc,
		struct pipe_ctx *pipe_ctx);

	void (*update_dchub)(
		struct dce_hwseq *hws,
		struct dchub_init_data *dh_data);

	void (*update_pending_status)(
			struct pipe_ctx *pipe_ctx);

	bool (*set_input_transfer_func)(
				struct pipe_ctx *pipe_ctx,
				const struct dc_plane_state *plane_state);

	bool (*set_output_transfer_func)(
				struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream);

	void (*power_down)(struct dc *dc);

	void (*enable_accelerated_mode)(struct dc *dc, struct dc_state *context);

	void (*enable_timing_synchronization)(
			struct dc *dc,
			int group_index,
			int group_size,
			struct pipe_ctx *grouped_pipes[]);

	void (*enable_per_frame_crtc_position_reset)(
			struct dc *dc,
			int group_size,
			struct pipe_ctx *grouped_pipes[]);

	void (*enable_display_pipe_clock_gating)(
					struct dc_context *ctx,
					bool clock_gating);

	bool (*enable_display_power_gating)(
					struct dc *dc,
					uint8_t controller_id,
					struct dc_bios *dcb,
					enum pipe_gating_control power_gating);

	void (*disable_plane)(struct dc *dc, struct pipe_ctx *pipe_ctx);

	void (*update_info_frame)(struct pipe_ctx *pipe_ctx);

	void (*enable_stream)(struct pipe_ctx *pipe_ctx);

	void (*disable_stream)(struct pipe_ctx *pipe_ctx,
			int option);

	void (*unblank_stream)(struct pipe_ctx *pipe_ctx,
			struct dc_link_settings *link_settings);

	void (*blank_stream)(struct pipe_ctx *pipe_ctx);
	void (*pipe_control_lock)(
				struct dc *dc,
				struct pipe_ctx *pipe,
				bool lock);

	void (*set_bandwidth)(
			struct dc *dc,
			struct dc_state *context,
			bool decrease_allowed);

	void (*set_drr)(struct pipe_ctx **pipe_ctx, int num_pipes,
			int vmin, int vmax);

	void (*get_position)(struct pipe_ctx **pipe_ctx, int num_pipes,
			struct crtc_position *position);

	void (*set_static_screen_control)(struct pipe_ctx **pipe_ctx,
			int num_pipes, const struct dc_static_screen_events *events);

	enum dc_status (*prog_pixclk_crtc_otg)(
			struct pipe_ctx *pipe_ctx,
			struct dc_state *context,
			struct dc *dc);

	void (*setup_stereo)(
			struct pipe_ctx *pipe_ctx,
			struct dc *dc);

	void (*set_avmute)(struct pipe_ctx *pipe_ctx, bool enable);

	void (*log_hw_state)(struct dc *dc);

	void (*wait_for_mpcc_disconnect)(struct dc *dc,
			struct resource_pool *res_pool,
			struct pipe_ctx *pipe_ctx);

	void (*ready_shared_resources)(struct dc *dc, struct dc_state *context);
	void (*optimize_shared_resources)(struct dc *dc);
	void (*pplib_apply_display_requirements)(
			struct dc *dc,
			struct dc_state *context);
	void (*edp_power_control)(
			struct dc_link *link,
			bool enable);
	void (*edp_backlight_control)(
			struct dc_link *link,
			bool enable);
	void (*edp_wait_for_hpd_ready)(struct dc_link *link, bool power_up);

	void (*set_cursor_position)(struct pipe_ctx *pipe);
	void (*set_cursor_attribute)(struct pipe_ctx *pipe);
};

void color_space_to_black_color(
	const struct dc *dc,
	enum dc_color_space colorspace,
	struct tg_color *black_color);

bool hwss_wait_for_blank_complete(
		struct timing_generator *tg);

const uint16_t *find_color_matrix(
		enum dc_color_space color_space,
		uint32_t *array_size);

#endif /* __DC_HW_SEQUENCER_H__ */
