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

struct dc;

void dcn10_hw_sequencer_construct(struct dc *dc);
extern void fill_display_configs(
	const struct dc_state *context,
	struct dm_pp_display_configuration *pp_display_cfg);

bool is_rgb_cspace(enum dc_color_space output_color_space);

void hwss1_plane_atomic_disconnect(struct dc *dc, struct pipe_ctx *pipe_ctx);

void dcn10_verify_allow_pstate_change_high(struct dc *dc);

void dcn10_program_pipe(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context);

void dcn10_get_hw_state(
		struct dc *dc,
		char *pBuf, unsigned int bufSize,
		unsigned int mask);

void dcn10_clear_status_bits(struct dc *dc, unsigned int mask);

bool is_lower_pipe_tree_visible(struct pipe_ctx *pipe_ctx);

bool is_upper_pipe_tree_visible(struct pipe_ctx *pipe_ctx);

bool is_pipe_tree_visible(struct pipe_ctx *pipe_ctx);

void dcn10_program_pte_vm(struct dce_hwseq *hws, struct hubp *hubp);

void set_hdr_multiplier(struct pipe_ctx *pipe_ctx);

void dcn10_get_surface_visual_confirm_color(
		const struct pipe_ctx *pipe_ctx,
		struct tg_color *color);

void dcn10_get_hdr_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);

bool dcn10_did_underflow_occur(struct dc *dc, struct pipe_ctx *pipe_ctx);

void update_dchubp_dpp(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct dc_state *context);

struct pipe_ctx *find_top_pipe_for_stream(
		struct dc *dc,
		struct dc_state *context,
		const struct dc_stream_state *stream);

int get_vupdate_offset_from_vsync(struct pipe_ctx *pipe_ctx);

void dcn10_build_prescale_params(struct  dc_bias_and_scale *bias_and_scale,
		const struct dc_plane_state *plane_state);
void lock_all_pipes(struct dc *dc,
	struct dc_state *context,
	bool lock);

#endif /* __DC_HWSS_DCN10_H__ */
