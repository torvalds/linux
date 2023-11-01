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

#ifndef __DML2_TRANSLATION_HELPER_H__
#define __DML2_TRANSLATION_HELPER_H__

void dml2_init_ip_params(struct dml2_context *dml2, const struct dc *in_dc, struct ip_params_st *out);
void dml2_init_socbb_params(struct dml2_context *dml2, const struct dc *in_dc, struct soc_bounding_box_st *out);
void dml2_init_soc_states(struct dml2_context *dml2, const struct dc *in_dc,
		const struct soc_bounding_box_st *in_bbox, struct soc_states_st *out);
void dml2_translate_ip_params(const struct dc *in_dc, struct ip_params_st *out);
void dml2_translate_socbb_params(const struct dc *in_dc, struct soc_bounding_box_st *out);
void dml2_translate_soc_states(const struct dc *in_dc, struct soc_states_st *out, int num_states);
void map_dc_state_into_dml_display_cfg(struct dml2_context *dml2, const struct dc_state *context, struct dml_display_cfg_st *dml_dispcfg);
void dml2_update_pipe_ctx_dchub_regs(struct _vcs_dpi_dml_display_rq_regs_st *rq_regs, struct _vcs_dpi_dml_display_dlg_regs_st *disp_dlg_regs, struct _vcs_dpi_dml_display_ttu_regs_st *disp_ttu_regs, struct pipe_ctx *out);
bool is_dp2p0_output_encoder(const struct pipe_ctx *pipe);

#endif //__DML2_TRANSLATION_HELPER_H__
