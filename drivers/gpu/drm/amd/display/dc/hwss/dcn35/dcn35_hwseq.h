/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __DC_HWSS_DCN35_H__
#define __DC_HWSS_DCN35_H__

#include "hw_sequencer_private.h"

struct dc;

void dcn35_update_odm(struct dc *dc, struct dc_state *context, struct pipe_ctx *pipe_ctx);

void dcn35_dsc_pg_control(struct dce_hwseq *hws, unsigned int dsc_inst, bool power_on);

void dcn35_dpp_root_clock_control(struct dce_hwseq *hws, unsigned int dpp_inst, bool clock_on);

void dcn35_dpstream_root_clock_control(struct dce_hwseq *hws, unsigned int dp_hpo_inst, bool clock_on);

void dcn35_enable_power_gating_plane(struct dce_hwseq *hws, bool enable);

void dcn35_set_dmu_fgcg(struct dce_hwseq *hws, bool enable);

void dcn35_init_hw(struct dc *dc);

void dcn35_disable_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal);

void dcn35_power_down_on_boot(struct dc *dc);

bool dcn35_apply_idle_power_optimizations(struct dc *dc, bool enable);

void dcn35_z10_restore(const struct dc *dc);

void dcn35_init_pipes(struct dc *dc, struct dc_state *context);
void dcn35_plane_atomic_disable(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn35_enable_plane(struct dc *dc, struct pipe_ctx *pipe_ctx,
			       struct dc_state *context);
void dcn35_disable_plane(struct dc *dc, struct dc_state *state, struct pipe_ctx *pipe_ctx);

void dcn35_calc_blocks_to_gate(struct dc *dc, struct dc_state *context,
	struct pg_block_update *update_state);
void dcn35_calc_blocks_to_ungate(struct dc *dc, struct dc_state *context,
	struct pg_block_update *update_state);
void dcn35_hw_block_power_up(struct dc *dc,
	struct pg_block_update *update_state);
void dcn35_hw_block_power_down(struct dc *dc,
	struct pg_block_update *update_state);
void dcn35_root_clock_control(struct dc *dc,
	struct pg_block_update *update_state, bool power_on);

void dcn35_prepare_bandwidth(
		struct dc *dc,
		struct dc_state *context);

void dcn35_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context);

void dcn35_setup_hpo_hw_control(const struct dce_hwseq *hws, bool enable);
void dcn35_dsc_pg_control(
		struct dce_hwseq *hws,
		unsigned int dsc_inst,
		bool power_on);

void dcn35_set_drr(struct pipe_ctx **pipe_ctx,
		int num_pipes, struct dc_crtc_timing_adjust adjust);

void dcn35_set_static_screen_control(struct pipe_ctx **pipe_ctx,
		int num_pipes, const struct dc_static_screen_params *params);

void dcn35_set_long_vblank(struct pipe_ctx **pipe_ctx,
		int num_pipes, uint32_t v_total_min, uint32_t v_total_max);

#endif /* __DC_HWSS_DCN35_H__ */
