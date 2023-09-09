/*
* Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef __DC_HWSS_DCN30_H__
#define __DC_HWSS_DCN30_H__

#include "hw_sequencer_private.h"
#include "dcn20/dcn20_hwseq.h"
struct dc;

void dcn30_init_hw(struct dc *dc);
void dcn30_program_all_writeback_pipes_in_tree(
		struct dc *dc,
		const struct dc_stream_state *stream,
		struct dc_state *context);
void dcn30_update_writeback(
		struct dc *dc,
		struct dc_writeback_info *wb_info,
		struct dc_state *context);
void dcn30_enable_writeback(
		struct dc *dc,
		struct dc_writeback_info *wb_info,
		struct dc_state *context);
void dcn30_disable_writeback(
		struct dc *dc,
		unsigned int dwb_pipe_inst);

bool dcn30_mmhubbub_warmup(
	struct dc *dc,
	unsigned int num_dwb,
	struct dc_writeback_info *wb_info);

bool dcn30_set_blend_lut(struct pipe_ctx *pipe_ctx,
		const struct dc_plane_state *plane_state);

bool dcn30_set_input_transfer_func(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_plane_state *plane_state);
bool dcn30_set_output_transfer_func(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream);
void dcn30_set_avmute(struct pipe_ctx *pipe_ctx, bool enable);
void dcn30_update_info_frame(struct pipe_ctx *pipe_ctx);
void dcn30_program_dmdata_engine(struct pipe_ctx *pipe_ctx);

bool dcn30_does_plane_fit_in_mall(struct dc *dc, struct dc_plane_state *plane,
		struct dc_cursor_attributes *cursor_attr);

bool dcn30_apply_idle_power_optimizations(struct dc *dc, bool enable);

void dcn30_hardware_release(struct dc *dc);

void dcn30_set_disp_pattern_generator(const struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum controller_dp_test_pattern test_pattern,
		enum controller_dp_color_space color_space,
		enum dc_color_depth color_depth,
		const struct tg_color *solid_color,
		int width, int height, int offset);

void dcn30_set_hubp_blank(const struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool blank_enable);

void dcn30_prepare_bandwidth(struct dc *dc,
	struct dc_state *context);

void dcn30_set_static_screen_control(struct pipe_ctx **pipe_ctx,
		int num_pipes, const struct dc_static_screen_params *params);

#endif /* __DC_HWSS_DCN30_H__ */
