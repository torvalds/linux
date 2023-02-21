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

#ifndef __DC_HWSS_DCN31_H__
#define __DC_HWSS_DCN31_H__

#include "hw_sequencer_private.h"

struct dc;

void dcn31_init_hw(struct dc *dc);

void dcn31_dsc_pg_control(
		struct dce_hwseq *hws,
		unsigned int dsc_inst,
		bool power_on);

void dcn31_enable_power_gating_plane(
	struct dce_hwseq *hws,
	bool enable);

void dcn31_update_info_frame(struct pipe_ctx *pipe_ctx);

void dcn31_z10_restore(const struct dc *dc);
void dcn31_z10_save_init(struct dc *dc);

void dcn31_hubp_pg_control(struct dce_hwseq *hws, unsigned int hubp_inst, bool power_on);
int dcn31_init_sys_ctx(struct dce_hwseq *hws, struct dc *dc, struct dc_phy_addr_space_config *pa_config);
void dcn31_reset_hw_ctx_wrap(
		struct dc *dc,
		struct dc_state *context);
bool dcn31_is_abm_supported(struct dc *dc,
		struct dc_state *context, struct dc_stream_state *stream);
void dcn31_init_pipes(struct dc *dc, struct dc_state *context);
void dcn31_setup_hpo_hw_control(const struct dce_hwseq *hws, bool enable);

void dcn31_set_static_screen_control(struct pipe_ctx **pipe_ctx,
		int num_pipes, const struct dc_static_screen_params *params);
void dcn31_set_drr(struct pipe_ctx **pipe_ctx,
		int num_pipes, struct dc_crtc_timing_adjust adjust);
#endif /* __DC_HWSS_DCN31_H__ */
