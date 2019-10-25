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

#ifndef __DC_HWSS_DCN20_H__
#define __DC_HWSS_DCN20_H__

struct dc;

void dcn20_hw_sequencer_construct(struct dc *dc);

enum dc_status dcn20_enable_stream_timing(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc);

void dcn20_blank_pixel_data(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool blank);

void dcn20_program_output_csc(struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix,
		int opp_id);

void dcn20_prepare_bandwidth(
		struct dc *dc,
		struct dc_state *context);

void dcn20_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context);

bool dcn20_update_bandwidth(
		struct dc *dc,
		struct dc_state *context);

void dcn20_disable_writeback(
		struct dc *dc,
		unsigned int dwb_pipe_inst);

bool dcn20_hwss_wait_for_blank_complete(
		struct output_pixel_processor *opp);

bool dcn20_set_output_transfer_func(struct pipe_ctx *pipe_ctx,
			const struct dc_stream_state *stream);

bool dcn20_set_input_transfer_func(struct pipe_ctx *pipe_ctx,
			const struct dc_plane_state *plane_state);

bool dcn20_dmdata_status_done(struct pipe_ctx *pipe_ctx);

void dcn20_set_dmdata_attributes(struct pipe_ctx *pipe_ctx);

void dcn20_disable_stream(struct pipe_ctx *pipe_ctx);

void dcn20_program_tripleBuffer(
		const struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool enableTripleBuffer);

void dcn20_setup_vupdate_interrupt(struct pipe_ctx *pipe_ctx);

void dcn20_pipe_control_lock_global(
		struct dc *dc,
		struct pipe_ctx *pipe,
		bool lock);
void dcn20_setup_gsl_group_as_lock(const struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				bool enable);
void dcn20_dccg_init(struct dce_hwseq *hws);
void dcn20_init_blank(
	   struct dc *dc,
	   struct timing_generator *tg);
void dcn20_display_init(struct dc *dc);
#endif /* __DC_HWSS_DCN20_H__ */
