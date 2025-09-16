/*
* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_HWSS_DCE110_H__
#define __DC_HWSS_DCE110_H__

#include "core_types.h"
#include "hw_sequencer_private.h"

struct dc;
struct dc_state;
struct dm_pp_display_configuration;

void dce110_hw_sequencer_construct(struct dc *dc);

enum dc_status dce110_apply_ctx_to_hw(
		struct dc *dc,
		struct dc_state *context);

enum dc_status dce110_apply_single_controller_ctx_to_hw(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc);

void dce110_enable_stream(struct pipe_ctx *pipe_ctx);

void dce110_disable_stream(struct pipe_ctx *pipe_ctx);

void dce110_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings);

void dce110_blank_stream(struct pipe_ctx *pipe_ctx);

void dce110_enable_audio_stream(struct pipe_ctx *pipe_ctx);
void dce110_disable_audio_stream(struct pipe_ctx *pipe_ctx);

void dce110_update_info_frame(struct pipe_ctx *pipe_ctx);

void dce110_set_avmute(struct pipe_ctx *pipe_ctx, bool enable);
void dce110_enable_accelerated_mode(struct dc *dc, struct dc_state *context);

void dce110_power_down(struct dc *dc);

void dce110_set_safe_displaymarks(
		struct resource_context *res_ctx,
		const struct resource_pool *pool);

void dce110_prepare_bandwidth(
		struct dc *dc,
		struct dc_state *context);

void dce110_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context);

void dce110_edp_power_control(
		struct dc_link *link,
		bool power_up);

void dce110_edp_backlight_control(
	struct dc_link *link,
	bool enable);

void dce110_edp_wait_for_hpd_ready(
		struct dc_link *link,
		bool power_up);

bool dce110_set_backlight_level(struct pipe_ctx *pipe_ctx,
	struct set_backlight_level_params *params);
void dce110_set_abm_immediate_disable(struct pipe_ctx *pipe_ctx);
void dce110_set_pipe(struct pipe_ctx *pipe_ctx);
void dce110_disable_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal);
void dce110_enable_lvds_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum clock_source_id clock_source,
		uint32_t pixel_clock);
void dce110_enable_tmds_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		enum dc_color_depth color_depth,
		uint32_t pixel_clock);
void dce110_enable_dp_link_output(
		struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		const struct dc_link_settings *link_settings);
void build_audio_output(
		struct dc_state *state,
		const struct pipe_ctx *pipe_ctx,
		struct audio_output *audio_output);
enum audio_dto_source translate_to_dto_source(enum controller_id crtc_id);
void populate_audio_dp_link_info(
	const struct pipe_ctx *pipe_ctx,
	struct audio_dp_link_info *dp_link_info);
void enable_fbc(
	struct dc *dc,
	struct dc_state *context);
#endif /* __DC_HWSS_DCE110_H__ */

