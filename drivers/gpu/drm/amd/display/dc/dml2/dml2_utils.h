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
 */

#ifndef _DML2_UTILS_H_
#define _DML2_UTILS_H_

#include "os_types.h"
#include "dml2_dc_types.h"

struct dc;
struct dml_timing_cfg_st;
struct dml2_dcn_clocks;
struct dc_state;

void dml2_util_copy_dml_timing(struct dml_timing_cfg_st *dml_timing_array, unsigned int dst_index, unsigned int src_index);
void dml2_util_copy_dml_plane(struct dml_plane_cfg_st *dml_plane_array, unsigned int dst_index, unsigned int src_index);
void dml2_util_copy_dml_surface(struct dml_surface_cfg_st *dml_surface_array, unsigned int dst_index, unsigned int src_index);
void dml2_util_copy_dml_output(struct dml_output_cfg_st *dml_output_array, unsigned int dst_index, unsigned int src_index);
unsigned int dml2_util_get_maximum_odm_combine_for_output(bool force_odm_4to1, enum dml_output_encoder_class encoder, bool dsc_enabled);
void dml2_copy_clocks_to_dc_state(struct dml2_dcn_clocks *out_clks, struct dc_state *context);
void dml2_extract_watermark_set(struct dcn_watermarks *watermark, struct display_mode_lib_st *dml_core_ctx);
void dml2_extract_writeback_wm(struct dc_state *context, struct display_mode_lib_st *dml_core_ctx);
int dml2_helper_find_dml_pipe_idx_by_stream_id(struct dml2_context *ctx, unsigned int stream_id);
bool is_dtbclk_required(const struct dc *dc, struct dc_state *context);
bool dml2_is_stereo_timing(const struct dc_stream_state *stream);
unsigned int dml2_calc_max_scaled_time(
		unsigned int time_per_pixel,
		enum mmhubbub_wbif_mode mode,
		unsigned int urgent_watermark);

/*
 * dml2_dc_construct_pipes - This function will determine if we need additional pipes based
 * on the DML calculated outputs for MPC, ODM and allocate them as necessary. This function
 * could be called after in dml_validate_build_resource after dml_mode_pragramming like :
 * {
 *   ...
 * map_hw_resources(&s->cur_display_config, &s->mode_support_info);
 * result = dml_mode_programming(&in_ctx->dml_core_ctx, s->mode_support_params.out_lowest_state_idx, &s->cur_display_config, true);
 * dml2_dc_construct_pipes(in_display_state, s->mode_support_info, out_hw_context);
 * ...
 * }
 *
 * @context: To obtain res_ctx and read other information like stream ID etc.
 * @dml_mode_support_st : To get the ODM, MPC outputs as determined by the DML.
 * @out_hw_context : Handle to the new hardware context.
 *
 *
 * Return: None.
 */
void dml2_dc_construct_pipes(struct dc_state *context, struct dml_mode_support_info_st *dml_mode_support_st,
		struct resource_context *out_hw_context);

/*
 * dml2_predict_pipe_split - This function is the dml2 version of predict split pipe. It predicts a
 * if pipe split is required or not and returns the output as a bool.
 * @context : dc_state.
 * @pipe : old_index is the index of the pipe as derived from pipe_idx.
 * @index : index of the pipe
 *
 *
 * Return: Returns the result in boolean.
 */
bool dml2_predict_pipe_split(struct dc_state *context, display_pipe_params_st pipe, int index);

/*
 * dml2_build_mapped_resource - This function is the dml2 version of build_mapped_resource.
 * In case of ODM, we need to build pipe hardware params again as done in dcn20_build_mapped_resource.
 * @dc : struct dc
 * @context : struct dc_state.
 * @stream : stream whoose corresponding pipe params need to be modified.
 *
 *
 * Return: Returns DC_OK if successful.
 */
enum dc_status dml2_build_mapped_resource(const struct dc *dc, struct dc_state *context, struct dc_stream_state *stream);

/*
 * dml2_extract_rq_regs - This function will extract information needed for struct _vcs_dpi_display_rq_regs_st
 * and populate it.
 * @context: To obtain and populate the res_ctx->pipe_ctx->rq_regs with DML outputs.
 * @support : This structure has the DML intermediate outputs required to populate rq_regs.
 *
 *
 * Return: None.
 */

 /*
  * dml2_calculate_rq_and_dlg_params - This function will call into DML2 functions needed
  * for populating rq, ttu and dlg param structures and populate it.
  * @dc : struct dc
  * @context : dc_state provides a handle to selectively populate pipe_ctx
  * @out_new_hw_state: To obtain and populate the rq, dlg and ttu regs in
  *                    out_new_hw_state->pipe_ctx with DML outputs.
  * @in_ctx : This structure has the pointer to display_mode_lib_st.
  * @pipe_cnt : DML functions to obtain RQ, TTu and DLG params need a pipe_index.
  *				This helps provide pipe_index in the pipe_cnt loop.
  *
  *
  * Return: None.
  */
void dml2_calculate_rq_and_dlg_params(const struct dc *dc, struct dc_state *context, struct resource_context *out_new_hw_state, struct dml2_context *in_ctx, unsigned int pipe_cnt);

/*
 * dml2_apply_det_buffer_allocation_policy - This function will determine the DET Buffer size
 * and return the number of streams.
 * @dml2 : Handle for dml2 context
 * @dml_dispcfg : dml_dispcfg is the DML2 struct representing the current display config
 * Return : None.
 */
void dml2_apply_det_buffer_allocation_policy(struct dml2_context *in_ctx, struct dml_display_cfg_st *dml_dispcfg);

/*
 * dml2_verify_det_buffer_configuration - This function will verify if the allocated DET buffer exceeds
 * the total available DET size available and outputs a boolean to indicate if recalulation is needed.
 * @dml2 : Handle for dml2 context
 * @dml_dispcfg : dml_dispcfg is the DML2 struct representing the current display config
 * @struct dml2_helper_det_policy_scratch : Pointer to DET helper scratch
 * Return : returns true if recalculation is required, false otherwise.
 */
bool dml2_verify_det_buffer_configuration(struct dml2_context *in_ctx, struct dc_state *display_state, struct dml2_helper_det_policy_scratch *det_scratch);

/*
 * dml2_initialize_det_scratch - This function will initialize the DET scratch space as per requirements.
 * @dml2 : Handle for dml2 context
 * Return : None
 */
void dml2_initialize_det_scratch(struct dml2_context *in_ctx);
#endif
