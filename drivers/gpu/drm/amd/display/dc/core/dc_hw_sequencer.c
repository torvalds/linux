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
#include "opp.h"
#include "dsc.h"
#include "dchubbub.h"
#include "dccg.h"
#include "abm.h"
#include "dcn10/dcn10_hubbub.h"
#include "dce/dmub_hw_lock_mgr.h"

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

	/*
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
	bool is_dmub_lock_required = false;
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
	if (dc->hwss.dmub_hw_control_lock_fast) {
		is_dmub_lock_required = dc_state_is_fams2_in_use(dc, context) ||
					dmub_hw_lock_mgr_does_link_require_lock(dc, stream->link);

		block_sequence[*num_steps].params.dmub_hw_control_lock_fast_params.dc = dc;
		block_sequence[*num_steps].params.dmub_hw_control_lock_fast_params.lock = true;
		block_sequence[*num_steps].params.dmub_hw_control_lock_fast_params.is_required = is_dmub_lock_required;
		block_sequence[*num_steps].func = DMUB_HW_CONTROL_LOCK_FAST;
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
					block_sequence[*num_steps].params.set_flip_control_gsl_params.hubp = current_mpc_pipe->plane_res.hubp;
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
	if (dc->hwss.dmub_hw_control_lock_fast) {
		block_sequence[*num_steps].params.dmub_hw_control_lock_fast_params.dc = dc;
		block_sequence[*num_steps].params.dmub_hw_control_lock_fast_params.lock = false;
		block_sequence[*num_steps].params.dmub_hw_control_lock_fast_params.is_required = is_dmub_lock_required;
		block_sequence[*num_steps].func = DMUB_HW_CONTROL_LOCK_FAST;
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
				if (dc->hwss.program_cursor_offload_now) {
					block_sequence[*num_steps].params.program_cursor_update_now_params.dc = dc;
					block_sequence[*num_steps].params.program_cursor_update_now_params.pipe_ctx = current_mpc_pipe;
					block_sequence[*num_steps].func = PROGRAM_CURSOR_UPDATE_NOW;
					(*num_steps)++;
				}

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
			params->set_flip_control_gsl_params.hubp->funcs->hubp_set_flip_control_surface_gsl(
				params->set_flip_control_gsl_params.hubp,
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
		case DMUB_HW_CONTROL_LOCK_FAST:
			dc->hwss.dmub_hw_control_lock_fast(params);
			break;
		case HUBP_PROGRAM_SURFACE_CONFIG:
			hwss_program_surface_config(params);
			break;
		case HUBP_PROGRAM_MCACHE_ID:
			hwss_program_mcache_id_and_split_coordinate(params);
			break;
		case PROGRAM_CURSOR_UPDATE_NOW:
			dc->hwss.program_cursor_offload_now(
				params->program_cursor_update_now_params.dc,
				params->program_cursor_update_now_params.pipe_ctx);
			break;
		case HUBP_WAIT_PIPE_READ_START:
			params->hubp_wait_pipe_read_start_params.hubp->funcs->hubp_wait_pipe_read_start(
				params->hubp_wait_pipe_read_start_params.hubp);
			break;
		case HWS_APPLY_UPDATE_FLAGS_FOR_PHANTOM:
			dc->hwss.apply_update_flags_for_phantom(params->apply_update_flags_for_phantom_params.pipe_ctx);
			break;
		case HWS_UPDATE_PHANTOM_VP_POSITION:
			dc->hwss.update_phantom_vp_position(params->update_phantom_vp_position_params.dc,
				params->update_phantom_vp_position_params.context,
				params->update_phantom_vp_position_params.pipe_ctx);
			break;
		case OPTC_SET_ODM_COMBINE:
			hwss_set_odm_combine(params);
			break;
		case OPTC_SET_ODM_BYPASS:
			hwss_set_odm_bypass(params);
			break;
		case OPP_PIPE_CLOCK_CONTROL:
			hwss_opp_pipe_clock_control(params);
			break;
		case OPP_PROGRAM_LEFT_EDGE_EXTRA_PIXEL:
			hwss_opp_program_left_edge_extra_pixel(params);
			break;
		case DCCG_SET_DTO_DSCCLK:
			hwss_dccg_set_dto_dscclk(params);
			break;
		case DSC_SET_CONFIG:
			hwss_dsc_set_config(params);
			break;
		case DSC_ENABLE:
			hwss_dsc_enable(params);
			break;
		case TG_SET_DSC_CONFIG:
			hwss_tg_set_dsc_config(params);
			break;
		case DSC_DISCONNECT:
			hwss_dsc_disconnect(params);
			break;
		case DSC_READ_STATE:
			hwss_dsc_read_state(params);
			break;
		case DSC_CALCULATE_AND_SET_CONFIG:
			hwss_dsc_calculate_and_set_config(params);
			break;
		case DSC_ENABLE_WITH_OPP:
			hwss_dsc_enable_with_opp(params);
			break;
		case TG_PROGRAM_GLOBAL_SYNC:
			hwss_tg_program_global_sync(params);
			break;
		case TG_WAIT_FOR_STATE:
			hwss_tg_wait_for_state(params);
			break;
		case TG_SET_VTG_PARAMS:
			hwss_tg_set_vtg_params(params);
			break;
		case TG_SETUP_VERTICAL_INTERRUPT2:
			hwss_tg_setup_vertical_interrupt2(params);
			break;
		case DPP_SET_HDR_MULTIPLIER:
			hwss_dpp_set_hdr_multiplier(params);
			break;
		case HUBP_PROGRAM_DET_SIZE:
			hwss_program_det_size(params);
			break;
		case HUBP_PROGRAM_DET_SEGMENTS:
			hwss_program_det_segments(params);
			break;
		case OPP_SET_DYN_EXPANSION:
			hwss_opp_set_dyn_expansion(params);
			break;
		case OPP_PROGRAM_FMT:
			hwss_opp_program_fmt(params);
			break;
		case OPP_PROGRAM_BIT_DEPTH_REDUCTION:
			hwss_opp_program_bit_depth_reduction(params);
			break;
		case OPP_SET_DISP_PATTERN_GENERATOR:
			hwss_opp_set_disp_pattern_generator(params);
			break;
		case ABM_SET_PIPE:
			hwss_set_abm_pipe(params);
			break;
		case ABM_SET_LEVEL:
			hwss_set_abm_level(params);
			break;
		case ABM_SET_IMMEDIATE_DISABLE:
			hwss_set_abm_immediate_disable(params);
			break;
		case MPC_REMOVE_MPCC:
			hwss_mpc_remove_mpcc(params);
			break;
		case OPP_SET_MPCC_DISCONNECT_PENDING:
			hwss_opp_set_mpcc_disconnect_pending(params);
			break;
		case DC_SET_OPTIMIZED_REQUIRED:
			hwss_dc_set_optimized_required(params);
			break;
		case HUBP_DISCONNECT:
			hwss_hubp_disconnect(params);
			break;
		case HUBBUB_FORCE_PSTATE_CHANGE_CONTROL:
			hwss_hubbub_force_pstate_change_control(params);
			break;
		case TG_ENABLE_CRTC:
			hwss_tg_enable_crtc(params);
			break;
		case TG_SET_GSL:
			hwss_tg_set_gsl(params);
			break;
		case TG_SET_GSL_SOURCE_SELECT:
			hwss_tg_set_gsl_source_select(params);
			break;
		case HUBP_WAIT_FLIP_PENDING:
			hwss_hubp_wait_flip_pending(params);
			break;
		case TG_WAIT_DOUBLE_BUFFER_PENDING:
			hwss_tg_wait_double_buffer_pending(params);
			break;
		case UPDATE_FORCE_PSTATE:
			hwss_update_force_pstate(params);
			break;
		case HUBBUB_APPLY_DEDCN21_147_WA:
			hwss_hubbub_apply_dedcn21_147_wa(params);
			break;
		case HUBBUB_ALLOW_SELF_REFRESH_CONTROL:
			hwss_hubbub_allow_self_refresh_control(params);
			break;
		case TG_GET_FRAME_COUNT:
			hwss_tg_get_frame_count(params);
			break;
		case MPC_SET_DWB_MUX:
			hwss_mpc_set_dwb_mux(params);
			break;
		case MPC_DISABLE_DWB_MUX:
			hwss_mpc_disable_dwb_mux(params);
			break;
		case MCIF_WB_CONFIG_BUF:
			hwss_mcif_wb_config_buf(params);
			break;
		case MCIF_WB_CONFIG_ARB:
			hwss_mcif_wb_config_arb(params);
			break;
		case MCIF_WB_ENABLE:
			hwss_mcif_wb_enable(params);
			break;
		case MCIF_WB_DISABLE:
			hwss_mcif_wb_disable(params);
			break;
		case DWBC_ENABLE:
			hwss_dwbc_enable(params);
			break;
		case DWBC_DISABLE:
			hwss_dwbc_disable(params);
			break;
		case DWBC_UPDATE:
			hwss_dwbc_update(params);
			break;
		case HUBP_UPDATE_MALL_SEL:
			hwss_hubp_update_mall_sel(params);
			break;
		case HUBP_PREPARE_SUBVP_BUFFERING:
			hwss_hubp_prepare_subvp_buffering(params);
			break;
		case HUBP_SET_BLANK_EN:
			hwss_hubp_set_blank_en(params);
			break;
		case HUBP_DISABLE_CONTROL:
			hwss_hubp_disable_control(params);
			break;
		case HUBBUB_SOFT_RESET:
			hwss_hubbub_soft_reset(params);
			break;
		case HUBP_CLK_CNTL:
			hwss_hubp_clk_cntl(params);
			break;
		case HUBP_INIT:
			hwss_hubp_init(params);
			break;
		case HUBP_SET_VM_SYSTEM_APERTURE_SETTINGS:
			hwss_hubp_set_vm_system_aperture_settings(params);
			break;
		case HUBP_SET_FLIP_INT:
			hwss_hubp_set_flip_int(params);
			break;
		case DPP_DPPCLK_CONTROL:
			hwss_dpp_dppclk_control(params);
			break;
		case DISABLE_PHANTOM_CRTC:
			hwss_disable_phantom_crtc(params);
			break;
		case DSC_PG_STATUS:
			hwss_dsc_pg_status(params);
			break;
		case DSC_WAIT_DISCONNECT_PENDING_CLEAR:
			hwss_dsc_wait_disconnect_pending_clear(params);
			break;
		case DSC_DISABLE:
			hwss_dsc_disable(params);
			break;
		case DCCG_SET_REF_DSCCLK:
			hwss_dccg_set_ref_dscclk(params);
			break;
		case DPP_PG_CONTROL:
			hwss_dpp_pg_control(params);
			break;
		case HUBP_PG_CONTROL:
			hwss_hubp_pg_control(params);
			break;
		case HUBP_RESET:
			hwss_hubp_reset(params);
			break;
		case DPP_RESET:
			hwss_dpp_reset(params);
			break;
		case DPP_ROOT_CLOCK_CONTROL:
			hwss_dpp_root_clock_control(params);
			break;
		case DC_IP_REQUEST_CNTL:
			hwss_dc_ip_request_cntl(params);
			break;
		case DCCG_UPDATE_DPP_DTO:
			hwss_dccg_update_dpp_dto(params);
			break;
		case HUBP_VTG_SEL:
			hwss_hubp_vtg_sel(params);
			break;
		case HUBP_SETUP2:
			hwss_hubp_setup2(params);
			break;
		case HUBP_SETUP:
			hwss_hubp_setup(params);
			break;
		case HUBP_SET_UNBOUNDED_REQUESTING:
			hwss_hubp_set_unbounded_requesting(params);
			break;
		case HUBP_SETUP_INTERDEPENDENT2:
			hwss_hubp_setup_interdependent2(params);
			break;
		case HUBP_SETUP_INTERDEPENDENT:
			hwss_hubp_setup_interdependent(params);
			break;
		case DPP_SET_CURSOR_MATRIX:
			hwss_dpp_set_cursor_matrix(params);
			break;
		case MPC_UPDATE_BLENDING:
			hwss_mpc_update_blending(params);
			break;
		case MPC_ASSERT_IDLE_MPCC:
			hwss_mpc_assert_idle_mpcc(params);
			break;
		case MPC_INSERT_PLANE:
			hwss_mpc_insert_plane(params);
			break;
		case DPP_SET_SCALER:
			hwss_dpp_set_scaler(params);
			break;
		case HUBP_MEM_PROGRAM_VIEWPORT:
			hwss_hubp_mem_program_viewport(params);
			break;
		case ABORT_CURSOR_OFFLOAD_UPDATE:
			hwss_abort_cursor_offload_update(params);
			break;
		case SET_CURSOR_ATTRIBUTE:
			hwss_set_cursor_attribute(params);
			break;
		case SET_CURSOR_POSITION:
			hwss_set_cursor_position(params);
			break;
		case SET_CURSOR_SDR_WHITE_LEVEL:
			hwss_set_cursor_sdr_white_level(params);
			break;
		case PROGRAM_OUTPUT_CSC:
			hwss_program_output_csc(params);
			break;
		case HUBP_SET_BLANK:
			hwss_hubp_set_blank(params);
			break;
		case PHANTOM_HUBP_POST_ENABLE:
			hwss_phantom_hubp_post_enable(params);
			break;
		default:
			ASSERT(false);
			break;
		}
	}
}

/*
 * Helper function to add OPTC pipe control lock to block sequence
 */
void hwss_add_optc_pipe_control_lock(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool lock)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.pipe_control_lock_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.pipe_control_lock_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].params.pipe_control_lock_params.lock = lock;
		seq_state->steps[*seq_state->num_steps].func = OPTC_PIPE_CONTROL_LOCK;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HUBP set flip control GSL to block sequence
 */
void hwss_add_hubp_set_flip_control_gsl(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool flip_immediate)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.set_flip_control_gsl_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.set_flip_control_gsl_params.flip_immediate = flip_immediate;
		seq_state->steps[*seq_state->num_steps].func = HUBP_SET_FLIP_CONTROL_GSL;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HUBP program triplebuffer to block sequence
 */
void hwss_add_hubp_program_triplebuffer(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool enableTripleBuffer)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.program_triplebuffer_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.program_triplebuffer_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].params.program_triplebuffer_params.enableTripleBuffer = enableTripleBuffer;
		seq_state->steps[*seq_state->num_steps].func = HUBP_PROGRAM_TRIPLEBUFFER;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HUBP update plane address to block sequence
 */
void hwss_add_hubp_update_plane_addr(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.update_plane_addr_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.update_plane_addr_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].func = HUBP_UPDATE_PLANE_ADDR;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add DPP set input transfer function to block sequence
 */
void hwss_add_dpp_set_input_transfer_func(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_plane_state *plane_state)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.set_input_transfer_func_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.set_input_transfer_func_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].params.set_input_transfer_func_params.plane_state = plane_state;
		seq_state->steps[*seq_state->num_steps].func = DPP_SET_INPUT_TRANSFER_FUNC;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add DPP program gamut remap to block sequence
 */
void hwss_add_dpp_program_gamut_remap(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.program_gamut_remap_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].func = DPP_PROGRAM_GAMUT_REMAP;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add DPP program bias and scale to block sequence
 */
void hwss_add_dpp_program_bias_and_scale(struct block_sequence_state *seq_state, struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.program_bias_and_scale_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].func = DPP_PROGRAM_BIAS_AND_SCALE;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add OPTC program manual trigger to block sequence
 */
void hwss_add_optc_program_manual_trigger(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.program_manual_trigger_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].func = OPTC_PROGRAM_MANUAL_TRIGGER;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add DPP set output transfer function to block sequence
 */
void hwss_add_dpp_set_output_transfer_func(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_stream_state *stream)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.set_output_transfer_func_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.set_output_transfer_func_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].params.set_output_transfer_func_params.stream = stream;
		seq_state->steps[*seq_state->num_steps].func = DPP_SET_OUTPUT_TRANSFER_FUNC;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add MPC update visual confirm to block sequence
 */
void hwss_add_mpc_update_visual_confirm(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		int mpcc_id)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.update_visual_confirm_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.update_visual_confirm_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].params.update_visual_confirm_params.mpcc_id = mpcc_id;
		seq_state->steps[*seq_state->num_steps].func = MPC_UPDATE_VISUAL_CONFIRM;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add MPC power on MPC mem PWR to block sequence
 */
void hwss_add_mpc_power_on_mpc_mem_pwr(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		int mpcc_id,
		bool power_on)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.power_on_mpc_mem_pwr_params.mpc = mpc;
		seq_state->steps[*seq_state->num_steps].params.power_on_mpc_mem_pwr_params.mpcc_id = mpcc_id;
		seq_state->steps[*seq_state->num_steps].params.power_on_mpc_mem_pwr_params.power_on = power_on;
		seq_state->steps[*seq_state->num_steps].func = MPC_POWER_ON_MPC_MEM_PWR;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add MPC set output CSC to block sequence
 */
void hwss_add_mpc_set_output_csc(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		int opp_id,
		const uint16_t *regval,
		enum mpc_output_csc_mode ocsc_mode)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.set_output_csc_params.mpc = mpc;
		seq_state->steps[*seq_state->num_steps].params.set_output_csc_params.opp_id = opp_id;
		seq_state->steps[*seq_state->num_steps].params.set_output_csc_params.regval = regval;
		seq_state->steps[*seq_state->num_steps].params.set_output_csc_params.ocsc_mode = ocsc_mode;
		seq_state->steps[*seq_state->num_steps].func = MPC_SET_OUTPUT_CSC;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add MPC set OCSC default to block sequence
 */
void hwss_add_mpc_set_ocsc_default(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		int opp_id,
		enum dc_color_space colorspace,
		enum mpc_output_csc_mode ocsc_mode)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.set_ocsc_default_params.mpc = mpc;
		seq_state->steps[*seq_state->num_steps].params.set_ocsc_default_params.opp_id = opp_id;
		seq_state->steps[*seq_state->num_steps].params.set_ocsc_default_params.color_space = colorspace;
		seq_state->steps[*seq_state->num_steps].params.set_ocsc_default_params.ocsc_mode = ocsc_mode;
		seq_state->steps[*seq_state->num_steps].func = MPC_SET_OCSC_DEFAULT;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add DMUB send DMCUB command to block sequence
 */
void hwss_add_dmub_send_dmcub_cmd(struct block_sequence_state *seq_state,
		struct dc_context *ctx,
		union dmub_rb_cmd *cmd,
		enum dm_dmub_wait_type wait_type)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.send_dmcub_cmd_params.ctx = ctx;
		seq_state->steps[*seq_state->num_steps].params.send_dmcub_cmd_params.cmd = cmd;
		seq_state->steps[*seq_state->num_steps].params.send_dmcub_cmd_params.wait_type = wait_type;
		seq_state->steps[*seq_state->num_steps].func = DMUB_SEND_DMCUB_CMD;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add DMUB SubVP save surface address to block sequence
 */
void hwss_add_dmub_subvp_save_surf_addr(struct block_sequence_state *seq_state,
		struct dc_dmub_srv *dc_dmub_srv,
		struct dc_plane_address *addr,
		uint8_t subvp_index)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.subvp_save_surf_addr.dc_dmub_srv = dc_dmub_srv;
		seq_state->steps[*seq_state->num_steps].params.subvp_save_surf_addr.addr = addr;
		seq_state->steps[*seq_state->num_steps].params.subvp_save_surf_addr.subvp_index = subvp_index;
		seq_state->steps[*seq_state->num_steps].func = DMUB_SUBVP_SAVE_SURF_ADDR;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HUBP wait for DCC meta propagation to block sequence
 */
void hwss_add_hubp_wait_for_dcc_meta_prop(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *top_pipe_to_program)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.wait_for_dcc_meta_propagation_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.wait_for_dcc_meta_propagation_params.top_pipe_to_program = top_pipe_to_program;
		seq_state->steps[*seq_state->num_steps].func = HUBP_WAIT_FOR_DCC_META_PROP;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HUBP wait pipe read start to block sequence
 */
void hwss_add_hubp_wait_pipe_read_start(struct block_sequence_state *seq_state,
		struct hubp *hubp)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.hubp_wait_pipe_read_start_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].func = HUBP_WAIT_PIPE_READ_START;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HWS apply update flags for phantom to block sequence
 */
void hwss_add_hws_apply_update_flags_for_phantom(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.apply_update_flags_for_phantom_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].func = HWS_APPLY_UPDATE_FLAGS_FOR_PHANTOM;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HWS update phantom VP position to block sequence
 */
void hwss_add_hws_update_phantom_vp_position(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.update_phantom_vp_position_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.update_phantom_vp_position_params.context = context;
		seq_state->steps[*seq_state->num_steps].params.update_phantom_vp_position_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].func = HWS_UPDATE_PHANTOM_VP_POSITION;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add OPTC set ODM combine to block sequence
 */
void hwss_add_optc_set_odm_combine(struct block_sequence_state *seq_state,
		struct timing_generator *tg, int opp_inst[MAX_PIPES], int opp_head_count,
		int odm_slice_width, int last_odm_slice_width)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.set_odm_combine_params.tg = tg;
		memcpy(seq_state->steps[*seq_state->num_steps].params.set_odm_combine_params.opp_inst, opp_inst, sizeof(int) * MAX_PIPES);
		seq_state->steps[*seq_state->num_steps].params.set_odm_combine_params.opp_head_count = opp_head_count;
		seq_state->steps[*seq_state->num_steps].params.set_odm_combine_params.odm_slice_width = odm_slice_width;
		seq_state->steps[*seq_state->num_steps].params.set_odm_combine_params.last_odm_slice_width = last_odm_slice_width;
		seq_state->steps[*seq_state->num_steps].func = OPTC_SET_ODM_COMBINE;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add OPTC set ODM bypass to block sequence
 */
void hwss_add_optc_set_odm_bypass(struct block_sequence_state *seq_state,
		struct timing_generator *tg, struct dc_crtc_timing *timing)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.set_odm_bypass_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].params.set_odm_bypass_params.timing = timing;
		seq_state->steps[*seq_state->num_steps].func = OPTC_SET_ODM_BYPASS;
		(*seq_state->num_steps)++;
	}
}

void hwss_send_dmcub_cmd(union block_sequence_params *params)
{
	struct dc_context *ctx = params->send_dmcub_cmd_params.ctx;
	union dmub_rb_cmd *cmd = params->send_dmcub_cmd_params.cmd;
	enum dm_dmub_wait_type wait_type = params->send_dmcub_cmd_params.wait_type;

	dc_wake_and_execute_dmub_cmd(ctx, cmd, wait_type);
}

/*
 * Helper function to add TG program global sync to block sequence
 */
void hwss_add_tg_program_global_sync(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		int vready_offset,
		unsigned int vstartup_lines,
		unsigned int vupdate_offset_pixels,
		unsigned int vupdate_vupdate_width_pixels,
		unsigned int pstate_keepout_start_lines)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.tg_program_global_sync_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].params.tg_program_global_sync_params.vready_offset = vready_offset;
		seq_state->steps[*seq_state->num_steps].params.tg_program_global_sync_params.vstartup_lines = vstartup_lines;
		seq_state->steps[*seq_state->num_steps].params.tg_program_global_sync_params.vupdate_offset_pixels = vupdate_offset_pixels;
		seq_state->steps[*seq_state->num_steps].params.tg_program_global_sync_params.vupdate_vupdate_width_pixels = vupdate_vupdate_width_pixels;
		seq_state->steps[*seq_state->num_steps].params.tg_program_global_sync_params.pstate_keepout_start_lines = pstate_keepout_start_lines;
		seq_state->steps[*seq_state->num_steps].func = TG_PROGRAM_GLOBAL_SYNC;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add TG wait for state to block sequence
 */
void hwss_add_tg_wait_for_state(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		enum crtc_state state)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.tg_wait_for_state_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].params.tg_wait_for_state_params.state = state;
		seq_state->steps[*seq_state->num_steps].func = TG_WAIT_FOR_STATE;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add TG set VTG params to block sequence
 */
void hwss_add_tg_set_vtg_params(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		struct dc_crtc_timing *dc_crtc_timing,
		bool program_fp2)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.tg_set_vtg_params_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].params.tg_set_vtg_params_params.timing = dc_crtc_timing;
		seq_state->steps[*seq_state->num_steps].params.tg_set_vtg_params_params.program_fp2 = program_fp2;
		seq_state->steps[*seq_state->num_steps].func = TG_SET_VTG_PARAMS;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add TG setup vertical interrupt2 to block sequence
 */
void hwss_add_tg_setup_vertical_interrupt2(struct block_sequence_state *seq_state,
		struct timing_generator *tg, int start_line)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.tg_setup_vertical_interrupt2_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].params.tg_setup_vertical_interrupt2_params.start_line = start_line;
		seq_state->steps[*seq_state->num_steps].func = TG_SETUP_VERTICAL_INTERRUPT2;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add DPP set HDR multiplier to block sequence
 */
void hwss_add_dpp_set_hdr_multiplier(struct block_sequence_state *seq_state,
		struct dpp *dpp, uint32_t hw_mult)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.dpp_set_hdr_multiplier_params.dpp = dpp;
		seq_state->steps[*seq_state->num_steps].params.dpp_set_hdr_multiplier_params.hw_mult = hw_mult;
		seq_state->steps[*seq_state->num_steps].func = DPP_SET_HDR_MULTIPLIER;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HUBP program DET size to block sequence
 */
void hwss_add_hubp_program_det_size(struct block_sequence_state *seq_state,
		struct hubbub *hubbub,
		unsigned int hubp_inst,
		unsigned int det_buffer_size_kb)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.program_det_size_params.hubbub = hubbub;
		seq_state->steps[*seq_state->num_steps].params.program_det_size_params.hubp_inst = hubp_inst;
		seq_state->steps[*seq_state->num_steps].params.program_det_size_params.det_buffer_size_kb = det_buffer_size_kb;
		seq_state->steps[*seq_state->num_steps].func = HUBP_PROGRAM_DET_SIZE;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_program_mcache_id(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		struct dml2_hubp_pipe_mcache_regs *mcache_regs)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.program_mcache_id_and_split_coordinate.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.program_mcache_id_and_split_coordinate.mcache_regs = mcache_regs;
		seq_state->steps[*seq_state->num_steps].func = HUBP_PROGRAM_MCACHE_ID;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubbub_force_pstate_change_control(struct block_sequence_state *seq_state,
		struct hubbub *hubbub,
		bool enable,
		bool wait)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.hubbub_force_pstate_change_control_params.hubbub = hubbub;
		seq_state->steps[*seq_state->num_steps].params.hubbub_force_pstate_change_control_params.enable = enable;
		seq_state->steps[*seq_state->num_steps].params.hubbub_force_pstate_change_control_params.wait = wait;
		seq_state->steps[*seq_state->num_steps].func = HUBBUB_FORCE_PSTATE_CHANGE_CONTROL;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HUBP program DET segments to block sequence
 */
void hwss_add_hubp_program_det_segments(struct block_sequence_state *seq_state,
		struct hubbub *hubbub,
		unsigned int hubp_inst,
		unsigned int det_size)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.program_det_segments_params.hubbub = hubbub;
		seq_state->steps[*seq_state->num_steps].params.program_det_segments_params.hubp_inst = hubp_inst;
		seq_state->steps[*seq_state->num_steps].params.program_det_segments_params.det_size = det_size;
		seq_state->steps[*seq_state->num_steps].func = HUBP_PROGRAM_DET_SEGMENTS;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add OPP set dynamic expansion to block sequence
 */
void hwss_add_opp_set_dyn_expansion(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp,
		enum dc_color_space color_space,
		enum dc_color_depth color_depth,
		enum signal_type signal)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.opp_set_dyn_expansion_params.opp = opp;
		seq_state->steps[*seq_state->num_steps].params.opp_set_dyn_expansion_params.color_space = color_space;
		seq_state->steps[*seq_state->num_steps].params.opp_set_dyn_expansion_params.color_depth = color_depth;
		seq_state->steps[*seq_state->num_steps].params.opp_set_dyn_expansion_params.signal = signal;
		seq_state->steps[*seq_state->num_steps].func = OPP_SET_DYN_EXPANSION;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add OPP program FMT to block sequence
 */
void hwss_add_opp_program_fmt(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp,
		struct bit_depth_reduction_params *fmt_bit_depth,
		struct clamping_and_pixel_encoding_params *clamping)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.opp_program_fmt_params.opp = opp;
		seq_state->steps[*seq_state->num_steps].params.opp_program_fmt_params.fmt_bit_depth = fmt_bit_depth;
		seq_state->steps[*seq_state->num_steps].params.opp_program_fmt_params.clamping = clamping;
		seq_state->steps[*seq_state->num_steps].func = OPP_PROGRAM_FMT;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_opp_program_left_edge_extra_pixel(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp,
		enum dc_pixel_encoding pixel_encoding,
		bool is_otg_master)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = OPP_PROGRAM_LEFT_EDGE_EXTRA_PIXEL;
		seq_state->steps[*seq_state->num_steps].params.opp_program_left_edge_extra_pixel_params.opp = opp;
		seq_state->steps[*seq_state->num_steps].params.opp_program_left_edge_extra_pixel_params.pixel_encoding = pixel_encoding;
		seq_state->steps[*seq_state->num_steps].params.opp_program_left_edge_extra_pixel_params.is_otg_master = is_otg_master;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add ABM set pipe to block sequence
 */
void hwss_add_abm_set_pipe(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.set_abm_pipe_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.set_abm_pipe_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].func = ABM_SET_PIPE;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add ABM set level to block sequence
 */
void hwss_add_abm_set_level(struct block_sequence_state *seq_state,
		struct abm *abm,
		uint32_t abm_level)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.set_abm_level_params.abm = abm;
		seq_state->steps[*seq_state->num_steps].params.set_abm_level_params.abm_level = abm_level;
		seq_state->steps[*seq_state->num_steps].func = ABM_SET_LEVEL;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add TG enable CRTC to block sequence
 */
void hwss_add_tg_enable_crtc(struct block_sequence_state *seq_state,
		struct timing_generator *tg)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.tg_enable_crtc_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].func = TG_ENABLE_CRTC;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HUBP wait flip pending to block sequence
 */
void hwss_add_hubp_wait_flip_pending(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		unsigned int timeout_us,
		unsigned int polling_interval_us)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.hubp_wait_flip_pending_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_wait_flip_pending_params.timeout_us = timeout_us;
		seq_state->steps[*seq_state->num_steps].params.hubp_wait_flip_pending_params.polling_interval_us = polling_interval_us;
		seq_state->steps[*seq_state->num_steps].func = HUBP_WAIT_FLIP_PENDING;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add TG wait double buffer pending to block sequence
 */
void hwss_add_tg_wait_double_buffer_pending(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		unsigned int timeout_us,
		unsigned int polling_interval_us)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].params.tg_wait_double_buffer_pending_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].params.tg_wait_double_buffer_pending_params.timeout_us = timeout_us;
		seq_state->steps[*seq_state->num_steps].params.tg_wait_double_buffer_pending_params.polling_interval_us = polling_interval_us;
		seq_state->steps[*seq_state->num_steps].func = TG_WAIT_DOUBLE_BUFFER_PENDING;
		(*seq_state->num_steps)++;
	}
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
}

void hwss_program_bias_and_scale(union block_sequence_params *params)
{
	struct pipe_ctx *pipe_ctx = params->program_bias_and_scale_params.pipe_ctx;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct dc_bias_and_scale bns_params = plane_state->bias_and_scale;

	//TODO :for CNVC set scale and bias registers if necessary
	if (dpp->funcs->dpp_program_bias_and_scale)
		dpp->funcs->dpp_program_bias_and_scale(dpp, &bns_params);
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

void hwss_program_surface_config(union block_sequence_params *params)
{
	struct hubp *hubp = params->program_surface_config_params.hubp;
	enum surface_pixel_format format = params->program_surface_config_params.format;
	struct dc_tiling_info *tiling_info = params->program_surface_config_params.tiling_info;
	struct plane_size size = params->program_surface_config_params.plane_size;
	enum dc_rotation_angle rotation = params->program_surface_config_params.rotation;
	struct dc_plane_dcc_param *dcc = params->program_surface_config_params.dcc;
	bool horizontal_mirror = params->program_surface_config_params.horizontal_mirror;
	int compat_level = params->program_surface_config_params.compat_level;

	hubp->funcs->hubp_program_surface_config(
		hubp,
		format,
		tiling_info,
		&size,
		rotation,
		dcc,
		horizontal_mirror,
		compat_level);

	hubp->power_gated = false;
}

void hwss_program_mcache_id_and_split_coordinate(union block_sequence_params *params)
{
	struct hubp *hubp = params->program_mcache_id_and_split_coordinate.hubp;
	struct dml2_hubp_pipe_mcache_regs *mcache_regs = params->program_mcache_id_and_split_coordinate.mcache_regs;

	hubp->funcs->hubp_program_mcache_id_and_split_coordinate(hubp, mcache_regs);

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

void hwss_set_odm_combine(union block_sequence_params *params)
{
	struct timing_generator *tg = params->set_odm_combine_params.tg;
	int *opp_inst = params->set_odm_combine_params.opp_inst;
	int opp_head_count = params->set_odm_combine_params.opp_head_count;
	int odm_slice_width = params->set_odm_combine_params.odm_slice_width;
	int last_odm_slice_width = params->set_odm_combine_params.last_odm_slice_width;

	if (tg && tg->funcs->set_odm_combine)
		tg->funcs->set_odm_combine(tg, opp_inst, opp_head_count,
				odm_slice_width, last_odm_slice_width);
}

void hwss_set_odm_bypass(union block_sequence_params *params)
{
	struct timing_generator *tg = params->set_odm_bypass_params.tg;
	const struct dc_crtc_timing *timing = params->set_odm_bypass_params.timing;

	if (tg && tg->funcs->set_odm_bypass)
		tg->funcs->set_odm_bypass(tg, timing);
}

void hwss_opp_pipe_clock_control(union block_sequence_params *params)
{
	struct output_pixel_processor *opp = params->opp_pipe_clock_control_params.opp;
	bool enable = params->opp_pipe_clock_control_params.enable;

	if (opp && opp->funcs->opp_pipe_clock_control)
		opp->funcs->opp_pipe_clock_control(opp, enable);
}

void hwss_opp_program_left_edge_extra_pixel(union block_sequence_params *params)
{
	struct output_pixel_processor *opp = params->opp_program_left_edge_extra_pixel_params.opp;
	enum dc_pixel_encoding pixel_encoding = params->opp_program_left_edge_extra_pixel_params.pixel_encoding;
	bool is_otg_master = params->opp_program_left_edge_extra_pixel_params.is_otg_master;

	if (opp && opp->funcs->opp_program_left_edge_extra_pixel)
		opp->funcs->opp_program_left_edge_extra_pixel(opp, pixel_encoding, is_otg_master);
}

void hwss_dccg_set_dto_dscclk(union block_sequence_params *params)
{
	struct dccg *dccg = params->dccg_set_dto_dscclk_params.dccg;
	int inst = params->dccg_set_dto_dscclk_params.inst;
	int num_slices_h = params->dccg_set_dto_dscclk_params.num_slices_h;

	if (dccg && dccg->funcs->set_dto_dscclk)
		dccg->funcs->set_dto_dscclk(dccg, inst, num_slices_h);
}

void hwss_dsc_set_config(union block_sequence_params *params)
{
	struct display_stream_compressor *dsc = params->dsc_set_config_params.dsc;
	struct dsc_config *dsc_cfg = params->dsc_set_config_params.dsc_cfg;
	struct dsc_optc_config *dsc_optc_cfg = params->dsc_set_config_params.dsc_optc_cfg;

	if (dsc && dsc->funcs->dsc_set_config)
		dsc->funcs->dsc_set_config(dsc, dsc_cfg, dsc_optc_cfg);
}

void hwss_dsc_enable(union block_sequence_params *params)
{
	struct display_stream_compressor *dsc = params->dsc_enable_params.dsc;
	int opp_inst = params->dsc_enable_params.opp_inst;

	if (dsc && dsc->funcs->dsc_enable)
		dsc->funcs->dsc_enable(dsc, opp_inst);
}

void hwss_tg_set_dsc_config(union block_sequence_params *params)
{
	struct timing_generator *tg = params->tg_set_dsc_config_params.tg;
	enum optc_dsc_mode optc_dsc_mode = OPTC_DSC_DISABLED;
	uint32_t bytes_per_pixel = 0;
	uint32_t slice_width = 0;

	if (params->tg_set_dsc_config_params.enable) {
		struct dsc_optc_config *dsc_optc_cfg = params->tg_set_dsc_config_params.dsc_optc_cfg;

		if (dsc_optc_cfg) {
			bytes_per_pixel = dsc_optc_cfg->bytes_per_pixel;
			slice_width = dsc_optc_cfg->slice_width;
			optc_dsc_mode = dsc_optc_cfg->is_pixel_format_444 ?
				OPTC_DSC_ENABLED_444 : OPTC_DSC_ENABLED_NATIVE_SUBSAMPLED;
		}
	}

	if (tg && tg->funcs->set_dsc_config)
		tg->funcs->set_dsc_config(tg, optc_dsc_mode, bytes_per_pixel, slice_width);
}

void hwss_dsc_disconnect(union block_sequence_params *params)
{
	struct display_stream_compressor *dsc = params->dsc_disconnect_params.dsc;

	if (dsc && dsc->funcs->dsc_disconnect)
		dsc->funcs->dsc_disconnect(dsc);
}

void hwss_dsc_read_state(union block_sequence_params *params)
{
	struct display_stream_compressor *dsc = params->dsc_read_state_params.dsc;
	struct dcn_dsc_state *dsc_state = params->dsc_read_state_params.dsc_state;

	if (dsc && dsc->funcs->dsc_read_state)
		dsc->funcs->dsc_read_state(dsc, dsc_state);
}

void hwss_dsc_calculate_and_set_config(union block_sequence_params *params)
{
	struct pipe_ctx *pipe_ctx = params->dsc_calculate_and_set_config_params.pipe_ctx;
	struct pipe_ctx *top_pipe = pipe_ctx;
	bool enable = params->dsc_calculate_and_set_config_params.enable;
	int opp_cnt = params->dsc_calculate_and_set_config_params.opp_cnt;

	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	struct dc_stream_state *stream = pipe_ctx->stream;

	if (!dsc || !enable)
		return;

	/* Calculate DSC configuration - extracted from dcn32_update_dsc_on_stream */
	struct dsc_config dsc_cfg;

	while (top_pipe->prev_odm_pipe)
		top_pipe = top_pipe->prev_odm_pipe;

	dsc_cfg.pic_width = (stream->timing.h_addressable + top_pipe->dsc_padding_params.dsc_hactive_padding +
			stream->timing.h_border_left + stream->timing.h_border_right) / opp_cnt;
	dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top + stream->timing.v_border_bottom;
	dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
	dsc_cfg.color_depth = stream->timing.display_color_depth;
	dsc_cfg.is_odm = top_pipe->next_odm_pipe ? true : false;
	dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;
	dsc_cfg.dc_dsc_cfg.num_slices_h /= opp_cnt;
	dsc_cfg.dsc_padding = top_pipe->dsc_padding_params.dsc_hactive_padding;

	/* Set DSC configuration */
	if (dsc->funcs->dsc_set_config)
		dsc->funcs->dsc_set_config(dsc, &dsc_cfg,
			&params->dsc_calculate_and_set_config_params.dsc_optc_cfg);
}

void hwss_dsc_enable_with_opp(union block_sequence_params *params)
{
	struct pipe_ctx *pipe_ctx = params->dsc_enable_with_opp_params.pipe_ctx;
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;

	if (dsc && dsc->funcs->dsc_enable)
		dsc->funcs->dsc_enable(dsc, pipe_ctx->stream_res.opp->inst);
}

void hwss_tg_program_global_sync(union block_sequence_params *params)
{
	struct timing_generator *tg = params->tg_program_global_sync_params.tg;
	int vready_offset = params->tg_program_global_sync_params.vready_offset;
	unsigned int vstartup_lines = params->tg_program_global_sync_params.vstartup_lines;
	unsigned int vupdate_offset_pixels = params->tg_program_global_sync_params.vupdate_offset_pixels;
	unsigned int vupdate_vupdate_width_pixels = params->tg_program_global_sync_params.vupdate_vupdate_width_pixels;
	unsigned int pstate_keepout_start_lines = params->tg_program_global_sync_params.pstate_keepout_start_lines;

	if (tg->funcs->program_global_sync) {
		tg->funcs->program_global_sync(tg, vready_offset, vstartup_lines,
			vupdate_offset_pixels, vupdate_vupdate_width_pixels, pstate_keepout_start_lines);
	}
}

void hwss_tg_wait_for_state(union block_sequence_params *params)
{
	struct timing_generator *tg = params->tg_wait_for_state_params.tg;
	enum crtc_state state = params->tg_wait_for_state_params.state;

	if (tg->funcs->wait_for_state)
		tg->funcs->wait_for_state(tg, state);
}

void hwss_tg_set_vtg_params(union block_sequence_params *params)
{
	struct timing_generator *tg = params->tg_set_vtg_params_params.tg;
	struct dc_crtc_timing *timing = params->tg_set_vtg_params_params.timing;
	bool program_fp2 = params->tg_set_vtg_params_params.program_fp2;

	if (tg->funcs->set_vtg_params)
		tg->funcs->set_vtg_params(tg, timing, program_fp2);
}

void hwss_tg_setup_vertical_interrupt2(union block_sequence_params *params)
{
	struct timing_generator *tg = params->tg_setup_vertical_interrupt2_params.tg;
	int start_line = params->tg_setup_vertical_interrupt2_params.start_line;

	if (tg->funcs->setup_vertical_interrupt2)
		tg->funcs->setup_vertical_interrupt2(tg, start_line);
}

void hwss_dpp_set_hdr_multiplier(union block_sequence_params *params)
{
	struct dpp *dpp = params->dpp_set_hdr_multiplier_params.dpp;
	uint32_t hw_mult = params->dpp_set_hdr_multiplier_params.hw_mult;

	if (dpp->funcs->dpp_set_hdr_multiplier)
		dpp->funcs->dpp_set_hdr_multiplier(dpp, hw_mult);
}

void hwss_program_det_size(union block_sequence_params *params)
{
	struct hubbub *hubbub = params->program_det_size_params.hubbub;
	unsigned int hubp_inst = params->program_det_size_params.hubp_inst;
	unsigned int det_buffer_size_kb = params->program_det_size_params.det_buffer_size_kb;

	if (hubbub->funcs->program_det_size)
		hubbub->funcs->program_det_size(hubbub, hubp_inst, det_buffer_size_kb);
}

void hwss_program_det_segments(union block_sequence_params *params)
{
	struct hubbub *hubbub = params->program_det_segments_params.hubbub;
	unsigned int hubp_inst = params->program_det_segments_params.hubp_inst;
	unsigned int det_size = params->program_det_segments_params.det_size;

	if (hubbub->funcs->program_det_segments)
		hubbub->funcs->program_det_segments(hubbub, hubp_inst, det_size);
}

void hwss_opp_set_dyn_expansion(union block_sequence_params *params)
{
	struct output_pixel_processor *opp = params->opp_set_dyn_expansion_params.opp;
	enum dc_color_space color_space = params->opp_set_dyn_expansion_params.color_space;
	enum dc_color_depth color_depth = params->opp_set_dyn_expansion_params.color_depth;
	enum signal_type signal = params->opp_set_dyn_expansion_params.signal;

	if (opp->funcs->opp_set_dyn_expansion)
		opp->funcs->opp_set_dyn_expansion(opp, color_space, color_depth, signal);
}

void hwss_opp_program_fmt(union block_sequence_params *params)
{
	struct output_pixel_processor *opp = params->opp_program_fmt_params.opp;
	struct bit_depth_reduction_params *fmt_bit_depth = params->opp_program_fmt_params.fmt_bit_depth;
	struct clamping_and_pixel_encoding_params *clamping = params->opp_program_fmt_params.clamping;

	if (opp->funcs->opp_program_fmt)
		opp->funcs->opp_program_fmt(opp, fmt_bit_depth, clamping);
}

void hwss_opp_program_bit_depth_reduction(union block_sequence_params *params)
{
	struct output_pixel_processor *opp = params->opp_program_bit_depth_reduction_params.opp;
	bool use_default_params = params->opp_program_bit_depth_reduction_params.use_default_params;
	struct pipe_ctx *pipe_ctx = params->opp_program_bit_depth_reduction_params.pipe_ctx;
	struct bit_depth_reduction_params bit_depth_params;

	if (use_default_params)
		memset(&bit_depth_params, 0, sizeof(bit_depth_params));
	else
		resource_build_bit_depth_reduction_params(pipe_ctx->stream, &bit_depth_params);

	if (opp->funcs->opp_program_bit_depth_reduction)
		opp->funcs->opp_program_bit_depth_reduction(opp, &bit_depth_params);
}

void hwss_opp_set_disp_pattern_generator(union block_sequence_params *params)
{
	struct output_pixel_processor *opp = params->opp_set_disp_pattern_generator_params.opp;
	enum controller_dp_test_pattern test_pattern = params->opp_set_disp_pattern_generator_params.test_pattern;
	enum controller_dp_color_space color_space = params->opp_set_disp_pattern_generator_params.color_space;
	enum dc_color_depth color_depth = params->opp_set_disp_pattern_generator_params.color_depth;
	struct tg_color *solid_color = params->opp_set_disp_pattern_generator_params.use_solid_color ?
		&params->opp_set_disp_pattern_generator_params.solid_color : NULL;
	int width = params->opp_set_disp_pattern_generator_params.width;
	int height = params->opp_set_disp_pattern_generator_params.height;
	int offset = params->opp_set_disp_pattern_generator_params.offset;

	if (opp && opp->funcs->opp_set_disp_pattern_generator) {
		opp->funcs->opp_set_disp_pattern_generator(opp, test_pattern, color_space,
			color_depth, solid_color, width, height, offset);
	}
}

void hwss_set_abm_pipe(union block_sequence_params *params)
{
	struct dc *dc = params->set_abm_pipe_params.dc;
	struct pipe_ctx *pipe_ctx = params->set_abm_pipe_params.pipe_ctx;

	dc->hwss.set_pipe(pipe_ctx);
}

void hwss_set_abm_level(union block_sequence_params *params)
{
	struct abm *abm = params->set_abm_level_params.abm;
	unsigned int abm_level = params->set_abm_level_params.abm_level;

	if (abm->funcs->set_abm_level)
		abm->funcs->set_abm_level(abm, abm_level);
}

void hwss_set_abm_immediate_disable(union block_sequence_params *params)
{
	struct dc *dc = params->set_abm_immediate_disable_params.dc;
	struct pipe_ctx *pipe_ctx = params->set_abm_immediate_disable_params.pipe_ctx;

	if (dc && dc->hwss.set_abm_immediate_disable)
		dc->hwss.set_abm_immediate_disable(pipe_ctx);
}

void hwss_mpc_remove_mpcc(union block_sequence_params *params)
{
	struct mpc *mpc = params->mpc_remove_mpcc_params.mpc;
	struct mpc_tree *mpc_tree_params = params->mpc_remove_mpcc_params.mpc_tree_params;
	struct mpcc *mpcc_to_remove = params->mpc_remove_mpcc_params.mpcc_to_remove;

	mpc->funcs->remove_mpcc(mpc, mpc_tree_params, mpcc_to_remove);
}

void hwss_opp_set_mpcc_disconnect_pending(union block_sequence_params *params)
{
	struct output_pixel_processor *opp = params->opp_set_mpcc_disconnect_pending_params.opp;
	int mpcc_inst = params->opp_set_mpcc_disconnect_pending_params.mpcc_inst;
	bool pending = params->opp_set_mpcc_disconnect_pending_params.pending;

	opp->mpcc_disconnect_pending[mpcc_inst] = pending;
}

void hwss_dc_set_optimized_required(union block_sequence_params *params)
{
	struct dc *dc = params->dc_set_optimized_required_params.dc;
	bool optimized_required = params->dc_set_optimized_required_params.optimized_required;

	dc->optimized_required = optimized_required;
}

void hwss_hubp_disconnect(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_disconnect_params.hubp;

	if (hubp->funcs->hubp_disconnect)
		hubp->funcs->hubp_disconnect(hubp);
}

void hwss_hubbub_force_pstate_change_control(union block_sequence_params *params)
{
	struct hubbub *hubbub = params->hubbub_force_pstate_change_control_params.hubbub;
	bool enable = params->hubbub_force_pstate_change_control_params.enable;
	bool wait = params->hubbub_force_pstate_change_control_params.wait;

	if (hubbub->funcs->force_pstate_change_control) {
		hubbub->funcs->force_pstate_change_control(hubbub, enable, wait);
		/* Add delay when enabling pstate change control */
		if (enable)
			udelay(500);
	}
}

void hwss_tg_enable_crtc(union block_sequence_params *params)
{
	struct timing_generator *tg = params->tg_enable_crtc_params.tg;

	if (tg->funcs->enable_crtc)
		tg->funcs->enable_crtc(tg);
}

void hwss_tg_set_gsl(union block_sequence_params *params)
{
	struct timing_generator *tg = params->tg_set_gsl_params.tg;
	struct gsl_params *gsl = &params->tg_set_gsl_params.gsl;

	if (tg->funcs->set_gsl)
		tg->funcs->set_gsl(tg, gsl);
}

void hwss_tg_set_gsl_source_select(union block_sequence_params *params)
{
	struct timing_generator *tg = params->tg_set_gsl_source_select_params.tg;
	int group_idx = params->tg_set_gsl_source_select_params.group_idx;
	uint32_t gsl_ready_signal = params->tg_set_gsl_source_select_params.gsl_ready_signal;

	if (tg->funcs->set_gsl_source_select)
		tg->funcs->set_gsl_source_select(tg, group_idx, gsl_ready_signal);
}

void hwss_hubp_wait_flip_pending(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_wait_flip_pending_params.hubp;
	unsigned int timeout_us = params->hubp_wait_flip_pending_params.timeout_us;
	unsigned int polling_interval_us = params->hubp_wait_flip_pending_params.polling_interval_us;
	int j = 0;

	for (j = 0; j < timeout_us / polling_interval_us
		&& hubp->funcs->hubp_is_flip_pending(hubp); j++)
		udelay(polling_interval_us);
}

void hwss_tg_wait_double_buffer_pending(union block_sequence_params *params)
{
	struct timing_generator *tg = params->tg_wait_double_buffer_pending_params.tg;
	unsigned int timeout_us = params->tg_wait_double_buffer_pending_params.timeout_us;
	unsigned int polling_interval_us = params->tg_wait_double_buffer_pending_params.polling_interval_us;
	int j = 0;

	if (tg->funcs->get_optc_double_buffer_pending) {
		for (j = 0; j < timeout_us / polling_interval_us
			&& tg->funcs->get_optc_double_buffer_pending(tg); j++)
			udelay(polling_interval_us);
	}
}

void hwss_update_force_pstate(union block_sequence_params *params)
{
	struct dc *dc = params->update_force_pstate_params.dc;
	struct dc_state *context = params->update_force_pstate_params.context;
	struct dce_hwseq *hwseq = dc->hwseq;

	if (hwseq->funcs.update_force_pstate)
		hwseq->funcs.update_force_pstate(dc, context);
}

void hwss_hubbub_apply_dedcn21_147_wa(union block_sequence_params *params)
{
	struct hubbub *hubbub = params->hubbub_apply_dedcn21_147_wa_params.hubbub;

	hubbub->funcs->apply_DEDCN21_147_wa(hubbub);
}

void hwss_hubbub_allow_self_refresh_control(union block_sequence_params *params)
{
	struct hubbub *hubbub = params->hubbub_allow_self_refresh_control_params.hubbub;
	bool allow = params->hubbub_allow_self_refresh_control_params.allow;

	hubbub->funcs->allow_self_refresh_control(hubbub, allow);

	if (!allow && params->hubbub_allow_self_refresh_control_params.disallow_self_refresh_applied)
		*params->hubbub_allow_self_refresh_control_params.disallow_self_refresh_applied = true;
}

void hwss_tg_get_frame_count(union block_sequence_params *params)
{
	struct timing_generator *tg = params->tg_get_frame_count_params.tg;
	unsigned int *frame_count = params->tg_get_frame_count_params.frame_count;

	*frame_count = tg->funcs->get_frame_count(tg);
}

void hwss_mpc_set_dwb_mux(union block_sequence_params *params)
{
	struct mpc *mpc = params->mpc_set_dwb_mux_params.mpc;
	int dwb_id = params->mpc_set_dwb_mux_params.dwb_id;
	int mpcc_id = params->mpc_set_dwb_mux_params.mpcc_id;

	if (mpc->funcs->set_dwb_mux)
		mpc->funcs->set_dwb_mux(mpc, dwb_id, mpcc_id);
}

void hwss_mpc_disable_dwb_mux(union block_sequence_params *params)
{
	struct mpc *mpc = params->mpc_disable_dwb_mux_params.mpc;
	unsigned int dwb_id = params->mpc_disable_dwb_mux_params.dwb_id;

	if (mpc->funcs->disable_dwb_mux)
		mpc->funcs->disable_dwb_mux(mpc, dwb_id);
}

void hwss_mcif_wb_config_buf(union block_sequence_params *params)
{
	struct mcif_wb *mcif_wb = params->mcif_wb_config_buf_params.mcif_wb;
	struct mcif_buf_params *mcif_buf_params = params->mcif_wb_config_buf_params.mcif_buf_params;
	unsigned int dest_height = params->mcif_wb_config_buf_params.dest_height;

	if (mcif_wb->funcs->config_mcif_buf)
		mcif_wb->funcs->config_mcif_buf(mcif_wb, mcif_buf_params, dest_height);
}

void hwss_mcif_wb_config_arb(union block_sequence_params *params)
{
	struct mcif_wb *mcif_wb = params->mcif_wb_config_arb_params.mcif_wb;
	struct mcif_arb_params *mcif_arb_params = params->mcif_wb_config_arb_params.mcif_arb_params;

	if (mcif_wb->funcs->config_mcif_arb)
		mcif_wb->funcs->config_mcif_arb(mcif_wb, mcif_arb_params);
}

void hwss_mcif_wb_enable(union block_sequence_params *params)
{
	struct mcif_wb *mcif_wb = params->mcif_wb_enable_params.mcif_wb;

	if (mcif_wb->funcs->enable_mcif)
		mcif_wb->funcs->enable_mcif(mcif_wb);
}

void hwss_mcif_wb_disable(union block_sequence_params *params)
{
	struct mcif_wb *mcif_wb = params->mcif_wb_disable_params.mcif_wb;

	if (mcif_wb->funcs->disable_mcif)
		mcif_wb->funcs->disable_mcif(mcif_wb);
}

void hwss_dwbc_enable(union block_sequence_params *params)
{
	struct dwbc *dwb = params->dwbc_enable_params.dwb;
	struct dc_dwb_params *dwb_params = params->dwbc_enable_params.dwb_params;

	if (dwb->funcs->enable)
		dwb->funcs->enable(dwb, dwb_params);
}

void hwss_dwbc_disable(union block_sequence_params *params)
{
	struct dwbc *dwb = params->dwbc_disable_params.dwb;

	if (dwb->funcs->disable)
		dwb->funcs->disable(dwb);
}

void hwss_dwbc_update(union block_sequence_params *params)
{
	struct dwbc *dwb = params->dwbc_update_params.dwb;
	struct dc_dwb_params *dwb_params = params->dwbc_update_params.dwb_params;

	if (dwb->funcs->update)
		dwb->funcs->update(dwb, dwb_params);
}

void hwss_hubp_update_mall_sel(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_update_mall_sel_params.hubp;
	uint32_t mall_sel = params->hubp_update_mall_sel_params.mall_sel;
	bool cache_cursor = params->hubp_update_mall_sel_params.cache_cursor;

	if (hubp && hubp->funcs->hubp_update_mall_sel)
		hubp->funcs->hubp_update_mall_sel(hubp, mall_sel, cache_cursor);
}

void hwss_hubp_prepare_subvp_buffering(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_prepare_subvp_buffering_params.hubp;
	bool enable = params->hubp_prepare_subvp_buffering_params.enable;

	if (hubp && hubp->funcs->hubp_prepare_subvp_buffering)
		hubp->funcs->hubp_prepare_subvp_buffering(hubp, enable);
}

void hwss_hubp_set_blank_en(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_set_blank_en_params.hubp;
	bool enable = params->hubp_set_blank_en_params.enable;

	if (hubp && hubp->funcs->set_hubp_blank_en)
		hubp->funcs->set_hubp_blank_en(hubp, enable);
}

void hwss_hubp_disable_control(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_disable_control_params.hubp;
	bool disable = params->hubp_disable_control_params.disable;

	if (hubp && hubp->funcs->hubp_disable_control)
		hubp->funcs->hubp_disable_control(hubp, disable);
}

void hwss_hubbub_soft_reset(union block_sequence_params *params)
{
	struct hubbub *hubbub = params->hubbub_soft_reset_params.hubbub;
	bool reset = params->hubbub_soft_reset_params.reset;

	if (hubbub)
		params->hubbub_soft_reset_params.hubbub_soft_reset(hubbub, reset);
}

void hwss_hubp_clk_cntl(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_clk_cntl_params.hubp;
	bool enable = params->hubp_clk_cntl_params.enable;

	if (hubp && hubp->funcs->hubp_clk_cntl) {
		hubp->funcs->hubp_clk_cntl(hubp, enable);
		hubp->power_gated = !enable;
	}
}

void hwss_hubp_init(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_init_params.hubp;

	if (hubp && hubp->funcs->hubp_init)
		hubp->funcs->hubp_init(hubp);
}

void hwss_hubp_set_vm_system_aperture_settings(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_set_vm_system_aperture_settings_params.hubp;
	struct vm_system_aperture_param apt;

	apt.sys_default = params->hubp_set_vm_system_aperture_settings_params.sys_default;
	apt.sys_high = params->hubp_set_vm_system_aperture_settings_params.sys_high;
	apt.sys_low = params->hubp_set_vm_system_aperture_settings_params.sys_low;

	if (hubp && hubp->funcs->hubp_set_vm_system_aperture_settings)
		hubp->funcs->hubp_set_vm_system_aperture_settings(hubp, &apt);
}

void hwss_hubp_set_flip_int(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_set_flip_int_params.hubp;

	if (hubp && hubp->funcs->hubp_set_flip_int)
		hubp->funcs->hubp_set_flip_int(hubp);
}

void hwss_dpp_dppclk_control(union block_sequence_params *params)
{
	struct dpp *dpp = params->dpp_dppclk_control_params.dpp;
	bool dppclk_div = params->dpp_dppclk_control_params.dppclk_div;
	bool enable = params->dpp_dppclk_control_params.enable;

	if (dpp && dpp->funcs->dpp_dppclk_control)
		dpp->funcs->dpp_dppclk_control(dpp, dppclk_div, enable);
}

void hwss_disable_phantom_crtc(union block_sequence_params *params)
{
	struct timing_generator *tg = params->disable_phantom_crtc_params.tg;

	if (tg && tg->funcs->disable_phantom_crtc)
		tg->funcs->disable_phantom_crtc(tg);
}

void hwss_dsc_pg_status(union block_sequence_params *params)
{
	struct dce_hwseq *hws = params->dsc_pg_status_params.hws;
	int dsc_inst = params->dsc_pg_status_params.dsc_inst;

	if (hws && hws->funcs.dsc_pg_status)
		params->dsc_pg_status_params.is_ungated = hws->funcs.dsc_pg_status(hws, dsc_inst);
}

void hwss_dsc_wait_disconnect_pending_clear(union block_sequence_params *params)
{
	struct display_stream_compressor *dsc = params->dsc_wait_disconnect_pending_clear_params.dsc;

	if (!params->dsc_wait_disconnect_pending_clear_params.is_ungated)
		return;
	if (*params->dsc_wait_disconnect_pending_clear_params.is_ungated == false)
		return;

	if (dsc && dsc->funcs->dsc_wait_disconnect_pending_clear)
		dsc->funcs->dsc_wait_disconnect_pending_clear(dsc);
}

void hwss_dsc_disable(union block_sequence_params *params)
{
	struct display_stream_compressor *dsc = params->dsc_disable_params.dsc;

	if (!params->dsc_disable_params.is_ungated)
		return;
	if (*params->dsc_disable_params.is_ungated == false)
		return;

	if (dsc && dsc->funcs->dsc_disable)
		dsc->funcs->dsc_disable(dsc);
}

void hwss_dccg_set_ref_dscclk(union block_sequence_params *params)
{
	struct dccg *dccg = params->dccg_set_ref_dscclk_params.dccg;
	int dsc_inst = params->dccg_set_ref_dscclk_params.dsc_inst;

	if (!params->dccg_set_ref_dscclk_params.is_ungated)
		return;
	if (*params->dccg_set_ref_dscclk_params.is_ungated == false)
		return;

	if (dccg && dccg->funcs->set_ref_dscclk)
		dccg->funcs->set_ref_dscclk(dccg, dsc_inst);
}

void hwss_dpp_pg_control(union block_sequence_params *params)
{
	struct dce_hwseq *hws = params->dpp_pg_control_params.hws;
	unsigned int dpp_inst = params->dpp_pg_control_params.dpp_inst;
	bool power_on = params->dpp_pg_control_params.power_on;

	if (hws->funcs.dpp_pg_control)
		hws->funcs.dpp_pg_control(hws, dpp_inst, power_on);
}

void hwss_hubp_pg_control(union block_sequence_params *params)
{
	struct dce_hwseq *hws = params->hubp_pg_control_params.hws;
	unsigned int hubp_inst = params->hubp_pg_control_params.hubp_inst;
	bool power_on = params->hubp_pg_control_params.power_on;

	if (hws->funcs.hubp_pg_control)
		hws->funcs.hubp_pg_control(hws, hubp_inst, power_on);
}

void hwss_hubp_reset(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_reset_params.hubp;

	if (hubp && hubp->funcs->hubp_reset)
		hubp->funcs->hubp_reset(hubp);
}

void hwss_dpp_reset(union block_sequence_params *params)
{
	struct dpp *dpp = params->dpp_reset_params.dpp;

	if (dpp && dpp->funcs->dpp_reset)
		dpp->funcs->dpp_reset(dpp);
}

void hwss_dpp_root_clock_control(union block_sequence_params *params)
{
	struct dce_hwseq *hws = params->dpp_root_clock_control_params.hws;
	unsigned int dpp_inst = params->dpp_root_clock_control_params.dpp_inst;
	bool clock_on = params->dpp_root_clock_control_params.clock_on;

	if (hws->funcs.dpp_root_clock_control)
		hws->funcs.dpp_root_clock_control(hws, dpp_inst, clock_on);
}

void hwss_dc_ip_request_cntl(union block_sequence_params *params)
{
	struct dc *dc = params->dc_ip_request_cntl_params.dc;
	bool enable = params->dc_ip_request_cntl_params.enable;
	struct dce_hwseq *hws = dc->hwseq;

	if (hws->funcs.dc_ip_request_cntl)
		hws->funcs.dc_ip_request_cntl(dc, enable);
}

void hwss_dccg_update_dpp_dto(union block_sequence_params *params)
{
	struct dccg *dccg = params->dccg_update_dpp_dto_params.dccg;
	int dpp_inst = params->dccg_update_dpp_dto_params.dpp_inst;
	int dppclk_khz = params->dccg_update_dpp_dto_params.dppclk_khz;

	if (dccg && dccg->funcs->update_dpp_dto)
		dccg->funcs->update_dpp_dto(dccg, dpp_inst, dppclk_khz);
}

void hwss_hubp_vtg_sel(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_vtg_sel_params.hubp;
	uint32_t otg_inst = params->hubp_vtg_sel_params.otg_inst;

	if (hubp && hubp->funcs->hubp_vtg_sel)
		hubp->funcs->hubp_vtg_sel(hubp, otg_inst);
}

void hwss_hubp_setup2(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_setup2_params.hubp;
	struct dml2_dchub_per_pipe_register_set *hubp_regs = params->hubp_setup2_params.hubp_regs;
	union dml2_global_sync_programming *global_sync = params->hubp_setup2_params.global_sync;
	struct dc_crtc_timing *timing = params->hubp_setup2_params.timing;

	if (hubp && hubp->funcs->hubp_setup2)
		hubp->funcs->hubp_setup2(hubp, hubp_regs, global_sync, timing);
}

void hwss_hubp_setup(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_setup_params.hubp;
	struct _vcs_dpi_display_dlg_regs_st *dlg_regs = params->hubp_setup_params.dlg_regs;
	struct _vcs_dpi_display_ttu_regs_st *ttu_regs = params->hubp_setup_params.ttu_regs;
	struct _vcs_dpi_display_rq_regs_st *rq_regs = params->hubp_setup_params.rq_regs;
	struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest = params->hubp_setup_params.pipe_dest;

	if (hubp && hubp->funcs->hubp_setup)
		hubp->funcs->hubp_setup(hubp, dlg_regs, ttu_regs, rq_regs, pipe_dest);
}

void hwss_hubp_set_unbounded_requesting(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_set_unbounded_requesting_params.hubp;
	bool unbounded_req = params->hubp_set_unbounded_requesting_params.unbounded_req;

	if (hubp && hubp->funcs->set_unbounded_requesting)
		hubp->funcs->set_unbounded_requesting(hubp, unbounded_req);
}

void hwss_hubp_setup_interdependent2(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_setup_interdependent2_params.hubp;
	struct dml2_dchub_per_pipe_register_set *hubp_regs = params->hubp_setup_interdependent2_params.hubp_regs;

	if (hubp && hubp->funcs->hubp_setup_interdependent2)
		hubp->funcs->hubp_setup_interdependent2(hubp, hubp_regs);
}

void hwss_hubp_setup_interdependent(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_setup_interdependent_params.hubp;
	struct _vcs_dpi_display_dlg_regs_st *dlg_regs = params->hubp_setup_interdependent_params.dlg_regs;
	struct _vcs_dpi_display_ttu_regs_st *ttu_regs = params->hubp_setup_interdependent_params.ttu_regs;

	if (hubp && hubp->funcs->hubp_setup_interdependent)
		hubp->funcs->hubp_setup_interdependent(hubp, dlg_regs, ttu_regs);
}

void hwss_dpp_set_cursor_matrix(union block_sequence_params *params)
{
	struct dpp *dpp = params->dpp_set_cursor_matrix_params.dpp;
	enum dc_color_space color_space = params->dpp_set_cursor_matrix_params.color_space;
	struct dc_csc_transform *cursor_csc_color_matrix = params->dpp_set_cursor_matrix_params.cursor_csc_color_matrix;

	if (dpp && dpp->funcs->set_cursor_matrix)
		dpp->funcs->set_cursor_matrix(dpp, color_space, *cursor_csc_color_matrix);
}

void hwss_mpc_update_mpcc(union block_sequence_params *params)
{
	struct dc *dc = params->mpc_update_mpcc_params.dc;
	struct pipe_ctx *pipe_ctx = params->mpc_update_mpcc_params.pipe_ctx;
	struct dce_hwseq *hws = dc->hwseq;

	if (hws->funcs.update_mpcc)
		hws->funcs.update_mpcc(dc, pipe_ctx);
}

void hwss_mpc_update_blending(union block_sequence_params *params)
{
	struct mpc *mpc = params->mpc_update_blending_params.mpc;
	struct mpcc_blnd_cfg *blnd_cfg = &params->mpc_update_blending_params.blnd_cfg;
	int mpcc_id = params->mpc_update_blending_params.mpcc_id;

	if (mpc && mpc->funcs->update_blending)
		mpc->funcs->update_blending(mpc, blnd_cfg, mpcc_id);
}

void hwss_mpc_assert_idle_mpcc(union block_sequence_params *params)
{
	struct mpc *mpc = params->mpc_assert_idle_mpcc_params.mpc;
	int mpcc_id = params->mpc_assert_idle_mpcc_params.mpcc_id;

	if (mpc && mpc->funcs->wait_for_idle)
		mpc->funcs->wait_for_idle(mpc, mpcc_id);
}

void hwss_mpc_insert_plane(union block_sequence_params *params)
{
	struct mpc *mpc = params->mpc_insert_plane_params.mpc;
	struct mpc_tree *tree = params->mpc_insert_plane_params.mpc_tree_params;
	struct mpcc_blnd_cfg *blnd_cfg = &params->mpc_insert_plane_params.blnd_cfg;
	struct mpcc_sm_cfg *sm_cfg = params->mpc_insert_plane_params.sm_cfg;
	struct mpcc *insert_above_mpcc = params->mpc_insert_plane_params.insert_above_mpcc;
	int mpcc_id = params->mpc_insert_plane_params.mpcc_id;
	int dpp_id = params->mpc_insert_plane_params.dpp_id;

	if (mpc && mpc->funcs->insert_plane)
		mpc->funcs->insert_plane(mpc, tree, blnd_cfg, sm_cfg, insert_above_mpcc,
			dpp_id, mpcc_id);
}

void hwss_dpp_set_scaler(union block_sequence_params *params)
{
	struct dpp *dpp = params->dpp_set_scaler_params.dpp;
	const struct scaler_data *scl_data = params->dpp_set_scaler_params.scl_data;

	if (dpp && dpp->funcs->dpp_set_scaler)
		dpp->funcs->dpp_set_scaler(dpp, scl_data);
}

void hwss_hubp_mem_program_viewport(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_mem_program_viewport_params.hubp;
	const struct rect *viewport = params->hubp_mem_program_viewport_params.viewport;
	const struct rect *viewport_c = params->hubp_mem_program_viewport_params.viewport_c;

	if (hubp && hubp->funcs->mem_program_viewport)
		hubp->funcs->mem_program_viewport(hubp, viewport, viewport_c);
}

void hwss_abort_cursor_offload_update(union block_sequence_params *params)
{
	struct dc *dc = params->abort_cursor_offload_update_params.dc;
	struct pipe_ctx *pipe_ctx = params->abort_cursor_offload_update_params.pipe_ctx;

	if (dc && dc->hwss.abort_cursor_offload_update)
		dc->hwss.abort_cursor_offload_update(dc, pipe_ctx);
}

void hwss_set_cursor_attribute(union block_sequence_params *params)
{
	struct dc *dc = params->set_cursor_attribute_params.dc;
	struct pipe_ctx *pipe_ctx = params->set_cursor_attribute_params.pipe_ctx;

	if (dc && dc->hwss.set_cursor_attribute)
		dc->hwss.set_cursor_attribute(pipe_ctx);
}

void hwss_set_cursor_position(union block_sequence_params *params)
{
	struct dc *dc = params->set_cursor_position_params.dc;
	struct pipe_ctx *pipe_ctx = params->set_cursor_position_params.pipe_ctx;

	if (dc && dc->hwss.set_cursor_position)
		dc->hwss.set_cursor_position(pipe_ctx);
}

void hwss_set_cursor_sdr_white_level(union block_sequence_params *params)
{
	struct dc *dc = params->set_cursor_sdr_white_level_params.dc;
	struct pipe_ctx *pipe_ctx = params->set_cursor_sdr_white_level_params.pipe_ctx;

	if (dc && dc->hwss.set_cursor_sdr_white_level)
		dc->hwss.set_cursor_sdr_white_level(pipe_ctx);
}

void hwss_program_output_csc(union block_sequence_params *params)
{
	struct dc *dc = params->program_output_csc_params.dc;
	struct pipe_ctx *pipe_ctx = params->program_output_csc_params.pipe_ctx;
	enum dc_color_space colorspace = params->program_output_csc_params.colorspace;
	uint16_t *matrix = params->program_output_csc_params.matrix;
	int opp_id = params->program_output_csc_params.opp_id;

	if (dc && dc->hwss.program_output_csc)
		dc->hwss.program_output_csc(dc, pipe_ctx, colorspace, matrix, opp_id);
}

void hwss_hubp_set_blank(union block_sequence_params *params)
{
	struct hubp *hubp = params->hubp_set_blank_params.hubp;
	bool blank = params->hubp_set_blank_params.blank;

	if (hubp && hubp->funcs->set_blank)
		hubp->funcs->set_blank(hubp, blank);
}

void hwss_phantom_hubp_post_enable(union block_sequence_params *params)
{
	struct hubp *hubp = params->phantom_hubp_post_enable_params.hubp;

	if (hubp && hubp->funcs->phantom_hubp_post_enable)
		hubp->funcs->phantom_hubp_post_enable(hubp);
}

void hwss_add_dccg_set_dto_dscclk(struct block_sequence_state *seq_state,
		struct dccg *dccg, int inst, int num_slices_h)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DCCG_SET_DTO_DSCCLK;
		seq_state->steps[*seq_state->num_steps].params.dccg_set_dto_dscclk_params.dccg = dccg;
		seq_state->steps[*seq_state->num_steps].params.dccg_set_dto_dscclk_params.inst = inst;
		seq_state->steps[*seq_state->num_steps].params.dccg_set_dto_dscclk_params.num_slices_h = num_slices_h;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dsc_calculate_and_set_config(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx, bool enable, int opp_cnt)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DSC_CALCULATE_AND_SET_CONFIG;
		seq_state->steps[*seq_state->num_steps].params.dsc_calculate_and_set_config_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].params.dsc_calculate_and_set_config_params.enable = enable;
		seq_state->steps[*seq_state->num_steps].params.dsc_calculate_and_set_config_params.opp_cnt = opp_cnt;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_mpc_remove_mpcc(struct block_sequence_state *seq_state,
		struct mpc *mpc, struct mpc_tree *mpc_tree_params, struct mpcc *mpcc_to_remove)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = MPC_REMOVE_MPCC;
		seq_state->steps[*seq_state->num_steps].params.mpc_remove_mpcc_params.mpc = mpc;
		seq_state->steps[*seq_state->num_steps].params.mpc_remove_mpcc_params.mpc_tree_params = mpc_tree_params;
		seq_state->steps[*seq_state->num_steps].params.mpc_remove_mpcc_params.mpcc_to_remove = mpcc_to_remove;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_opp_set_mpcc_disconnect_pending(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp, int mpcc_inst, bool pending)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = OPP_SET_MPCC_DISCONNECT_PENDING;
		seq_state->steps[*seq_state->num_steps].params.opp_set_mpcc_disconnect_pending_params.opp = opp;
		seq_state->steps[*seq_state->num_steps].params.opp_set_mpcc_disconnect_pending_params.mpcc_inst = mpcc_inst;
		seq_state->steps[*seq_state->num_steps].params.opp_set_mpcc_disconnect_pending_params.pending = pending;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_disconnect(struct block_sequence_state *seq_state,
		struct hubp *hubp)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_DISCONNECT;
		seq_state->steps[*seq_state->num_steps].params.hubp_disconnect_params.hubp = hubp;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dsc_enable_with_opp(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DSC_ENABLE_WITH_OPP;
		seq_state->steps[*seq_state->num_steps].params.dsc_enable_with_opp_params.pipe_ctx = pipe_ctx;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_tg_set_dsc_config(struct block_sequence_state *seq_state,
		struct timing_generator *tg, struct dsc_optc_config *dsc_optc_cfg, bool enable)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = TG_SET_DSC_CONFIG;
		seq_state->steps[*seq_state->num_steps].params.tg_set_dsc_config_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].params.tg_set_dsc_config_params.dsc_optc_cfg = dsc_optc_cfg;
		seq_state->steps[*seq_state->num_steps].params.tg_set_dsc_config_params.enable = enable;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dsc_disconnect(struct block_sequence_state *seq_state,
		struct display_stream_compressor *dsc)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DSC_DISCONNECT;
		seq_state->steps[*seq_state->num_steps].params.dsc_disconnect_params.dsc = dsc;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dc_set_optimized_required(struct block_sequence_state *seq_state,
		struct dc *dc, bool optimized_required)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DC_SET_OPTIMIZED_REQUIRED;
		seq_state->steps[*seq_state->num_steps].params.dc_set_optimized_required_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.dc_set_optimized_required_params.optimized_required = optimized_required;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_abm_set_immediate_disable(struct block_sequence_state *seq_state,
		struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = ABM_SET_IMMEDIATE_DISABLE;
		seq_state->steps[*seq_state->num_steps].params.set_abm_immediate_disable_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.set_abm_immediate_disable_params.pipe_ctx = pipe_ctx;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_opp_set_disp_pattern_generator(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp,
		enum controller_dp_test_pattern test_pattern,
		enum controller_dp_color_space color_space,
		enum dc_color_depth color_depth,
		struct tg_color solid_color,
		bool use_solid_color,
		int width,
		int height,
		int offset)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = OPP_SET_DISP_PATTERN_GENERATOR;
		seq_state->steps[*seq_state->num_steps].params.opp_set_disp_pattern_generator_params.opp = opp;
		seq_state->steps[*seq_state->num_steps].params.opp_set_disp_pattern_generator_params.test_pattern = test_pattern;
		seq_state->steps[*seq_state->num_steps].params.opp_set_disp_pattern_generator_params.color_space = color_space;
		seq_state->steps[*seq_state->num_steps].params.opp_set_disp_pattern_generator_params.color_depth = color_depth;
		seq_state->steps[*seq_state->num_steps].params.opp_set_disp_pattern_generator_params.solid_color = solid_color;
		seq_state->steps[*seq_state->num_steps].params.opp_set_disp_pattern_generator_params.use_solid_color = use_solid_color;
		seq_state->steps[*seq_state->num_steps].params.opp_set_disp_pattern_generator_params.width = width;
		seq_state->steps[*seq_state->num_steps].params.opp_set_disp_pattern_generator_params.height = height;
		seq_state->steps[*seq_state->num_steps].params.opp_set_disp_pattern_generator_params.offset = offset;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add MPC update blending to block sequence
 */
void hwss_add_mpc_update_blending(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		struct mpcc_blnd_cfg blnd_cfg,
		int mpcc_id)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = MPC_UPDATE_BLENDING;
		seq_state->steps[*seq_state->num_steps].params.mpc_update_blending_params.mpc = mpc;
		seq_state->steps[*seq_state->num_steps].params.mpc_update_blending_params.blnd_cfg = blnd_cfg;
		seq_state->steps[*seq_state->num_steps].params.mpc_update_blending_params.mpcc_id = mpcc_id;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add MPC insert plane to block sequence
 */
void hwss_add_mpc_insert_plane(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		struct mpc_tree *mpc_tree_params,
		struct mpcc_blnd_cfg blnd_cfg,
		struct mpcc_sm_cfg *sm_cfg,
		struct mpcc *insert_above_mpcc,
		int dpp_id,
		int mpcc_id)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = MPC_INSERT_PLANE;
		seq_state->steps[*seq_state->num_steps].params.mpc_insert_plane_params.mpc = mpc;
		seq_state->steps[*seq_state->num_steps].params.mpc_insert_plane_params.mpc_tree_params = mpc_tree_params;
		seq_state->steps[*seq_state->num_steps].params.mpc_insert_plane_params.blnd_cfg = blnd_cfg;
		seq_state->steps[*seq_state->num_steps].params.mpc_insert_plane_params.sm_cfg = sm_cfg;
		seq_state->steps[*seq_state->num_steps].params.mpc_insert_plane_params.insert_above_mpcc = insert_above_mpcc;
		seq_state->steps[*seq_state->num_steps].params.mpc_insert_plane_params.dpp_id = dpp_id;
		seq_state->steps[*seq_state->num_steps].params.mpc_insert_plane_params.mpcc_id = mpcc_id;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add MPC assert idle MPCC to block sequence
 */
void hwss_add_mpc_assert_idle_mpcc(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		int mpcc_id)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = MPC_ASSERT_IDLE_MPCC;
		seq_state->steps[*seq_state->num_steps].params.mpc_assert_idle_mpcc_params.mpc = mpc;
		seq_state->steps[*seq_state->num_steps].params.mpc_assert_idle_mpcc_params.mpcc_id = mpcc_id;
		(*seq_state->num_steps)++;
	}
}

/*
 * Helper function to add HUBP set blank to block sequence
 */
void hwss_add_hubp_set_blank(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool blank)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_SET_BLANK;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_blank_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_blank_params.blank = blank;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_opp_program_bit_depth_reduction(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp,
		bool use_default_params,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = OPP_PROGRAM_BIT_DEPTH_REDUCTION;
		seq_state->steps[*seq_state->num_steps].params.opp_program_bit_depth_reduction_params.opp = opp;
		seq_state->steps[*seq_state->num_steps].params.opp_program_bit_depth_reduction_params.use_default_params = use_default_params;
		seq_state->steps[*seq_state->num_steps].params.opp_program_bit_depth_reduction_params.pipe_ctx = pipe_ctx;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dc_ip_request_cntl(struct block_sequence_state *seq_state,
		struct dc *dc,
		bool enable)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DC_IP_REQUEST_CNTL;
		seq_state->steps[*seq_state->num_steps].params.dc_ip_request_cntl_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.dc_ip_request_cntl_params.enable = enable;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dwbc_update(struct block_sequence_state *seq_state,
		struct dwbc *dwb,
		struct dc_dwb_params *dwb_params)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DWBC_UPDATE;
		seq_state->steps[*seq_state->num_steps].params.dwbc_update_params.dwb = dwb;
		seq_state->steps[*seq_state->num_steps].params.dwbc_update_params.dwb_params = dwb_params;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_mcif_wb_config_buf(struct block_sequence_state *seq_state,
		struct mcif_wb *mcif_wb,
		struct mcif_buf_params *mcif_buf_params,
		unsigned int dest_height)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = MCIF_WB_CONFIG_BUF;
		seq_state->steps[*seq_state->num_steps].params.mcif_wb_config_buf_params.mcif_wb = mcif_wb;
		seq_state->steps[*seq_state->num_steps].params.mcif_wb_config_buf_params.mcif_buf_params = mcif_buf_params;
		seq_state->steps[*seq_state->num_steps].params.mcif_wb_config_buf_params.dest_height = dest_height;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_mcif_wb_config_arb(struct block_sequence_state *seq_state,
		struct mcif_wb *mcif_wb,
		struct mcif_arb_params *mcif_arb_params)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = MCIF_WB_CONFIG_ARB;
		seq_state->steps[*seq_state->num_steps].params.mcif_wb_config_arb_params.mcif_wb = mcif_wb;
		seq_state->steps[*seq_state->num_steps].params.mcif_wb_config_arb_params.mcif_arb_params = mcif_arb_params;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_mcif_wb_enable(struct block_sequence_state *seq_state,
		struct mcif_wb *mcif_wb)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = MCIF_WB_ENABLE;
		seq_state->steps[*seq_state->num_steps].params.mcif_wb_enable_params.mcif_wb = mcif_wb;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_mcif_wb_disable(struct block_sequence_state *seq_state,
		struct mcif_wb *mcif_wb)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = MCIF_WB_DISABLE;
		seq_state->steps[*seq_state->num_steps].params.mcif_wb_disable_params.mcif_wb = mcif_wb;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_mpc_set_dwb_mux(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		int dwb_id,
		int mpcc_id)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = MPC_SET_DWB_MUX;
		seq_state->steps[*seq_state->num_steps].params.mpc_set_dwb_mux_params.mpc = mpc;
		seq_state->steps[*seq_state->num_steps].params.mpc_set_dwb_mux_params.dwb_id = dwb_id;
		seq_state->steps[*seq_state->num_steps].params.mpc_set_dwb_mux_params.mpcc_id = mpcc_id;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_mpc_disable_dwb_mux(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		unsigned int dwb_id)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = MPC_DISABLE_DWB_MUX;
		seq_state->steps[*seq_state->num_steps].params.mpc_disable_dwb_mux_params.mpc = mpc;
		seq_state->steps[*seq_state->num_steps].params.mpc_disable_dwb_mux_params.dwb_id = dwb_id;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dwbc_enable(struct block_sequence_state *seq_state,
		struct dwbc *dwb,
		struct dc_dwb_params *dwb_params)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DWBC_ENABLE;
		seq_state->steps[*seq_state->num_steps].params.dwbc_enable_params.dwb = dwb;
		seq_state->steps[*seq_state->num_steps].params.dwbc_enable_params.dwb_params = dwb_params;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dwbc_disable(struct block_sequence_state *seq_state,
		struct dwbc *dwb)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DWBC_DISABLE;
		seq_state->steps[*seq_state->num_steps].params.dwbc_disable_params.dwb = dwb;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_tg_set_gsl(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		struct gsl_params gsl)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = TG_SET_GSL;
		seq_state->steps[*seq_state->num_steps].params.tg_set_gsl_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].params.tg_set_gsl_params.gsl = gsl;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_tg_set_gsl_source_select(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		int group_idx,
		uint32_t gsl_ready_signal)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = TG_SET_GSL_SOURCE_SELECT;
		seq_state->steps[*seq_state->num_steps].params.tg_set_gsl_source_select_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].params.tg_set_gsl_source_select_params.group_idx = group_idx;
		seq_state->steps[*seq_state->num_steps].params.tg_set_gsl_source_select_params.gsl_ready_signal = gsl_ready_signal;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_update_mall_sel(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		uint32_t mall_sel,
		bool cache_cursor)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_UPDATE_MALL_SEL;
		seq_state->steps[*seq_state->num_steps].params.hubp_update_mall_sel_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_update_mall_sel_params.mall_sel = mall_sel;
		seq_state->steps[*seq_state->num_steps].params.hubp_update_mall_sel_params.cache_cursor = cache_cursor;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_prepare_subvp_buffering(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool enable)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_PREPARE_SUBVP_BUFFERING;
		seq_state->steps[*seq_state->num_steps].params.hubp_prepare_subvp_buffering_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_prepare_subvp_buffering_params.enable = enable;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_set_blank_en(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool enable)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_SET_BLANK_EN;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_blank_en_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_blank_en_params.enable = enable;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_disable_control(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool disable)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_DISABLE_CONTROL;
		seq_state->steps[*seq_state->num_steps].params.hubp_disable_control_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_disable_control_params.disable = disable;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubbub_soft_reset(struct block_sequence_state *seq_state,
		struct hubbub *hubbub,
		void (*hubbub_soft_reset)(struct hubbub *hubbub, bool reset),
		bool reset)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBBUB_SOFT_RESET;
		seq_state->steps[*seq_state->num_steps].params.hubbub_soft_reset_params.hubbub = hubbub;
		seq_state->steps[*seq_state->num_steps].params.hubbub_soft_reset_params.hubbub_soft_reset = hubbub_soft_reset;
		seq_state->steps[*seq_state->num_steps].params.hubbub_soft_reset_params.reset = reset;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_clk_cntl(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool enable)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_CLK_CNTL;
		seq_state->steps[*seq_state->num_steps].params.hubp_clk_cntl_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_clk_cntl_params.enable = enable;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dpp_dppclk_control(struct block_sequence_state *seq_state,
		struct dpp *dpp,
		bool dppclk_div,
		bool enable)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DPP_DPPCLK_CONTROL;
		seq_state->steps[*seq_state->num_steps].params.dpp_dppclk_control_params.dpp = dpp;
		seq_state->steps[*seq_state->num_steps].params.dpp_dppclk_control_params.dppclk_div = dppclk_div;
		seq_state->steps[*seq_state->num_steps].params.dpp_dppclk_control_params.enable = enable;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_disable_phantom_crtc(struct block_sequence_state *seq_state,
		struct timing_generator *tg)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DISABLE_PHANTOM_CRTC;
		seq_state->steps[*seq_state->num_steps].params.disable_phantom_crtc_params.tg = tg;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dsc_pg_status(struct block_sequence_state *seq_state,
		struct dce_hwseq *hws,
		int dsc_inst,
		bool is_ungated)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DSC_PG_STATUS;
		seq_state->steps[*seq_state->num_steps].params.dsc_pg_status_params.hws = hws;
		seq_state->steps[*seq_state->num_steps].params.dsc_pg_status_params.dsc_inst = dsc_inst;
		seq_state->steps[*seq_state->num_steps].params.dsc_pg_status_params.is_ungated = is_ungated;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dsc_wait_disconnect_pending_clear(struct block_sequence_state *seq_state,
		struct display_stream_compressor *dsc,
		bool *is_ungated)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DSC_WAIT_DISCONNECT_PENDING_CLEAR;
		seq_state->steps[*seq_state->num_steps].params.dsc_wait_disconnect_pending_clear_params.dsc = dsc;
		seq_state->steps[*seq_state->num_steps].params.dsc_wait_disconnect_pending_clear_params.is_ungated = is_ungated;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dsc_disable(struct block_sequence_state *seq_state,
		struct display_stream_compressor *dsc,
		bool *is_ungated)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DSC_DISABLE;
		seq_state->steps[*seq_state->num_steps].params.dsc_disable_params.dsc = dsc;
		seq_state->steps[*seq_state->num_steps].params.dsc_disable_params.is_ungated = is_ungated;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dccg_set_ref_dscclk(struct block_sequence_state *seq_state,
		struct dccg *dccg,
		int dsc_inst,
		bool *is_ungated)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DCCG_SET_REF_DSCCLK;
		seq_state->steps[*seq_state->num_steps].params.dccg_set_ref_dscclk_params.dccg = dccg;
		seq_state->steps[*seq_state->num_steps].params.dccg_set_ref_dscclk_params.dsc_inst = dsc_inst;
		seq_state->steps[*seq_state->num_steps].params.dccg_set_ref_dscclk_params.is_ungated = is_ungated;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dpp_root_clock_control(struct block_sequence_state *seq_state,
		struct dce_hwseq *hws,
		unsigned int dpp_inst,
		bool clock_on)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DPP_ROOT_CLOCK_CONTROL;
		seq_state->steps[*seq_state->num_steps].params.dpp_root_clock_control_params.hws = hws;
		seq_state->steps[*seq_state->num_steps].params.dpp_root_clock_control_params.dpp_inst = dpp_inst;
		seq_state->steps[*seq_state->num_steps].params.dpp_root_clock_control_params.clock_on = clock_on;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dpp_pg_control(struct block_sequence_state *seq_state,
		struct dce_hwseq *hws,
		unsigned int dpp_inst,
		bool power_on)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DPP_PG_CONTROL;
		seq_state->steps[*seq_state->num_steps].params.dpp_pg_control_params.hws = hws;
		seq_state->steps[*seq_state->num_steps].params.dpp_pg_control_params.dpp_inst = dpp_inst;
		seq_state->steps[*seq_state->num_steps].params.dpp_pg_control_params.power_on = power_on;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_pg_control(struct block_sequence_state *seq_state,
		struct dce_hwseq *hws,
		unsigned int hubp_inst,
		bool power_on)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_PG_CONTROL;
		seq_state->steps[*seq_state->num_steps].params.hubp_pg_control_params.hws = hws;
		seq_state->steps[*seq_state->num_steps].params.hubp_pg_control_params.hubp_inst = hubp_inst;
		seq_state->steps[*seq_state->num_steps].params.hubp_pg_control_params.power_on = power_on;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_init(struct block_sequence_state *seq_state,
		struct hubp *hubp)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_INIT;
		seq_state->steps[*seq_state->num_steps].params.hubp_init_params.hubp = hubp;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_reset(struct block_sequence_state *seq_state,
		struct hubp *hubp)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_RESET;
		seq_state->steps[*seq_state->num_steps].params.hubp_reset_params.hubp = hubp;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dpp_reset(struct block_sequence_state *seq_state,
		struct dpp *dpp)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DPP_RESET;
		seq_state->steps[*seq_state->num_steps].params.dpp_reset_params.dpp = dpp;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_opp_pipe_clock_control(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp,
		bool enable)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = OPP_PIPE_CLOCK_CONTROL;
		seq_state->steps[*seq_state->num_steps].params.opp_pipe_clock_control_params.opp = opp;
		seq_state->steps[*seq_state->num_steps].params.opp_pipe_clock_control_params.enable = enable;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_set_vm_system_aperture_settings(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		uint64_t sys_default,
		uint64_t sys_low,
		uint64_t sys_high)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_SET_VM_SYSTEM_APERTURE_SETTINGS;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_vm_system_aperture_settings_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_vm_system_aperture_settings_params.sys_default.quad_part = sys_default;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_vm_system_aperture_settings_params.sys_low.quad_part = sys_low;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_vm_system_aperture_settings_params.sys_high.quad_part = sys_high;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_set_flip_int(struct block_sequence_state *seq_state,
		struct hubp *hubp)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_SET_FLIP_INT;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_flip_int_params.hubp = hubp;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dccg_update_dpp_dto(struct block_sequence_state *seq_state,
		struct dccg *dccg,
		int dpp_inst,
		int dppclk_khz)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DCCG_UPDATE_DPP_DTO;
		seq_state->steps[*seq_state->num_steps].params.dccg_update_dpp_dto_params.dccg = dccg;
		seq_state->steps[*seq_state->num_steps].params.dccg_update_dpp_dto_params.dpp_inst = dpp_inst;
		seq_state->steps[*seq_state->num_steps].params.dccg_update_dpp_dto_params.dppclk_khz = dppclk_khz;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_vtg_sel(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		uint32_t otg_inst)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_VTG_SEL;
		seq_state->steps[*seq_state->num_steps].params.hubp_vtg_sel_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_vtg_sel_params.otg_inst = otg_inst;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_setup2(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		struct dml2_dchub_per_pipe_register_set *hubp_regs,
		union dml2_global_sync_programming *global_sync,
		struct dc_crtc_timing *timing)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_SETUP2;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup2_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup2_params.hubp_regs = hubp_regs;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup2_params.global_sync = global_sync;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup2_params.timing = timing;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_setup(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_regs,
		struct _vcs_dpi_display_ttu_regs_st *ttu_regs,
		struct _vcs_dpi_display_rq_regs_st *rq_regs,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_SETUP;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup_params.dlg_regs = dlg_regs;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup_params.ttu_regs = ttu_regs;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup_params.rq_regs = rq_regs;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup_params.pipe_dest = pipe_dest;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_set_unbounded_requesting(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool unbounded_req)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_SET_UNBOUNDED_REQUESTING;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_unbounded_requesting_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_set_unbounded_requesting_params.unbounded_req = unbounded_req;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_setup_interdependent2(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		struct dml2_dchub_per_pipe_register_set *hubp_regs)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_SETUP_INTERDEPENDENT2;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup_interdependent2_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup_interdependent2_params.hubp_regs = hubp_regs;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_setup_interdependent(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_regs,
		struct _vcs_dpi_display_ttu_regs_st *ttu_regs)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_SETUP_INTERDEPENDENT;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup_interdependent_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup_interdependent_params.dlg_regs = dlg_regs;
		seq_state->steps[*seq_state->num_steps].params.hubp_setup_interdependent_params.ttu_regs = ttu_regs;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_program_surface_config(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		enum surface_pixel_format format,
		struct dc_tiling_info *tiling_info,
		struct plane_size plane_size,
		enum dc_rotation_angle rotation,
		struct dc_plane_dcc_param *dcc,
		bool horizontal_mirror,
		int compat_level)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_PROGRAM_SURFACE_CONFIG;
		seq_state->steps[*seq_state->num_steps].params.program_surface_config_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.program_surface_config_params.format = format;
		seq_state->steps[*seq_state->num_steps].params.program_surface_config_params.tiling_info = tiling_info;
		seq_state->steps[*seq_state->num_steps].params.program_surface_config_params.plane_size = plane_size;
		seq_state->steps[*seq_state->num_steps].params.program_surface_config_params.rotation = rotation;
		seq_state->steps[*seq_state->num_steps].params.program_surface_config_params.dcc = dcc;
		seq_state->steps[*seq_state->num_steps].params.program_surface_config_params.horizontal_mirror = horizontal_mirror;
		seq_state->steps[*seq_state->num_steps].params.program_surface_config_params.compat_level = compat_level;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dpp_setup_dpp(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DPP_SETUP_DPP;
		seq_state->steps[*seq_state->num_steps].params.setup_dpp_params.pipe_ctx = pipe_ctx;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dpp_set_cursor_matrix(struct block_sequence_state *seq_state,
		struct dpp *dpp,
		enum dc_color_space color_space,
		struct dc_csc_transform *cursor_csc_color_matrix)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DPP_SET_CURSOR_MATRIX;
		seq_state->steps[*seq_state->num_steps].params.dpp_set_cursor_matrix_params.dpp = dpp;
		seq_state->steps[*seq_state->num_steps].params.dpp_set_cursor_matrix_params.color_space = color_space;
		seq_state->steps[*seq_state->num_steps].params.dpp_set_cursor_matrix_params.cursor_csc_color_matrix = cursor_csc_color_matrix;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_dpp_set_scaler(struct block_sequence_state *seq_state,
		struct dpp *dpp,
		const struct scaler_data *scl_data)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = DPP_SET_SCALER;
		seq_state->steps[*seq_state->num_steps].params.dpp_set_scaler_params.dpp = dpp;
		seq_state->steps[*seq_state->num_steps].params.dpp_set_scaler_params.scl_data = scl_data;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubp_mem_program_viewport(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		const struct rect *viewport,
		const struct rect *viewport_c)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBP_MEM_PROGRAM_VIEWPORT;
		seq_state->steps[*seq_state->num_steps].params.hubp_mem_program_viewport_params.hubp = hubp;
		seq_state->steps[*seq_state->num_steps].params.hubp_mem_program_viewport_params.viewport = viewport;
		seq_state->steps[*seq_state->num_steps].params.hubp_mem_program_viewport_params.viewport_c = viewport_c;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_abort_cursor_offload_update(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = ABORT_CURSOR_OFFLOAD_UPDATE;
		seq_state->steps[*seq_state->num_steps].params.abort_cursor_offload_update_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.abort_cursor_offload_update_params.pipe_ctx = pipe_ctx;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_set_cursor_attribute(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = SET_CURSOR_ATTRIBUTE;
		seq_state->steps[*seq_state->num_steps].params.set_cursor_attribute_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.set_cursor_attribute_params.pipe_ctx = pipe_ctx;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_set_cursor_position(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = SET_CURSOR_POSITION;
		seq_state->steps[*seq_state->num_steps].params.set_cursor_position_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.set_cursor_position_params.pipe_ctx = pipe_ctx;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_set_cursor_sdr_white_level(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = SET_CURSOR_SDR_WHITE_LEVEL;
		seq_state->steps[*seq_state->num_steps].params.set_cursor_sdr_white_level_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.set_cursor_sdr_white_level_params.pipe_ctx = pipe_ctx;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_program_output_csc(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix,
		int opp_id)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = PROGRAM_OUTPUT_CSC;
		seq_state->steps[*seq_state->num_steps].params.program_output_csc_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.program_output_csc_params.pipe_ctx = pipe_ctx;
		seq_state->steps[*seq_state->num_steps].params.program_output_csc_params.colorspace = colorspace;
		seq_state->steps[*seq_state->num_steps].params.program_output_csc_params.matrix = matrix;
		seq_state->steps[*seq_state->num_steps].params.program_output_csc_params.opp_id = opp_id;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_phantom_hubp_post_enable(struct block_sequence_state *seq_state,
		struct hubp *hubp)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = PHANTOM_HUBP_POST_ENABLE;
		seq_state->steps[*seq_state->num_steps].params.phantom_hubp_post_enable_params.hubp = hubp;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_update_force_pstate(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct dc_state *context)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = UPDATE_FORCE_PSTATE;
		seq_state->steps[*seq_state->num_steps].params.update_force_pstate_params.dc = dc;
		seq_state->steps[*seq_state->num_steps].params.update_force_pstate_params.context = context;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubbub_apply_dedcn21_147_wa(struct block_sequence_state *seq_state,
		struct hubbub *hubbub)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBBUB_APPLY_DEDCN21_147_WA;
		seq_state->steps[*seq_state->num_steps].params.hubbub_apply_dedcn21_147_wa_params.hubbub = hubbub;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_hubbub_allow_self_refresh_control(struct block_sequence_state *seq_state,
		struct hubbub *hubbub,
		bool allow,
		bool *disallow_self_refresh_applied)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = HUBBUB_ALLOW_SELF_REFRESH_CONTROL;
		seq_state->steps[*seq_state->num_steps].params.hubbub_allow_self_refresh_control_params.hubbub = hubbub;
		seq_state->steps[*seq_state->num_steps].params.hubbub_allow_self_refresh_control_params.allow = allow;
		seq_state->steps[*seq_state->num_steps].params.hubbub_allow_self_refresh_control_params.disallow_self_refresh_applied = disallow_self_refresh_applied;
		(*seq_state->num_steps)++;
	}
}

void hwss_add_tg_get_frame_count(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		unsigned int *frame_count)
{
	if (*seq_state->num_steps < MAX_HWSS_BLOCK_SEQUENCE_SIZE) {
		seq_state->steps[*seq_state->num_steps].func = TG_GET_FRAME_COUNT;
		seq_state->steps[*seq_state->num_steps].params.tg_get_frame_count_params.tg = tg;
		seq_state->steps[*seq_state->num_steps].params.tg_get_frame_count_params.frame_count = frame_count;
		(*seq_state->num_steps)++;
	}
}
