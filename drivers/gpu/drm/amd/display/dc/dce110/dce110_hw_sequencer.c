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
#include "dce110_timing_generator.h"
#include "dce/dce_hwseq.h"
#include "gpio_service_interface.h"

#include "dce110_compressor.h"

#include "bios/bios_parser_helper.h"
#include "timing_generator.h"
#include "mem_input.h"
#include "opp.h"
#include "ipp.h"
#include "transform.h"
#include "stream_encoder.h"
#include "link_encoder.h"
#include "link_enc_cfg.h"
#include "link_hwss.h"
#include "dc_link_dp.h"
#include "dccg.h"
#include "clock_source.h"
#include "clk_mgr.h"
#include "abm.h"
#include "audio.h"
#include "reg_helper.h"
#include "panel_cntl.h"
#include "inc/link_dpcd.h"
#include "dpcd_defs.h"
/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "custom_float.h"

#include "atomfirmware.h"

#include "dcn10/dcn10_hw_sequencer.h"

#include "link/link_dp_trace.h"
#include "dce110_hw_sequencer.h"

#define GAMMA_HW_POINTS_NUM 256

/*
 * All values are in milliseconds;
 * For eDP, after power-up/power/down,
 * 300/500 msec max. delay from LCDVCC to black video generation
 */
#define PANEL_POWER_UP_TIMEOUT 300
#define PANEL_POWER_DOWN_TIMEOUT 500
#define HPD_CHECK_INTERVAL 10
#define OLED_POST_T7_DELAY 100
#define OLED_PRE_T11_DELAY 150

#define CTX \
	hws->ctx

#define DC_LOGGER_INIT()

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
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		prescale_params->scale = 0x2082;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
		prescale_params->scale = 0x2020;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		prescale_params->scale = 0x2008;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		prescale_params->scale = 0x2000;
		break;
	default:
		ASSERT(false);
		break;
	}
}

static bool
dce110_set_input_transfer_func(struct dc *dc, struct pipe_ctx *pipe_ctx,
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

	if (plane_state->gamma_correction &&
			!plane_state->gamma_correction->is_identity &&
			dce_use_lut(plane_state->format))
		ipp->funcs->ipp_program_input_lut(ipp, plane_state->gamma_correction);

	if (tf == NULL) {
		/* Default case if no input transfer function specified */
		ipp->funcs->ipp_set_degamma(ipp, IPP_DEGAMMA_MODE_HW_sRGB);
	} else if (tf->type == TF_TYPE_PREDEFINED) {
		switch (tf->tf) {
		case TRANSFER_FUNCTION_SRGB:
			ipp->funcs->ipp_set_degamma(ipp, IPP_DEGAMMA_MODE_HW_sRGB);
			break;
		case TRANSFER_FUNCTION_BT709:
			ipp->funcs->ipp_set_degamma(ipp, IPP_DEGAMMA_MODE_HW_xvYCC);
			break;
		case TRANSFER_FUNCTION_LINEAR:
			ipp->funcs->ipp_set_degamma(ipp, IPP_DEGAMMA_MODE_BYPASS);
			break;
		case TRANSFER_FUNCTION_PQ:
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

static bool convert_to_custom_float(struct pwl_result_data *rgb_resulted,
				    struct curve_points *arr_points,
				    uint32_t hw_points_num)
{
	struct custom_float_format fmt;

	struct pwl_result_data *rgb = rgb_resulted;

	uint32_t i = 0;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = true;

	if (!convert_to_custom_float_format(arr_points[0].x, &fmt,
					    &arr_points[0].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(arr_points[0].offset, &fmt,
					    &arr_points[0].custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(arr_points[0].slope, &fmt,
					    &arr_points[0].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 10;
	fmt.sign = false;

	if (!convert_to_custom_float_format(arr_points[1].x, &fmt,
					    &arr_points[1].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(arr_points[1].y, &fmt,
					    &arr_points[1].custom_float_y)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(arr_points[1].slope, &fmt,
					    &arr_points[1].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 12;
	fmt.sign = true;

	while (i != hw_points_num) {
		if (!convert_to_custom_float_format(rgb->red, &fmt,
						    &rgb->red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(rgb->green, &fmt,
						    &rgb->green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(rgb->blue, &fmt,
						    &rgb->blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(rgb->delta_red, &fmt,
						    &rgb->delta_red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(rgb->delta_green, &fmt,
						    &rgb->delta_green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(rgb->delta_blue, &fmt,
						    &rgb->delta_blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		++rgb;
		++i;
	}

	return true;
}

#define MAX_LOW_POINT      25
#define NUMBER_REGIONS     16
#define NUMBER_SW_SEGMENTS 16

static bool
dce110_translate_regamma_to_hw_format(const struct dc_transfer_func *output_tf,
				      struct pwl_params *regamma_params)
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

	int32_t region_start, region_end;
	uint32_t i, j, k, seg_distr[NUMBER_REGIONS], increment, start_index, hw_points;

	if (output_tf == NULL || regamma_params == NULL || output_tf->type == TF_TYPE_BYPASS)
		return false;

	arr_points = regamma_params->arr_points;
	rgb_resulted = regamma_params->rgb_resulted;
	hw_points = 0;

	memset(regamma_params, 0, sizeof(struct pwl_params));

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* 16 segments
		 * segments are from 2^-11 to 2^5
		 */
		region_start = -11;
		region_end = region_start + NUMBER_REGIONS;

		for (i = 0; i < NUMBER_REGIONS; i++)
			seg_distr[i] = 4;

	} else {
		/* 10 segments
		 * segment is from 2^-10 to 2^1
		 * We include an extra segment for range [2^0, 2^1). This is to
		 * ensure that colors with normalized values of 1 don't miss the
		 * LUT.
		 */
		region_start = -10;
		region_end = 1;

		seg_distr[0] = 4;
		seg_distr[1] = 4;
		seg_distr[2] = 4;
		seg_distr[3] = 4;
		seg_distr[4] = 4;
		seg_distr[5] = 4;
		seg_distr[6] = 4;
		seg_distr[7] = 4;
		seg_distr[8] = 4;
		seg_distr[9] = 4;
		seg_distr[10] = 0;
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
	for (k = 0; k < (region_end - region_start); k++) {
		increment = NUMBER_SW_SEGMENTS / (1 << seg_distr[k]);
		start_index = (region_start + k + MAX_LOW_POINT) *
				NUMBER_SW_SEGMENTS;
		for (i = start_index; i < start_index + NUMBER_SW_SEGMENTS;
				i += increment) {
			if (j == hw_points - 1)
				break;
			rgb_resulted[j].red = output_tf->tf_pts.red[i];
			rgb_resulted[j].green = output_tf->tf_pts.green[i];
			rgb_resulted[j].blue = output_tf->tf_pts.blue[i];
			j++;
		}
	}

	/* last point */
	start_index = (region_end + MAX_LOW_POINT) * NUMBER_SW_SEGMENTS;
	rgb_resulted[hw_points - 1].red = output_tf->tf_pts.red[start_index];
	rgb_resulted[hw_points - 1].green = output_tf->tf_pts.green[start_index];
	rgb_resulted[hw_points - 1].blue = output_tf->tf_pts.blue[start_index];

	arr_points[0].x = dc_fixpt_pow(dc_fixpt_from_int(2),
					     dc_fixpt_from_int(region_start));
	arr_points[1].x = dc_fixpt_pow(dc_fixpt_from_int(2),
					     dc_fixpt_from_int(region_end));

	y_r = rgb_resulted[0].red;
	y_g = rgb_resulted[0].green;
	y_b = rgb_resulted[0].blue;

	y1_min = dc_fixpt_min(y_r, dc_fixpt_min(y_g, y_b));

	arr_points[0].y = y1_min;
	arr_points[0].slope = dc_fixpt_div(arr_points[0].y,
						 arr_points[0].x);

	y_r = rgb_resulted[hw_points - 1].red;
	y_g = rgb_resulted[hw_points - 1].green;
	y_b = rgb_resulted[hw_points - 1].blue;

	/* see comment above, m_arrPoints[1].y should be the Y value for the
	 * region end (m_numOfHwPoints), not last HW point(m_numOfHwPoints - 1)
	 */
	y3_max = dc_fixpt_max(y_r, dc_fixpt_max(y_g, y_b));

	arr_points[1].y = y3_max;

	arr_points[1].slope = dc_fixpt_zero;

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* for PQ, we want to have a straight line from last HW X point,
		 * and the slope to be such that we hit 1.0 at 10000 nits.
		 */
		const struct fixed31_32 end_value = dc_fixpt_from_int(125);

		arr_points[1].slope = dc_fixpt_div(
				dc_fixpt_sub(dc_fixpt_one, arr_points[1].y),
				dc_fixpt_sub(end_value, arr_points[1].x));
	}

	regamma_params->hw_points_num = hw_points;

	k = 0;
	for (i = 1; i < 16; i++) {
		if (seg_distr[k] != -1) {
			regamma_params->arr_curve_points[k].segments_num = seg_distr[k];
			regamma_params->arr_curve_points[i].offset =
					regamma_params->arr_curve_points[k].offset + (1 << seg_distr[k]);
		}
		k++;
	}

	if (seg_distr[k] != -1)
		regamma_params->arr_curve_points[k].segments_num = seg_distr[k];

	rgb = rgb_resulted;
	rgb_plus_1 = rgb_resulted + 1;

	i = 1;

	while (i != hw_points + 1) {
		if (dc_fixpt_lt(rgb_plus_1->red, rgb->red))
			rgb_plus_1->red = rgb->red;
		if (dc_fixpt_lt(rgb_plus_1->green, rgb->green))
			rgb_plus_1->green = rgb->green;
		if (dc_fixpt_lt(rgb_plus_1->blue, rgb->blue))
			rgb_plus_1->blue = rgb->blue;

		rgb->delta_red = dc_fixpt_sub(rgb_plus_1->red, rgb->red);
		rgb->delta_green = dc_fixpt_sub(rgb_plus_1->green, rgb->green);
		rgb->delta_blue = dc_fixpt_sub(rgb_plus_1->blue, rgb->blue);

		++rgb_plus_1;
		++rgb;
		++i;
	}

	convert_to_custom_float(rgb_resulted, arr_points, hw_points);

	return true;
}

static bool
dce110_set_output_transfer_func(struct dc *dc, struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream)
{
	struct transform *xfm = pipe_ctx->plane_res.xfm;

	xfm->funcs->opp_power_on_regamma_lut(xfm, true);
	xfm->regamma_params.hw_points_num = GAMMA_HW_POINTS_NUM;

	if (stream->out_transfer_func &&
	    stream->out_transfer_func->type == TF_TYPE_PREDEFINED &&
	    stream->out_transfer_func->tf == TRANSFER_FUNCTION_SRGB) {
		xfm->funcs->opp_set_regamma_mode(xfm, OPP_REGAMMA_SRGB);
	} else if (dce110_translate_regamma_to_hw_format(stream->out_transfer_func,
							 &xfm->regamma_params)) {
		xfm->funcs->opp_program_regamma_pwl(xfm, &xfm->regamma_params);
		xfm->funcs->opp_set_regamma_mode(xfm, OPP_REGAMMA_USER);
	} else {
		xfm->funcs->opp_set_regamma_mode(xfm, OPP_REGAMMA_BYPASS);
	}

	xfm->funcs->opp_power_on_regamma_lut(xfm, false);

	return true;
}

void dce110_update_info_frame(struct pipe_ctx *pipe_ctx)
{
	bool is_hdmi_tmds;
	bool is_dp;

	ASSERT(pipe_ctx->stream);

	if (pipe_ctx->stream_res.stream_enc == NULL)
		return;  /* this is not root pipe */

	is_hdmi_tmds = dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal);
	is_dp = dc_is_dp_signal(pipe_ctx->stream->signal);

	if (!is_hdmi_tmds && !is_dp)
		return;

	if (is_hdmi_tmds)
		pipe_ctx->stream_res.stream_enc->funcs->update_hdmi_info_packets(
			pipe_ctx->stream_res.stream_enc,
			&pipe_ctx->stream_res.encoder_info_frame);
	else
		pipe_ctx->stream_res.stream_enc->funcs->update_dp_info_packets(
			pipe_ctx->stream_res.stream_enc,
			&pipe_ctx->stream_res.encoder_info_frame);
}

void dce110_enable_stream(struct pipe_ctx *pipe_ctx)
{
	enum dc_lane_count lane_count =
		pipe_ctx->stream->link->cur_link_settings.lane_count;
	struct dc_crtc_timing *timing = &pipe_ctx->stream->timing;
	struct dc_link *link = pipe_ctx->stream->link;
	const struct dc *dc = link->dc;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
	uint32_t active_total_with_borders;
	uint32_t early_control = 0;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;

	link_hwss->setup_stream_encoder(pipe_ctx);

	dc->hwss.update_info_frame(pipe_ctx);

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
void dce110_edp_wait_for_hpd_ready(
		struct dc_link *link,
		bool power_up)
{
	struct dc_context *ctx = link->ctx;
	struct graphics_object_id connector = link->link_enc->connector;
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

	if (link != NULL) {
		if (link->panel_config.pps.extra_t3_ms > 0) {
			int extra_t3_in_ms = link->panel_config.pps.extra_t3_ms;

			msleep(extra_t3_in_ms);
		}
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
		DC_LOG_WARNING(
				"%s: wait timed out!\n", __func__);
	}
}

void dce110_edp_power_control(
		struct dc_link *link,
		bool power_up)
{
	struct dc_context *ctx = link->ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result bp_result;
	uint8_t panel_instance;


	if (dal_graphics_object_id_get_connector_id(link->link_enc->connector)
			!= CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (!link->panel_cntl)
		return;
	if (power_up !=
		link->panel_cntl->funcs->is_panel_powered_on(link->panel_cntl)) {

		unsigned long long current_ts = dm_get_timestamp(ctx);
		unsigned long long time_since_edp_poweroff_ms =
				div64_u64(dm_get_elapse_time_in_ns(
						ctx,
						current_ts,
						dp_trace_get_edp_poweroff_timestamp(link)), 1000000);
		unsigned long long time_since_edp_poweron_ms =
				div64_u64(dm_get_elapse_time_in_ns(
						ctx,
						current_ts,
						dp_trace_get_edp_poweron_timestamp(link)), 1000000);
		DC_LOG_HW_RESUME_S3(
				"%s: transition: power_up=%d current_ts=%llu edp_poweroff=%llu edp_poweron=%llu time_since_edp_poweroff_ms=%llu time_since_edp_poweron_ms=%llu",
				__func__,
				power_up,
				current_ts,
				dp_trace_get_edp_poweroff_timestamp(link),
				dp_trace_get_edp_poweron_timestamp(link),
				time_since_edp_poweroff_ms,
				time_since_edp_poweron_ms);

		/* Send VBIOS command to prompt eDP panel power */
		if (power_up) {
			/* edp requires a min of 500ms from LCDVDD off to on */
			unsigned long long remaining_min_edp_poweroff_time_ms = 500;

			/* add time defined by a patch, if any (usually patch extra_t12_ms is 0) */
			if (link->local_sink != NULL)
				remaining_min_edp_poweroff_time_ms +=
					link->panel_config.pps.extra_t12_ms;

			/* Adjust remaining_min_edp_poweroff_time_ms if this is not the first time. */
			if (dp_trace_get_edp_poweroff_timestamp(link) != 0) {
				if (time_since_edp_poweroff_ms < remaining_min_edp_poweroff_time_ms)
					remaining_min_edp_poweroff_time_ms =
						remaining_min_edp_poweroff_time_ms - time_since_edp_poweroff_ms;
				else
					remaining_min_edp_poweroff_time_ms = 0;
			}

			if (remaining_min_edp_poweroff_time_ms) {
				DC_LOG_HW_RESUME_S3(
						"%s: remaining_min_edp_poweroff_time_ms=%llu: begin wait.\n",
						__func__, remaining_min_edp_poweroff_time_ms);
				msleep(remaining_min_edp_poweroff_time_ms);
				DC_LOG_HW_RESUME_S3(
						"%s: remaining_min_edp_poweroff_time_ms=%llu: end wait.\n",
						__func__, remaining_min_edp_poweroff_time_ms);
				dm_output_to_console("%s: wait %lld ms to power on eDP.\n",
						__func__, remaining_min_edp_poweroff_time_ms);
			} else {
				DC_LOG_HW_RESUME_S3(
						"%s: remaining_min_edp_poweroff_time_ms=%llu: no wait required.\n",
						__func__, remaining_min_edp_poweroff_time_ms);
			}
		}

		DC_LOG_HW_RESUME_S3(
				"%s: BEGIN: Panel Power action: %s\n",
				__func__, (power_up ? "On":"Off"));

		cntl.action = power_up ?
			TRANSMITTER_CONTROL_POWER_ON :
			TRANSMITTER_CONTROL_POWER_OFF;
		cntl.transmitter = link->link_enc->transmitter;
		cntl.connector_obj_id = link->link_enc->connector;
		cntl.coherent = false;
		cntl.lanes_number = LANE_COUNT_FOUR;
		cntl.hpd_sel = link->link_enc->hpd_source;
		panel_instance = link->panel_cntl->inst;

		if (ctx->dc->ctx->dmub_srv &&
				ctx->dc->debug.dmub_command_table) {
			if (cntl.action == TRANSMITTER_CONTROL_POWER_ON)
				bp_result = ctx->dc_bios->funcs->enable_lvtma_control(ctx->dc_bios,
						LVTMA_CONTROL_POWER_ON,
						panel_instance);
			else
				bp_result = ctx->dc_bios->funcs->enable_lvtma_control(ctx->dc_bios,
						LVTMA_CONTROL_POWER_OFF,
						panel_instance);
		}

		bp_result = link_transmitter_control(ctx->dc_bios, &cntl);

		DC_LOG_HW_RESUME_S3(
				"%s: END: Panel Power action: %s bp_result=%u\n",
				__func__, (power_up ? "On":"Off"),
				bp_result);

		dp_trace_set_edp_power_timestamp(link, power_up);

		DC_LOG_HW_RESUME_S3(
				"%s: updated values: edp_poweroff=%llu edp_poweron=%llu\n",
				__func__,
				dp_trace_get_edp_poweroff_timestamp(link),
				dp_trace_get_edp_poweron_timestamp(link));

		if (bp_result != BP_RESULT_OK)
			DC_LOG_ERROR(
					"%s: Panel Power bp_result: %d\n",
					__func__, bp_result);
	} else {
		DC_LOG_HW_RESUME_S3(
				"%s: Skipping Panel Power action: %s\n",
				__func__, (power_up ? "On":"Off"));
	}
}

void dce110_edp_wait_for_T12(
		struct dc_link *link)
{
	struct dc_context *ctx = link->ctx;

	if (dal_graphics_object_id_get_connector_id(link->link_enc->connector)
			!= CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (!link->panel_cntl)
		return;

	if (!link->panel_cntl->funcs->is_panel_powered_on(link->panel_cntl) &&
			dp_trace_get_edp_poweroff_timestamp(link) != 0) {
		unsigned int t12_duration = 500; // Default T12 as per spec
		unsigned long long current_ts = dm_get_timestamp(ctx);
		unsigned long long time_since_edp_poweroff_ms =
				div64_u64(dm_get_elapse_time_in_ns(
						ctx,
						current_ts,
						dp_trace_get_edp_poweroff_timestamp(link)), 1000000);

		t12_duration += link->panel_config.pps.extra_t12_ms; // Add extra T12

		if (time_since_edp_poweroff_ms < t12_duration)
			msleep(t12_duration - time_since_edp_poweroff_ms);
	}
}

/*todo: cloned in stream enc, fix*/
/*
 * @brief
 * eDP only. Control the backlight of the eDP panel
 */
void dce110_edp_backlight_control(
		struct dc_link *link,
		bool enable)
{
	struct dc_context *ctx = link->ctx;
	struct bp_transmitter_control cntl = { 0 };
	uint8_t panel_instance;
	unsigned int pre_T11_delay = OLED_PRE_T11_DELAY;
	unsigned int post_T7_delay = OLED_POST_T7_DELAY;

	if (dal_graphics_object_id_get_connector_id(link->link_enc->connector)
		!= CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (link->panel_cntl) {
		bool is_backlight_on = link->panel_cntl->funcs->is_panel_backlight_on(link->panel_cntl);

		if ((enable && is_backlight_on) || (!enable && !is_backlight_on)) {
			DC_LOG_HW_RESUME_S3(
				"%s: panel already powered up/off. Do nothing.\n",
				__func__);
			return;
		}
	}

	/* Send VBIOS command to control eDP panel backlight */

	DC_LOG_HW_RESUME_S3(
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
	cntl.signal = SIGNAL_TYPE_EDP;

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
		/*edp 1.2*/
	panel_instance = link->panel_cntl->inst;

	if (cntl.action == TRANSMITTER_CONTROL_BACKLIGHT_ON) {
		if (!link->dc->config.edp_no_power_sequencing)
		/*
		 * Sometimes, DP receiver chip power-controlled externally by an
		 * Embedded Controller could be treated and used as eDP,
		 * if it drives mobile display. In this case,
		 * we shouldn't be doing power-sequencing, hence we can skip
		 * waiting for T7-ready.
		 */
			edp_receiver_ready_T7(link);
		else
			DC_LOG_DC("edp_receiver_ready_T7 skipped\n");
	}

	if (ctx->dc->ctx->dmub_srv &&
			ctx->dc->debug.dmub_command_table) {
		if (cntl.action == TRANSMITTER_CONTROL_BACKLIGHT_ON)
			ctx->dc_bios->funcs->enable_lvtma_control(ctx->dc_bios,
					LVTMA_CONTROL_LCD_BLON,
					panel_instance);
		else
			ctx->dc_bios->funcs->enable_lvtma_control(ctx->dc_bios,
					LVTMA_CONTROL_LCD_BLOFF,
					panel_instance);
	}

	link_transmitter_control(ctx->dc_bios, &cntl);

	if (enable && link->dpcd_sink_ext_caps.bits.oled) {
		post_T7_delay += link->panel_config.pps.extra_post_t7_ms;
		msleep(post_T7_delay);
	}

	if (link->dpcd_sink_ext_caps.bits.oled ||
		link->dpcd_sink_ext_caps.bits.hdr_aux_backlight_control == 1 ||
		link->dpcd_sink_ext_caps.bits.sdr_aux_backlight_control == 1)
		dc_link_backlight_enable_aux(link, enable);

	/*edp 1.2*/
	if (cntl.action == TRANSMITTER_CONTROL_BACKLIGHT_OFF) {
		if (!link->dc->config.edp_no_power_sequencing)
		/*
		 * Sometimes, DP receiver chip power-controlled externally by an
		 * Embedded Controller could be treated and used as eDP,
		 * if it drives mobile display. In this case,
		 * we shouldn't be doing power-sequencing, hence we can skip
		 * waiting for T9-ready.
		 */
			edp_add_delay_for_T9(link);
		else
			DC_LOG_DC("edp_receiver_ready_T9 skipped\n");
	}

	if (!enable && link->dpcd_sink_ext_caps.bits.oled) {
		pre_T11_delay += link->panel_config.pps.extra_pre_t11_ms;
		msleep(pre_T11_delay);
	}
}

void dce110_enable_audio_stream(struct pipe_ctx *pipe_ctx)
{
	/* notify audio driver for audio modes of monitor */
	struct dc *dc;
	struct clk_mgr *clk_mgr;
	unsigned int i, num_audio = 1;
	const struct link_hwss *link_hwss;

	if (!pipe_ctx->stream)
		return;

	dc = pipe_ctx->stream->ctx->dc;
	clk_mgr = dc->clk_mgr;
	link_hwss = get_link_hwss(pipe_ctx->stream->link, &pipe_ctx->link_res);

	if (pipe_ctx->stream_res.audio && pipe_ctx->stream_res.audio->enabled == true)
		return;

	if (pipe_ctx->stream_res.audio) {
		for (i = 0; i < MAX_PIPES; i++) {
			/*current_state not updated yet*/
			if (dc->current_state->res_ctx.pipe_ctx[i].stream_res.audio != NULL)
				num_audio++;
		}

		pipe_ctx->stream_res.audio->funcs->az_enable(pipe_ctx->stream_res.audio);

		if (num_audio >= 1 && clk_mgr->funcs->enable_pme_wa)
			/*this is the first audio. apply the PME w/a in order to wake AZ from D3*/
			clk_mgr->funcs->enable_pme_wa(clk_mgr);

		link_hwss->enable_audio_packet(pipe_ctx);

		if (pipe_ctx->stream_res.audio)
			pipe_ctx->stream_res.audio->enabled = true;
	}
}

void dce110_disable_audio_stream(struct pipe_ctx *pipe_ctx)
{
	struct dc *dc;
	struct clk_mgr *clk_mgr;
	const struct link_hwss *link_hwss;

	if (!pipe_ctx || !pipe_ctx->stream)
		return;

	dc = pipe_ctx->stream->ctx->dc;
	clk_mgr = dc->clk_mgr;
	link_hwss = get_link_hwss(pipe_ctx->stream->link, &pipe_ctx->link_res);

	if (pipe_ctx->stream_res.audio && pipe_ctx->stream_res.audio->enabled == false)
		return;

	link_hwss->disable_audio_packet(pipe_ctx);

	if (pipe_ctx->stream_res.audio) {
		pipe_ctx->stream_res.audio->enabled = false;

		if (clk_mgr->funcs->enable_pme_wa)
			/*this is the first audio. apply the PME w/a in order to wake AZ from D3*/
			clk_mgr->funcs->enable_pme_wa(clk_mgr);

		/* TODO: notify audio driver for if audio modes list changed
		 * add audio mode list change flag */
		/* dal_audio_disable_azalia_audio_jack_presence(stream->audio,
		 * stream->stream_engine_id);
		 */
	}
}

void dce110_disable_stream(struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);

	if (dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal)) {
		pipe_ctx->stream_res.stream_enc->funcs->stop_hdmi_info_packets(
			pipe_ctx->stream_res.stream_enc);
		pipe_ctx->stream_res.stream_enc->funcs->hdmi_reset_stream_attribute(
			pipe_ctx->stream_res.stream_enc);
	}

	if (is_dp_128b_132b_signal(pipe_ctx)) {
		pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->stop_dp_info_packets(
					pipe_ctx->stream_res.hpo_dp_stream_enc);
	} else if (dc_is_dp_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->stop_dp_info_packets(
			pipe_ctx->stream_res.stream_enc);

	dc->hwss.disable_audio_stream(pipe_ctx);

	link_hwss->reset_stream_encoder(pipe_ctx);

	if (is_dp_128b_132b_signal(pipe_ctx)) {
		/* TODO: This looks like a bug to me as we are disabling HPO IO when
		 * we are just disabling a single HPO stream. Shouldn't we disable HPO
		 * HW control only when HPOs for all streams are disabled?
		 */
		if (pipe_ctx->stream->ctx->dc->hwseq->funcs.setup_hpo_hw_control)
			pipe_ctx->stream->ctx->dc->hwseq->funcs.setup_hpo_hw_control(
					pipe_ctx->stream->ctx->dc->hwseq, false);
	}
}

void dce110_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings)
{
	struct encoder_unblank_param params = { { 0 } };
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dce_hwseq *hws = link->dc->hwseq;

	/* only 3 items below are used by unblank */
	params.timing = pipe_ctx->stream->timing;
	params.link_settings.link_rate = link_settings->link_rate;

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->dp_unblank(link, pipe_ctx->stream_res.stream_enc, &params);

	if (link->local_sink && link->local_sink->sink_signal == SIGNAL_TYPE_EDP) {
		hws->funcs.edp_backlight_control(link, true);
	}
}

void dce110_blank_stream(struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dce_hwseq *hws = link->dc->hwseq;

	if (link->local_sink && link->local_sink->sink_signal == SIGNAL_TYPE_EDP) {
		hws->funcs.edp_backlight_control(link, false);
		link->dc->hwss.set_abm_immediate_disable(pipe_ctx);
	}

	if (is_dp_128b_132b_signal(pipe_ctx)) {
		/* TODO - DP2.0 HW: Set ODM mode in dp hpo encoder here */
		pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_blank(
				pipe_ctx->stream_res.hpo_dp_stream_enc);
	} else if (dc_is_dp_signal(pipe_ctx->stream->signal)) {
		pipe_ctx->stream_res.stream_enc->funcs->dp_blank(link, pipe_ctx->stream_res.stream_enc);

		if (!dc_is_embedded_signal(pipe_ctx->stream->signal)) {
			/*
			 * After output is idle pattern some sinks need time to recognize the stream
			 * has changed or they enter protection state and hang.
			 */
			msleep(60);
		} else if (pipe_ctx->stream->signal == SIGNAL_TYPE_EDP) {
			if (!link->dc->config.edp_no_power_sequencing) {
				/*
				 * Sometimes, DP receiver chip power-controlled externally by an
				 * Embedded Controller could be treated and used as eDP,
				 * if it drives mobile display. In this case,
				 * we shouldn't be doing power-sequencing, hence we can skip
				 * waiting for T9-ready.
				 */
				edp_receiver_ready_T9(link);
			}
		}
	}

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
		(stream->timing.pix_clk_100hz*100)/
		(stream->timing.h_total*stream->timing.v_total);

	audio_output->crtc_info.color_depth =
		stream->timing.display_color_depth;

	audio_output->crtc_info.requested_pixel_clock_100Hz =
			pipe_ctx->stream_res.pix_clk_params.requested_pix_clk_100hz;

	audio_output->crtc_info.calculated_pixel_clock_100Hz =
			pipe_ctx->stream_res.pix_clk_params.requested_pix_clk_100hz;

/*for HDMI, audio ACR is with deep color ratio factor*/
	if (dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal) &&
		audio_output->crtc_info.requested_pixel_clock_100Hz ==
				(stream->timing.pix_clk_100hz)) {
		if (pipe_ctx->stream_res.pix_clk_params.pixel_encoding == PIXEL_ENCODING_YCBCR420) {
			audio_output->crtc_info.requested_pixel_clock_100Hz =
					audio_output->crtc_info.requested_pixel_clock_100Hz/2;
			audio_output->crtc_info.calculated_pixel_clock_100Hz =
					pipe_ctx->stream_res.pix_clk_params.requested_pix_clk_100hz/2;

		}
	}

	if (state->clk_mgr &&
		(pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT ||
			pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)) {
		audio_output->pll_info.dp_dto_source_clock_in_khz =
				state->clk_mgr->funcs->get_dp_ref_clk_frequency(
						state->clk_mgr);
	}

	audio_output->pll_info.feed_back_divider =
			pipe_ctx->pll_settings.feedback_divider;

	audio_output->pll_info.dto_source =
		translate_to_dto_source(
			pipe_ctx->stream_res.tg->inst + 1);

	/* TODO hard code to enable for now. Need get from stream */
	audio_output->pll_info.ss_enabled = true;

	audio_output->pll_info.ss_percentage =
			pipe_ctx->pll_settings.ss_percentage;
}

static void program_scaler(const struct dc *dc,
		const struct pipe_ctx *pipe_ctx)
{
	struct tg_color color = {0};

	/* TOFPGA */
	if (pipe_ctx->plane_res.xfm->funcs->transform_set_pixel_storage_depth == NULL)
		return;

	if (dc->debug.visual_confirm == VISUAL_CONFIRM_SURFACE)
		get_surface_visual_confirm_color(pipe_ctx, &color);
	else
		color_space_to_black_color(dc,
				pipe_ctx->stream->output_color_space,
				&color);

	pipe_ctx->plane_res.xfm->funcs->transform_set_pixel_storage_depth(
		pipe_ctx->plane_res.xfm,
		pipe_ctx->plane_res.scl_data.lb_params.depth,
		&pipe_ctx->stream->bit_depth_params);

	if (pipe_ctx->stream_res.tg->funcs->set_overscan_blank_color) {
		/*
		 * The way 420 is packed, 2 channels carry Y component, 1 channel
		 * alternate between Cb and Cr, so both channels need the pixel
		 * value for Y
		 */
		if (pipe_ctx->stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
			color.color_r_cr = color.color_g_y;

		pipe_ctx->stream_res.tg->funcs->set_overscan_blank_color(
				pipe_ctx->stream_res.tg,
				&color);
	}

	pipe_ctx->plane_res.xfm->funcs->transform_set_scaler(pipe_ctx->plane_res.xfm,
		&pipe_ctx->plane_res.scl_data);
}

static enum dc_status dce110_enable_stream_timing(
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
				dp_get_link_encoding_format(&pipe_ctx->link_config.dp_link_settings),
				&pipe_ctx->pll_settings)) {
			BREAK_TO_DEBUGGER();
			return DC_ERROR_UNEXPECTED;
		}

		if (dc_is_hdmi_tmds_signal(stream->signal)) {
			stream->link->phy_state.symclk_ref_cnts.otg = 1;
			if (stream->link->phy_state.symclk_state == SYMCLK_OFF_TX_OFF)
				stream->link->phy_state.symclk_state = SYMCLK_ON_TX_OFF;
			else
				stream->link->phy_state.symclk_state = SYMCLK_ON_TX_ON;
		}

		pipe_ctx->stream_res.tg->funcs->program_timing(
				pipe_ctx->stream_res.tg,
				&stream->timing,
				0,
				0,
				0,
				0,
				pipe_ctx->stream->signal,
				true);
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
	struct dc_link *link = stream->link;
	struct drr_params params = {0};
	unsigned int event_triggers = 0;
	struct pipe_ctx *odm_pipe = pipe_ctx->next_odm_pipe;
	struct dce_hwseq *hws = dc->hwseq;
	const struct link_hwss *link_hwss = get_link_hwss(
			link, &pipe_ctx->link_res);


	if (hws->funcs.disable_stream_gating) {
		hws->funcs.disable_stream_gating(dc, pipe_ctx);
	}

	if (pipe_ctx->stream_res.audio != NULL) {
		struct audio_output audio_output;

		build_audio_output(context, pipe_ctx, &audio_output);

		link_hwss->setup_audio_output(pipe_ctx, &audio_output,
				pipe_ctx->stream_res.audio->inst);

		pipe_ctx->stream_res.audio->funcs->az_configure(
				pipe_ctx->stream_res.audio,
				pipe_ctx->stream->signal,
				&audio_output.crtc_info,
				&pipe_ctx->stream->audio_info);
	}

	/* make sure no pipes syncd to the pipe being enabled */
	if (!pipe_ctx->stream->apply_seamless_boot_optimization && dc->config.use_pipe_ctx_sync_logic)
		check_syncd_pipes_for_disabled_master_pipe(dc, context, pipe_ctx->pipe_idx);

	pipe_ctx->stream_res.opp->funcs->opp_program_fmt(
		pipe_ctx->stream_res.opp,
		&stream->bit_depth_params,
		&stream->clamping);

	pipe_ctx->stream_res.opp->funcs->opp_set_dyn_expansion(
			pipe_ctx->stream_res.opp,
			COLOR_SPACE_YCBCR601,
			stream->timing.display_color_depth,
			stream->signal);

	while (odm_pipe) {
		odm_pipe->stream_res.opp->funcs->opp_set_dyn_expansion(
				odm_pipe->stream_res.opp,
				COLOR_SPACE_YCBCR601,
				stream->timing.display_color_depth,
				stream->signal);

		odm_pipe->stream_res.opp->funcs->opp_program_fmt(
				odm_pipe->stream_res.opp,
				&stream->bit_depth_params,
				&stream->clamping);
		odm_pipe = odm_pipe->next_odm_pipe;
	}

	/* DCN3.1 FPGA Workaround
	 * Need to enable HPO DP Stream Encoder before setting OTG master enable.
	 * To do so, move calling function enable_stream_timing to only be done AFTER calling
	 * function core_link_enable_stream
	 */
	if (!(hws->wa.dp_hpo_and_otg_sequence && is_dp_128b_132b_signal(pipe_ctx)))
		/*  */
		/* Do not touch stream timing on seamless boot optimization. */
		if (!pipe_ctx->stream->apply_seamless_boot_optimization)
			hws->funcs.enable_stream_timing(pipe_ctx, context, dc);

	if (hws->funcs.setup_vupdate_interrupt)
		hws->funcs.setup_vupdate_interrupt(dc, pipe_ctx);

	params.vertical_total_min = stream->adjust.v_total_min;
	params.vertical_total_max = stream->adjust.v_total_max;
	if (pipe_ctx->stream_res.tg->funcs->set_drr)
		pipe_ctx->stream_res.tg->funcs->set_drr(
			pipe_ctx->stream_res.tg, &params);

	// DRR should set trigger event to monitor surface update event
	if (stream->adjust.v_total_min != 0 && stream->adjust.v_total_max != 0)
		event_triggers = 0x80;
	/* Event triggers and num frames initialized for DRR, but can be
	 * later updated for PSR use. Note DRR trigger events are generated
	 * regardless of whether num frames met.
	 */
	if (pipe_ctx->stream_res.tg->funcs->set_static_screen_control)
		pipe_ctx->stream_res.tg->funcs->set_static_screen_control(
				pipe_ctx->stream_res.tg, event_triggers, 2);

	if (!dc_is_virtual_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->dig_connect_to_otg(
			pipe_ctx->stream_res.stream_enc,
			pipe_ctx->stream_res.tg->inst);

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_CONNECT_DIG_FE_OTG);

	if (!stream->dpms_off)
		core_link_enable_stream(context, pipe_ctx);

	/* DCN3.1 FPGA Workaround
	 * Need to enable HPO DP Stream Encoder before setting OTG master enable.
	 * To do so, move calling function enable_stream_timing to only be done AFTER calling
	 * function core_link_enable_stream
	 */
	if (hws->wa.dp_hpo_and_otg_sequence && is_dp_128b_132b_signal(pipe_ctx)) {
		if (!pipe_ctx->stream->apply_seamless_boot_optimization)
			hws->funcs.enable_stream_timing(pipe_ctx, context, dc);
	}

	pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->bottom_pipe != NULL;

	/* Phantom and main stream share the same link (because the stream
	 * is constructed with the same sink). Make sure not to override
	 * and link programming on the main.
	 */
	if (pipe_ctx->stream->mall_stream_config.type != SUBVP_PHANTOM) {
		pipe_ctx->stream->link->psr_settings.psr_feature_enabled = false;
	}
	return DC_OK;
}

/******************************************************************************/

static void power_down_encoders(struct dc *dc)
{
	int i;

	for (i = 0; i < dc->link_count; i++) {
		enum signal_type signal = dc->links[i]->connector_signal;

		dc_link_blank_dp_stream(dc->links[i], false);

		if (signal != SIGNAL_TYPE_EDP)
			signal = SIGNAL_TYPE_NONE;

		if (dc->links[i]->ep_type == DISPLAY_ENDPOINT_PHY)
			dc->links[i]->link_enc->funcs->disable_output(
					dc->links[i]->link_enc, signal);

		dc->links[i]->link_status.link_active = false;
		memset(&dc->links[i]->cur_link_settings, 0,
				sizeof(dc->links[i]->cur_link_settings));
	}
}

static void power_down_controllers(struct dc *dc)
{
	int i;

	for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
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

	if (dc->fbc_compressor)
		dc->fbc_compressor->funcs->disable_fbc(dc->fbc_compressor);
}

static void disable_vga_and_power_gate_all_controllers(
		struct dc *dc)
{
	int i;
	struct timing_generator *tg;
	struct dc_context *ctx = dc->ctx;

	for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
		tg = dc->res_pool->timing_generators[i];

		if (tg->funcs->disable_vga)
			tg->funcs->disable_vga(tg);
	}
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		/* Enable CLOCK gating for each pipe BEFORE controller
		 * powergating. */
		enable_display_pipe_clock_gating(ctx,
				true);

		dc->current_state->res_ctx.pipe_ctx[i].pipe_idx = i;
		dc->hwss.disable_plane(dc,
			&dc->current_state->res_ctx.pipe_ctx[i]);
	}
}


static void get_edp_streams(struct dc_state *context,
		struct dc_stream_state **edp_streams,
		int *edp_stream_num)
{
	int i;

	*edp_stream_num = 0;
	for (i = 0; i < context->stream_count; i++) {
		if (context->streams[i]->signal == SIGNAL_TYPE_EDP) {
			edp_streams[*edp_stream_num] = context->streams[i];
			if (++(*edp_stream_num) == MAX_NUM_EDP)
				return;
		}
	}
}

static void get_edp_links_with_sink(
		struct dc *dc,
		struct dc_link **edp_links_with_sink,
		int *edp_with_sink_num)
{
	int i;

	/* check if there is an eDP panel not in use */
	*edp_with_sink_num = 0;
	for (i = 0; i < dc->link_count; i++) {
		if (dc->links[i]->local_sink &&
			dc->links[i]->local_sink->sink_signal == SIGNAL_TYPE_EDP) {
			edp_links_with_sink[*edp_with_sink_num] = dc->links[i];
			if (++(*edp_with_sink_num) == MAX_NUM_EDP)
				return;
		}
	}
}

/*
 * When ASIC goes from VBIOS/VGA mode to driver/accelerated mode we need:
 *  1. Power down all DC HW blocks
 *  2. Disable VGA engine on all controllers
 *  3. Enable power gating for controller
 *  4. Set acc_mode_change bit (VBIOS will clear this bit when going to FSDOS)
 */
void dce110_enable_accelerated_mode(struct dc *dc, struct dc_state *context)
{
	struct dc_link *edp_links_with_sink[MAX_NUM_EDP];
	struct dc_link *edp_links[MAX_NUM_EDP];
	struct dc_stream_state *edp_streams[MAX_NUM_EDP];
	struct dc_link *edp_link_with_sink = NULL;
	struct dc_link *edp_link = NULL;
	struct dce_hwseq *hws = dc->hwseq;
	int edp_with_sink_num;
	int edp_num;
	int edp_stream_num;
	int i;
	bool can_apply_edp_fast_boot = false;
	bool can_apply_seamless_boot = false;
	bool keep_edp_vdd_on = false;
	DC_LOGGER_INIT();


	get_edp_links_with_sink(dc, edp_links_with_sink, &edp_with_sink_num);
	get_edp_links(dc, edp_links, &edp_num);

	if (hws->funcs.init_pipes)
		hws->funcs.init_pipes(dc, context);

	get_edp_streams(context, edp_streams, &edp_stream_num);

	// Check fastboot support, disable on DCE8 because of blank screens
	if (edp_num && edp_stream_num && dc->ctx->dce_version != DCE_VERSION_8_0 &&
		    dc->ctx->dce_version != DCE_VERSION_8_1 &&
		    dc->ctx->dce_version != DCE_VERSION_8_3) {
		for (i = 0; i < edp_num; i++) {
			edp_link = edp_links[i];
			if (edp_link != edp_streams[0]->link)
				continue;
			// enable fastboot if backend is enabled on eDP
			if (edp_link->link_enc->funcs->is_dig_enabled &&
			    edp_link->link_enc->funcs->is_dig_enabled(edp_link->link_enc) &&
			    edp_link->link_status.link_active) {
				struct dc_stream_state *edp_stream = edp_streams[0];

				can_apply_edp_fast_boot = dc_validate_boot_timing(dc,
					edp_stream->sink, &edp_stream->timing);
				edp_stream->apply_edp_fast_boot_optimization = can_apply_edp_fast_boot;
				if (can_apply_edp_fast_boot)
					DC_LOG_EVENT_LINK_TRAINING("eDP fast boot disabled to optimize link rate\n");

				break;
			}
		}
		// We are trying to enable eDP, don't power down VDD
		if (can_apply_edp_fast_boot)
			keep_edp_vdd_on = true;
	}

	// Check seamless boot support
	for (i = 0; i < context->stream_count; i++) {
		if (context->streams[i]->apply_seamless_boot_optimization) {
			can_apply_seamless_boot = true;
			break;
		}
	}

	/* eDP should not have stream in resume from S4 and so even with VBios post
	 * it should get turned off
	 */
	if (edp_with_sink_num)
		edp_link_with_sink = edp_links_with_sink[0];

	if (!can_apply_edp_fast_boot && !can_apply_seamless_boot) {
		if (edp_link_with_sink && !keep_edp_vdd_on) {
			/*turn off backlight before DP_blank and encoder powered down*/
			hws->funcs.edp_backlight_control(edp_link_with_sink, false);
		}
		/*resume from S3, no vbios posting, no need to power down again*/
		power_down_all_hw_blocks(dc);
		disable_vga_and_power_gate_all_controllers(dc);
		if (edp_link_with_sink && !keep_edp_vdd_on)
			dc->hwss.edp_power_control(edp_link_with_sink, false);
	}
	bios_set_scratch_acc_mode_change(dc->ctx->dc_bios, 1);
}

static uint32_t compute_pstate_blackout_duration(
	struct bw_fixed blackout_duration,
	const struct dc_stream_state *stream)
{
	uint32_t total_dest_line_time_ns;
	uint32_t pstate_blackout_duration_ns;

	pstate_blackout_duration_ns = 1000 * blackout_duration.value >> 24;

	total_dest_line_time_ns = 1000000UL *
		(stream->timing.h_total * 10) /
		stream->timing.pix_clk_100hz +
		pstate_blackout_duration_ns;

	return total_dest_line_time_ns;
}

static void dce110_set_displaymarks(
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
			context->bw_ctx.bw.dce.nbp_state_change_wm_ns[num_pipes],
			context->bw_ctx.bw.dce.stutter_exit_wm_ns[num_pipes],
			context->bw_ctx.bw.dce.stutter_entry_wm_ns[num_pipes],
			context->bw_ctx.bw.dce.urgent_wm_ns[num_pipes],
			total_dest_line_time_ns);
		if (i == underlay_idx) {
			num_pipes++;
			pipe_ctx->plane_res.mi->funcs->mem_input_program_chroma_display_marks(
				pipe_ctx->plane_res.mi,
				context->bw_ctx.bw.dce.nbp_state_change_wm_ns[num_pipes],
				context->bw_ctx.bw.dce.stutter_exit_wm_ns[num_pipes],
				context->bw_ctx.bw.dce.urgent_wm_ns[num_pipes],
				total_dest_line_time_ns);
		}
		num_pipes++;
	}
}

void dce110_set_safe_displaymarks(
		struct resource_context *res_ctx,
		const struct resource_pool *pool)
{
	int i;
	int underlay_idx = pool->underlay_pipe_index;
	struct dce_watermarks max_marks = {
		MAX_WATERMARK, MAX_WATERMARK, MAX_WATERMARK, MAX_WATERMARK };
	struct dce_watermarks nbp_marks = {
		SAFE_NBP_MARK, SAFE_NBP_MARK, SAFE_NBP_MARK, SAFE_NBP_MARK };
	struct dce_watermarks min_marks = { 0, 0, 0, 0};

	for (i = 0; i < MAX_PIPES; i++) {
		if (res_ctx->pipe_ctx[i].stream == NULL || res_ctx->pipe_ctx[i].plane_res.mi == NULL)
			continue;

		res_ctx->pipe_ctx[i].plane_res.mi->funcs->mem_input_program_display_marks(
				res_ctx->pipe_ctx[i].plane_res.mi,
				nbp_marks,
				max_marks,
				min_marks,
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
		int num_pipes, struct dc_crtc_timing_adjust adjust)
{
	int i = 0;
	struct drr_params params = {0};
	// DRR should set trigger event to monitor surface update event
	unsigned int event_triggers = 0x80;
	// Note DRR trigger events are generated regardless of whether num frames met.
	unsigned int num_frames = 2;

	params.vertical_total_max = adjust.v_total_max;
	params.vertical_total_min = adjust.v_total_min;

	/* TODO: If multiple pipes are to be supported, you need
	 * some GSL stuff. Static screen triggers may be programmed differently
	 * as well.
	 */
	for (i = 0; i < num_pipes; i++) {
		pipe_ctx[i]->stream_res.tg->funcs->set_drr(
			pipe_ctx[i]->stream_res.tg, &params);

		if (adjust.v_total_max != 0 && adjust.v_total_min != 0)
			pipe_ctx[i]->stream_res.tg->funcs->set_static_screen_control(
					pipe_ctx[i]->stream_res.tg,
					event_triggers, num_frames);
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
		int num_pipes, const struct dc_static_screen_params *params)
{
	unsigned int i;
	unsigned int triggers = 0;

	if (params->triggers.overlay_update)
		triggers |= 0x100;
	if (params->triggers.surface_update)
		triggers |= 0x80;
	if (params->triggers.cursor_update)
		triggers |= 0x2;
	if (params->triggers.force_trigger)
		triggers |= 0x1;

	if (num_pipes) {
		struct dc *dc = pipe_ctx[0]->stream->ctx->dc;

		if (dc->fbc_compressor)
			triggers |= 0x84;
	}

	for (i = 0; i < num_pipes; i++)
		pipe_ctx[i]->stream_res.tg->funcs->
			set_static_screen_control(pipe_ctx[i]->stream_res.tg,
					triggers, params->num_frames);
}

/*
 *  Check if FBC can be enabled
 */
static bool should_enable_fbc(struct dc *dc,
		struct dc_state *context,
		uint32_t *pipe_idx)
{
	uint32_t i;
	struct pipe_ctx *pipe_ctx = NULL;
	struct resource_context *res_ctx = &context->res_ctx;
	unsigned int underlay_idx = dc->res_pool->underlay_pipe_index;


	ASSERT(dc->fbc_compressor);

	/* FBC memory should be allocated */
	if (!dc->ctx->fbc_gpu_addr)
		return false;

	/* Only supports single display */
	if (context->stream_count != 1)
		return false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (res_ctx->pipe_ctx[i].stream) {

			pipe_ctx = &res_ctx->pipe_ctx[i];

			if (!pipe_ctx)
				continue;

			/* fbc not applicable on underlay pipe */
			if (pipe_ctx->pipe_idx != underlay_idx) {
				*pipe_idx = i;
				break;
			}
		}
	}

	if (i == dc->res_pool->pipe_count)
		return false;

	if (!pipe_ctx->stream->link)
		return false;

	/* Only supports eDP */
	if (pipe_ctx->stream->link->connector_signal != SIGNAL_TYPE_EDP)
		return false;

	/* PSR should not be enabled */
	if (pipe_ctx->stream->link->psr_settings.psr_feature_enabled)
		return false;

	/* Nothing to compress */
	if (!pipe_ctx->plane_state)
		return false;

	/* Only for non-linear tiling */
	if (pipe_ctx->plane_state->tiling_info.gfx8.array_mode == DC_ARRAY_LINEAR_GENERAL)
		return false;

	return true;
}

/*
 *  Enable FBC
 */
static void enable_fbc(
		struct dc *dc,
		struct dc_state *context)
{
	uint32_t pipe_idx = 0;

	if (should_enable_fbc(dc, context, &pipe_idx)) {
		/* Program GRPH COMPRESSED ADDRESS and PITCH */
		struct compr_addr_and_pitch_params params = {0, 0, 0};
		struct compressor *compr = dc->fbc_compressor;
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[pipe_idx];

		params.source_view_width = pipe_ctx->stream->timing.h_addressable;
		params.source_view_height = pipe_ctx->stream->timing.v_addressable;
		params.inst = pipe_ctx->stream_res.tg->inst;
		compr->compr_surface_address.quad_part = dc->ctx->fbc_gpu_addr;

		compr->funcs->surface_address_and_pitch(compr, &params);
		compr->funcs->set_fbc_invalidation_triggers(compr, 1);

		compr->funcs->enable_fbc(compr, &params);
	}
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
			if (!pipe_ctx->stream || !pipe_ctx->stream->dpms_off) {
				core_link_disable_stream(pipe_ctx_old);

				/* free acquired resources*/
				if (pipe_ctx_old->stream_res.audio) {
					/*disable az_endpoint*/
					pipe_ctx_old->stream_res.audio->funcs->
							az_disable(pipe_ctx_old->stream_res.audio);

					/*free audio*/
					if (dc->caps.dynamic_audio == true) {
						/*we have to dynamic arbitrate the audio endpoints*/
						/*we free the resource, need reset is_audio_acquired*/
						update_audio_usage(&dc->current_state->res_ctx, dc->res_pool,
								pipe_ctx_old->stream_res.audio, false);
						pipe_ctx_old->stream_res.audio = NULL;
					}
				}
			}

			pipe_ctx_old->stream_res.tg->funcs->set_blank(pipe_ctx_old->stream_res.tg, true);
			if (!hwss_wait_for_blank_complete(pipe_ctx_old->stream_res.tg)) {
				dm_error("DC: failed to blank crtc!\n");
				BREAK_TO_DEBUGGER();
			}
			pipe_ctx_old->stream_res.tg->funcs->disable_crtc(pipe_ctx_old->stream_res.tg);
			pipe_ctx_old->stream->link->phy_state.symclk_ref_cnts.otg = 0;
			pipe_ctx_old->plane_res.mi->funcs->free_mem_input(
					pipe_ctx_old->plane_res.mi, dc->current_state->stream_count);

			if (old_clk && 0 == resource_get_clock_source_reference(&context->res_ctx,
										dc->res_pool,
										old_clk))
				old_clk->funcs->cs_power_down(old_clk);

			dc->hwss.disable_plane(dc, pipe_ctx_old);

			pipe_ctx_old->stream = NULL;
		}
	}
}

static void dce110_setup_audio_dto(
		struct dc *dc,
		struct dc_state *context)
{
	int i;

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

			if (dc->res_pool->dccg && dc->res_pool->dccg->funcs->set_audio_dtbclk_dto) {
				struct dtbclk_dto_params dto_params = {0};

				dc->res_pool->dccg->funcs->set_audio_dtbclk_dto(
					dc->res_pool->dccg, &dto_params);

				pipe_ctx->stream_res.audio->funcs->wall_dto_setup(
						pipe_ctx->stream_res.audio,
						pipe_ctx->stream->signal,
						&audio_output.crtc_info,
						&audio_output.pll_info);
			} else
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
}

enum dc_status dce110_apply_ctx_to_hw(
		struct dc *dc,
		struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	enum dc_status status;
	int i;

	/* reset syncd pipes from disabled pipes */
	if (dc->config.use_pipe_ctx_sync_logic)
		reset_syncd_pipes_from_disabled_pipes(dc, context);

	/* Reset old context */
	/* look up the targets that have been removed since last commit */
	hws->funcs.reset_hw_ctx_wrap(dc, context);

	/* Skip applying if no targets */
	if (context->stream_count <= 0)
		return DC_OK;

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

		hws->funcs.enable_display_power_gating(
				dc, i, dc->ctx->dc_bios,
				PIPE_GATING_CONTROL_DISABLE);
	}

	if (dc->fbc_compressor)
		dc->fbc_compressor->funcs->disable_fbc(dc->fbc_compressor);

	dce110_setup_audio_dto(dc, context);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx_old =
					&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		if (pipe_ctx->stream == pipe_ctx_old->stream &&
			pipe_ctx->stream->link->link_state_valid) {
			continue;
		}

		if (pipe_ctx_old->stream && !pipe_need_reprogram(pipe_ctx_old, pipe_ctx))
			continue;

		if (pipe_ctx->top_pipe || pipe_ctx->prev_odm_pipe)
			continue;

		status = apply_single_controller_ctx_to_hw(
				pipe_ctx,
				context,
				dc);

		if (DC_OK != status)
			return status;
	}

	if (dc->fbc_compressor)
		enable_fbc(dc, dc->current_state);

	dcb->funcs->set_scratch_critical_state(dcb, false);

	return DC_OK;
}

/*******************************************************************************
 * Front End programming
 ******************************************************************************/
static void set_default_colors(struct pipe_ctx *pipe_ctx)
{
	struct default_adjustment default_adjust = { 0 };

	default_adjust.force_hw_default = false;
	default_adjust.in_color_space = pipe_ctx->plane_state->color_space;
	default_adjust.out_color_space = pipe_ctx->stream->output_color_space;
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

	dce_set_blender_mode(dc->hwseq, pipe_ctx->stream_res.tg->inst, blender_mode);
	pipe_ctx->stream_res.tg->funcs->set_blank(pipe_ctx->stream_res.tg, blank_target);

}

static void program_gamut_remap(struct pipe_ctx *pipe_ctx)
{
	int i = 0;
	struct xfm_grph_csc_adjustment adjust;
	memset(&adjust, 0, sizeof(adjust));
	adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;


	if (pipe_ctx->stream->gamut_remap_matrix.enable_remap == true) {
		adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;

		for (i = 0; i < CSC_TEMPERATURE_MATRIX_SIZE; i++)
			adjust.temperature_matrix[i] =
				pipe_ctx->stream->gamut_remap_matrix.matrix[i];
	}

	pipe_ctx->plane_res.xfm->funcs->transform_set_gamut_remap(pipe_ctx->plane_res.xfm, &adjust);
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

static void dce110_update_pending_status(struct pipe_ctx *pipe_ctx)
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
				grouped_pipes[i]->stream_res.tg,
				gsl_params.gsl_group);

	for (i = 1 /* skip the master */; i < group_size; i++) {
		DC_SYNC_INFO("GSL: waiting for reset to occur.\n");
		wait_for_reset_trigger_to_occur(dc_ctx, grouped_pipes[i]->stream_res.tg);
		grouped_pipes[i]->stream_res.tg->funcs->disable_reset_trigger(
				grouped_pipes[i]->stream_res.tg);
	}

	/* GSL Vblank synchronization is a one time sync mechanism, assumption
	 * is that the sync'ed displays will not drift out of sync over time*/
	DC_SYNC_INFO("GSL: Restoring register states.\n");
	for (i = 0; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->tear_down_global_swap_lock(grouped_pipes[i]->stream_res.tg);

	DC_SYNC_INFO("GSL: Set-up complete.\n");
}

static void dce110_enable_per_frame_crtc_position_reset(
		struct dc *dc,
		int group_size,
		struct pipe_ctx *grouped_pipes[])
{
	struct dc_context *dc_ctx = dc->ctx;
	struct dcp_gsl_params gsl_params = { 0 };
	int i;

	gsl_params.gsl_group = 0;
	gsl_params.gsl_master = 0;

	for (i = 0; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->setup_global_swap_lock(
					grouped_pipes[i]->stream_res.tg, &gsl_params);

	DC_SYNC_INFO("GSL: enabling trigger-reset\n");

	for (i = 1; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->enable_crtc_reset(
				grouped_pipes[i]->stream_res.tg,
				gsl_params.gsl_master,
				&grouped_pipes[i]->stream->triggered_crtc_reset);

	DC_SYNC_INFO("GSL: waiting for reset to occur.\n");
	for (i = 1; i < group_size; i++)
		wait_for_reset_trigger_to_occur(dc_ctx, grouped_pipes[i]->stream_res.tg);

	for (i = 0; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->tear_down_global_swap_lock(grouped_pipes[i]->stream_res.tg);

}

static void init_pipes(struct dc *dc, struct dc_state *context)
{
	// Do nothing
}

static void init_hw(struct dc *dc)
{
	int i;
	struct dc_bios *bp;
	struct transform *xfm;
	struct abm *abm;
	struct dmcu *dmcu;
	struct dce_hwseq *hws = dc->hwseq;
	uint32_t backlight = MAX_BACKLIGHT_LEVEL;

	bp = dc->ctx->dc_bios;
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		xfm = dc->res_pool->transforms[i];
		xfm->funcs->transform_reset(xfm);

		hws->funcs.enable_display_power_gating(
				dc, i, bp,
				PIPE_GATING_CONTROL_INIT);
		hws->funcs.enable_display_power_gating(
				dc, i, bp,
				PIPE_GATING_CONTROL_DISABLE);
		hws->funcs.enable_display_pipe_clock_gating(
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

	for (i = 0; i < dc->link_count; i++) {
		struct dc_link *link = dc->links[i];

		if (link->panel_cntl)
			backlight = link->panel_cntl->funcs->hw_init(link->panel_cntl);
	}

	abm = dc->res_pool->abm;
	if (abm != NULL)
		abm->funcs->abm_init(abm, backlight);

	dmcu = dc->res_pool->dmcu;
	if (dmcu != NULL && abm != NULL)
		abm->dmcu_is_running = dmcu->funcs->is_dmcu_initialized(dmcu);

	if (dc->fbc_compressor)
		dc->fbc_compressor->funcs->power_up_fbc(dc->fbc_compressor);

}


void dce110_prepare_bandwidth(
		struct dc *dc,
		struct dc_state *context)
{
	struct clk_mgr *dccg = dc->clk_mgr;

	dce110_set_safe_displaymarks(&context->res_ctx, dc->res_pool);

	dccg->funcs->update_clocks(
			dccg,
			context,
			false);
}

void dce110_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context)
{
	struct clk_mgr *dccg = dc->clk_mgr;

	dce110_set_displaymarks(dc, context);

	dccg->funcs->update_clocks(
			dccg,
			context,
			true);
}

static void dce110_program_front_end_for_pipe(
		struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct mem_input *mi = pipe_ctx->plane_res.mi;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct xfm_grph_csc_adjustment adjust;
	struct out_csc_color_matrix tbl_entry;
	unsigned int i;
	struct dce_hwseq *hws = dc->hwseq;

	DC_LOGGER_INIT();
	memset(&tbl_entry, 0, sizeof(tbl_entry));

	memset(&adjust, 0, sizeof(adjust));
	adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;

	dce_enable_fe_clock(dc->hwseq, mi->inst, true);

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

		for (i = 0; i < CSC_TEMPERATURE_MATRIX_SIZE; i++)
			adjust.temperature_matrix[i] =
				pipe_ctx->stream->gamut_remap_matrix.matrix[i];
	}

	pipe_ctx->plane_res.xfm->funcs->transform_set_gamut_remap(pipe_ctx->plane_res.xfm, &adjust);

	pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->bottom_pipe != NULL;

	program_scaler(dc, pipe_ctx);

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
	if (pipe_ctx->plane_state->update_flags.bits.full_update ||
			pipe_ctx->plane_state->update_flags.bits.in_transfer_func_change ||
			pipe_ctx->plane_state->update_flags.bits.gamma_change)
		hws->funcs.set_input_transfer_func(dc, pipe_ctx, pipe_ctx->plane_state);

	if (pipe_ctx->plane_state->update_flags.bits.full_update)
		hws->funcs.set_output_transfer_func(dc, pipe_ctx, pipe_ctx->stream);

	DC_LOG_SURFACE(
			"Pipe:%d %p: addr hi:0x%x, "
			"addr low:0x%x, "
			"src: %d, %d, %d,"
			" %d; dst: %d, %d, %d, %d;"
			"clip: %d, %d, %d, %d\n",
			pipe_ctx->pipe_idx,
			(void *) pipe_ctx->plane_state,
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

	DC_LOG_SURFACE(
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
	int i;

	if (num_planes == 0)
		return;

	if (dc->fbc_compressor)
		dc->fbc_compressor->funcs->disable_fbc(dc->fbc_compressor);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream != stream)
			continue;

		/* Need to allocate mem before program front end for Fiji */
		pipe_ctx->plane_res.mi->funcs->allocate_mem_input(
				pipe_ctx->plane_res.mi,
				pipe_ctx->stream->timing.h_total,
				pipe_ctx->stream->timing.v_total,
				pipe_ctx->stream->timing.pix_clk_100hz / 10,
				context->stream_count);

		dce110_program_front_end_for_pipe(dc, pipe_ctx);

		dc->hwss.update_plane_addr(dc, pipe_ctx);

		program_surface_visibility(dc, pipe_ctx);

	}

	if (dc->fbc_compressor)
		enable_fbc(dc, context);
}

static void dce110_post_unlock_program_front_end(
		struct dc *dc,
		struct dc_state *context)
{
}

static void dce110_power_down_fe(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;
	int fe_idx = pipe_ctx->plane_res.mi ?
		pipe_ctx->plane_res.mi->inst : pipe_ctx->pipe_idx;

	/* Do not power down fe when stream is active on dce*/
	if (dc->current_state->res_ctx.pipe_ctx[fe_idx].stream)
		return;

	hws->funcs.enable_display_power_gating(
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

static void program_output_csc(struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix,
		int opp_id)
{
	int i;
	struct out_csc_color_matrix tbl_entry;

	if (pipe_ctx->stream->csc_color_matrix.enable_adjustment == true) {
		enum dc_color_space color_space = pipe_ctx->stream->output_color_space;

		for (i = 0; i < 12; i++)
			tbl_entry.regval[i] = pipe_ctx->stream->csc_color_matrix.matrix[i];

		tbl_entry.color_space = color_space;

		pipe_ctx->plane_res.xfm->funcs->opp_set_csc_adjustment(
				pipe_ctx->plane_res.xfm, &tbl_entry);
	}
}

static void dce110_set_cursor_position(struct pipe_ctx *pipe_ctx)
{
	struct dc_cursor_position pos_cpy = pipe_ctx->stream->cursor_position;
	struct input_pixel_processor *ipp = pipe_ctx->plane_res.ipp;
	struct mem_input *mi = pipe_ctx->plane_res.mi;
	struct dc_cursor_mi_param param = {
		.pixel_clk_khz = pipe_ctx->stream->timing.pix_clk_100hz / 10,
		.ref_clk_khz = pipe_ctx->stream->ctx->dc->res_pool->ref_clocks.xtalin_clock_inKhz,
		.viewport = pipe_ctx->plane_res.scl_data.viewport,
		.h_scale_ratio = pipe_ctx->plane_res.scl_data.ratios.horz,
		.v_scale_ratio = pipe_ctx->plane_res.scl_data.ratios.vert,
		.rotation = pipe_ctx->plane_state->rotation,
		.mirror = pipe_ctx->plane_state->horizontal_mirror
	};

	/**
	 * If the cursor's source viewport is clipped then we need to
	 * translate the cursor to appear in the correct position on
	 * the screen.
	 *
	 * This translation isn't affected by scaling so it needs to be
	 * done *after* we adjust the position for the scale factor.
	 *
	 * This is only done by opt-in for now since there are still
	 * some usecases like tiled display that might enable the
	 * cursor on both streams while expecting dc to clip it.
	 */
	if (pos_cpy.translate_by_source) {
		pos_cpy.x += pipe_ctx->plane_state->src_rect.x;
		pos_cpy.y += pipe_ctx->plane_state->src_rect.y;
	}

	if (pipe_ctx->plane_state->address.type
			== PLN_ADDR_TYPE_VIDEO_PROGRESSIVE)
		pos_cpy.enable = false;

	if (pipe_ctx->top_pipe && pipe_ctx->plane_state != pipe_ctx->top_pipe->plane_state)
		pos_cpy.enable = false;

	if (ipp->funcs->ipp_cursor_set_position)
		ipp->funcs->ipp_cursor_set_position(ipp, &pos_cpy, &param);
	if (mi->funcs->set_cursor_position)
		mi->funcs->set_cursor_position(mi, &pos_cpy, &param);
}

static void dce110_set_cursor_attribute(struct pipe_ctx *pipe_ctx)
{
	struct dc_cursor_attributes *attributes = &pipe_ctx->stream->cursor_attributes;

	if (pipe_ctx->plane_res.ipp &&
	    pipe_ctx->plane_res.ipp->funcs->ipp_cursor_set_attributes)
		pipe_ctx->plane_res.ipp->funcs->ipp_cursor_set_attributes(
				pipe_ctx->plane_res.ipp, attributes);

	if (pipe_ctx->plane_res.mi &&
	    pipe_ctx->plane_res.mi->funcs->set_cursor_attributes)
		pipe_ctx->plane_res.mi->funcs->set_cursor_attributes(
				pipe_ctx->plane_res.mi, attributes);

	if (pipe_ctx->plane_res.xfm &&
	    pipe_ctx->plane_res.xfm->funcs->set_cursor_attributes)
		pipe_ctx->plane_res.xfm->funcs->set_cursor_attributes(
				pipe_ctx->plane_res.xfm, attributes);
}

bool dce110_set_backlight_level(struct pipe_ctx *pipe_ctx,
		uint32_t backlight_pwm_u16_16,
		uint32_t frame_ramp)
{
	struct dc_link *link = pipe_ctx->stream->link;
	struct dc  *dc = link->ctx->dc;
	struct abm *abm = pipe_ctx->stream_res.abm;
	struct panel_cntl *panel_cntl = link->panel_cntl;
	struct dmcu *dmcu = dc->res_pool->dmcu;
	bool fw_set_brightness = true;
	/* DMCU -1 for all controller id values,
	 * therefore +1 here
	 */
	uint32_t controller_id = pipe_ctx->stream_res.tg->inst + 1;

	if (abm == NULL || panel_cntl == NULL || (abm->funcs->set_backlight_level_pwm == NULL))
		return false;

	if (dmcu)
		fw_set_brightness = dmcu->funcs->is_dmcu_initialized(dmcu);

	if (!fw_set_brightness && panel_cntl->funcs->driver_set_backlight)
		panel_cntl->funcs->driver_set_backlight(panel_cntl, backlight_pwm_u16_16);
	else
		abm->funcs->set_backlight_level_pwm(
				abm,
				backlight_pwm_u16_16,
				frame_ramp,
				controller_id,
				link->panel_cntl->inst);

	return true;
}

void dce110_set_abm_immediate_disable(struct pipe_ctx *pipe_ctx)
{
	struct abm *abm = pipe_ctx->stream_res.abm;
	struct panel_cntl *panel_cntl = pipe_ctx->stream->link->panel_cntl;

	if (abm)
		abm->funcs->set_abm_immediate_disable(abm,
				pipe_ctx->stream->link->panel_cntl->inst);

	if (panel_cntl)
		panel_cntl->funcs->store_backlight_level(panel_cntl);
}

void dce110_set_pipe(struct pipe_ctx *pipe_ctx)
{
	struct abm *abm = pipe_ctx->stream_res.abm;
	struct panel_cntl *panel_cntl = pipe_ctx->stream->link->panel_cntl;
	uint32_t otg_inst = pipe_ctx->stream_res.tg->inst + 1;

	if (abm && panel_cntl)
		abm->funcs->set_pipe(abm, otg_inst, panel_cntl->inst);
}

void dce110_enable_lvds_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum clock_source_id clock_source,
		uint32_t pixel_clock)
{
	link->link_enc->funcs->enable_lvds_output(
			link->link_enc,
			clock_source,
			pixel_clock);
	link->phy_state.symclk_state = SYMCLK_ON_TX_ON;
}

void dce110_enable_tmds_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		enum dc_color_depth color_depth,
		uint32_t pixel_clock)
{
	link->link_enc->funcs->enable_tmds_output(
			link->link_enc,
			clock_source,
			color_depth,
			signal,
			pixel_clock);
	link->phy_state.symclk_state = SYMCLK_ON_TX_ON;
}

void dce110_enable_dp_link_output(
		struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		const struct dc_link_settings *link_settings)
{
	struct dc  *dc = link->ctx->dc;
	struct dmcu *dmcu = dc->res_pool->dmcu;
	struct pipe_ctx *pipes =
			link->dc->current_state->res_ctx.pipe_ctx;
	struct clock_source *dp_cs =
			link->dc->res_pool->dp_clock_source;
	const struct link_hwss *link_hwss = get_link_hwss(link, link_res);
	unsigned int i;


	if (link->connector_signal == SIGNAL_TYPE_EDP) {
		if (!link->dc->config.edp_no_power_sequencing)
			link->dc->hwss.edp_power_control(link, true);
		link->dc->hwss.edp_wait_for_hpd_ready(link, true);
	}

	/* If the current pixel clock source is not DTO(happens after
	 * switching from HDMI passive dongle to DP on the same connector),
	 * switch the pixel clock source to DTO.
	 */

	for (i = 0; i < MAX_PIPES; i++) {
		if (pipes[i].stream != NULL &&
				pipes[i].stream->link == link) {
			if (pipes[i].clock_source != NULL &&
					pipes[i].clock_source->id != CLOCK_SOURCE_ID_DP_DTO) {
				pipes[i].clock_source = dp_cs;
				pipes[i].stream_res.pix_clk_params.requested_pix_clk_100hz =
						pipes[i].stream->timing.pix_clk_100hz;
				pipes[i].clock_source->funcs->program_pix_clk(
						pipes[i].clock_source,
						&pipes[i].stream_res.pix_clk_params,
						dp_get_link_encoding_format(link_settings),
						&pipes[i].pll_settings);
			}
		}
	}

	if (dp_get_link_encoding_format(link_settings) == DP_8b_10b_ENCODING) {
		if (dc->clk_mgr->funcs->notify_link_rate_change)
			dc->clk_mgr->funcs->notify_link_rate_change(dc->clk_mgr, link);
	}

	if (dmcu != NULL && dmcu->funcs->lock_phy)
		dmcu->funcs->lock_phy(dmcu);

	if (link_hwss->ext.enable_dp_link_output)
		link_hwss->ext.enable_dp_link_output(link, link_res, signal,
				clock_source, link_settings);

	link->phy_state.symclk_state = SYMCLK_ON_TX_ON;

	if (dmcu != NULL && dmcu->funcs->unlock_phy)
		dmcu->funcs->unlock_phy(dmcu);

	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_ENABLE_LINK_PHY);
}

void dce110_disable_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	struct dc *dc = link->ctx->dc;
	const struct link_hwss *link_hwss = get_link_hwss(link, link_res);
	struct dmcu *dmcu = dc->res_pool->dmcu;

	if (signal == SIGNAL_TYPE_EDP &&
			link->dc->hwss.edp_backlight_control)
		link->dc->hwss.edp_backlight_control(link, false);
	else if (dmcu != NULL && dmcu->funcs->lock_phy)
		dmcu->funcs->lock_phy(dmcu);

	link_hwss->disable_link_output(link, link_res, signal);
	link->phy_state.symclk_state = SYMCLK_OFF_TX_OFF;

	if (signal == SIGNAL_TYPE_EDP &&
			link->dc->hwss.edp_backlight_control)
		link->dc->hwss.edp_power_control(link, false);
	else if (dmcu != NULL && dmcu->funcs->lock_phy)
		dmcu->funcs->unlock_phy(dmcu);
	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_DISABLE_LINK_PHY);
}

static const struct hw_sequencer_funcs dce110_funcs = {
	.program_gamut_remap = program_gamut_remap,
	.program_output_csc = program_output_csc,
	.init_hw = init_hw,
	.apply_ctx_to_hw = dce110_apply_ctx_to_hw,
	.apply_ctx_for_surface = dce110_apply_ctx_for_surface,
	.post_unlock_program_front_end = dce110_post_unlock_program_front_end,
	.update_plane_addr = update_plane_addr,
	.update_pending_status = dce110_update_pending_status,
	.enable_accelerated_mode = dce110_enable_accelerated_mode,
	.enable_timing_synchronization = dce110_enable_timing_synchronization,
	.enable_per_frame_crtc_position_reset = dce110_enable_per_frame_crtc_position_reset,
	.update_info_frame = dce110_update_info_frame,
	.enable_stream = dce110_enable_stream,
	.disable_stream = dce110_disable_stream,
	.unblank_stream = dce110_unblank_stream,
	.blank_stream = dce110_blank_stream,
	.enable_audio_stream = dce110_enable_audio_stream,
	.disable_audio_stream = dce110_disable_audio_stream,
	.disable_plane = dce110_power_down_fe,
	.pipe_control_lock = dce_pipe_control_lock,
	.interdependent_update_lock = NULL,
	.cursor_lock = dce_pipe_control_lock,
	.prepare_bandwidth = dce110_prepare_bandwidth,
	.optimize_bandwidth = dce110_optimize_bandwidth,
	.set_drr = set_drr,
	.get_position = get_position,
	.set_static_screen_control = set_static_screen_control,
	.setup_stereo = NULL,
	.set_avmute = dce110_set_avmute,
	.wait_for_mpcc_disconnect = dce110_wait_for_mpcc_disconnect,
	.edp_backlight_control = dce110_edp_backlight_control,
	.edp_power_control = dce110_edp_power_control,
	.edp_wait_for_hpd_ready = dce110_edp_wait_for_hpd_ready,
	.set_cursor_position = dce110_set_cursor_position,
	.set_cursor_attribute = dce110_set_cursor_attribute,
	.set_backlight_level = dce110_set_backlight_level,
	.set_abm_immediate_disable = dce110_set_abm_immediate_disable,
	.set_pipe = dce110_set_pipe,
	.enable_lvds_link_output = dce110_enable_lvds_link_output,
	.enable_tmds_link_output = dce110_enable_tmds_link_output,
	.enable_dp_link_output = dce110_enable_dp_link_output,
	.disable_link_output = dce110_disable_link_output,
};

static const struct hwseq_private_funcs dce110_private_funcs = {
	.init_pipes = init_pipes,
	.update_plane_addr = update_plane_addr,
	.set_input_transfer_func = dce110_set_input_transfer_func,
	.set_output_transfer_func = dce110_set_output_transfer_func,
	.power_down = dce110_power_down,
	.enable_display_pipe_clock_gating = enable_display_pipe_clock_gating,
	.enable_display_power_gating = dce110_enable_display_power_gating,
	.reset_hw_ctx_wrap = dce110_reset_hw_ctx_wrap,
	.enable_stream_timing = dce110_enable_stream_timing,
	.disable_stream_gating = NULL,
	.enable_stream_gating = NULL,
	.edp_backlight_control = dce110_edp_backlight_control,
};

void dce110_hw_sequencer_construct(struct dc *dc)
{
	dc->hwss = dce110_funcs;
	dc->hwseq->funcs = dce110_private_funcs;
}

