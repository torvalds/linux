/*
 * Copyright 2019-2021 Advanced Micro Devices, Inc.
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

#ifndef __DCN31_FPU_H__
#define __DCN31_FPU_H__

#define DCN3_1_DEFAULT_DET_SIZE 384
#define DCN3_15_DEFAULT_DET_SIZE 192
#define DCN3_15_MIN_COMPBUF_SIZE_KB 128
#define DCN3_16_DEFAULT_DET_SIZE 192

void dcn31_zero_pipe_dcc_fraction(display_e2e_pipe_params_st *pipes,
				  int pipe_cnt);

void dcn31_update_soc_for_wm_a(struct dc *dc, struct dc_state *context);
void dcn315_update_soc_for_wm_a(struct dc *dc, struct dc_state *context);

void dcn31_calculate_wm_and_dlg_fp(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel);

void dcn31_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params);
void dcn315_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params);
void dcn316_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params);
int dcn_get_max_non_odm_pix_rate_100hz(struct _vcs_dpi_soc_bounding_box_st *soc);

#endif /* __DCN31_FPU_H__*/
