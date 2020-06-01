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

#include "hw_sequencer_private.h"

bool dcn20_set_blend_lut(
	struct pipe_ctx *pipe_ctx, const struct dc_plane_state *plane_state);
bool dcn20_set_shaper_3dlut(
	struct pipe_ctx *pipe_ctx, const struct dc_plane_state *plane_state);
void dcn20_program_front_end_for_ctx(
		struct dc *dc,
		struct dc_state *context);
void dcn20_post_unlock_program_front_end(
		struct dc *dc,
		struct dc_state *context);
void dcn20_update_plane_addr(const struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn20_update_mpcc(struct dc *dc, struct pipe_ctx *pipe_ctx);
bool dcn20_set_input_transfer_func(struct dc *dc, struct pipe_ctx *pipe_ctx,
			const struct dc_plane_state *plane_state);
bool dcn20_set_output_transfer_func(struct dc *dc, struct pipe_ctx *pipe_ctx,
			const struct dc_stream_state *stream);
void dcn20_program_output_csc(struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix,
		int opp_id);
void dcn20_enable_stream(struct pipe_ctx *pipe_ctx);
void dcn20_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings);
void dcn20_disable_plane(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn20_blank_pixel_data(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool blank);
void dcn20_pipe_control_lock(
	struct dc *dc,
	struct pipe_ctx *pipe,
	bool lock);
void dcn20_prepare_bandwidth(
		struct dc *dc,
		struct dc_state *context);
void dcn20_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context);
bool dcn20_update_bandwidth(
		struct dc *dc,
		struct dc_state *context);
void dcn20_reset_hw_ctx_wrap(
		struct dc *dc,
		struct dc_state *context);
enum dc_status dcn20_enable_stream_timing(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc);
void dcn20_disable_stream_gating(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn20_enable_stream_gating(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn20_setup_vupdate_interrupt(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn20_init_blank(
		struct dc *dc,
		struct timing_generator *tg);
void dcn20_disable_vga(
	struct dce_hwseq *hws);
void dcn20_plane_atomic_disable(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn20_enable_power_gating_plane(
	struct dce_hwseq *hws,
	bool enable);
void dcn20_dpp_pg_control(
		struct dce_hwseq *hws,
		unsigned int dpp_inst,
		bool power_on);
void dcn20_hubp_pg_control(
		struct dce_hwseq *hws,
		unsigned int hubp_inst,
		bool power_on);
void dcn20_program_triple_buffer(
	const struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	bool enable_triple_buffer);
void dcn20_enable_writeback(
		struct dc *dc,
		struct dc_writeback_info *wb_info,
		struct dc_state *context);
void dcn20_disable_writeback(
		struct dc *dc,
		unsigned int dwb_pipe_inst);
void dcn20_update_odm(struct dc *dc, struct dc_state *context, struct pipe_ctx *pipe_ctx);
bool dcn20_dmdata_status_done(struct pipe_ctx *pipe_ctx);
void dcn20_program_dmdata_engine(struct pipe_ctx *pipe_ctx);
void dcn20_set_dmdata_attributes(struct pipe_ctx *pipe_ctx);
void dcn20_init_vm_ctx(
		struct dce_hwseq *hws,
		struct dc *dc,
		struct dc_virtual_addr_space_config *va_config,
		int vmid);
void dcn20_set_flip_control_gsl(
		struct pipe_ctx *pipe_ctx,
		bool flip_immediate);
void dcn20_dsc_pg_control(
		struct dce_hwseq *hws,
		unsigned int dsc_inst,
		bool power_on);
void dcn20_fpga_init_hw(struct dc *dc);
bool dcn20_wait_for_blank_complete(
		struct output_pixel_processor *opp);
void dcn20_dccg_init(struct dce_hwseq *hws);
int dcn20_init_sys_ctx(struct dce_hwseq *hws,
		struct dc *dc,
		struct dc_phy_addr_space_config *pa_config);

#endif /* __DC_HWSS_DCN20_H__ */

