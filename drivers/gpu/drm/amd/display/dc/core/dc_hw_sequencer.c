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
#include "core_types.h"
#include "timing_generator.h"
#include "hw_sequencer.h"
#include "hw_sequencer_private.h"
#include "basics/dc_common.h"
#include "resource.h"
#include "dc_dmub_srv.h"
#include "dc_state_priv.h"

#define NUM_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_NUM_MCACHE 8

/* used as index in array of black_color_format */
enum black_color_format {
	BLACK_COLOR_FORMAT_RGB_FULLRANGE = 0,
	BLACK_COLOR_FORMAT_RGB_LIMITED,
	BLACK_COLOR_FORMAT_YUV_TV,
	BLACK_COLOR_FORMAT_YUV_CV,
	BLACK_COLOR_FORMAT_YUV_SUPER_AA,
	BLACK_COLOR_FORMAT_DEBUG,
};

enum dc_color_space_type {
	COLOR_SPACE_RGB_TYPE,
	COLOR_SPACE_RGB_LIMITED_TYPE,
	COLOR_SPACE_YCBCR601_TYPE,
	COLOR_SPACE_YCBCR709_TYPE,
	COLOR_SPACE_YCBCR2020_TYPE,
	COLOR_SPACE_YCBCR601_LIMITED_TYPE,
	COLOR_SPACE_YCBCR709_LIMITED_TYPE,
	COLOR_SPACE_YCBCR709_BLACK_TYPE,
};

static const struct tg_color black_color_format[] = {
	/* BlackColorFormat_RGB_FullRange */
	{0, 0, 0},
	/* BlackColorFormat_RGB_Limited */
	{0x40, 0x40, 0x40},
	/* BlackColorFormat_YUV_TV */
	{0x200, 0x40, 0x200},
	/* BlackColorFormat_YUV_CV */
	{0x1f4, 0x40, 0x1f4},
	/* BlackColorFormat_YUV_SuperAA */
	{0x1a2, 0x20, 0x1a2},
	/* visual confirm debug */
	{0xff, 0xff, 0},
};

struct out_csc_color_matrix_type {
	enum dc_color_space_type color_space_type;
	uint16_t regval[12];
};

static const struct out_csc_color_matrix_type output_csc_matrix[] = {
	{ COLOR_SPACE_RGB_TYPE,
		{ 0x2000, 0,      0,      0,
		  0,      0x2000, 0,      0,
		  0,      0,      0x2000, 0} },
	{ COLOR_SPACE_RGB_LIMITED_TYPE,
		{ 0x1B67, 0,      0,      0x201,
		  0,      0x1B67, 0,      0x201,
		  0,      0,      0x1B67, 0x201} },
	{ COLOR_SPACE_YCBCR601_TYPE,
		{ 0xE04,  0xF444, 0xFDB9, 0x1004,
		  0x831,  0x1016, 0x320,  0x201,
		  0xFB45, 0xF6B7, 0xE04,  0x1004} },
	{ COLOR_SPACE_YCBCR709_TYPE,
		{ 0xE04,  0xF345, 0xFEB7, 0x1004,
		  0x5D3,  0x1399, 0x1FA,  0x201,
		  0xFCCA, 0xF533, 0xE04,  0x1004} },
	/* TODO: correct values below */
	{ COLOR_SPACE_YCBCR601_LIMITED_TYPE,
		{ 0xE00,  0xF447, 0xFDB9, 0x1000,
		  0x991,  0x12C9, 0x3A6,  0x200,
		  0xFB47, 0xF6B9, 0xE00,  0x1000} },
	{ COLOR_SPACE_YCBCR709_LIMITED_TYPE,
		{ 0xE00, 0xF349, 0xFEB7, 0x1000,
		  0x6CE, 0x16E3, 0x24F,  0x200,
		  0xFCCB, 0xF535, 0xE00, 0x1000} },
	{ COLOR_SPACE_YCBCR2020_TYPE,
		{ 0x1000, 0xF149, 0xFEB7, 0x1004,
		  0x0868, 0x15B2, 0x01E6, 0x201,
		  0xFB88, 0xF478, 0x1000, 0x1004} },
	{ COLOR_SPACE_YCBCR709_BLACK_TYPE,
		{ 0x0000, 0x0000, 0x0000, 0x1000,
		  0x0000, 0x0000, 0x0000, 0x0200,
		  0x0000, 0x0000, 0x0000, 0x1000} },
};

static bool is_rgb_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_SRGB			||
		color_space == COLOR_SPACE_XR_RGB		||
		color_space == COLOR_SPACE_MSREF_SCRGB		||
		color_space == COLOR_SPACE_2020_RGB_FULLRANGE	||
		color_space == COLOR_SPACE_ADOBERGB		||
		color_space == COLOR_SPACE_DCIP3	||
		color_space == COLOR_SPACE_DOLBYVISION)
		ret = true;
	return ret;
}

static bool is_rgb_limited_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_SRGB_LIMITED		||
		color_space == COLOR_SPACE_2020_RGB_LIMITEDRANGE)
		ret = true;
	return ret;
}

static bool is_ycbcr601_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_YCBCR601	||
		color_space == COLOR_SPACE_XV_YCC_601)
		ret = true;
	return ret;
}

static bool is_ycbcr601_limited_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_YCBCR601_LIMITED)
		ret = true;
	return ret;
}

static bool is_ycbcr709_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_YCBCR709	||
		color_space == COLOR_SPACE_XV_YCC_709)
		ret = true;
	return ret;
}

static bool is_ycbcr2020_type(
	enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_2020_YCBCR_LIMITED || color_space == COLOR_SPACE_2020_YCBCR_FULL)
		ret = true;
	return ret;
}

static bool is_ycbcr709_limited_type(
		enum dc_color_space color_space)
{
	bool ret = false;

	if (color_space == COLOR_SPACE_YCBCR709_LIMITED)
		ret = true;
	return ret;
}

static enum dc_color_space_type get_color_space_type(enum dc_color_space color_space)
{
	enum dc_color_space_type type = COLOR_SPACE_RGB_TYPE;

	if (is_rgb_type(color_space))
		type = COLOR_SPACE_RGB_TYPE;
	else if (is_rgb_limited_type(color_space))
		type = COLOR_SPACE_RGB_LIMITED_TYPE;
	else if (is_ycbcr601_type(color_space))
		type = COLOR_SPACE_YCBCR601_TYPE;
	else if (is_ycbcr709_type(color_space))
		type = COLOR_SPACE_YCBCR709_TYPE;
	else if (is_ycbcr601_limited_type(color_space))
		type = COLOR_SPACE_YCBCR601_LIMITED_TYPE;
	else if (is_ycbcr709_limited_type(color_space))
		type = COLOR_SPACE_YCBCR709_LIMITED_TYPE;
	else if (is_ycbcr2020_type(color_space))
		type = COLOR_SPACE_YCBCR2020_TYPE;
	else if (color_space == COLOR_SPACE_YCBCR709)
		type = COLOR_SPACE_YCBCR709_BLACK_TYPE;
	else if (color_space == COLOR_SPACE_YCBCR709_BLACK)
		type = COLOR_SPACE_YCBCR709_BLACK_TYPE;
	return type;
}

const uint16_t *find_color_matrix(enum dc_color_space color_space,
							uint32_t *array_size)
{
	int i;
	enum dc_color_space_type type;
	const uint16_t *val = NULL;
	int arr_size = NUM_ELEMENTS(output_csc_matrix);

	type = get_color_space_type(color_space);
	for (i = 0; i < arr_size; i++)
		if (output_csc_matrix[i].color_space_type == type) {
			val = output_csc_matrix[i].regval;
			*array_size = 12;
			break;
		}

	return val;
}


void color_space_to_black_color(
	const struct dc *dc,
	enum dc_color_space colorspace,
	struct tg_color *black_color)
{
	switch (colorspace) {
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR709_BLACK:
	case COLOR_SPACE_YCBCR601_LIMITED:
	case COLOR_SPACE_YCBCR709_LIMITED:
	case COLOR_SPACE_2020_YCBCR_LIMITED:
	case COLOR_SPACE_2020_YCBCR_FULL:
		*black_color = black_color_format[BLACK_COLOR_FORMAT_YUV_CV];
		break;

	case COLOR_SPACE_SRGB_LIMITED:
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_RGB_LIMITED];
		break;

	/**
	 * Remove default and add case for all color space
	 * so when we forget to add new color space
	 * compiler will give a warning
	 */
	case COLOR_SPACE_UNKNOWN:
	case COLOR_SPACE_SRGB:
	case COLOR_SPACE_XR_RGB:
	case COLOR_SPACE_MSREF_SCRGB:
	case COLOR_SPACE_XV_YCC_709:
	case COLOR_SPACE_XV_YCC_601:
	case COLOR_SPACE_2020_RGB_FULLRANGE:
	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
	case COLOR_SPACE_ADOBERGB:
	case COLOR_SPACE_DCIP3:
	case COLOR_SPACE_DISPLAYNATIVE:
	case COLOR_SPACE_DOLBYVISION:
	case COLOR_SPACE_APPCTRL:
	case COLOR_SPACE_CUSTOMPOINTS:
		/* fefault is sRGB black (full range). */
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_RGB_FULLRANGE];
		/* default is sRGB black 0. */
		break;
	}
}

bool hwss_wait_for_blank_complete(
		struct timing_generator *tg)
{
	int counter;

	/* Not applicable if the pipe is not primary, save 300ms of boot time */
	if (!tg->funcs->is_blanked)
		return true;
	for (counter = 0; counter < 100; counter++) {
		if (tg->funcs->is_blanked(tg))
			break;

		msleep(1);
	}

	if (counter == 100) {
		dm_error("DC: failed to blank crtc!\n");
		return false;
	}

	return true;
}

void get_mpctree_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	const struct tg_color pipe_colors[6] = {
			{MAX_TG_COLOR_VALUE, 0, 0}, /* red */
			{MAX_TG_COLOR_VALUE, MAX_TG_COLOR_VALUE, 0}, /* yellow */
			{0, MAX_TG_COLOR_VALUE, 0}, /* green */
			{0, MAX_TG_COLOR_VALUE, MAX_TG_COLOR_VALUE}, /* cyan */
			{0, 0, MAX_TG_COLOR_VALUE}, /* blue */
			{MAX_TG_COLOR_VALUE, 0, MAX_TG_COLOR_VALUE}, /* magenta */
	};

	struct pipe_ctx *top_pipe = pipe_ctx;

	while (top_pipe->top_pipe)
		top_pipe = top_pipe->top_pipe;

	*color = pipe_colors[top_pipe->pipe_idx];
}

void get_surface_visual_confirm_color(
		const struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;

	switch (pipe_ctx->plane_res.scl_data.format) {
	case PIXEL_FORMAT_ARGB8888:
		/* set border color to red */
		color->color_r_cr = color_value;
		if (pipe_ctx->plane_state->layer_index > 0) {
			/* set border color to pink */
			color->color_b_cb = color_value;
			color->color_g_y = color_value * 0.5;
		}
		break;

	case PIXEL_FORMAT_ARGB2101010:
		/* set border color to blue */
		color->color_b_cb = color_value;
		if (pipe_ctx->plane_state->layer_index > 0) {
			/* set border color to cyan */
			color->color_g_y = color_value;
		}
		break;
	case PIXEL_FORMAT_420BPP8:
		/* set border color to green */
		color->color_g_y = color_value;
		break;
	case PIXEL_FORMAT_420BPP10:
		/* set border color to yellow */
		color->color_g_y = color_value;
		color->color_r_cr = color_value;
		break;
	case PIXEL_FORMAT_FP16:
		/* set border color to white */
		color->color_r_cr = color_value;
		color->color_b_cb = color_value;
		color->color_g_y = color_value;
		if (pipe_ctx->plane_state->layer_index > 0) {
			/* set border color to orange */
			color->color_g_y = 0.22 * color_value;
			color->color_b_cb = 0;
		}
		break;
	default:
		break;
	}
}

void get_hdr_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;
	bool is_sdr = false;

	/* Determine the overscan color based on the top-most (desktop) plane's context */
	struct pipe_ctx *top_pipe_ctx  = pipe_ctx;

	while (top_pipe_ctx->top_pipe != NULL)
		top_pipe_ctx = top_pipe_ctx->top_pipe;

	switch (top_pipe_ctx->plane_res.scl_data.format) {
	case PIXEL_FORMAT_ARGB2101010:
		if (top_pipe_ctx->stream->out_transfer_func.tf == TRANSFER_FUNCTION_PQ) {
			/* HDR10, ARGB2101010 - set border color to red */
			color->color_r_cr = color_value;
		} else if (top_pipe_ctx->stream->out_transfer_func.tf == TRANSFER_FUNCTION_GAMMA22) {
			/* FreeSync 2 ARGB2101010 - set border color to pink */
			color->color_r_cr = color_value;
			color->color_b_cb = color_value;
		} else
			is_sdr = true;
		break;
	case PIXEL_FORMAT_FP16:
		if (top_pipe_ctx->stream->out_transfer_func.tf == TRANSFER_FUNCTION_PQ) {
			/* HDR10, FP16 - set border color to blue */
			color->color_b_cb = color_value;
		} else if (top_pipe_ctx->stream->out_transfer_func.tf == TRANSFER_FUNCTION_GAMMA22) {
			/* FreeSync 2 HDR - set border color to green */
			color->color_g_y = color_value;
		} else
			is_sdr = true;
		break;
	default:
		is_sdr = true;
		break;
	}

	if (is_sdr) {
		/* SDR - set border color to Gray */
		color->color_r_cr = color_value/2;
		color->color_b_cb = color_value/2;
		color->color_g_y = color_value/2;
	}
}

/* Visual Confirm color definition for Smart Mux */
void get_smartmux_visual_confirm_color(
	struct dc *dc,
	struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;

	const struct tg_color sm_ver_colors[5] = {
			{0, 0, 0},					/* SMUX_MUXCONTROL_UNSUPPORTED - Black */
			{0, MAX_TG_COLOR_VALUE, 0},			/* SMUX_MUXCONTROL_v10 - Green */
			{0, MAX_TG_COLOR_VALUE, MAX_TG_COLOR_VALUE},	/* SMUX_MUXCONTROL_v15 - Cyan */
			{MAX_TG_COLOR_VALUE, MAX_TG_COLOR_VALUE, 0}, 	/* SMUX_MUXCONTROL_MDM - Yellow */
			{MAX_TG_COLOR_VALUE, 0, MAX_TG_COLOR_VALUE}, 	/* SMUX_MUXCONTROL_vUNKNOWN - Magenta*/
	};

	if (dc->caps.is_apu) {
		/* APU driving the eDP */
		*color = sm_ver_colors[dc->config.smart_mux_version];
	} else {
		/* dGPU driving the eDP - red */
		color->color_r_cr = color_value;
		color->color_g_y = 0;
		color->color_b_cb = 0;
	}
}

/* Visual Confirm color definition for VABC */
void get_vabc_visual_confirm_color(
	struct pipe_ctx *pipe_ctx,
	struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;
	struct dc_link *edp_link = NULL;

	if (pipe_ctx && pipe_ctx->stream && pipe_ctx->stream->link) {
		if (pipe_ctx->stream->link->connector_signal == SIGNAL_TYPE_EDP)
			edp_link = pipe_ctx->stream->link;
	}

	if (edp_link) {
		switch (edp_link->backlight_control_type) {
		case BACKLIGHT_CONTROL_PWM:
			color->color_r_cr = color_value;
			color->color_g_y = 0;
			color->color_b_cb = 0;
			break;
		case BACKLIGHT_CONTROL_AMD_AUX:
			color->color_r_cr = 0;
			color->color_g_y = color_value;
			color->color_b_cb = 0;
			break;
		case BACKLIGHT_CONTROL_VESA_AUX:
			color->color_r_cr = 0;
			color->color_g_y = 0;
			color->color_b_cb = color_value;
			break;
		}
	} else {
		color->color_r_cr = 0;
		color->color_g_y = 0;
		color->color_b_cb = 0;
	}
}

void get_subvp_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;
	if (pipe_ctx) {
		switch (pipe_ctx->p_state_type) {
		case P_STATE_SUB_VP:
			color->color_r_cr = color_value;
			color->color_g_y  = 0;
			color->color_b_cb = 0;
			break;
		case P_STATE_DRR_SUB_VP:
			color->color_r_cr = 0;
			color->color_g_y  = color_value;
			color->color_b_cb = 0;
			break;
		case P_STATE_V_BLANK_SUB_VP:
			color->color_r_cr = 0;
			color->color_g_y  = 0;
			color->color_b_cb = color_value;
			break;
		default:
			break;
		}
	}
}

void get_mclk_switch_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;

	if (pipe_ctx) {
		switch (pipe_ctx->p_state_type) {
		case P_STATE_V_BLANK:
			color->color_r_cr = color_value;
			color->color_g_y = color_value;
			color->color_b_cb = 0;
			break;
		case P_STATE_FPO:
			color->color_r_cr = 0;
			color->color_g_y  = color_value;
			color->color_b_cb = color_value;
			break;
		case P_STATE_V_ACTIVE:
			color->color_r_cr = color_value;
			color->color_g_y  = 0;
			color->color_b_cb = color_value;
			break;
		case P_STATE_SUB_VP:
			color->color_r_cr = color_value;
			color->color_g_y  = 0;
			color->color_b_cb = 0;
			break;
		case P_STATE_DRR_SUB_VP:
			color->color_r_cr = 0;
			color->color_g_y  = color_value;
			color->color_b_cb = 0;
			break;
		case P_STATE_V_BLANK_SUB_VP:
			color->color_r_cr = 0;
			color->color_g_y  = 0;
			color->color_b_cb = color_value;
			break;
		default:
			break;
		}
	}
}

void get_cursor_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;

	if (pipe_ctx->stream && pipe_ctx->stream->cursor_position.enable) {
		color->color_r_cr = color_value;
		color->color_g_y = 0;
		color->color_b_cb = 0;
	} else {
		color->color_r_cr = 0;
		color->color_g_y = 0;
		color->color_b_cb = color_value;
	}
}

void get_dcc_visual_confirm_color(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct tg_color *color)
{
	const uint32_t MCACHE_ID_UNASSIGNED = 0xF;

	if (!pipe_ctx->plane_state->dcc.enable) {
		color->color_r_cr = 0; /* black - DCC disabled */
		color->color_g_y = 0;
		color->color_b_cb = 0;
		return;
	}

	if (dc->ctx->dce_version < DCN_VERSION_4_01) {
		color->color_r_cr = MAX_TG_COLOR_VALUE; /* red - DCC enabled */
		color->color_g_y = 0;
		color->color_b_cb = 0;
		return;
	}

	uint32_t first_id = pipe_ctx->mcache_regs.main.p0.mcache_id_first;
	uint32_t second_id = pipe_ctx->mcache_regs.main.p0.mcache_id_second;

	if (first_id != MCACHE_ID_UNASSIGNED && second_id != MCACHE_ID_UNASSIGNED && first_id != second_id) {
		color->color_r_cr = MAX_TG_COLOR_VALUE/2; /* grey - 2 mcache */
		color->color_g_y = MAX_TG_COLOR_VALUE/2;
		color->color_b_cb = MAX_TG_COLOR_VALUE/2;
	}

	else if (first_id != MCACHE_ID_UNASSIGNED || second_id != MCACHE_ID_UNASSIGNED) {
		const struct tg_color id_colors[MAX_NUM_MCACHE] = {
		{0, MAX_TG_COLOR_VALUE, 0}, /* green */
		{0, 0, MAX_TG_COLOR_VALUE}, /* blue */
		{MAX_TG_COLOR_VALUE, MAX_TG_COLOR_VALUE, 0}, /* yellow */
		{MAX_TG_COLOR_VALUE, 0, MAX_TG_COLOR_VALUE}, /* magenta */
		{0, MAX_TG_COLOR_VALUE, MAX_TG_COLOR_VALUE}, /* cyan */
		{MAX_TG_COLOR_VALUE, MAX_TG_COLOR_VALUE, MAX_TG_COLOR_VALUE}, /* white */
		{MAX_TG_COLOR_VALUE/2, 0, 0}, /* dark red */
		{0, MAX_TG_COLOR_VALUE/2, 0}, /* dark green */
		};

		uint32_t assigned_id = (first_id != MCACHE_ID_UNASSIGNED) ? first_id : second_id;
		*color = id_colors[assigned_id];
	}
}

void set_p_state_switch_method(
		struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx)
{
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;
	bool enable_subvp;

	if (!dc->ctx || !dc->ctx->dmub_srv || !pipe_ctx || !vba)
		return;

	pipe_ctx->p_state_type = P_STATE_UNKNOWN;
	if (vba->DRAMClockChangeSupport[vba->VoltageLevel][vba->maxMpcComb] !=
			dm_dram_clock_change_unsupported) {
		/* MCLK switching is supported */
		if (!pipe_ctx->has_vactive_margin) {
			/* In Vblank - yellow */
			pipe_ctx->p_state_type = P_STATE_V_BLANK;

			if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching) {
				/* FPO + Vblank - cyan */
				pipe_ctx->p_state_type = P_STATE_FPO;
			}
		} else {
			/* In Vactive - pink */
			pipe_ctx->p_state_type = P_STATE_V_ACTIVE;
		}

		/* SubVP */
		enable_subvp = false;

		for (int i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

			if (pipe->stream && dc_state_get_paired_subvp_stream(context, pipe->stream) &&
					dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_MAIN) {
				/* SubVP enable - red */
				pipe_ctx->p_state_type = P_STATE_SUB_VP;
				enable_subvp = true;

				if (pipe_ctx->stream == pipe->stream)
					return;
				break;
			}
		}

		if (enable_subvp && dc_state_get_pipe_subvp_type(context, pipe_ctx) == SUBVP_NONE) {
			if (pipe_ctx->stream->allow_freesync == 1) {
				/* SubVP enable and DRR on - green */
				pipe_ctx->p_state_type = P_STATE_DRR_SUB_VP;
			} else {
				/* SubVP enable and No DRR - blue */
				pipe_ctx->p_state_type = P_STATE_V_BLANK_SUB_VP;
			}
		}
	}
}

void set_drr_and_clear_adjust_pending(
		struct pipe_ctx *pipe_ctx,
		struct dc_stream_state *stream,
		struct drr_params *params)
{
	/* params can be null.*/
	if (pipe_ctx && pipe_ctx->stream_res.tg &&
			pipe_ctx->stream_res.tg->funcs->set_drr)
		pipe_ctx->stream_res.tg->funcs->set_drr(
				pipe_ctx->stream_res.tg, params);

	if (stream)
		stream->adjust.timing_adjust_pending = false;
}

void get_fams2_visual_confirm_color(
		struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;

	if (!dc->ctx || !dc->ctx->dmub_srv || !pipe_ctx || !context || !dc->debug.fams2_config.bits.enable)
		return;

	/* driver only handles visual confirm when FAMS2 is disabled */
	if (!dc_state_is_fams2_in_use(dc, context)) {
		/* when FAMS2 is disabled, all pipes are grey */
		color->color_g_y = color_value / 2;
		color->color_b_cb = color_value / 2;
		color->color_r_cr = color_value / 2;
	}
}

void hwss_build_fast_sequence(struct dc *dc,
		struct dc_dmub_cmd *dc_dmub_cmd,
		unsigned int dmub_cmd_count,
		struct block_sequence block_sequence[MAX_HWSS_BLOCK_SEQUENCE_SIZE],
		unsigned int *num_steps,
		struct pipe_ctx *pipe_ctx,
		struct dc_stream_status *stream_status,
		struct dc_state *context)
{
	struct dc_plane_state *plane = pipe_ctx->plane_state;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dce_hwseq *hws = dc->hwseq;
	struct pipe_ctx *current_pipe = NULL;
	struct pipe_ctx *current_mpc_pipe = NULL;
	unsigned int i = 0;

	*num_steps = 0; // Initialize to 0

	if (!plane || !stream)
		return;

	if (dc->hwss.wait_for_dcc_meta_propagation) {
		block_sequence[*num_steps].params.wait_for_dcc_meta_propagation_params.dc = dc;
		block_sequence[*num_steps].params.wait_for_dcc_meta_propagation_params.top_pipe_to_program = pipe_ctx;
		block_sequence[*num_steps].func = HUBP_WAIT_FOR_DCC_META_PROP;
		(*num_steps)++;
	}
	if (dc->hwss.subvp_pipe_control_lock_fast) {
		block_sequence[*num_steps].params.subvp_pipe_control_lock_fast_params.dc = dc;
		block_sequence[*num_steps].params.subvp_pipe_control_lock_fast_params.lock = true;
		block_sequence[*num_steps].params.subvp_pipe_control_lock_fast_params.subvp_immediate_flip =
				plane->flip_immediate && stream_status->mall_stream_config.type == SUBVP_MAIN;
		block_sequence[*num_steps].func = DMUB_SUBVP_PIPE_CONTROL_LOCK_FAST;
		(*num_steps)++;
	}
	if (dc->hwss.fams2_global_control_lock_fast) {
		block_sequence[*num_steps].params.fams2_global_control_lock_fast_params.dc = dc;
		block_sequence[*num_steps].params.fams2_global_control_lock_fast_params.lock = true;
		block_sequence[*num_steps].params.fams2_global_control_lock_fast_params.is_required = dc_state_is_fams2_in_use(dc, context);
		block_sequence[*num_steps].func = DMUB_FAMS2_GLOBAL_CONTROL_LOCK_FAST;
		(*num_steps)++;
	}
	if (dc->hwss.pipe_control_lock) {
		block_sequence[*num_steps].params.pipe_control_lock_params.dc = dc;
		block_sequence[*num_steps].params.pipe_control_lock_params.lock = true;
		block_sequence[*num_steps].params.pipe_control_lock_params.pipe_ctx = pipe_ctx;
		block_sequence[*num_steps].func = OPTC_PIPE_CONTROL_LOCK;
		(*num_steps)++;
	}

	for (i = 0; i < dmub_cmd_count; i++) {
		block_sequence[*num_steps].params.send_dmcub_cmd_params.ctx = dc->ctx;
		block_sequence[*num_steps].params.send_dmcub_cmd_params.cmd = &(dc_dmub_cmd[i].dmub_cmd);
		block_sequence[*num_steps].params.send_dmcub_cmd_params.wait_type = dc_dmub_cmd[i].wait_type;
		block_sequence[*num_steps].func = DMUB_SEND_DMCUB_CMD;
		(*num_steps)++;
	}

	current_pipe = pipe_ctx;
	while (current_pipe) {
		current_mpc_pipe = current_pipe;
		while (current_mpc_pipe) {
			if (current_mpc_pipe->plane_state) {
				if (dc->hwss.set_flip_control_gsl && current_mpc_pipe->plane_state->update_flags.raw) {
					block_sequence[*num_steps].params.set_flip_control_gsl_params.pipe_ctx = current_mpc_pipe;
					block_sequence[*num_steps].params.set_flip_control_gsl_params.flip_immediate = current_mpc_pipe->plane_state->flip_immediate;
					block_sequence[*num_steps].func = HUBP_SET_FLIP_CONTROL_GSL;
					(*num_steps)++;
				}
				if (dc->hwss.program_triplebuffer && dc->debug.enable_tri_buf && current_mpc_pipe->plane_state->update_flags.raw) {
					block_sequence[*num_steps].params.program_triplebuffer_params.dc = dc;
					block_sequence[*num_steps].params.program_triplebuffer_params.pipe_ctx = current_mpc_pipe;
					block_sequence[*num_steps].params.program_triplebuffer_params.enableTripleBuffer = current_mpc_pipe->plane_state->triplebuffer_flips;
					block_sequence[*num_steps].func = HUBP_PROGRAM_TRIPLEBUFFER;
					(*num_steps)++;
				}
				if (dc->hwss.update_plane_addr && current_mpc_pipe->plane_state->update_flags.bits.addr_update) {
					if (resource_is_pipe_type(current_mpc_pipe, OTG_MASTER) &&
							stream_status->mall_stream_config.type == SUBVP_MAIN) {
						block_sequence[*num_steps].params.subvp_save_surf_addr.dc_dmub_srv = dc->ctx->dmub_srv;
						block_sequence[*num_steps].params.subvp_save_surf_addr.addr = &current_mpc_pipe->plane_state->address;
						block_sequence[*num_steps].params.subvp_save_surf_addr.subvp_index = current_mpc_pipe->subvp_index;
						block_sequence[*num_steps].func = DMUB_SUBVP_SAVE_SURF_ADDR;
						(*num_steps)++;
					}

					block_sequence[*num_steps].params.update_plane_addr_params.dc = dc;
					block_sequence[*num_steps].params.update_plane_addr_params.pipe_ctx = current_mpc_pipe;
					block_sequence[*num_steps].func = HUBP_UPDATE_PLANE_ADDR;
					(*num_steps)++;
				}

				if (hws->funcs.set_input_transfer_func && current_mpc_pipe->plane_state->update_flags.bits.gamma_change) {
					block_sequence[*num_steps].params.set_input_transfer_func_params.dc = dc;
					block_sequence[*num_steps].params.set_input_transfer_func_params.pipe_ctx = current_mpc_pipe;
					block_sequence[*num_steps].params.set_input_transfer_func_params.plane_state = current_mpc_pipe->plane_state;
					block_sequence[*num_steps].func = DPP_SET_INPUT_TRANSFER_FUNC;
					(*num_steps)++;
				}

				if (dc->hwss.program_gamut_remap && current_mpc_pipe->plane_state->update_flags.bits.gamut_remap_change) {
					block_sequence[*num_steps].params.program_gamut_remap_params.pipe_ctx = current_mpc_pipe;
					block_sequence[*num_steps].func = DPP_PROGRAM_GAMUT_REMAP;
					(*num_steps)++;
				}
				if (current_mpc_pipe->plane_state->update_flags.bits.input_csc_change) {
					block_sequence[*num_steps].params.setup_dpp_params.pipe_ctx = current_mpc_pipe;
					block_sequence[*num_steps].func = DPP_SETUP_DPP;
					(*num_steps)++;
				}
				if (current_mpc_pipe->plane_state->update_flags.bits.coeff_reduction_change) {
					block_sequence[*num_steps].params.program_bias_and_scale_params.pipe_ctx = current_mpc_pipe;
					block_sequence[*num_steps].func = DPP_PROGRAM_BIAS_AND_SCALE;
					(*num_steps)++;
				}
			}
			if (hws->funcs.set_output_transfer_func && current_mpc_pipe->stream->update_flags.bits.out_tf) {
				block_sequence[*num_steps].params.set_output_transfer_func_params.dc = dc;
				block_sequence[*num_steps].params.set_output_transfer_func_params.pipe_ctx = current_mpc_pipe;
				block_sequence[*num_steps].params.set_output_transfer_func_params.stream = current_mpc_pipe->stream;
				block_sequence[*num_steps].func = DPP_SET_OUTPUT_TRANSFER_FUNC;
				(*num_steps)++;
			}
			if (dc->debug.visual_confirm != VISUAL_CONFIRM_DISABLE &&
				dc->hwss.update_visual_confirm_color) {
				block_sequence[*num_steps].params.update_visual_confirm_params.dc = dc;
				block_sequence[*num_steps].params.update_visual_confirm_params.pipe_ctx = current_mpc_pipe;
				block_sequence[*num_steps].params.update_visual_confirm_params.mpcc_id = current_mpc_pipe->plane_res.hubp->inst;
				block_sequence[*num_steps].func = MPC_UPDATE_VISUAL_CONFIRM;
				(*num_steps)++;
			}
			if (current_mpc_pipe->stream->update_flags.bits.out_csc) {
				block_sequence[*num_steps].params.power_on_mpc_mem_pwr_params.mpc = dc->res_pool->mpc;
				block_sequence[*num_steps].params.power_on_mpc_mem_pwr_params.mpcc_id = current_mpc_pipe->plane_res.hubp->inst;
				block_sequence[*num_steps].params.power_on_mpc_mem_pwr_params.power_on = true;
				block_sequence[*num_steps].func = MPC_POWER_ON_MPC_MEM_PWR;
				(*num_steps)++;

				if (current_mpc_pipe->stream->csc_color_matrix.enable_adjustment == true) {
					block_sequence[*num_steps].params.set_output_csc_params.mpc = dc->res_pool->mpc;
					block_sequence[*num_steps].params.set_output_csc_params.opp_id = current_mpc_pipe->stream_res.opp->inst;
					block_sequence[*num_steps].params.set_output_csc_params.regval = current_mpc_pipe->stream->csc_color_matrix.matrix;
					block_sequence[*num_steps].params.set_output_csc_params.ocsc_mode = MPC_OUTPUT_CSC_COEF_A;
					block_sequence[*num_steps].func = MPC_SET_OUTPUT_CSC;
					(*num_steps)++;
				} else {
					block_sequence[*num_steps].params.set_ocsc_default_params.mpc = dc->res_pool->mpc;
					block_sequence[*num_steps].params.set_ocsc_default_params.opp_id = current_mpc_pipe->stream_res.opp->inst;
					block_sequence[*num_steps].params.set_ocsc_default_params.color_space = current_mpc_pipe->stream->output_color_space;
					block_sequence[*num_steps].params.set_ocsc_default_params.ocsc_mode = MPC_OUTPUT_CSC_COEF_A;
					block_sequence[*num_steps].func = MPC_SET_OCSC_DEFAULT;
					(*num_steps)++;
				}
			}
			current_mpc_pipe = current_mpc_pipe->bottom_pipe;
		}
		current_pipe = current_pipe->next_odm_pipe;
	}

	if (dc->hwss.pipe_control_lock) {
		block_sequence[*num_steps].params.pipe_control_lock_params.dc = dc;
		block_sequence[*num_steps].params.pipe_control_lock_params.lock = false;
		block_sequence[*num_steps].params.pipe_control_lock_params.pipe_ctx = pipe_ctx;
		block_sequence[*num_steps].func = OPTC_PIPE_CONTROL_LOCK;
		(*num_steps)++;
	}
	if (dc->hwss.subvp_pipe_control_lock_fast) {
		block_sequence[*num_steps].params.subvp_pipe_control_lock_fast_params.dc = dc;
		block_sequence[*num_steps].params.subvp_pipe_control_lock_fast_params.lock = false;
		block_sequence[*num_steps].params.subvp_pipe_control_lock_fast_params.subvp_immediate_flip =
				plane->flip_immediate && stream_status->mall_stream_config.type == SUBVP_MAIN;
		block_sequence[*num_steps].func = DMUB_SUBVP_PIPE_CONTROL_LOCK_FAST;
		(*num_steps)++;
	}
	if (dc->hwss.fams2_global_control_lock_fast) {
		block_sequence[*num_steps].params.fams2_global_control_lock_fast_params.dc = dc;
		block_sequence[*num_steps].params.fams2_global_control_lock_fast_params.lock = false;
		block_sequence[*num_steps].params.fams2_global_control_lock_fast_params.is_required = dc_state_is_fams2_in_use(dc, context);
		block_sequence[*num_steps].func = DMUB_FAMS2_GLOBAL_CONTROL_LOCK_FAST;
		(*num_steps)++;
	}

	current_pipe = pipe_ctx;
	while (current_pipe) {
		current_mpc_pipe = current_pipe;

		while (current_mpc_pipe) {
			if (!current_mpc_pipe->bottom_pipe && !current_mpc_pipe->next_odm_pipe &&
					current_mpc_pipe->stream && current_mpc_pipe->plane_state &&
					current_mpc_pipe->plane_state->update_flags.bits.addr_update &&
					!current_mpc_pipe->plane_state->skip_manual_trigger) {
				block_sequence[*num_steps].params.program_manual_trigger_params.pipe_ctx = current_mpc_pipe;
				block_sequence[*num_steps].func = OPTC_PROGRAM_MANUAL_TRIGGER;
				(*num_steps)++;
			}
			current_mpc_pipe = current_mpc_pipe->bottom_pipe;
		}
		current_pipe = current_pipe->next_odm_pipe;
	}
}

void hwss_execute_sequence(struct dc *dc,
		struct block_sequence block_sequence[MAX_HWSS_BLOCK_SEQUENCE_SIZE],
		int num_steps)
{
	unsigned int i;
	union block_sequence_params *params;
	struct dce_hwseq *hws = dc->hwseq;

	for (i = 0; i < num_steps; i++) {
		params = &(block_sequence[i].params);
		switch (block_sequence[i].func) {

		case DMUB_SUBVP_PIPE_CONTROL_LOCK_FAST:
			dc->hwss.subvp_pipe_control_lock_fast(params);
			break;
		case OPTC_PIPE_CONTROL_LOCK:
			dc->hwss.pipe_control_lock(params->pipe_control_lock_params.dc,
					params->pipe_control_lock_params.pipe_ctx,
					params->pipe_control_lock_params.lock);
			break;
		case HUBP_SET_FLIP_CONTROL_GSL:
			dc->hwss.set_flip_control_gsl(params->set_flip_control_gsl_params.pipe_ctx,
					params->set_flip_control_gsl_params.flip_immediate);
			break;
		case HUBP_PROGRAM_TRIPLEBUFFER:
			dc->hwss.program_triplebuffer(params->program_triplebuffer_params.dc,
					params->program_triplebuffer_params.pipe_ctx,
					params->program_triplebuffer_params.enableTripleBuffer);
			break;
		case HUBP_UPDATE_PLANE_ADDR:
			dc->hwss.update_plane_addr(params->update_plane_addr_params.dc,
					params->update_plane_addr_params.pipe_ctx);
			break;
		case DPP_SET_INPUT_TRANSFER_FUNC:
			hws->funcs.set_input_transfer_func(params->set_input_transfer_func_params.dc,
					params->set_input_transfer_func_params.pipe_ctx,
					params->set_input_transfer_func_params.plane_state);
			break;
		case DPP_PROGRAM_GAMUT_REMAP:
			dc->hwss.program_gamut_remap(params->program_gamut_remap_params.pipe_ctx);
			break;
		case DPP_SETUP_DPP:
			hwss_setup_dpp(params);
			break;
		case DPP_PROGRAM_BIAS_AND_SCALE:
			hwss_program_bias_and_scale(params);
			break;
		case OPTC_PROGRAM_MANUAL_TRIGGER:
			hwss_program_manual_trigger(params);
			break;
		case DPP_SET_OUTPUT_TRANSFER_FUNC:
			hws->funcs.set_output_transfer_func(params->set_output_transfer_func_params.dc,
					params->set_output_transfer_func_params.pipe_ctx,
					params->set_output_transfer_func_params.stream);
			break;
		case MPC_UPDATE_VISUAL_CONFIRM:
			dc->hwss.update_visual_confirm_color(params->update_visual_confirm_params.dc,
					params->update_visual_confirm_params.pipe_ctx,
					params->update_visual_confirm_params.mpcc_id);
			break;
		case MPC_POWER_ON_MPC_MEM_PWR:
			hwss_power_on_mpc_mem_pwr(params);
			break;
		case MPC_SET_OUTPUT_CSC:
			hwss_set_output_csc(params);
			break;
		case MPC_SET_OCSC_DEFAULT:
			hwss_set_ocsc_default(params);
			break;
		case DMUB_SEND_DMCUB_CMD:
			hwss_send_dmcub_cmd(params);
			break;
		case DMUB_SUBVP_SAVE_SURF_ADDR:
			hwss_subvp_save_surf_addr(params);
			break;
		case HUBP_WAIT_FOR_DCC_META_PROP:
			dc->hwss.wait_for_dcc_meta_propagation(
					params->wait_for_dcc_meta_propagation_params.dc,
					params->wait_for_dcc_meta_propagation_params.top_pipe_to_program);
			break;
		case DMUB_FAMS2_GLOBAL_CONTROL_LOCK_FAST:
			dc->hwss.fams2_global_control_lock_fast(params);
			break;
		default:
			ASSERT(false);
			break;
		}
	}
}

void hwss_send_dmcub_cmd(union block_sequence_params *params)
{
	struct dc_context *ctx = params->send_dmcub_cmd_params.ctx;
	union dmub_rb_cmd *cmd = params->send_dmcub_cmd_params.cmd;
	enum dm_dmub_wait_type wait_type = params->send_dmcub_cmd_params.wait_type;

	dc_wake_and_execute_dmub_cmd(ctx, cmd, wait_type);
}

void hwss_program_manual_trigger(union block_sequence_params *params)
{
	struct pipe_ctx *pipe_ctx = params->program_manual_trigger_params.pipe_ctx;

	if (pipe_ctx->stream_res.tg->funcs->program_manual_trigger)
		pipe_ctx->stream_res.tg->funcs->program_manual_trigger(pipe_ctx->stream_res.tg);
}

void hwss_setup_dpp(union block_sequence_params *params)
{
	struct pipe_ctx *pipe_ctx = params->setup_dpp_params.pipe_ctx;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;

	if (!plane_state)
		return;

	if (dpp && dpp->funcs->dpp_setup) {
		// program the input csc
		dpp->funcs->dpp_setup(dpp,
				plane_state->format,
				EXPANSION_MODE_ZERO,
				plane_state->input_csc_color_matrix,
				plane_state->color_space,
				NULL);
	}

	if (dpp && dpp->funcs->set_cursor_matrix) {
		dpp->funcs->set_cursor_matrix(dpp,
			plane_state->color_space,
			plane_state->cursor_csc_color_matrix);
	}
}

void hwss_program_bias_and_scale(union block_sequence_params *params)
{
	struct pipe_ctx *pipe_ctx = params->program_bias_and_scale_params.pipe_ctx;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct dc_bias_and_scale bns_params = plane_state->bias_and_scale;

	//TODO :for CNVC set scale and bias registers if necessary
	if (dpp->funcs->dpp_program_bias_and_scale) {
		dpp->funcs->dpp_program_bias_and_scale(dpp, &bns_params);
	}
}

void hwss_power_on_mpc_mem_pwr(union block_sequence_params *params)
{
	struct mpc *mpc = params->power_on_mpc_mem_pwr_params.mpc;
	int mpcc_id = params->power_on_mpc_mem_pwr_params.mpcc_id;
	bool power_on = params->power_on_mpc_mem_pwr_params.power_on;

	if (mpc->funcs->power_on_mpc_mem_pwr)
		mpc->funcs->power_on_mpc_mem_pwr(mpc, mpcc_id, power_on);
}

void hwss_set_output_csc(union block_sequence_params *params)
{
	struct mpc *mpc = params->set_output_csc_params.mpc;
	int opp_id = params->set_output_csc_params.opp_id;
	const uint16_t *matrix = params->set_output_csc_params.regval;
	enum mpc_output_csc_mode ocsc_mode = params->set_output_csc_params.ocsc_mode;

	if (mpc->funcs->set_output_csc != NULL)
		mpc->funcs->set_output_csc(mpc,
				opp_id,
				matrix,
				ocsc_mode);
}

void hwss_set_ocsc_default(union block_sequence_params *params)
{
	struct mpc *mpc = params->set_ocsc_default_params.mpc;
	int opp_id = params->set_ocsc_default_params.opp_id;
	enum dc_color_space colorspace = params->set_ocsc_default_params.color_space;
	enum mpc_output_csc_mode ocsc_mode = params->set_ocsc_default_params.ocsc_mode;

	if (mpc->funcs->set_ocsc_default != NULL)
		mpc->funcs->set_ocsc_default(mpc,
				opp_id,
				colorspace,
				ocsc_mode);
}

void hwss_subvp_save_surf_addr(union block_sequence_params *params)
{
	struct dc_dmub_srv *dc_dmub_srv = params->subvp_save_surf_addr.dc_dmub_srv;
	const struct dc_plane_address *addr = params->subvp_save_surf_addr.addr;
	uint8_t subvp_index = params->subvp_save_surf_addr.subvp_index;

	dc_dmub_srv_subvp_save_surf_addr(dc_dmub_srv, addr, subvp_index);
}

void get_surface_tile_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;
	/* Determine the overscan color based on the bottom-most plane's context */
	struct pipe_ctx *bottom_pipe_ctx  = pipe_ctx;

	while (bottom_pipe_ctx->bottom_pipe != NULL)
		bottom_pipe_ctx = bottom_pipe_ctx->bottom_pipe;

	switch (bottom_pipe_ctx->plane_state->tiling_info.gfx9.swizzle) {
	case DC_SW_LINEAR:
		/* LINEAR Surface - set border color to red */
		color->color_r_cr = color_value;
		break;
	default:
		break;
	}
}

/**
 * hwss_wait_for_all_blank_complete - wait for all active OPPs to finish pending blank
 * pattern updates
 *
 * @dc: [in] dc reference
 * @context: [in] hardware context in use
 */
void hwss_wait_for_all_blank_complete(struct dc *dc,
		struct dc_state *context)
{
	struct pipe_ctx *opp_head;
	struct dce_hwseq *hws = dc->hwseq;
	int i;

	if (!hws->funcs.wait_for_blank_complete)
		return;

	for (i = 0; i < MAX_PIPES; i++) {
		opp_head = &context->res_ctx.pipe_ctx[i];

		if (!resource_is_pipe_type(opp_head, OPP_HEAD) ||
				dc_state_get_pipe_subvp_type(context, opp_head) == SUBVP_PHANTOM)
			continue;

		hws->funcs.wait_for_blank_complete(opp_head->stream_res.opp);
	}
}

void hwss_wait_for_odm_update_pending_complete(struct dc *dc, struct dc_state *context)
{
	struct pipe_ctx *otg_master;
	struct timing_generator *tg;
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		otg_master = &context->res_ctx.pipe_ctx[i];
		if (!resource_is_pipe_type(otg_master, OTG_MASTER) ||
				dc_state_get_pipe_subvp_type(context, otg_master) == SUBVP_PHANTOM)
			continue;
		tg = otg_master->stream_res.tg;
		if (tg->funcs->wait_odm_doublebuffer_pending_clear)
			tg->funcs->wait_odm_doublebuffer_pending_clear(tg);
		if (tg->funcs->wait_otg_disable)
			tg->funcs->wait_otg_disable(tg);
	}

	/* ODM update may require to reprogram blank pattern for each OPP */
	hwss_wait_for_all_blank_complete(dc, context);
}

void hwss_wait_for_no_pipes_pending(struct dc *dc, struct dc_state *context)
{
	int i;
	for (i = 0; i < MAX_PIPES; i++) {
		int count = 0;
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->plane_state || dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM)
			continue;

		/* Timeout 100 ms */
		while (count < 100000) {
			/* Must set to false to start with, due to OR in update function */
			pipe->plane_state->status.is_flip_pending = false;
			dc->hwss.update_pending_status(pipe);
			if (!pipe->plane_state->status.is_flip_pending)
				break;
			udelay(1);
			count++;
		}
		ASSERT(!pipe->plane_state->status.is_flip_pending);
	}
}

void hwss_wait_for_outstanding_hw_updates(struct dc *dc, struct dc_state *dc_context)
{
/*
 * This function calls HWSS to wait for any potentially double buffered
 * operations to complete. It should be invoked as a pre-amble prior
 * to full update programming before asserting any HW locks.
 */
	int pipe_idx;
	int opp_inst;
	int opp_count = dc->res_pool->res_cap->num_opp;
	struct hubp *hubp;
	int mpcc_inst;
	const struct pipe_ctx *pipe_ctx;

	for (pipe_idx = 0; pipe_idx < dc->res_pool->pipe_count; pipe_idx++) {
		pipe_ctx = &dc_context->res_ctx.pipe_ctx[pipe_idx];

		if (!pipe_ctx->stream)
			continue;

		/* For full update we must wait for all double buffer updates, not just DRR updates. This
		 * is particularly important for minimal transitions. Only check for OTG_MASTER pipes,
		 * as non-OTG Master pipes share the same OTG as
		 */
		if (resource_is_pipe_type(pipe_ctx, OTG_MASTER) && dc->hwss.wait_for_all_pending_updates) {
			dc->hwss.wait_for_all_pending_updates(pipe_ctx);
		}

		hubp = pipe_ctx->plane_res.hubp;
		if (!hubp)
			continue;

		mpcc_inst = hubp->inst;
		// MPCC inst is equal to pipe index in practice
		for (opp_inst = 0; opp_inst < opp_count; opp_inst++) {
			if ((dc->res_pool->opps[opp_inst] != NULL) &&
				(dc->res_pool->opps[opp_inst]->mpcc_disconnect_pending[mpcc_inst])) {
				dc->res_pool->mpc->funcs->wait_for_idle(dc->res_pool->mpc, mpcc_inst);
				dc->res_pool->opps[opp_inst]->mpcc_disconnect_pending[mpcc_inst] = false;
				break;
			}
		}
	}
	hwss_wait_for_odm_update_pending_complete(dc, dc_context);
}

void hwss_process_outstanding_hw_updates(struct dc *dc, struct dc_state *dc_context)
{
	/* wait for outstanding updates */
	hwss_wait_for_outstanding_hw_updates(dc, dc_context);

	/* perform outstanding post update programming */
	if (dc->hwss.program_outstanding_updates)
		dc->hwss.program_outstanding_updates(dc, dc_context);
}
