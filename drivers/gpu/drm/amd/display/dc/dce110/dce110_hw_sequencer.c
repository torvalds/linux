/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include "dm_services.h"
#include "dc.h"
#include "dc_bios_types.h"
#include "core_types.h"
#include "core_status.h"
#include "resource.h"
#include "dm_helpers.h"
#include "dce110_hw_sequencer.h"
#include "dce110_timing_generator.h"
#include "dce/dce_hwseq.h"
#include "gpio_service_interface.h"

#if defined(CONFIG_DRM_AMD_DC_FBC)
#include "dce110_compressor.h"
#endif

#include "bios/bios_parser_helper.h"
#include "timing_generator.h"
#include "mem_input.h"
#include "opp.h"
#include "ipp.h"
#include "transform.h"
#include "stream_encoder.h"
#include "link_encoder.h"
#include "link_hwss.h"
#include "clock_source.h"
#include "abm.h"
#include "audio.h"
#include "reg_helper.h"

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "custom_float.h"

/*
 * All values are in milliseconds;
 * For eDP, after power-up/power/down,
 * 300/500 msec max. delay from LCDVCC to black video generation
 */
#define PANEL_POWER_UP_TIMEOUT 300
#define PANEL_POWER_DOWN_TIMEOUT 500
#define HPD_CHECK_INTERVAL 10

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

struct dce110_hw_seq_reg_offsets {
	uint32_t crtc;
};

static const struct dce110_hw_seq_reg_offsets reg_offsets[] = {
{
	.crtc = (mmCRTC0_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC1_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC2_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTCV_GSL_CONTROL - mmCRTC_GSL_CONTROL),
}
};

#define HW_REG_BLND(reg, id)\
	(reg + reg_offsets[id].blnd)

#define HW_REG_CRTC(reg, id)\
	(reg + reg_offsets[id].crtc)

#define MAX_WATERMARK 0xFFFF
#define SAFE_NBP_MARK 0x7FFF

/*******************************************************************************
 * Private definitions
 ******************************************************************************/
/***************************PIPE_CONTROL***********************************/
static void dce110_init_pte(struct dc_context *ctx)
{
	uint32_t addr;
	uint32_t value = 0;
	uint32_t chunk_int = 0;
	uint32_t chunk_mul = 0;

	addr = mmUNP_DVMM_PTE_CONTROL;
	value = dm_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		0,
		DVMM_PTE_CONTROL,
		DVMM_USE_SINGLE_PTE);

	set_reg_field_value(
		value,
		1,
		DVMM_PTE_CONTROL,
		DVMM_PTE_BUFFER_MODE0);

	set_reg_field_value(
		value,
		1,
		DVMM_PTE_CONTROL,
		DVMM_PTE_BUFFER_MODE1);

	dm_write_reg(ctx, addr, value);

	addr = mmDVMM_PTE_REQ;
	value = dm_read_reg(ctx, addr);

	chunk_int = get_reg_field_value(
		value,
		DVMM_PTE_REQ,
		HFLIP_PTEREQ_PER_CHUNK_INT);

	chunk_mul = get_reg_field_value(
		value,
		DVMM_PTE_REQ,
		HFLIP_PTEREQ_PER_CHUNK_MULTIPLIER);

	if (chunk_int != 0x4 || chunk_mul != 0x4) {

		set_reg_field_value(
			value,
			255,
			DVMM_PTE_REQ,
			MAX_PTEREQ_TO_ISSUE);

		set_reg_field_value(
			value,
			4,
			DVMM_PTE_REQ,
			HFLIP_PTEREQ_PER_CHUNK_INT);

		set_reg_field_value(
			value,
			4,
			DVMM_PTE_REQ,
			HFLIP_PTEREQ_PER_CHUNK_MULTIPLIER);

		dm_write_reg(ctx, addr, value);
	}
}
/**************************************************************************/

static void enable_display_pipe_clock_gating(
	struct dc_context *ctx,
	bool clock_gating)
{
	/*TODO*/
}

static bool dce110_enable_display_power_gating(
	struct dc *dc,
	uint8_t controller_id,
	struct dc_bios *dcb,
	enum pipe_gating_control power_gating)
{
	enum bp_result bp_result = BP_RESULT_OK;
	enum bp_pipe_control_action cntl;
	struct dc_context *ctx = dc->ctx;
	unsigned int underlay_idx = dc->res_pool->underlay_pipe_index;

	if (IS_FPGA_MAXIMUS_DC(ctx->dce_environment))
		return true;

	if (power_gating == PIPE_GATING_CONTROL_INIT)
		cntl = ASIC_PIPE_INIT;
	else if (power_gating == PIPE_GATING_CONTROL_ENABLE)
		cntl = ASIC_PIPE_ENABLE;
	else
		cntl = ASIC_PIPE_DISABLE;

	if (controller_id == underlay_idx)
		controller_id = CONTROLLER_ID_UNDERLAY0 - 1;

	if (power_gating != PIPE_GATING_CONTROL_INIT || controller_id == 0){

		bp_result = dcb->funcs->enable_disp_power_gating(
						dcb, controller_id + 1, cntl);

		/* Revert MASTER_UPDATE_MODE to 0 because bios sets it 2
		 * by default when command table is called
		 *
		 * Bios parser accepts controller_id = 6 as indicative of
		 * underlay pipe in dce110. But we do not support more
		 * than 3.
		 */
		if (controller_id < CONTROLLER_ID_MAX - 1)
			dm_write_reg(ctx,
				HW_REG_CRTC(mmCRTC_MASTER_UPDATE_MODE, controller_id),
				0);
	}

	if (power_gating != PIPE_GATING_CONTROL_ENABLE)
		dce110_init_pte(ctx);

	if (bp_result == BP_RESULT_OK)
		return true;
	else
		return false;
}

static void build_prescale_params(struct ipp_prescale_params *prescale_params,
		const struct dc_plane_state *plane_state)
{
	prescale_params->mode = IPP_PRESCALE_MODE_FIXED_UNSIGNED;

	switch (plane_state->format) {
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
		prescale_params->scale = 0x2020;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		prescale_params->scale = 0x2008;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		prescale_params->scale = 0x2000;
		break;
	default:
		ASSERT(false);
		break;
	}
}

static bool dce110_set_input_transfer_func(
	struct pipe_ctx *pipe_ctx,
	const struct dc_plane_state *plane_state)
{
	struct input_pixel_processor *ipp = pipe_ctx->plane_res.ipp;
	const struct dc_transfer_func *tf = NULL;
	struct ipp_prescale_params prescale_params = { 0 };
	bool result = true;

	if (ipp == NULL)
		return false;

	if (plane_state->in_transfer_func)
		tf = plane_state->in_transfer_func;

	build_prescale_params(&prescale_params, plane_state);
	ipp->funcs->ipp_program_prescale(ipp, &prescale_params);

	if (plane_state->gamma_correction && dce_use_lut(plane_state))
		ipp->funcs->ipp_program_input_lut(ipp, plane_state->gamma_correction);

	if (tf == NULL) {
		/* Default case if no input transfer function specified */
		ipp->funcs->ipp_set_degamma(ipp,
				IPP_DEGAMMA_MODE_HW_sRGB);
	} else if (tf->type == TF_TYPE_PREDEFINED) {
		switch (tf->tf) {
		case TRANSFER_FUNCTION_SRGB:
			ipp->funcs->ipp_set_degamma(ipp,
					IPP_DEGAMMA_MODE_HW_sRGB);
			break;
		case TRANSFER_FUNCTION_BT709:
			ipp->funcs->ipp_set_degamma(ipp,
					IPP_DEGAMMA_MODE_HW_xvYCC);
			break;
		case TRANSFER_FUNCTION_LINEAR:
			ipp->funcs->ipp_set_degamma(ipp,
					IPP_DEGAMMA_MODE_BYPASS);
			break;
		case TRANSFER_FUNCTION_PQ:
			result = false;
			break;
		default:
			result = false;
			break;
		}
	} else if (tf->type == TF_TYPE_BYPASS) {
		ipp->funcs->ipp_set_degamma(ipp, IPP_DEGAMMA_MODE_BYPASS);
	} else {
		/*TF_TYPE_DISTRIBUTED_POINTS - Not supported in DCE 11*/
		result = false;
	}

	return result;
}

static bool convert_to_custom_float(
		struct pwl_result_data *rgb_resulted,
		struct curve_points *arr_points,
		uint32_t hw_points_num)
{
	struct custom_float_format fmt;

	struct pwl_result_data *rgb = rgb_resulted;

	uint32_t i = 0;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = true;

	if (!convert_to_custom_float_format(
		arr_points[0].x,
		&fmt,
		&arr_points[0].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[0].offset,
		&fmt,
		&arr_points[0].custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[0].slope,
		&fmt,
		&arr_points[0].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 10;
	fmt.sign = false;

	if (!convert_to_custom_float_format(
		arr_points[1].x,
		&fmt,
		&arr_points[1].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[1].y,
		&fmt,
		&arr_points[1].custom_float_y)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[2].slope,
		&fmt,
		&arr_points[2].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 12;
	fmt.sign = true;

	while (i != hw_points_num) {
		if (!convert_to_custom_float_format(
			rgb->red,
			&fmt,
			&rgb->red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->green,
			&fmt,
			&rgb->green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->blue,
			&fmt,
			&rgb->blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_red,
			&fmt,
			&rgb->delta_red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_green,
			&fmt,
			&rgb->delta_green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_blue,
			&fmt,
			&rgb->delta_blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		++rgb;
		++i;
	}

	return true;
}

static bool dce110_translate_regamma_to_hw_format(const struct dc_transfer_func
		*output_tf, struct pwl_params *regamma_params)
{
	struct curve_points *arr_points;
	struct pwl_result_data *rgb_resulted;
	struct pwl_result_data *rgb;
	struct pwl_result_data *rgb_plus_1;
	struct fixed31_32 y_r;
	struct fixed31_32 y_g;
	struct fixed31_32 y_b;
	struct fixed31_32 y1_min;
	struct fixed31_32 y3_max;

	int32_t segment_start, segment_end;
	uint32_t i, j, k, seg_distr[16], increment, start_index, hw_points;

	if (output_tf == NULL || regamma_params == NULL ||
			output_tf->type == TF_TYPE_BYPASS)
		return false;

	arr_points = regamma_params->arr_points;
	rgb_resulted = regamma_params->rgb_resulted;
	hw_points = 0;

	memset(regamma_params, 0, sizeof(struct pwl_params));

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* 16 segments
		 * segments are from 2^-11 to 2^5
		 */
		segment_start = -11;
		segment_end = 5;

		seg_distr[0] = 2;
		seg_distr[1] = 2;
		seg_distr[2] = 2;
		seg_distr[3] = 2;
		seg_distr[4] = 2;
		seg_distr[5] = 2;
		seg_distr[6] = 3;
		seg_distr[7] = 4;
		seg_distr[8] = 4;
		seg_distr[9] = 4;
		seg_distr[10] = 4;
		seg_distr[11] = 5;
		seg_distr[12] = 5;
		seg_distr[13] = 5;
		seg_distr[14] = 5;
		seg_distr[15] = 5;

	} else {
		/* 10 segments
		 * segment is from 2^-10 to 2^0
		 */
		segment_start = -10;
		segment_end = 0;

		seg_distr[0] = 3;
		seg_distr[1] = 4;
		seg_distr[2] = 4;
		seg_distr[3] = 4;
		seg_distr[4] = 4;
		seg_distr[5] = 4;
		seg_distr[6] = 4;
		seg_distr[7] = 4;
		seg_distr[8] = 5;
		seg_distr[9] = 5;
		seg_distr[10] = -1;
		seg_distr[11] = -1;
		seg_distr[12] = -1;
		seg_distr[13] = -1;
		seg_distr[14] = -1;
		seg_distr[15] = -1;
	}

	for (k = 0; k < 16; k++) {
		if (seg_distr[k] != -1)
			hw_points += (1 << seg_distr[k]);
	}

	j = 0;
	for (k = 0; k < (segment_end - segment_start); k++) {
		increment = 32 / (1 << seg_distr[k]);
		start_index = (segment_start + k + 25) * 32;
		for (i = start_index; i < start_index + 32; i += increment) {
			if (j == hw_points - 1)
				break;
			rgb_resulted[j].red = output_tf->tf_pts.red[i];
			rgb_resulted[j].green = output_tf->tf_pts.green[i];
			rgb_resulted[j].blue = output_tf->tf_pts.blue[i];
			j++;
		}
	}

	/* last point */
	start_index = (segment_end + 25) * 32;
	rgb_resulted[hw_points - 1].red =
			output_tf->tf_pts.red[start_index];
	rgb_resulted[hw_points - 1].green =
			output_tf->tf_pts.green[start_index];
	rgb_resulted[hw_points - 1].blue =
			output_tf->tf_pts.blue[start_index];

	arr_points[0].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
			dal_fixed31_32_from_int(segment_start));
	arr_points[1].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
			dal_fixed31_32_from_int(segment_end));
	arr_points[2].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
			dal_fixed31_32_from_int(segment_end));

	y_r = rgb_resulted[0].red;
	y_g = rgb_resulted[0].green;
	y_b = rgb_resulted[0].blue;

	y1_min = dal_fixed31_32_min(y_r, dal_fixed31_32_min(y_g, y_b));

	arr_points[0].y = y1_min;
	arr_points[0].slope = dal_fixed31_32_div(
					arr_points[0].y,
					arr_points[0].x);

	y_r = rgb_resulted[hw_points - 1].red;
	y_g = rgb_resulted[hw_points - 1].green;
	y_b = rgb_resulted[hw_points - 1].blue;

	/* see comment above, m_arrPoints[1].y should be the Y value for the
	 * region end (m_numOfHwPoints), not last HW point(m_numOfHwPoints - 1)
	 */
	y3_max = dal_fixed31_32_max(y_r, dal_fixed31_32_max(y_g, y_b));

	arr_points[1].y = y3_max;
	arr_points[2].y = y3_max;

	arr_points[1].slope = dal_fixed31_32_zero;
	arr_points[2].slope = dal_fixed31_32_zero;

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* for PQ, we want to have a straight line from last HW X point,
		 * and the slope to be such that we hit 1.0 at 10000 nits.
		 */
		const struct fixed31_32 end_value =
				dal_fixed31_32_from_int(125);

		arr_points[1].slope = dal_fixed31_32_div(
			dal_fixed31_32_sub(dal_fixed31_32_one, arr_points[1].y),
			dal_fixed31_32_sub(end_value, arr_points[1].x));
		arr_points[2].slope = dal_fixed31_32_div(
			dal_fixed31_32_sub(dal_fixed31_32_one, arr_points[1].y),
			dal_fixed31_32_sub(end_value, arr_points[1].x));
	}

	regamma_params->hw_points_num = hw_points;

	i = 1;
	for (k = 0; k < 16 && i < 16; k++) {
		if (seg_distr[k] != -1) {
			regamma_params->arr_curve_points[k].segments_num =
					seg_distr[k];
			regamma_params->arr_curve_points[i].offset =
					regamma_params->arr_curve_points[k].
					offset + (1 << seg_distr[k]);
		}
		i++;
	}

	if (seg_distr[k] != -1)
		regamma_params->arr_curve_points[k].segments_num =
				seg_distr[k];

	rgb = rgb_resulted;
	rgb_plus_1 = rgb_resulted + 1;

	i = 1;

	while (i != hw_points + 1) {
		if (dal_fixed31_32_lt(rgb_plus_1->red, rgb->red))
			rgb_plus_1->red = rgb->red;
		if (dal_fixed31_32_lt(rgb_plus_1->green, rgb->green))
			rgb_plus_1->green = rgb->green;
		if (dal_fixed31_32_lt(rgb_plus_1->blue, rgb->blue))
			rgb_plus_1->blue = rgb->blue;

		rgb->delta_red = dal_fixed31_32_sub(
			rgb_plus_1->red,
			rgb->red);
		rgb->delta_green = dal_fixed31_32_sub(
			rgb_plus_1->green,
			rgb->green);
		rgb->delta_blue = dal_fixed31_32_sub(
			rgb_plus_1->blue,
			rgb->blue);

		++rgb_plus_1;
		++rgb;
		++i;
	}

	convert_to_custom_float(rgb_resulted, arr_points, hw_points);

	return true;
}

static bool dce110_set_output_transfer_func(
	struct pipe_ctx *pipe_ctx,
	const struct dc_stream_state *stream)
{
	struct transform *xfm = pipe_ctx->plane_res.xfm;

	xfm->funcs->opp_power_on_regamma_lut(xfm, true);
	xfm->regamma_params.hw_points_num = GAMMA_HW_POINTS_NUM;

	if (stream->out_transfer_func &&
		stream->out_transfer_func->type ==
			TF_TYPE_PREDEFINED &&
		stream->out_transfer_func->tf ==
			TRANSFER_FUNCTION_SRGB) {
		xfm->funcs->opp_set_regamma_mode(xfm, OPP_REGAMMA_SRGB);
	} else if (dce110_translate_regamma_to_hw_format(
				stream->out_transfer_func, &xfm->regamma_params)) {
		xfm->funcs->opp_program_regamma_pwl(xfm, &xfm->regamma_params);
		xfm->funcs->opp_set_regamma_mode(xfm, OPP_REGAMMA_USER);
	} else {
		xfm->funcs->opp_set_regamma_mode(xfm, OPP_REGAMMA_BYPASS);
	}

	xfm->funcs->opp_power_on_regamma_lut(xfm, false);

	return true;
}

static enum dc_status bios_parser_crtc_source_select(
		struct pipe_ctx *pipe_ctx)
{
	struct dc_bios *dcb;
	/* call VBIOS table to set CRTC source for the HW
	 * encoder block
	 * note: video bios clears all FMT setting here. */
	struct bp_crtc_source_select crtc_source_select = {0};
	const struct dc_sink *sink = pipe_ctx->stream->sink;

	crtc_source_select.engine_id = pipe_ctx->stream_res.stream_enc->id;
	crtc_source_select.controller_id = pipe_ctx->pipe_idx + 1;
	/*TODO: Need to un-hardcode color depth, dp_audio and account for
	 * the case where signal and sink signal is different (translator
	 * encoder)*/
	crtc_source_select.signal = pipe_ctx->stream->signal;
	crtc_source_select.enable_dp_audio = false;
	crtc_source_select.sink_signal = pipe_ctx->stream->signal;

	switch (pipe_ctx->stream->timing.display_color_depth) {
	case COLOR_DEPTH_666:
		crtc_source_select.display_output_bit_depth = PANEL_6BIT_COLOR;
		break;
	case COLOR_DEPTH_888:
		crtc_source_select.display_output_bit_depth = PANEL_8BIT_COLOR;
		break;
	case COLOR_DEPTH_101010:
		crtc_source_select.display_output_bit_depth = PANEL_10BIT_COLOR;
		break;
	case COLOR_DEPTH_121212:
		crtc_source_select.display_output_bit_depth = PANEL_12BIT_COLOR;
		break;
	default:
		BREAK_TO_DEBUGGER();
		crtc_source_select.display_output_bit_depth = PANEL_8BIT_COLOR;
		break;
	}

	dcb = sink->ctx->dc_bios;

	if (BP_RESULT_OK != dcb->funcs->crtc_source_select(
		dcb,
		&crtc_source_select)) {
		return DC_ERROR_UNEXPECTED;
	}

	return DC_OK;
}

void dce110_update_info_frame(struct pipe_ctx *pipe_ctx)
{
	ASSERT(pipe_ctx->stream);

	if (pipe_ctx->stream_res.stream_enc == NULL)
		return;  /* this is not root pipe */

	if (dc_is_hdmi_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->update_hdmi_info_packets(
			pipe_ctx->stream_res.stream_enc,
			&pipe_ctx->stream_res.encoder_info_frame);
	else if (dc_is_dp_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->update_dp_info_packets(
			pipe_ctx->stream_res.stream_enc,
			&pipe_ctx->stream_res.encoder_info_frame);
}

void dce110_enable_stream(struct pipe_ctx *pipe_ctx)
{
	enum dc_lane_count lane_count =
		pipe_ctx->stream->sink->link->cur_link_settings.lane_count;

	struct dc_crtc_timing *timing = &pipe_ctx->stream->timing;
	struct dc_link *link = pipe_ctx->stream->sink->link;

	/* 1. update AVI info frame (HDMI, DP)
	 * we always need to update info frame
	*/
	uint32_t active_total_with_borders;
	uint32_t early_control = 0;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;

	/* TODOFPGA may change to hwss.update_info_frame */
	dce110_update_info_frame(pipe_ctx);
	/* enable early control to avoid corruption on DP monitor*/
	active_total_with_borders =
			timing->h_addressable
				+ timing->h_border_left
				+ timing->h_border_right;

	if (lane_count != 0)
		early_control = active_total_with_borders % lane_count;

	if (early_control == 0)
		early_control = lane_count;

	tg->funcs->set_early_control(tg, early_control);

	/* enable audio only within mode set */
	if (pipe_ctx->stream_res.audio != NULL) {
		if (dc_is_dp_signal(pipe_ctx->stream->signal))
			pipe_ctx->stream_res.stream_enc->funcs->dp_audio_enable(pipe_ctx->stream_res.stream_enc);
	}

	/* For MST, there are multiply stream go to only one link.
	 * connect DIG back_end to front_end while enable_stream and
	 * disconnect them during disable_stream
	 * BY this, it is logic clean to separate stream and link */
	link->link_enc->funcs->connect_dig_be_to_fe(link->link_enc,
						    pipe_ctx->stream_res.stream_enc->id, true);

}

/*todo: cloned in stream enc, fix*/
static bool is_panel_backlight_on(struct dce_hwseq *hws)
{
	uint32_t value;

	REG_GET(LVTMA_PWRSEQ_CNTL, LVTMA_BLON, &value);

	return value;
}

static bool is_panel_powered_on(struct dce_hwseq *hws)
{
	uint32_t value;

	REG_GET(LVTMA_PWRSEQ_STATE, LVTMA_PWRSEQ_TARGET_STATE_R, &value);
	return value == 1;
}

static enum bp_result link_transmitter_control(
		struct dc_bios *bios,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result;

	result = bios->funcs->transmitter_control(bios, cntl);

	return result;
}

/*
 * @brief
 * eDP only.
 */
void hwss_edp_wait_for_hpd_ready(
	struct link_encoder *enc,
	bool power_up)
{
	struct dc_context *ctx = enc->ctx;
	struct graphics_object_id connector = enc->connector;
	struct gpio *hpd;
	bool edp_hpd_high = false;
	uint32_t time_elapsed = 0;
	uint32_t timeout = power_up ?
		PANEL_POWER_UP_TIMEOUT : PANEL_POWER_DOWN_TIMEOUT;

	if (dal_graphics_object_id_get_connector_id(connector)
			!= CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (!power_up)
		/*
		 * From KV, we will not HPD low after turning off VCC -
		 * instead, we will check the SW timer in power_up().
		 */
		return;

	/*
	 * When we power on/off the eDP panel,
	 * we need to wait until SENSE bit is high/low.
	 */

	/* obtain HPD */
	/* TODO what to do with this? */
	hpd = get_hpd_gpio(ctx->dc_bios, connector, ctx->gpio_service);

	if (!hpd) {
		BREAK_TO_DEBUGGER();
		return;
	}

	dal_gpio_open(hpd, GPIO_MODE_INTERRUPT);

	/* wait until timeout or panel detected */

	do {
		uint32_t detected = 0;

		dal_gpio_get_value(hpd, &detected);

		if (!(detected ^ power_up)) {
			edp_hpd_high = true;
			break;
		}

		msleep(HPD_CHECK_INTERVAL);

		time_elapsed += HPD_CHECK_INTERVAL;
	} while (time_elapsed < timeout);

	dal_gpio_close(hpd);

	dal_gpio_destroy_irq(&hpd);

	if (false == edp_hpd_high) {
		dm_logger_write(ctx->logger, LOG_ERROR,
				"%s: wait timed out!\n", __func__);
	}
}

void hwss_edp_power_control(
	struct link_encoder *enc,
	bool power_up)
{
	struct dc_context *ctx = enc->ctx;
	struct dce_hwseq *hwseq = ctx->dc->hwseq;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result bp_result;


	if (dal_graphics_object_id_get_connector_id(enc->connector)
			!= CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (power_up != is_panel_powered_on(hwseq)) {
		/* Send VBIOS command to prompt eDP panel power */

		dm_logger_write(ctx->logger, LOG_HW_RESUME_S3,
				"%s: Panel Power action: %s\n",
				__func__, (power_up ? "On":"Off"));

		cntl.action = power_up ?
			TRANSMITTER_CONTROL_POWER_ON :
			TRANSMITTER_CONTROL_POWER_OFF;
		cntl.transmitter = enc->transmitter;
		cntl.connector_obj_id = enc->connector;
		cntl.coherent = false;
		cntl.lanes_number = LANE_COUNT_FOUR;
		cntl.hpd_sel = enc->hpd_source;

		bp_result = link_transmitter_control(ctx->dc_bios, &cntl);

		if (bp_result != BP_RESULT_OK)
			dm_logger_write(ctx->logger, LOG_ERROR,
					"%s: Panel Power bp_result: %d\n",
					__func__, bp_result);
	} else {
		dm_logger_write(ctx->logger, LOG_HW_RESUME_S3,
				"%s: Skipping Panel Power action: %s\n",
				__func__, (power_up ? "On":"Off"));
	}

	hwss_edp_wait_for_hpd_ready(enc, true);
}

/*todo: cloned in stream enc, fix*/
/*
 * @brief
 * eDP only. Control the backlight of the eDP panel
 */
void hwss_edp_backlight_control(
	struct dc_link *link,
	bool enable)
{
	struct dce_hwseq *hws = link->dc->hwseq;
	struct dc_context *ctx = link->dc->ctx;
	struct bp_transmitter_control cntl = { 0 };

	if (dal_graphics_object_id_get_connector_id(link->link_id)
		!= CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (enable && is_panel_backlight_on(hws)) {
		dm_logger_write(ctx->logger, LOG_HW_RESUME_S3,
				"%s: panel already powered up. Do nothing.\n",
				__func__);
		return;
	}

	/* Send VBIOS command to control eDP panel backlight */

	dm_logger_write(ctx->logger, LOG_HW_RESUME_S3,
			"%s: backlight action: %s\n",
			__func__, (enable ? "On":"Off"));

	cntl.action = enable ?
		TRANSMITTER_CONTROL_BACKLIGHT_ON :
		TRANSMITTER_CONTROL_BACKLIGHT_OFF;

	/*cntl.engine_id = ctx->engine;*/
	cntl.transmitter = link->link_enc->transmitter;
	cntl.connector_obj_id = link->link_enc->connector;
	/*todo: unhardcode*/
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.hpd_sel = link->link_enc->hpd_source;

	/* For eDP, the following delays might need to be considered
	 * after link training completed:
	 * idle period - min. accounts for required BS-Idle pattern,
	 * max. allows for source frame synchronization);
	 * 50 msec max. delay from valid video data from source
	 * to video on dislpay or backlight enable.
	 *
	 * Disable the delay for now.
	 * Enable it in the future if necessary.
	 */
	/* dc_service_sleep_in_milliseconds(50); */
	link_transmitter_control(link->dc->ctx->dc_bios, &cntl);
}

void dce110_disable_stream(struct pipe_ctx *pipe_ctx, int option)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->sink->link;
	struct dc *dc = pipe_ctx->stream->ctx->dc;

	if (dc_is_hdmi_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->stop_hdmi_info_packets(
			pipe_ctx->stream_res.stream_enc);

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->stop_dp_info_packets(
			pipe_ctx->stream_res.stream_enc);

	pipe_ctx->stream_res.stream_enc->funcs->audio_mute_control(
			pipe_ctx->stream_res.stream_enc, true);
	if (pipe_ctx->stream_res.audio) {
		pipe_ctx->stream_res.audio->funcs->az_disable(pipe_ctx->stream_res.audio);

		if (dc_is_dp_signal(pipe_ctx->stream->signal))
			pipe_ctx->stream_res.stream_enc->funcs->dp_audio_disable(
					pipe_ctx->stream_res.stream_enc);
		else
			pipe_ctx->stream_res.stream_enc->funcs->hdmi_audio_disable(
					pipe_ctx->stream_res.stream_enc);
		/*don't free audio if it is from retrain or internal disable stream*/
		if (option == FREE_ACQUIRED_RESOURCE && dc->caps.dynamic_audio == true) {
			/*we have to dynamic arbitrate the audio endpoints*/
			pipe_ctx->stream_res.audio = NULL;
			/*we free the resource, need reset is_audio_acquired*/
			update_audio_usage(&dc->current_state->res_ctx, dc->res_pool, pipe_ctx->stream_res.audio, false);
		}

		/* TODO: notify audio driver for if audio modes list changed
		 * add audio mode list change flag */
		/* dal_audio_disable_azalia_audio_jack_presence(stream->audio,
		 * stream->stream_engine_id);
		 */
	}

	/* blank at encoder level */
	if (dc_is_dp_signal(pipe_ctx->stream->signal)) {
		if (pipe_ctx->stream->sink->link->connector_signal == SIGNAL_TYPE_EDP)
			hwss_edp_backlight_control(link, false);
		pipe_ctx->stream_res.stream_enc->funcs->dp_blank(pipe_ctx->stream_res.stream_enc);
	}
	link->link_enc->funcs->connect_dig_be_to_fe(
			link->link_enc,
			pipe_ctx->stream_res.stream_enc->id,
			false);

}

void dce110_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings)
{
	struct encoder_unblank_param params = { { 0 } };
	struct dc_link *link = pipe_ctx->stream->sink->link;

	/* only 3 items below are used by unblank */
	params.pixel_clk_khz =
		pipe_ctx->stream->timing.pix_clk_khz;
	params.link_settings.link_rate = link_settings->link_rate;
	pipe_ctx->stream_res.stream_enc->funcs->dp_unblank(pipe_ctx->stream_res.stream_enc, &params);
	if (link->connector_signal == SIGNAL_TYPE_EDP)
		hwss_edp_backlight_control(link, true);
}


void dce110_set_avmute(struct pipe_ctx *pipe_ctx, bool enable)
{
	if (pipe_ctx != NULL && pipe_ctx->stream_res.stream_enc != NULL)
		pipe_ctx->stream_res.stream_enc->funcs->set_avmute(pipe_ctx->stream_res.stream_enc, enable);
}

static enum audio_dto_source translate_to_dto_source(enum controller_id crtc_id)
{
	switch (crtc_id) {
	case CONTROLLER_ID_D0:
		return DTO_SOURCE_ID0;
	case CONTROLLER_ID_D1:
		return DTO_SOURCE_ID1;
	case CONTROLLER_ID_D2:
		return DTO_SOURCE_ID2;
	case CONTROLLER_ID_D3:
		return DTO_SOURCE_ID3;
	case CONTROLLER_ID_D4:
		return DTO_SOURCE_ID4;
	case CONTROLLER_ID_D5:
		return DTO_SOURCE_ID5;
	default:
		return DTO_SOURCE_UNKNOWN;
	}
}

static void build_audio_output(
	struct dc_state *state,
	const struct pipe_ctx *pipe_ctx,
	struct audio_output *audio_output)
{
	const struct dc_stream_state *stream = pipe_ctx->stream;
	audio_output->engine_id = pipe_ctx->stream_res.stream_enc->id;

	audio_output->signal = pipe_ctx->stream->signal;

	/* audio_crtc_info  */

	audio_output->crtc_info.h_total =
		stream->timing.h_total;

	/*
	 * Audio packets are sent during actual CRTC blank physical signal, we
	 * need to specify actual active signal portion
	 */
	audio_output->crtc_info.h_active =
			stream->timing.h_addressable
			+ stream->timing.h_border_left
			+ stream->timing.h_border_right;

	audio_output->crtc_info.v_active =
			stream->timing.v_addressable
			+ stream->timing.v_border_top
			+ stream->timing.v_border_bottom;

	audio_output->crtc_info.pixel_repetition = 1;

	audio_output->crtc_info.interlaced =
			stream->timing.flags.INTERLACE;

	audio_output->crtc_info.refresh_rate =
		(stream->timing.pix_clk_khz*1000)/
		(stream->timing.h_total*stream->timing.v_total);

	audio_output->crtc_info.color_depth =
		stream->timing.display_color_depth;

	audio_output->crtc_info.requested_pixel_clock =
			pipe_ctx->stream_res.pix_clk_params.requested_pix_clk;

	audio_output->crtc_info.calculated_pixel_clock =
			pipe_ctx->stream_res.pix_clk_params.requested_pix_clk;

/*for HDMI, audio ACR is with deep color ratio factor*/
	if (dc_is_hdmi_signal(pipe_ctx->stream->signal) &&
		audio_output->crtc_info.requested_pixel_clock ==
				stream->timing.pix_clk_khz) {
		if (pipe_ctx->stream_res.pix_clk_params.pixel_encoding == PIXEL_ENCODING_YCBCR420) {
			audio_output->crtc_info.requested_pixel_clock =
					audio_output->crtc_info.requested_pixel_clock/2;
			audio_output->crtc_info.calculated_pixel_clock =
					pipe_ctx->stream_res.pix_clk_params.requested_pix_clk/2;

		}
	}

	if (pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT ||
			pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		audio_output->pll_info.dp_dto_source_clock_in_khz =
				state->dis_clk->funcs->get_dp_ref_clk_frequency(
						state->dis_clk);
	}

	audio_output->pll_info.feed_back_divider =
			pipe_ctx->pll_settings.feedback_divider;

	audio_output->pll_info.dto_source =
		translate_to_dto_source(
			pipe_ctx->pipe_idx + 1);

	/* TODO hard code to enable for now. Need get from stream */
	audio_output->pll_info.ss_enabled = true;

	audio_output->pll_info.ss_percentage =
			pipe_ctx->pll_settings.ss_percentage;
}

static void get_surface_visual_confirm_color(const struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE * (4 - pipe_ctx->pipe_idx) / 4;

	switch (pipe_ctx->plane_res.scl_data.format) {
	case PIXEL_FORMAT_ARGB8888:
		/* set boarder color to red */
		color->color_r_cr = color_value;
		break;

	case PIXEL_FORMAT_ARGB2101010:
		/* set boarder color to blue */
		color->color_b_cb = color_value;
		break;
	case PIXEL_FORMAT_420BPP8:
		/* set boarder color to green */
		color->color_g_y = color_value;
		break;
	case PIXEL_FORMAT_420BPP10:
		/* set boarder color to yellow */
		color->color_g_y = color_value;
		color->color_r_cr = color_value;
		break;
	case PIXEL_FORMAT_FP16:
		/* set boarder color to white */
		color->color_r_cr = color_value;
		color->color_b_cb = color_value;
		color->color_g_y = color_value;
		break;
	default:
		break;
	}
}

static void program_scaler(const struct dc *dc,
		const struct pipe_ctx *pipe_ctx)
{
	struct tg_color color = {0};

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	/* TOFPGA */
	if (pipe_ctx->plane_res.xfm->funcs->transform_set_pixel_storage_depth == NULL)
		return;
#endif

	if (dc->debug.surface_visual_confirm)
		get_surface_visual_confirm_color(pipe_ctx, &color);
	else
		color_space_to_black_color(dc,
				pipe_ctx->stream->output_color_space,
				&color);

	pipe_ctx->plane_res.xfm->funcs->transform_set_pixel_storage_depth(
		pipe_ctx->plane_res.xfm,
		pipe_ctx->plane_res.scl_data.lb_params.depth,
		&pipe_ctx->stream->bit_depth_params);

	if (pipe_ctx->stream_res.tg->funcs->set_overscan_blank_color)
		pipe_ctx->stream_res.tg->funcs->set_overscan_blank_color(
				pipe_ctx->stream_res.tg,
				&color);

	pipe_ctx->plane_res.xfm->funcs->transform_set_scaler(pipe_ctx->plane_res.xfm,
		&pipe_ctx->plane_res.scl_data);
}

static enum dc_status dce110_prog_pixclk_crtc_otg(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct pipe_ctx *pipe_ctx_old = &dc->current_state->res_ctx.
			pipe_ctx[pipe_ctx->pipe_idx];
	struct tg_color black_color = {0};

	if (!pipe_ctx_old->stream) {

		/* program blank color */
		color_space_to_black_color(dc,
				stream->output_color_space, &black_color);
		pipe_ctx->stream_res.tg->funcs->set_blank_color(
				pipe_ctx->stream_res.tg,
				&black_color);

		/*
		 * Must blank CRTC after disabling power gating and before any
		 * programming, otherwise CRTC will be hung in bad state
		 */
		pipe_ctx->stream_res.tg->funcs->set_blank(pipe_ctx->stream_res.tg, true);

		if (false == pipe_ctx->clock_source->funcs->program_pix_clk(
				pipe_ctx->clock_source,
				&pipe_ctx->stream_res.pix_clk_params,
				&pipe_ctx->pll_settings)) {
			BREAK_TO_DEBUGGER();
			return DC_ERROR_UNEXPECTED;
		}

		pipe_ctx->stream_res.tg->funcs->program_timing(
				pipe_ctx->stream_res.tg,
				&stream->timing,
				true);

		pipe_ctx->stream_res.tg->funcs->set_static_screen_control(
				pipe_ctx->stream_res.tg,
				0x182);
	}

	if (!pipe_ctx_old->stream) {
		if (false == pipe_ctx->stream_res.tg->funcs->enable_crtc(
				pipe_ctx->stream_res.tg)) {
			BREAK_TO_DEBUGGER();
			return DC_ERROR_UNEXPECTED;
		}
	}



	return DC_OK;
}

static enum dc_status apply_single_controller_ctx_to_hw(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct pipe_ctx *pipe_ctx_old = &dc->current_state->res_ctx.
			pipe_ctx[pipe_ctx->pipe_idx];

	/*  */
	dc->hwss.prog_pixclk_crtc_otg(pipe_ctx, context, dc);

	/* FPGA does not program backend */
	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		pipe_ctx->stream_res.opp->funcs->opp_set_dyn_expansion(
		pipe_ctx->stream_res.opp,
		COLOR_SPACE_YCBCR601,
		stream->timing.display_color_depth,
		pipe_ctx->stream->signal);

		pipe_ctx->stream_res.opp->funcs->opp_program_fmt(
			pipe_ctx->stream_res.opp,
			&stream->bit_depth_params,
			&stream->clamping);
		return DC_OK;
	}
	/* TODO: move to stream encoder */
	if (pipe_ctx->stream->signal != SIGNAL_TYPE_VIRTUAL)
		if (DC_OK != bios_parser_crtc_source_select(pipe_ctx)) {
			BREAK_TO_DEBUGGER();
			return DC_ERROR_UNEXPECTED;
		}
	pipe_ctx->stream_res.opp->funcs->opp_set_dyn_expansion(
			pipe_ctx->stream_res.opp,
			COLOR_SPACE_YCBCR601,
			stream->timing.display_color_depth,
			pipe_ctx->stream->signal);

	if (pipe_ctx->stream->signal != SIGNAL_TYPE_VIRTUAL)
		stream->sink->link->link_enc->funcs->setup(
			stream->sink->link->link_enc,
			pipe_ctx->stream->signal);

	if (pipe_ctx->stream->signal != SIGNAL_TYPE_VIRTUAL)
		pipe_ctx->stream_res.stream_enc->funcs->setup_stereo_sync(
		pipe_ctx->stream_res.stream_enc,
		pipe_ctx->stream_res.tg->inst,
		stream->timing.timing_3d_format != TIMING_3D_FORMAT_NONE);


	pipe_ctx->stream_res.opp->funcs->opp_program_fmt(
		pipe_ctx->stream_res.opp,
		&stream->bit_depth_params,
		&stream->clamping);

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->dp_set_stream_attribute(
			pipe_ctx->stream_res.stream_enc,
			&stream->timing,
			stream->output_color_space);

	if (dc_is_hdmi_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->hdmi_set_stream_attribute(
			pipe_ctx->stream_res.stream_enc,
			&stream->timing,
			stream->phy_pix_clk,
			pipe_ctx->stream_res.audio != NULL);

	if (dc_is_dvi_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->dvi_set_stream_attribute(
			pipe_ctx->stream_res.stream_enc,
			&stream->timing,
			(pipe_ctx->stream->signal == SIGNAL_TYPE_DVI_DUAL_LINK) ?
			true : false);

	resource_build_info_frame(pipe_ctx);
	dce110_update_info_frame(pipe_ctx);
	if (!pipe_ctx_old->stream) {
		if (!pipe_ctx->stream->dpms_off)
			core_link_enable_stream(context, pipe_ctx);
	}

	pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->bottom_pipe != 0;

	pipe_ctx->stream->sink->link->psr_enabled = false;

	return DC_OK;
}

/******************************************************************************/

static void power_down_encoders(struct dc *dc)
{
	int i;
	enum connector_id connector_id;
	enum signal_type signal = SIGNAL_TYPE_NONE;

	/* do not know BIOS back-front mapping, simply blank all. It will not
	 * hurt for non-DP
	 */
	for (i = 0; i < dc->res_pool->stream_enc_count; i++) {
		dc->res_pool->stream_enc[i]->funcs->dp_blank(
					dc->res_pool->stream_enc[i]);
	}

	for (i = 0; i < dc->link_count; i++) {
		connector_id = dal_graphics_object_id_get_connector_id(dc->links[i]->link_id);
		if ((connector_id == CONNECTOR_ID_DISPLAY_PORT) ||
			(connector_id == CONNECTOR_ID_EDP)) {

			if (!dc->links[i]->wa_flags.dp_keep_receiver_powered)
				dp_receiver_power_ctrl(dc->links[i], false);
			if (connector_id == CONNECTOR_ID_EDP)
				signal = SIGNAL_TYPE_EDP;
		}

		dc->links[i]->link_enc->funcs->disable_output(
				dc->links[i]->link_enc, signal, dc->links[i]);
	}
}

static void power_down_controllers(struct dc *dc)
{
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		dc->res_pool->timing_generators[i]->funcs->disable_crtc(
				dc->res_pool->timing_generators[i]);
	}
}

static void power_down_clock_sources(struct dc *dc)
{
	int i;

	if (dc->res_pool->dp_clock_source->funcs->cs_power_down(
		dc->res_pool->dp_clock_source) == false)
		dm_error("Failed to power down pll! (dp clk src)\n");

	for (i = 0; i < dc->res_pool->clk_src_count; i++) {
		if (dc->res_pool->clock_sources[i]->funcs->cs_power_down(
				dc->res_pool->clock_sources[i]) == false)
			dm_error("Failed to power down pll! (clk src index=%d)\n", i);
	}
}

static void power_down_all_hw_blocks(struct dc *dc)
{
	power_down_encoders(dc);

	power_down_controllers(dc);

	power_down_clock_sources(dc);

#if defined(CONFIG_DRM_AMD_DC_FBC)
	if (dc->fbc_compressor)
		dc->fbc_compressor->funcs->disable_fbc(dc->fbc_compressor);
#endif
}

static void disable_vga_and_power_gate_all_controllers(
		struct dc *dc)
{
	int i;
	struct timing_generator *tg;
	struct dc_context *ctx = dc->ctx;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		tg = dc->res_pool->timing_generators[i];

		if (tg->funcs->disable_vga)
			tg->funcs->disable_vga(tg);

		/* Enable CLOCK gating for each pipe BEFORE controller
		 * powergating. */
		enable_display_pipe_clock_gating(ctx,
				true);

		dc->hwss.power_down_front_end(dc, i);
	}
}

/**
 * When ASIC goes from VBIOS/VGA mode to driver/accelerated mode we need:
 *  1. Power down all DC HW blocks
 *  2. Disable VGA engine on all controllers
 *  3. Enable power gating for controller
 *  4. Set acc_mode_change bit (VBIOS will clear this bit when going to FSDOS)
 */
void dce110_enable_accelerated_mode(struct dc *dc)
{
	power_down_all_hw_blocks(dc);

	disable_vga_and_power_gate_all_controllers(dc);
	bios_set_scratch_acc_mode_change(dc->ctx->dc_bios);
}

static uint32_t compute_pstate_blackout_duration(
	struct bw_fixed blackout_duration,
	const struct dc_stream_state *stream)
{
	uint32_t total_dest_line_time_ns;
	uint32_t pstate_blackout_duration_ns;

	pstate_blackout_duration_ns = 1000 * blackout_duration.value >> 24;

	total_dest_line_time_ns = 1000000UL *
		stream->timing.h_total /
		stream->timing.pix_clk_khz +
		pstate_blackout_duration_ns;

	return total_dest_line_time_ns;
}

void dce110_set_displaymarks(
	const struct dc *dc,
	struct dc_state *context)
{
	uint8_t i, num_pipes;
	unsigned int underlay_idx = dc->res_pool->underlay_pipe_index;

	for (i = 0, num_pipes = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		uint32_t total_dest_line_time_ns;

		if (pipe_ctx->stream == NULL)
			continue;

		total_dest_line_time_ns = compute_pstate_blackout_duration(
			dc->bw_vbios->blackout_duration, pipe_ctx->stream);
		pipe_ctx->plane_res.mi->funcs->mem_input_program_display_marks(
			pipe_ctx->plane_res.mi,
			context->bw.dce.nbp_state_change_wm_ns[num_pipes],
			context->bw.dce.stutter_exit_wm_ns[num_pipes],
			context->bw.dce.urgent_wm_ns[num_pipes],
			total_dest_line_time_ns);
		if (i == underlay_idx) {
			num_pipes++;
			pipe_ctx->plane_res.mi->funcs->mem_input_program_chroma_display_marks(
				pipe_ctx->plane_res.mi,
				context->bw.dce.nbp_state_change_wm_ns[num_pipes],
				context->bw.dce.stutter_exit_wm_ns[num_pipes],
				context->bw.dce.urgent_wm_ns[num_pipes],
				total_dest_line_time_ns);
		}
		num_pipes++;
	}
}

static void set_safe_displaymarks(
		struct resource_context *res_ctx,
		const struct resource_pool *pool)
{
	int i;
	int underlay_idx = pool->underlay_pipe_index;
	struct dce_watermarks max_marks = {
		MAX_WATERMARK, MAX_WATERMARK, MAX_WATERMARK, MAX_WATERMARK };
	struct dce_watermarks nbp_marks = {
		SAFE_NBP_MARK, SAFE_NBP_MARK, SAFE_NBP_MARK, SAFE_NBP_MARK };

	for (i = 0; i < MAX_PIPES; i++) {
		if (res_ctx->pipe_ctx[i].stream == NULL || res_ctx->pipe_ctx[i].plane_res.mi == NULL)
			continue;

		res_ctx->pipe_ctx[i].plane_res.mi->funcs->mem_input_program_display_marks(
				res_ctx->pipe_ctx[i].plane_res.mi,
				nbp_marks,
				max_marks,
				max_marks,
				MAX_WATERMARK);

		if (i == underlay_idx)
			res_ctx->pipe_ctx[i].plane_res.mi->funcs->mem_input_program_chroma_display_marks(
				res_ctx->pipe_ctx[i].plane_res.mi,
				nbp_marks,
				max_marks,
				max_marks,
				MAX_WATERMARK);

	}
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/

static void set_drr(struct pipe_ctx **pipe_ctx,
		int num_pipes, int vmin, int vmax)
{
	int i = 0;
	struct drr_params params = {0};

	params.vertical_total_max = vmax;
	params.vertical_total_min = vmin;

	/* TODO: If multiple pipes are to be supported, you need
	 * some GSL stuff
	 */

	for (i = 0; i < num_pipes; i++) {
		pipe_ctx[i]->stream_res.tg->funcs->set_drr(pipe_ctx[i]->stream_res.tg, &params);
	}
}

static void get_position(struct pipe_ctx **pipe_ctx,
		int num_pipes,
		struct crtc_position *position)
{
	int i = 0;

	/* TODO: handle pipes > 1
	 */
	for (i = 0; i < num_pipes; i++)
		pipe_ctx[i]->stream_res.tg->funcs->get_position(pipe_ctx[i]->stream_res.tg, position);
}

static void set_static_screen_control(struct pipe_ctx **pipe_ctx,
		int num_pipes, const struct dc_static_screen_events *events)
{
	unsigned int i;
	unsigned int value = 0;

	if (events->overlay_update)
		value |= 0x100;
	if (events->surface_update)
		value |= 0x80;
	if (events->cursor_update)
		value |= 0x2;

#if defined(CONFIG_DRM_AMD_DC_FBC)
	value |= 0x84;
#endif

	for (i = 0; i < num_pipes; i++)
		pipe_ctx[i]->stream_res.tg->funcs->
			set_static_screen_control(pipe_ctx[i]->stream_res.tg, value);
}

/* unit: in_khz before mode set, get pixel clock from context. ASIC register
 * may not be programmed yet.
 * TODO: after mode set, pre_mode_set = false,
 * may read PLL register to get pixel clock
 */
static uint32_t get_max_pixel_clock_for_all_paths(
	struct dc *dc,
	struct dc_state *context,
	bool pre_mode_set)
{
	uint32_t max_pix_clk = 0;
	int i;

	if (!pre_mode_set) {
		/* TODO: read ASIC register to get pixel clock */
		ASSERT(0);
	}

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		/* do not check under lay */
		if (pipe_ctx->top_pipe)
			continue;

		if (pipe_ctx->stream_res.pix_clk_params.requested_pix_clk > max_pix_clk)
			max_pix_clk =
				pipe_ctx->stream_res.pix_clk_params.requested_pix_clk;
	}

	if (max_pix_clk == 0)
		ASSERT(0);

	return max_pix_clk;
}

/*
 * Find clock state based on clock requested. if clock value is 0, simply
 * set clock state as requested without finding clock state by clock value
 */

static void apply_min_clocks(
	struct dc *dc,
	struct dc_state *context,
	enum dm_pp_clocks_state *clocks_state,
	bool pre_mode_set)
{
	struct state_dependent_clocks req_clocks = {0};

	if (!pre_mode_set) {
		/* set clock_state without verification */
		if (context->dis_clk->funcs->set_min_clocks_state) {
			context->dis_clk->funcs->set_min_clocks_state(
						context->dis_clk, *clocks_state);
			return;
		}

		/* TODO: This is incorrect. Figure out how to fix. */
		context->dis_clk->funcs->apply_clock_voltage_request(
				context->dis_clk,
				DM_PP_CLOCK_TYPE_DISPLAY_CLK,
				context->dis_clk->cur_clocks_value.dispclk_in_khz,
				pre_mode_set,
				false);

		context->dis_clk->funcs->apply_clock_voltage_request(
				context->dis_clk,
				DM_PP_CLOCK_TYPE_PIXELCLK,
				context->dis_clk->cur_clocks_value.max_pixelclk_in_khz,
				pre_mode_set,
				false);

		context->dis_clk->funcs->apply_clock_voltage_request(
				context->dis_clk,
				DM_PP_CLOCK_TYPE_DISPLAYPHYCLK,
				context->dis_clk->cur_clocks_value.max_non_dp_phyclk_in_khz,
				pre_mode_set,
				false);
		return;
	}

	/* get the required state based on state dependent clocks:
	 * display clock and pixel clock
	 */
	req_clocks.display_clk_khz = context->bw.dce.dispclk_khz;

	req_clocks.pixel_clk_khz = get_max_pixel_clock_for_all_paths(
			dc, context, true);

	if (context->dis_clk->funcs->get_required_clocks_state) {
		*clocks_state = context->dis_clk->funcs->get_required_clocks_state(
				context->dis_clk, &req_clocks);
		context->dis_clk->funcs->set_min_clocks_state(
			context->dis_clk, *clocks_state);
	} else {
		context->dis_clk->funcs->apply_clock_voltage_request(
				context->dis_clk,
				DM_PP_CLOCK_TYPE_DISPLAY_CLK,
				req_clocks.display_clk_khz,
				pre_mode_set,
				false);

		context->dis_clk->funcs->apply_clock_voltage_request(
				context->dis_clk,
				DM_PP_CLOCK_TYPE_PIXELCLK,
				req_clocks.pixel_clk_khz,
				pre_mode_set,
				false);

		context->dis_clk->funcs->apply_clock_voltage_request(
				context->dis_clk,
				DM_PP_CLOCK_TYPE_DISPLAYPHYCLK,
				req_clocks.pixel_clk_khz,
				pre_mode_set,
				false);
	}
}

#if defined(CONFIG_DRM_AMD_DC_FBC)

/*
 *  Check if FBC can be enabled
 */
static enum dc_status validate_fbc(struct dc *dc,
		struct dc_state *context)
{
	struct pipe_ctx *pipe_ctx =
			      &context->res_ctx.pipe_ctx[0];

	ASSERT(dc->fbc_compressor);

	/* FBC memory should be allocated */
	if (!dc->ctx->fbc_gpu_addr)
		return DC_ERROR_UNEXPECTED;

	/* Only supports single display */
	if (context->stream_count != 1)
		return DC_ERROR_UNEXPECTED;

	/* Only supports eDP */
	if (pipe_ctx->stream->sink->link->connector_signal != SIGNAL_TYPE_EDP)
		return DC_ERROR_UNEXPECTED;

	/* PSR should not be enabled */
	if (pipe_ctx->stream->sink->link->psr_enabled)
		return DC_ERROR_UNEXPECTED;

	/* Nothing to compress */
	if (!pipe_ctx->plane_state)
		return DC_ERROR_UNEXPECTED;

	/* Only for non-linear tiling */
	if (pipe_ctx->plane_state->tiling_info.gfx8.array_mode == DC_ARRAY_LINEAR_GENERAL)
		return DC_ERROR_UNEXPECTED;

	return DC_OK;
}

/*
 *  Enable FBC
 */
static enum dc_status enable_fbc(struct dc *dc,
		struct dc_state *context)
{
	enum dc_status status = validate_fbc(dc, context);

	if (status == DC_OK) {
		/* Program GRPH COMPRESSED ADDRESS and PITCH */
		struct compr_addr_and_pitch_params params = {0, 0, 0};
		struct compressor *compr = dc->fbc_compressor;
		struct pipe_ctx *pipe_ctx =
				      &context->res_ctx.pipe_ctx[0];

		params.source_view_width =
				pipe_ctx->stream->timing.h_addressable;
		params.source_view_height =
				pipe_ctx->stream->timing.v_addressable;

		compr->compr_surface_address.quad_part = dc->ctx->fbc_gpu_addr;

		compr->funcs->surface_address_and_pitch(compr, &params);
		compr->funcs->set_fbc_invalidation_triggers(compr, 1);

		compr->funcs->enable_fbc(compr, &params);
	}
	return status;
}
#endif

static enum dc_status apply_ctx_to_hw_fpga(
		struct dc *dc,
		struct dc_state *context)
{
	enum dc_status status = DC_ERROR_UNEXPECTED;
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx_old =
				&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		if (pipe_ctx->stream == pipe_ctx_old->stream)
			continue;

		status = apply_single_controller_ctx_to_hw(
				pipe_ctx,
				context,
				dc);

		if (status != DC_OK)
			return status;
	}

	return DC_OK;
}

static void dce110_reset_hw_ctx_wrap(
		struct dc *dc,
		struct dc_state *context)
{
	int i;

	/* Reset old context */
	/* look up the targets that have been removed since last commit */
	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx_old =
			&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* Note: We need to disable output if clock sources change,
		 * since bios does optimization and doesn't apply if changing
		 * PHY when not already disabled.
		 */

		/* Skip underlay pipe since it will be handled in commit surface*/
		if (!pipe_ctx_old->stream || pipe_ctx_old->top_pipe)
			continue;

		if (!pipe_ctx->stream ||
				pipe_need_reprogram(pipe_ctx_old, pipe_ctx)) {
			struct clock_source *old_clk = pipe_ctx_old->clock_source;

			/* Disable if new stream is null. O/w, if stream is
			 * disabled already, no need to disable again.
			 */
			if (!pipe_ctx->stream || !pipe_ctx->stream->dpms_off)
				core_link_disable_stream(pipe_ctx_old, FREE_ACQUIRED_RESOURCE);

			pipe_ctx_old->stream_res.tg->funcs->set_blank(pipe_ctx_old->stream_res.tg, true);
			if (!hwss_wait_for_blank_complete(pipe_ctx_old->stream_res.tg)) {
				dm_error("DC: failed to blank crtc!\n");
				BREAK_TO_DEBUGGER();
			}
			pipe_ctx_old->stream_res.tg->funcs->disable_crtc(pipe_ctx_old->stream_res.tg);
			pipe_ctx_old->plane_res.mi->funcs->free_mem_input(
					pipe_ctx_old->plane_res.mi, dc->current_state->stream_count);

			if (old_clk)
				old_clk->funcs->cs_power_down(old_clk);

			dc->hwss.power_down_front_end(dc, pipe_ctx_old->pipe_idx);

			pipe_ctx_old->stream = NULL;
		}
	}
}


enum dc_status dce110_apply_ctx_to_hw(
		struct dc *dc,
		struct dc_state *context)
{
	struct dc_bios *dcb = dc->ctx->dc_bios;
	enum dc_status status;
	int i;
	enum dm_pp_clocks_state clocks_state = DM_PP_CLOCKS_STATE_INVALID;

	/* Reset old context */
	/* look up the targets that have been removed since last commit */
	dc->hwss.reset_hw_ctx_wrap(dc, context);

	/* Skip applying if no targets */
	if (context->stream_count <= 0)
		return DC_OK;

	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		apply_ctx_to_hw_fpga(dc, context);
		return DC_OK;
	}

	/* Apply new context */
	dcb->funcs->set_scratch_critical_state(dcb, true);

	/* below is for real asic only */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx_old =
					&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL || pipe_ctx->top_pipe)
			continue;

		if (pipe_ctx->stream == pipe_ctx_old->stream) {
			if (pipe_ctx_old->clock_source != pipe_ctx->clock_source)
				dce_crtc_switch_to_clk_src(dc->hwseq,
						pipe_ctx->clock_source, i);
			continue;
		}

		dc->hwss.enable_display_power_gating(
				dc, i, dc->ctx->dc_bios,
				PIPE_GATING_CONTROL_DISABLE);
	}

	set_safe_displaymarks(&context->res_ctx, dc->res_pool);

#if defined(CONFIG_DRM_AMD_DC_FBC)
	if (dc->fbc_compressor)
		dc->fbc_compressor->funcs->disable_fbc(dc->fbc_compressor);
#endif
	/*TODO: when pplib works*/
	apply_min_clocks(dc, context, &clocks_state, true);

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	if (dc->ctx->dce_version >= DCN_VERSION_1_0) {
		if (context->bw.dcn.calc_clk.fclk_khz
				> dc->current_state->bw.dcn.cur_clk.fclk_khz) {
			struct dm_pp_clock_for_voltage_req clock;

			clock.clk_type = DM_PP_CLOCK_TYPE_FCLK;
			clock.clocks_in_khz = context->bw.dcn.calc_clk.fclk_khz;
			dm_pp_apply_clock_for_voltage_request(dc->ctx, &clock);
			dc->current_state->bw.dcn.cur_clk.fclk_khz = clock.clocks_in_khz;
			context->bw.dcn.cur_clk.fclk_khz = clock.clocks_in_khz;
		}
		if (context->bw.dcn.calc_clk.dcfclk_khz
				> dc->current_state->bw.dcn.cur_clk.dcfclk_khz) {
			struct dm_pp_clock_for_voltage_req clock;

			clock.clk_type = DM_PP_CLOCK_TYPE_DCFCLK;
			clock.clocks_in_khz = context->bw.dcn.calc_clk.dcfclk_khz;
			dm_pp_apply_clock_for_voltage_request(dc->ctx, &clock);
			dc->current_state->bw.dcn.cur_clk.dcfclk_khz = clock.clocks_in_khz;
			context->bw.dcn.cur_clk.dcfclk_khz = clock.clocks_in_khz;
		}
		if (context->bw.dcn.calc_clk.dispclk_khz
				> dc->current_state->bw.dcn.cur_clk.dispclk_khz) {
			dc->res_pool->display_clock->funcs->set_clock(
					dc->res_pool->display_clock,
					context->bw.dcn.calc_clk.dispclk_khz);
			dc->current_state->bw.dcn.cur_clk.dispclk_khz =
					context->bw.dcn.calc_clk.dispclk_khz;
			context->bw.dcn.cur_clk.dispclk_khz =
					context->bw.dcn.calc_clk.dispclk_khz;
		}
	} else
#endif
	if (context->bw.dce.dispclk_khz
			> dc->current_state->bw.dce.dispclk_khz) {
		dc->res_pool->display_clock->funcs->set_clock(
				dc->res_pool->display_clock,
				context->bw.dce.dispclk_khz * 115 / 100);
	}
	/* program audio wall clock. use HDMI as clock source if HDMI
	 * audio active. Otherwise, use DP as clock source
	 * first, loop to find any HDMI audio, if not, loop find DP audio
	 */
	/* Setup audio rate clock source */
	/* Issue:
	* Audio lag happened on DP monitor when unplug a HDMI monitor
	*
	* Cause:
	* In case of DP and HDMI connected or HDMI only, DCCG_AUDIO_DTO_SEL
	* is set to either dto0 or dto1, audio should work fine.
	* In case of DP connected only, DCCG_AUDIO_DTO_SEL should be dto1,
	* set to dto0 will cause audio lag.
	*
	* Solution:
	* Not optimized audio wall dto setup. When mode set, iterate pipe_ctx,
	* find first available pipe with audio, setup audio wall DTO per topology
	* instead of per pipe.
	*/
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		if (pipe_ctx->top_pipe)
			continue;

		if (pipe_ctx->stream->signal != SIGNAL_TYPE_HDMI_TYPE_A)
			continue;

		if (pipe_ctx->stream_res.audio != NULL) {
			struct audio_output audio_output;

			build_audio_output(context, pipe_ctx, &audio_output);

			pipe_ctx->stream_res.audio->funcs->wall_dto_setup(
				pipe_ctx->stream_res.audio,
				pipe_ctx->stream->signal,
				&audio_output.crtc_info,
				&audio_output.pll_info);
			break;
		}
	}

	/* no HDMI audio is found, try DP audio */
	if (i == dc->res_pool->pipe_count) {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

			if (pipe_ctx->stream == NULL)
				continue;

			if (pipe_ctx->top_pipe)
				continue;

			if (!dc_is_dp_signal(pipe_ctx->stream->signal))
				continue;

			if (pipe_ctx->stream_res.audio != NULL) {
				struct audio_output audio_output;

				build_audio_output(context, pipe_ctx, &audio_output);

				pipe_ctx->stream_res.audio->funcs->wall_dto_setup(
					pipe_ctx->stream_res.audio,
					pipe_ctx->stream->signal,
					&audio_output.crtc_info,
					&audio_output.pll_info);
				break;
			}
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx_old =
					&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		if (pipe_ctx->stream == pipe_ctx_old->stream)
			continue;

		if (pipe_ctx->stream && pipe_ctx_old->stream
				&& !pipe_need_reprogram(pipe_ctx_old, pipe_ctx))
			continue;

		if (pipe_ctx->top_pipe)
			continue;

		if (context->res_ctx.pipe_ctx[i].stream_res.audio != NULL) {

			struct audio_output audio_output;

			build_audio_output(context, pipe_ctx, &audio_output);

			if (dc_is_dp_signal(pipe_ctx->stream->signal))
				pipe_ctx->stream_res.stream_enc->funcs->dp_audio_setup(
						pipe_ctx->stream_res.stream_enc,
						pipe_ctx->stream_res.audio->inst,
						&pipe_ctx->stream->audio_info);
			else
				pipe_ctx->stream_res.stream_enc->funcs->hdmi_audio_setup(
						pipe_ctx->stream_res.stream_enc,
						pipe_ctx->stream_res.audio->inst,
						&pipe_ctx->stream->audio_info,
						&audio_output.crtc_info);

			pipe_ctx->stream_res.audio->funcs->az_configure(
					pipe_ctx->stream_res.audio,
					pipe_ctx->stream->signal,
					&audio_output.crtc_info,
					&pipe_ctx->stream->audio_info);
		}

		status = apply_single_controller_ctx_to_hw(
				pipe_ctx,
				context,
				dc);

		if (dc->hwss.power_on_front_end)
			dc->hwss.power_on_front_end(dc, pipe_ctx, context);

		if (DC_OK != status)
			return status;
	}

	/* pplib is notified if disp_num changed */
	dc->hwss.set_bandwidth(dc, context, true);

	/* to save power */
	apply_min_clocks(dc, context, &clocks_state, false);

	dcb->funcs->set_scratch_critical_state(dcb, false);

#if defined(CONFIG_DRM_AMD_DC_FBC)
	if (dc->fbc_compressor)
		enable_fbc(dc, context);

#endif

	return DC_OK;
}

/*******************************************************************************
 * Front End programming
 ******************************************************************************/
static void set_default_colors(struct pipe_ctx *pipe_ctx)
{
	struct default_adjustment default_adjust = { 0 };

	default_adjust.force_hw_default = false;
	if (pipe_ctx->plane_state == NULL)
		default_adjust.in_color_space = COLOR_SPACE_SRGB;
	else
		default_adjust.in_color_space =
				pipe_ctx->plane_state->color_space;
	if (pipe_ctx->stream == NULL)
		default_adjust.out_color_space = COLOR_SPACE_SRGB;
	else
		default_adjust.out_color_space =
				pipe_ctx->stream->output_color_space;
	default_adjust.csc_adjust_type = GRAPHICS_CSC_ADJUST_TYPE_SW;
	default_adjust.surface_pixel_format = pipe_ctx->plane_res.scl_data.format;

	/* display color depth */
	default_adjust.color_depth =
		pipe_ctx->stream->timing.display_color_depth;

	/* Lb color depth */
	default_adjust.lb_color_depth = pipe_ctx->plane_res.scl_data.lb_params.depth;

	pipe_ctx->plane_res.xfm->funcs->opp_set_csc_default(
					pipe_ctx->plane_res.xfm, &default_adjust);
}


/*******************************************************************************
 * In order to turn on/off specific surface we will program
 * Blender + CRTC
 *
 * In case that we have two surfaces and they have a different visibility
 * we can't turn off the CRTC since it will turn off the entire display
 *
 * |----------------------------------------------- |
 * |bottom pipe|curr pipe  |              |         |
 * |Surface    |Surface    | Blender      |  CRCT   |
 * |visibility |visibility | Configuration|         |
 * |------------------------------------------------|
 * |   off     |    off    | CURRENT_PIPE | blank   |
 * |   off     |    on     | CURRENT_PIPE | unblank |
 * |   on      |    off    | OTHER_PIPE   | unblank |
 * |   on      |    on     | BLENDING     | unblank |
 * -------------------------------------------------|
 *
 ******************************************************************************/
static void program_surface_visibility(const struct dc *dc,
		struct pipe_ctx *pipe_ctx)
{
	enum blnd_mode blender_mode = BLND_MODE_CURRENT_PIPE;
	bool blank_target = false;

	if (pipe_ctx->bottom_pipe) {

		/* For now we are supporting only two pipes */
		ASSERT(pipe_ctx->bottom_pipe->bottom_pipe == NULL);

		if (pipe_ctx->bottom_pipe->plane_state->visible) {
			if (pipe_ctx->plane_state->visible)
				blender_mode = BLND_MODE_BLENDING;
			else
				blender_mode = BLND_MODE_OTHER_PIPE;

		} else if (!pipe_ctx->plane_state->visible)
			blank_target = true;

	} else if (!pipe_ctx->plane_state->visible)
		blank_target = true;

	dce_set_blender_mode(dc->hwseq, pipe_ctx->pipe_idx, blender_mode);
	pipe_ctx->stream_res.tg->funcs->set_blank(pipe_ctx->stream_res.tg, blank_target);

}

static void program_gamut_remap(struct pipe_ctx *pipe_ctx)
{
	struct xfm_grph_csc_adjustment adjust;
	memset(&adjust, 0, sizeof(adjust));
	adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;


	if (pipe_ctx->stream->gamut_remap_matrix.enable_remap == true) {
		adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
		adjust.temperature_matrix[0] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[0];
		adjust.temperature_matrix[1] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[1];
		adjust.temperature_matrix[2] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[2];
		adjust.temperature_matrix[3] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[4];
		adjust.temperature_matrix[4] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[5];
		adjust.temperature_matrix[5] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[6];
		adjust.temperature_matrix[6] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[8];
		adjust.temperature_matrix[7] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[9];
		adjust.temperature_matrix[8] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[10];
	}

	pipe_ctx->plane_res.xfm->funcs->transform_set_gamut_remap(pipe_ctx->plane_res.xfm, &adjust);
}

/**
 * TODO REMOVE, USE UPDATE INSTEAD
 */
static void set_plane_config(
	const struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct resource_context *res_ctx)
{
	struct mem_input *mi = pipe_ctx->plane_res.mi;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct xfm_grph_csc_adjustment adjust;
	struct out_csc_color_matrix tbl_entry;
	unsigned int i;

	memset(&adjust, 0, sizeof(adjust));
	memset(&tbl_entry, 0, sizeof(tbl_entry));
	adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;

	dce_enable_fe_clock(dc->hwseq, pipe_ctx->pipe_idx, true);

	set_default_colors(pipe_ctx);
	if (pipe_ctx->stream->csc_color_matrix.enable_adjustment
			== true) {
		tbl_entry.color_space =
			pipe_ctx->stream->output_color_space;

		for (i = 0; i < 12; i++)
			tbl_entry.regval[i] =
			pipe_ctx->stream->csc_color_matrix.matrix[i];

		pipe_ctx->plane_res.xfm->funcs->opp_set_csc_adjustment
				(pipe_ctx->plane_res.xfm, &tbl_entry);
	}

	if (pipe_ctx->stream->gamut_remap_matrix.enable_remap == true) {
		adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
		adjust.temperature_matrix[0] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[0];
		adjust.temperature_matrix[1] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[1];
		adjust.temperature_matrix[2] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[2];
		adjust.temperature_matrix[3] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[4];
		adjust.temperature_matrix[4] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[5];
		adjust.temperature_matrix[5] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[6];
		adjust.temperature_matrix[6] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[8];
		adjust.temperature_matrix[7] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[9];
		adjust.temperature_matrix[8] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[10];
	}

	pipe_ctx->plane_res.xfm->funcs->transform_set_gamut_remap(pipe_ctx->plane_res.xfm, &adjust);

	pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->bottom_pipe != 0;
	program_scaler(dc, pipe_ctx);

	program_surface_visibility(dc, pipe_ctx);

	mi->funcs->mem_input_program_surface_config(
			mi,
			plane_state->format,
			&plane_state->tiling_info,
			&plane_state->plane_size,
			plane_state->rotation,
			NULL,
			false);
	if (mi->funcs->set_blank)
		mi->funcs->set_blank(mi, pipe_ctx->plane_state->visible);

	if (dc->config.gpu_vm_support)
		mi->funcs->mem_input_program_pte_vm(
				pipe_ctx->plane_res.mi,
				plane_state->format,
				&plane_state->tiling_info,
				plane_state->rotation);
}

static void update_plane_addr(const struct dc *dc,
		struct pipe_ctx *pipe_ctx)
{
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;

	if (plane_state == NULL)
		return;

	pipe_ctx->plane_res.mi->funcs->mem_input_program_surface_flip_and_addr(
			pipe_ctx->plane_res.mi,
			&plane_state->address,
			plane_state->flip_immediate);

	plane_state->status.requested_address = plane_state->address;
}

void dce110_update_pending_status(struct pipe_ctx *pipe_ctx)
{
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;

	if (plane_state == NULL)
		return;

	plane_state->status.is_flip_pending =
			pipe_ctx->plane_res.mi->funcs->mem_input_is_flip_pending(
					pipe_ctx->plane_res.mi);

	if (plane_state->status.is_flip_pending && !plane_state->visible)
		pipe_ctx->plane_res.mi->current_address = pipe_ctx->plane_res.mi->request_address;

	plane_state->status.current_address = pipe_ctx->plane_res.mi->current_address;
	if (pipe_ctx->plane_res.mi->current_address.type == PLN_ADDR_TYPE_GRPH_STEREO &&
			pipe_ctx->stream_res.tg->funcs->is_stereo_left_eye) {
		plane_state->status.is_right_eye =\
				!pipe_ctx->stream_res.tg->funcs->is_stereo_left_eye(pipe_ctx->stream_res.tg);
	}
}

void dce110_power_down(struct dc *dc)
{
	power_down_all_hw_blocks(dc);
	disable_vga_and_power_gate_all_controllers(dc);
}

static bool wait_for_reset_trigger_to_occur(
	struct dc_context *dc_ctx,
	struct timing_generator *tg)
{
	bool rc = false;

	/* To avoid endless loop we wait at most
	 * frames_to_wait_on_triggered_reset frames for the reset to occur. */
	const uint32_t frames_to_wait_on_triggered_reset = 10;
	uint32_t i;

	for (i = 0; i < frames_to_wait_on_triggered_reset; i++) {

		if (!tg->funcs->is_counter_moving(tg)) {
			DC_ERROR("TG counter is not moving!\n");
			break;
		}

		if (tg->funcs->did_triggered_reset_occur(tg)) {
			rc = true;
			/* usually occurs at i=1 */
			DC_SYNC_INFO("GSL: reset occurred at wait count: %d\n",
					i);
			break;
		}

		/* Wait for one frame. */
		tg->funcs->wait_for_state(tg, CRTC_STATE_VACTIVE);
		tg->funcs->wait_for_state(tg, CRTC_STATE_VBLANK);
	}

	if (false == rc)
		DC_ERROR("GSL: Timeout on reset trigger!\n");

	return rc;
}

/* Enable timing synchronization for a group of Timing Generators. */
static void dce110_enable_timing_synchronization(
		struct dc *dc,
		int group_index,
		int group_size,
		struct pipe_ctx *grouped_pipes[])
{
	struct dc_context *dc_ctx = dc->ctx;
	struct dcp_gsl_params gsl_params = { 0 };
	int i;

	DC_SYNC_INFO("GSL: Setting-up...\n");

	/* Designate a single TG in the group as a master.
	 * Since HW doesn't care which one, we always assign
	 * the 1st one in the group. */
	gsl_params.gsl_group = 0;
	gsl_params.gsl_master = grouped_pipes[0]->stream_res.tg->inst;

	for (i = 0; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->setup_global_swap_lock(
					grouped_pipes[i]->stream_res.tg, &gsl_params);

	/* Reset slave controllers on master VSync */
	DC_SYNC_INFO("GSL: enabling trigger-reset\n");

	for (i = 1 /* skip the master */; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->enable_reset_trigger(
					grouped_pipes[i]->stream_res.tg, gsl_params.gsl_group);



	for (i = 1 /* skip the master */; i < group_size; i++) {
		DC_SYNC_INFO("GSL: waiting for reset to occur.\n");
		wait_for_reset_trigger_to_occur(dc_ctx, grouped_pipes[i]->stream_res.tg);
		/* Regardless of success of the wait above, remove the reset or
		 * the driver will start timing out on Display requests. */
		DC_SYNC_INFO("GSL: disabling trigger-reset.\n");
		grouped_pipes[i]->stream_res.tg->funcs->disable_reset_trigger(grouped_pipes[i]->stream_res.tg);
	}


	/* GSL Vblank synchronization is a one time sync mechanism, assumption
	 * is that the sync'ed displays will not drift out of sync over time*/
	DC_SYNC_INFO("GSL: Restoring register states.\n");
	for (i = 0; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->tear_down_global_swap_lock(grouped_pipes[i]->stream_res.tg);

	DC_SYNC_INFO("GSL: Set-up complete.\n");
}

static void init_hw(struct dc *dc)
{
	int i;
	struct dc_bios *bp;
	struct transform *xfm;
	struct abm *abm;

	bp = dc->ctx->dc_bios;
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		xfm = dc->res_pool->transforms[i];
		xfm->funcs->transform_reset(xfm);

		dc->hwss.enable_display_power_gating(
				dc, i, bp,
				PIPE_GATING_CONTROL_INIT);
		dc->hwss.enable_display_power_gating(
				dc, i, bp,
				PIPE_GATING_CONTROL_DISABLE);
		dc->hwss.enable_display_pipe_clock_gating(
			dc->ctx,
			true);
	}

	dce_clock_gating_power_up(dc->hwseq, false);
	/***************************************/

	for (i = 0; i < dc->link_count; i++) {
		/****************************************/
		/* Power up AND update implementation according to the
		 * required signal (which may be different from the
		 * default signal on connector). */
		struct dc_link *link = dc->links[i];
		link->link_enc->funcs->hw_init(link->link_enc);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];

		tg->funcs->disable_vga(tg);

		/* Blank controller using driver code instead of
		 * command table. */
		tg->funcs->set_blank(tg, true);
		hwss_wait_for_blank_complete(tg);
	}

	for (i = 0; i < dc->res_pool->audio_count; i++) {
		struct audio *audio = dc->res_pool->audios[i];
		audio->funcs->hw_init(audio);
	}

	abm = dc->res_pool->abm;
	if (abm != NULL) {
		abm->funcs->init_backlight(abm);
		abm->funcs->abm_init(abm);
	}
#if defined(CONFIG_DRM_AMD_DC_FBC)
	if (dc->fbc_compressor)
		dc->fbc_compressor->funcs->power_up_fbc(dc->fbc_compressor);
#endif

}

void dce110_fill_display_configs(
	const struct dc_state *context,
	struct dm_pp_display_configuration *pp_display_cfg)
{
	int j;
	int num_cfgs = 0;

	for (j = 0; j < context->stream_count; j++) {
		int k;

		const struct dc_stream_state *stream = context->streams[j];
		struct dm_pp_single_disp_config *cfg =
			&pp_display_cfg->disp_configs[num_cfgs];
		const struct pipe_ctx *pipe_ctx = NULL;

		for (k = 0; k < MAX_PIPES; k++)
			if (stream == context->res_ctx.pipe_ctx[k].stream) {
				pipe_ctx = &context->res_ctx.pipe_ctx[k];
				break;
			}

		ASSERT(pipe_ctx != NULL);

		num_cfgs++;
		cfg->signal = pipe_ctx->stream->signal;
		cfg->pipe_idx = pipe_ctx->pipe_idx;
		cfg->src_height = stream->src.height;
		cfg->src_width = stream->src.width;
		cfg->ddi_channel_mapping =
			stream->sink->link->ddi_channel_mapping.raw;
		cfg->transmitter =
			stream->sink->link->link_enc->transmitter;
		cfg->link_settings.lane_count =
			stream->sink->link->cur_link_settings.lane_count;
		cfg->link_settings.link_rate =
			stream->sink->link->cur_link_settings.link_rate;
		cfg->link_settings.link_spread =
			stream->sink->link->cur_link_settings.link_spread;
		cfg->sym_clock = stream->phy_pix_clk;
		/* Round v_refresh*/
		cfg->v_refresh = stream->timing.pix_clk_khz * 1000;
		cfg->v_refresh /= stream->timing.h_total;
		cfg->v_refresh = (cfg->v_refresh + stream->timing.v_total / 2)
							/ stream->timing.v_total;
	}

	pp_display_cfg->display_count = num_cfgs;
}

uint32_t dce110_get_min_vblank_time_us(const struct dc_state *context)
{
	uint8_t j;
	uint32_t min_vertical_blank_time = -1;

	for (j = 0; j < context->stream_count; j++) {
		struct dc_stream_state *stream = context->streams[j];
		uint32_t vertical_blank_in_pixels = 0;
		uint32_t vertical_blank_time = 0;

		vertical_blank_in_pixels = stream->timing.h_total *
			(stream->timing.v_total
			 - stream->timing.v_addressable);

		vertical_blank_time = vertical_blank_in_pixels
			* 1000 / stream->timing.pix_clk_khz;

		if (min_vertical_blank_time > vertical_blank_time)
			min_vertical_blank_time = vertical_blank_time;
	}

	return min_vertical_blank_time;
}

static int determine_sclk_from_bounding_box(
		const struct dc *dc,
		int required_sclk)
{
	int i;

	/*
	 * Some asics do not give us sclk levels, so we just report the actual
	 * required sclk
	 */
	if (dc->sclk_lvls.num_levels == 0)
		return required_sclk;

	for (i = 0; i < dc->sclk_lvls.num_levels; i++) {
		if (dc->sclk_lvls.clocks_in_khz[i] >= required_sclk)
			return dc->sclk_lvls.clocks_in_khz[i];
	}
	/*
	 * even maximum level could not satisfy requirement, this
	 * is unexpected at this stage, should have been caught at
	 * validation time
	 */
	ASSERT(0);
	return dc->sclk_lvls.clocks_in_khz[dc->sclk_lvls.num_levels - 1];
}

static void pplib_apply_display_requirements(
	struct dc *dc,
	struct dc_state *context)
{
	struct dm_pp_display_configuration *pp_display_cfg = &context->pp_display_cfg;

	pp_display_cfg->all_displays_in_sync =
		context->bw.dce.all_displays_in_sync;
	pp_display_cfg->nb_pstate_switch_disable =
			context->bw.dce.nbp_state_change_enable == false;
	pp_display_cfg->cpu_cc6_disable =
			context->bw.dce.cpuc_state_change_enable == false;
	pp_display_cfg->cpu_pstate_disable =
			context->bw.dce.cpup_state_change_enable == false;
	pp_display_cfg->cpu_pstate_separation_time =
			context->bw.dce.blackout_recovery_time_us;

	pp_display_cfg->min_memory_clock_khz = context->bw.dce.yclk_khz
		/ MEMORY_TYPE_MULTIPLIER;

	pp_display_cfg->min_engine_clock_khz = determine_sclk_from_bounding_box(
			dc,
			context->bw.dce.sclk_khz);

	pp_display_cfg->min_engine_clock_deep_sleep_khz
			= context->bw.dce.sclk_deep_sleep_khz;

	pp_display_cfg->avail_mclk_switch_time_us =
						dce110_get_min_vblank_time_us(context);
	/* TODO: dce11.2*/
	pp_display_cfg->avail_mclk_switch_time_in_disp_active_us = 0;

	pp_display_cfg->disp_clk_khz = context->bw.dce.dispclk_khz;

	dce110_fill_display_configs(context, pp_display_cfg);

	/* TODO: is this still applicable?*/
	if (pp_display_cfg->display_count == 1) {
		const struct dc_crtc_timing *timing =
			&context->streams[0]->timing;

		pp_display_cfg->crtc_index =
			pp_display_cfg->disp_configs[0].pipe_idx;
		pp_display_cfg->line_time_in_us = timing->h_total * 1000
							/ timing->pix_clk_khz;
	}

	if (memcmp(&dc->prev_display_config, pp_display_cfg, sizeof(
			struct dm_pp_display_configuration)) !=  0)
		dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);

	dc->prev_display_config = *pp_display_cfg;
}

static void dce110_set_bandwidth(
		struct dc *dc,
		struct dc_state *context,
		bool decrease_allowed)
{
	dce110_set_displaymarks(dc, context);

	if (decrease_allowed || context->bw.dce.dispclk_khz > dc->current_state->bw.dce.dispclk_khz) {
		dc->res_pool->display_clock->funcs->set_clock(
				dc->res_pool->display_clock,
				context->bw.dce.dispclk_khz * 115 / 100);
		dc->current_state->bw.dce.dispclk_khz = context->bw.dce.dispclk_khz;
	}

	pplib_apply_display_requirements(dc, context);
}

static void dce110_program_front_end_for_pipe(
		struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct mem_input *mi = pipe_ctx->plane_res.mi;
	struct pipe_ctx *old_pipe = NULL;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct xfm_grph_csc_adjustment adjust;
	struct out_csc_color_matrix tbl_entry;
	struct pipe_ctx *cur_pipe_ctx =
					&dc->current_state->res_ctx.pipe_ctx[pipe_ctx->pipe_idx];
	unsigned int i;

	memset(&tbl_entry, 0, sizeof(tbl_entry));

	if (dc->current_state)
		old_pipe = &dc->current_state->res_ctx.pipe_ctx[pipe_ctx->pipe_idx];

	memset(&adjust, 0, sizeof(adjust));
	adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;

	dce_enable_fe_clock(dc->hwseq, pipe_ctx->pipe_idx, true);

	set_default_colors(pipe_ctx);
	if (pipe_ctx->stream->csc_color_matrix.enable_adjustment
			== true) {
		tbl_entry.color_space =
			pipe_ctx->stream->output_color_space;

		for (i = 0; i < 12; i++)
			tbl_entry.regval[i] =
			pipe_ctx->stream->csc_color_matrix.matrix[i];

		pipe_ctx->plane_res.xfm->funcs->opp_set_csc_adjustment
				(pipe_ctx->plane_res.xfm, &tbl_entry);
	}

	if (pipe_ctx->stream->gamut_remap_matrix.enable_remap == true) {
		adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
		adjust.temperature_matrix[0] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[0];
		adjust.temperature_matrix[1] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[1];
		adjust.temperature_matrix[2] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[2];
		adjust.temperature_matrix[3] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[4];
		adjust.temperature_matrix[4] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[5];
		adjust.temperature_matrix[5] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[6];
		adjust.temperature_matrix[6] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[8];
		adjust.temperature_matrix[7] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[9];
		adjust.temperature_matrix[8] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[10];
	}

	pipe_ctx->plane_res.xfm->funcs->transform_set_gamut_remap(pipe_ctx->plane_res.xfm, &adjust);

	pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->bottom_pipe != 0;

	program_scaler(dc, pipe_ctx);

#if defined(CONFIG_DRM_AMD_DC_FBC)
	if (dc->fbc_compressor && old_pipe->stream) {
		if (plane_state->tiling_info.gfx8.array_mode == DC_ARRAY_LINEAR_GENERAL)
			dc->fbc_compressor->funcs->disable_fbc(dc->fbc_compressor);
		else
			enable_fbc(dc, dc->current_state);
	}
#endif

	mi->funcs->mem_input_program_surface_config(
			mi,
			plane_state->format,
			&plane_state->tiling_info,
			&plane_state->plane_size,
			plane_state->rotation,
			NULL,
			false);
	if (mi->funcs->set_blank)
		mi->funcs->set_blank(mi, pipe_ctx->plane_state->visible);

	if (dc->config.gpu_vm_support)
		mi->funcs->mem_input_program_pte_vm(
				pipe_ctx->plane_res.mi,
				plane_state->format,
				&plane_state->tiling_info,
				plane_state->rotation);

	/* Moved programming gamma from dc to hwss */
	if (cur_pipe_ctx->plane_state != pipe_ctx->plane_state) {
		dc->hwss.set_input_transfer_func(
				pipe_ctx, pipe_ctx->plane_state);
		dc->hwss.set_output_transfer_func(
				pipe_ctx, pipe_ctx->stream);
	}

	dm_logger_write(dc->ctx->logger, LOG_SURFACE,
			"Pipe:%d 0x%x: addr hi:0x%x, "
			"addr low:0x%x, "
			"src: %d, %d, %d,"
			" %d; dst: %d, %d, %d, %d;"
			"clip: %d, %d, %d, %d\n",
			pipe_ctx->pipe_idx,
			pipe_ctx->plane_state,
			pipe_ctx->plane_state->address.grph.addr.high_part,
			pipe_ctx->plane_state->address.grph.addr.low_part,
			pipe_ctx->plane_state->src_rect.x,
			pipe_ctx->plane_state->src_rect.y,
			pipe_ctx->plane_state->src_rect.width,
			pipe_ctx->plane_state->src_rect.height,
			pipe_ctx->plane_state->dst_rect.x,
			pipe_ctx->plane_state->dst_rect.y,
			pipe_ctx->plane_state->dst_rect.width,
			pipe_ctx->plane_state->dst_rect.height,
			pipe_ctx->plane_state->clip_rect.x,
			pipe_ctx->plane_state->clip_rect.y,
			pipe_ctx->plane_state->clip_rect.width,
			pipe_ctx->plane_state->clip_rect.height);

	dm_logger_write(dc->ctx->logger, LOG_SURFACE,
			"Pipe %d: width, height, x, y\n"
			"viewport:%d, %d, %d, %d\n"
			"recout:  %d, %d, %d, %d\n",
			pipe_ctx->pipe_idx,
			pipe_ctx->plane_res.scl_data.viewport.width,
			pipe_ctx->plane_res.scl_data.viewport.height,
			pipe_ctx->plane_res.scl_data.viewport.x,
			pipe_ctx->plane_res.scl_data.viewport.y,
			pipe_ctx->plane_res.scl_data.recout.width,
			pipe_ctx->plane_res.scl_data.recout.height,
			pipe_ctx->plane_res.scl_data.recout.x,
			pipe_ctx->plane_res.scl_data.recout.y);
}

static void dce110_apply_ctx_for_surface(
		struct dc *dc,
		const struct dc_stream_state *stream,
		int num_planes,
		struct dc_state *context)
{
	int i, be_idx;

	if (num_planes == 0)
		return;

	be_idx = -1;
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

		if (stream == context->res_ctx.pipe_ctx[i].stream) {
			be_idx = context->res_ctx.pipe_ctx[i].stream_res.tg->inst;
			if (!pipe_ctx->top_pipe &&
				(pipe_ctx->plane_state || old_pipe_ctx->plane_state))
				dc->hwss.pipe_control_lock(dc, pipe_ctx, true);
			break;
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream != stream)
			continue;

		/* Need to allocate mem before program front end for Fiji */
		if (pipe_ctx->plane_res.mi != NULL)
			pipe_ctx->plane_res.mi->funcs->allocate_mem_input(
					pipe_ctx->plane_res.mi,
					pipe_ctx->stream->timing.h_total,
					pipe_ctx->stream->timing.v_total,
					pipe_ctx->stream->timing.pix_clk_khz,
					context->stream_count);

		dce110_program_front_end_for_pipe(dc, pipe_ctx);
		program_surface_visibility(dc, pipe_ctx);

	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

		if ((stream == pipe_ctx->stream) &&
			(!pipe_ctx->top_pipe) &&
			(pipe_ctx->plane_state || old_pipe_ctx->plane_state))
			dc->hwss.pipe_control_lock(dc, pipe_ctx, false);
	}
}

static void dce110_power_down_fe(struct dc *dc, int fe_idx)
{
	/* Do not power down fe when stream is active on dce*/
	if (dc->current_state->res_ctx.pipe_ctx[fe_idx].stream)
		return;

	dc->hwss.enable_display_power_gating(
		dc, fe_idx, dc->ctx->dc_bios, PIPE_GATING_CONTROL_ENABLE);

	dc->res_pool->transforms[fe_idx]->funcs->transform_reset(
				dc->res_pool->transforms[fe_idx]);
}

static void dce110_wait_for_mpcc_disconnect(
		struct dc *dc,
		struct resource_pool *res_pool,
		struct pipe_ctx *pipe_ctx)
{
	/* do nothing*/
}

static void program_csc_matrix(struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix)
{
	int i;
	struct out_csc_color_matrix tbl_entry;

	if (pipe_ctx->stream->csc_color_matrix.enable_adjustment
				== true) {
			enum dc_color_space color_space =
				pipe_ctx->stream->output_color_space;

			//uint16_t matrix[12];
			for (i = 0; i < 12; i++)
				tbl_entry.regval[i] = pipe_ctx->stream->csc_color_matrix.matrix[i];

			tbl_entry.color_space = color_space;
			//tbl_entry.regval = matrix;
			pipe_ctx->plane_res.xfm->funcs->opp_set_csc_adjustment(pipe_ctx->plane_res.xfm, &tbl_entry);
	}
}

static void ready_shared_resources(struct dc *dc, struct dc_state *context) {}

static void optimize_shared_resources(struct dc *dc) {}

static const struct hw_sequencer_funcs dce110_funcs = {
	.program_gamut_remap = program_gamut_remap,
	.program_csc_matrix = program_csc_matrix,
	.init_hw = init_hw,
	.apply_ctx_to_hw = dce110_apply_ctx_to_hw,
	.apply_ctx_for_surface = dce110_apply_ctx_for_surface,
	.set_plane_config = set_plane_config,
	.update_plane_addr = update_plane_addr,
	.update_pending_status = dce110_update_pending_status,
	.set_input_transfer_func = dce110_set_input_transfer_func,
	.set_output_transfer_func = dce110_set_output_transfer_func,
	.power_down = dce110_power_down,
	.enable_accelerated_mode = dce110_enable_accelerated_mode,
	.enable_timing_synchronization = dce110_enable_timing_synchronization,
	.update_info_frame = dce110_update_info_frame,
	.enable_stream = dce110_enable_stream,
	.disable_stream = dce110_disable_stream,
	.unblank_stream = dce110_unblank_stream,
	.enable_display_pipe_clock_gating = enable_display_pipe_clock_gating,
	.enable_display_power_gating = dce110_enable_display_power_gating,
	.power_down_front_end = dce110_power_down_fe,
	.pipe_control_lock = dce_pipe_control_lock,
	.set_bandwidth = dce110_set_bandwidth,
	.set_drr = set_drr,
	.get_position = get_position,
	.set_static_screen_control = set_static_screen_control,
	.reset_hw_ctx_wrap = dce110_reset_hw_ctx_wrap,
	.prog_pixclk_crtc_otg = dce110_prog_pixclk_crtc_otg,
	.setup_stereo = NULL,
	.set_avmute = dce110_set_avmute,
	.wait_for_mpcc_disconnect = dce110_wait_for_mpcc_disconnect,
	.ready_shared_resources = ready_shared_resources,
	.optimize_shared_resources = optimize_shared_resources,
	.edp_backlight_control = hwss_edp_backlight_control,
	.edp_power_control = hwss_edp_power_control,
};

void dce110_hw_sequencer_construct(struct dc *dc)
{
	dc->hwss = dce110_funcs;
}

