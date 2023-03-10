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

#include "dm_services.h"

#include "core_types.h"

#include "reg_helper.h"
#include "dcn10_dpp.h"
#include "basics/conversion.h"


#define NUM_PHASES    64
#define HORZ_MAX_TAPS 8
#define VERT_MAX_TAPS 8

#define BLACK_OFFSET_RGB_Y 0x0
#define BLACK_OFFSET_CBCR  0x8000

#define VISUAL_CONFIRM_RECT_HEIGHT_DEFAULT 3
#define VISUAL_CONFIRM_RECT_HEIGHT_MIN 1
#define VISUAL_CONFIRM_RECT_HEIGHT_MAX 10

#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

enum dcn10_coef_filter_type_sel {
	SCL_COEF_LUMA_VERT_FILTER = 0,
	SCL_COEF_LUMA_HORZ_FILTER = 1,
	SCL_COEF_CHROMA_VERT_FILTER = 2,
	SCL_COEF_CHROMA_HORZ_FILTER = 3,
	SCL_COEF_ALPHA_VERT_FILTER = 4,
	SCL_COEF_ALPHA_HORZ_FILTER = 5
};

enum dscl_autocal_mode {
	AUTOCAL_MODE_OFF = 0,

	/* Autocal calculate the scaling ratio and initial phase and the
	 * DSCL_MODE_SEL must be set to 1
	 */
	AUTOCAL_MODE_AUTOSCALE = 1,
	/* Autocal perform auto centering without replication and the
	 * DSCL_MODE_SEL must be set to 0
	 */
	AUTOCAL_MODE_AUTOCENTER = 2,
	/* Autocal perform auto centering and auto replication and the
	 * DSCL_MODE_SEL must be set to 0
	 */
	AUTOCAL_MODE_AUTOREPLICATE = 3
};

enum dscl_mode_sel {
	DSCL_MODE_SCALING_444_BYPASS = 0,
	DSCL_MODE_SCALING_444_RGB_ENABLE = 1,
	DSCL_MODE_SCALING_444_YCBCR_ENABLE = 2,
	DSCL_MODE_SCALING_420_YCBCR_ENABLE = 3,
	DSCL_MODE_SCALING_420_LUMA_BYPASS = 4,
	DSCL_MODE_SCALING_420_CHROMA_BYPASS = 5,
	DSCL_MODE_DSCL_BYPASS = 6
};

static int dpp1_dscl_get_pixel_depth_val(enum lb_pixel_depth depth)
{
	if (depth == LB_PIXEL_DEPTH_30BPP)
		return 0; /* 10 bpc */
	else if (depth == LB_PIXEL_DEPTH_24BPP)
		return 1; /* 8 bpc */
	else if (depth == LB_PIXEL_DEPTH_18BPP)
		return 2; /* 6 bpc */
	else if (depth == LB_PIXEL_DEPTH_36BPP)
		return 3; /* 12 bpc */
	else {
		ASSERT(0);
		return -1; /* Unsupported */
	}
}

static bool dpp1_dscl_is_video_format(enum pixel_format format)
{
	if (format >= PIXEL_FORMAT_VIDEO_BEGIN
			&& format <= PIXEL_FORMAT_VIDEO_END)
		return true;
	else
		return false;
}

static bool dpp1_dscl_is_420_format(enum pixel_format format)
{
	if (format == PIXEL_FORMAT_420BPP8 ||
			format == PIXEL_FORMAT_420BPP10)
		return true;
	else
		return false;
}

static enum dscl_mode_sel dpp1_dscl_get_dscl_mode(
		struct dpp *dpp_base,
		const struct scaler_data *data,
		bool dbg_always_scale)
{
	const long long one = dc_fixpt_one.value;

	if (dpp_base->caps->dscl_data_proc_format == DSCL_DATA_PRCESSING_FIXED_FORMAT) {
		/* DSCL is processing data in fixed format */
		if (data->format == PIXEL_FORMAT_FP16)
			return DSCL_MODE_DSCL_BYPASS;
	}

	if (data->ratios.horz.value == one
			&& data->ratios.vert.value == one
			&& data->ratios.horz_c.value == one
			&& data->ratios.vert_c.value == one
			&& !dbg_always_scale)
		return DSCL_MODE_SCALING_444_BYPASS;

	if (!dpp1_dscl_is_420_format(data->format)) {
		if (dpp1_dscl_is_video_format(data->format))
			return DSCL_MODE_SCALING_444_YCBCR_ENABLE;
		else
			return DSCL_MODE_SCALING_444_RGB_ENABLE;
	}
	if (data->ratios.horz.value == one && data->ratios.vert.value == one)
		return DSCL_MODE_SCALING_420_LUMA_BYPASS;
	if (data->ratios.horz_c.value == one && data->ratios.vert_c.value == one)
		return DSCL_MODE_SCALING_420_CHROMA_BYPASS;

	return DSCL_MODE_SCALING_420_YCBCR_ENABLE;
}

static void dpp1_power_on_dscl(
	struct dpp *dpp_base,
	bool power_on)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	if (dpp->tf_regs->DSCL_MEM_PWR_CTRL) {
		if (power_on) {
			REG_UPDATE(DSCL_MEM_PWR_CTRL, LUT_MEM_PWR_FORCE, 0);
			REG_WAIT(DSCL_MEM_PWR_STATUS, LUT_MEM_PWR_STATE, 0, 1, 5);
		} else {
			if (dpp->base.ctx->dc->debug.enable_mem_low_power.bits.dscl) {
				dpp->base.ctx->dc->optimized_required = true;
				dpp->base.deferred_reg_writes.bits.disable_dscl = true;
			} else {
				REG_UPDATE(DSCL_MEM_PWR_CTRL, LUT_MEM_PWR_FORCE, 3);
			}
		}
	}
}


static void dpp1_dscl_set_lb(
	struct dcn10_dpp *dpp,
	const struct line_buffer_params *lb_params,
	enum lb_memory_config mem_size_config)
{
	uint32_t max_partitions = 63; /* Currently hardcoded on all ASICs before DCN 3.2 */

	/* LB */
	if (dpp->base.caps->dscl_data_proc_format == DSCL_DATA_PRCESSING_FIXED_FORMAT) {
		/* DSCL caps: pixel data processed in fixed format */
		uint32_t pixel_depth = dpp1_dscl_get_pixel_depth_val(lb_params->depth);
		uint32_t dyn_pix_depth = lb_params->dynamic_pixel_depth;

		REG_SET_7(LB_DATA_FORMAT, 0,
			PIXEL_DEPTH, pixel_depth, /* Pixel depth stored in LB */
			PIXEL_EXPAN_MODE, lb_params->pixel_expan_mode, /* Pixel expansion mode */
			PIXEL_REDUCE_MODE, 1, /* Pixel reduction mode: Rounding */
			DYNAMIC_PIXEL_DEPTH, dyn_pix_depth, /* Dynamic expansion pixel depth */
			DITHER_EN, 0, /* Dithering enable: Disabled */
			INTERLEAVE_EN, lb_params->interleave_en, /* Interleave source enable */
			LB_DATA_FORMAT__ALPHA_EN, lb_params->alpha_en); /* Alpha enable */
	}
	else {
		/* DSCL caps: pixel data processed in float format */
		REG_SET_2(LB_DATA_FORMAT, 0,
			INTERLEAVE_EN, lb_params->interleave_en, /* Interleave source enable */
			LB_DATA_FORMAT__ALPHA_EN, lb_params->alpha_en); /* Alpha enable */
	}

	if (dpp->base.caps->max_lb_partitions == 31)
		max_partitions = 31;

	REG_SET_2(LB_MEMORY_CTRL, 0,
		MEMORY_CONFIG, mem_size_config,
		LB_MAX_PARTITIONS, max_partitions);
}

static const uint16_t *dpp1_dscl_get_filter_coeffs_64p(int taps, struct fixed31_32 ratio)
{
	if (taps == 8)
		return get_filter_8tap_64p(ratio);
	else if (taps == 7)
		return get_filter_7tap_64p(ratio);
	else if (taps == 6)
		return get_filter_6tap_64p(ratio);
	else if (taps == 5)
		return get_filter_5tap_64p(ratio);
	else if (taps == 4)
		return get_filter_4tap_64p(ratio);
	else if (taps == 3)
		return get_filter_3tap_64p(ratio);
	else if (taps == 2)
		return get_filter_2tap_64p();
	else if (taps == 1)
		return NULL;
	else {
		/* should never happen, bug */
		BREAK_TO_DEBUGGER();
		return NULL;
	}
}

static void dpp1_dscl_set_scaler_filter(
		struct dcn10_dpp *dpp,
		uint32_t taps,
		enum dcn10_coef_filter_type_sel filter_type,
		const uint16_t *filter)
{
	const int tap_pairs = (taps + 1) / 2;
	int phase;
	int pair;
	uint16_t odd_coef, even_coef;

	REG_SET_3(SCL_COEF_RAM_TAP_SELECT, 0,
		SCL_COEF_RAM_TAP_PAIR_IDX, 0,
		SCL_COEF_RAM_PHASE, 0,
		SCL_COEF_RAM_FILTER_TYPE, filter_type);

	for (phase = 0; phase < (NUM_PHASES / 2 + 1); phase++) {
		for (pair = 0; pair < tap_pairs; pair++) {
			even_coef = filter[phase * taps + 2 * pair];
			if ((pair * 2 + 1) < taps)
				odd_coef = filter[phase * taps + 2 * pair + 1];
			else
				odd_coef = 0;

			REG_SET_4(SCL_COEF_RAM_TAP_DATA, 0,
				/* Even tap coefficient (bits 1:0 fixed to 0) */
				SCL_COEF_RAM_EVEN_TAP_COEF, even_coef,
				/* Write/read control for even coefficient */
				SCL_COEF_RAM_EVEN_TAP_COEF_EN, 1,
				/* Odd tap coefficient (bits 1:0 fixed to 0) */
				SCL_COEF_RAM_ODD_TAP_COEF, odd_coef,
				/* Write/read control for odd coefficient */
				SCL_COEF_RAM_ODD_TAP_COEF_EN, 1);
		}
	}

}

static void dpp1_dscl_set_scl_filter(
		struct dcn10_dpp *dpp,
		const struct scaler_data *scl_data,
		bool chroma_coef_mode)
{
	bool h_2tap_hardcode_coef_en = false;
	bool v_2tap_hardcode_coef_en = false;
	bool h_2tap_sharp_en = false;
	bool v_2tap_sharp_en = false;
	uint32_t h_2tap_sharp_factor = scl_data->sharpness.horz;
	uint32_t v_2tap_sharp_factor = scl_data->sharpness.vert;
	bool coef_ram_current;
	const uint16_t *filter_h = NULL;
	const uint16_t *filter_v = NULL;
	const uint16_t *filter_h_c = NULL;
	const uint16_t *filter_v_c = NULL;

	h_2tap_hardcode_coef_en = scl_data->taps.h_taps < 3
					&& scl_data->taps.h_taps_c < 3
		&& (scl_data->taps.h_taps > 1 && scl_data->taps.h_taps_c > 1);
	v_2tap_hardcode_coef_en = scl_data->taps.v_taps < 3
					&& scl_data->taps.v_taps_c < 3
		&& (scl_data->taps.v_taps > 1 && scl_data->taps.v_taps_c > 1);

	h_2tap_sharp_en = h_2tap_hardcode_coef_en && h_2tap_sharp_factor != 0;
	v_2tap_sharp_en = v_2tap_hardcode_coef_en && v_2tap_sharp_factor != 0;

	REG_UPDATE_6(DSCL_2TAP_CONTROL,
		SCL_H_2TAP_HARDCODE_COEF_EN, h_2tap_hardcode_coef_en,
		SCL_H_2TAP_SHARP_EN, h_2tap_sharp_en,
		SCL_H_2TAP_SHARP_FACTOR, h_2tap_sharp_factor,
		SCL_V_2TAP_HARDCODE_COEF_EN, v_2tap_hardcode_coef_en,
		SCL_V_2TAP_SHARP_EN, v_2tap_sharp_en,
		SCL_V_2TAP_SHARP_FACTOR, v_2tap_sharp_factor);

	if (!v_2tap_hardcode_coef_en || !h_2tap_hardcode_coef_en) {
		bool filter_updated = false;

		filter_h = dpp1_dscl_get_filter_coeffs_64p(
				scl_data->taps.h_taps, scl_data->ratios.horz);
		filter_v = dpp1_dscl_get_filter_coeffs_64p(
				scl_data->taps.v_taps, scl_data->ratios.vert);

		filter_updated = (filter_h && (filter_h != dpp->filter_h))
				|| (filter_v && (filter_v != dpp->filter_v));

		if (chroma_coef_mode) {
			filter_h_c = dpp1_dscl_get_filter_coeffs_64p(
					scl_data->taps.h_taps_c, scl_data->ratios.horz_c);
			filter_v_c = dpp1_dscl_get_filter_coeffs_64p(
					scl_data->taps.v_taps_c, scl_data->ratios.vert_c);
			filter_updated = filter_updated || (filter_h_c && (filter_h_c != dpp->filter_h_c))
							|| (filter_v_c && (filter_v_c != dpp->filter_v_c));
		}

		if (filter_updated) {
			uint32_t scl_mode = REG_READ(SCL_MODE);

			if (!h_2tap_hardcode_coef_en && filter_h) {
				dpp1_dscl_set_scaler_filter(
					dpp, scl_data->taps.h_taps,
					SCL_COEF_LUMA_HORZ_FILTER, filter_h);
			}
			dpp->filter_h = filter_h;
			if (!v_2tap_hardcode_coef_en && filter_v) {
				dpp1_dscl_set_scaler_filter(
					dpp, scl_data->taps.v_taps,
					SCL_COEF_LUMA_VERT_FILTER, filter_v);
			}
			dpp->filter_v = filter_v;
			if (chroma_coef_mode) {
				if (!h_2tap_hardcode_coef_en && filter_h_c) {
					dpp1_dscl_set_scaler_filter(
						dpp, scl_data->taps.h_taps_c,
						SCL_COEF_CHROMA_HORZ_FILTER, filter_h_c);
				}
				if (!v_2tap_hardcode_coef_en && filter_v_c) {
					dpp1_dscl_set_scaler_filter(
						dpp, scl_data->taps.v_taps_c,
						SCL_COEF_CHROMA_VERT_FILTER, filter_v_c);
				}
			}
			dpp->filter_h_c = filter_h_c;
			dpp->filter_v_c = filter_v_c;

			coef_ram_current = get_reg_field_value_ex(
				scl_mode, dpp->tf_mask->SCL_COEF_RAM_SELECT_CURRENT,
				dpp->tf_shift->SCL_COEF_RAM_SELECT_CURRENT);

			/* Swap coefficient RAM and set chroma coefficient mode */
			REG_SET_2(SCL_MODE, scl_mode,
					SCL_COEF_RAM_SELECT, !coef_ram_current,
					SCL_CHROMA_COEF_MODE, chroma_coef_mode);
		}
	}
}

static int dpp1_dscl_get_lb_depth_bpc(enum lb_pixel_depth depth)
{
	if (depth == LB_PIXEL_DEPTH_30BPP)
		return 10;
	else if (depth == LB_PIXEL_DEPTH_24BPP)
		return 8;
	else if (depth == LB_PIXEL_DEPTH_18BPP)
		return 6;
	else if (depth == LB_PIXEL_DEPTH_36BPP)
		return 12;
	else {
		BREAK_TO_DEBUGGER();
		return -1; /* Unsupported */
	}
}

void dpp1_dscl_calc_lb_num_partitions(
		const struct scaler_data *scl_data,
		enum lb_memory_config lb_config,
		int *num_part_y,
		int *num_part_c)
{
	int lb_memory_size, lb_memory_size_c, lb_memory_size_a, num_partitions_a,
	lb_bpc, memory_line_size_y, memory_line_size_c, memory_line_size_a;

	int line_size = scl_data->viewport.width < scl_data->recout.width ?
			scl_data->viewport.width : scl_data->recout.width;
	int line_size_c = scl_data->viewport_c.width < scl_data->recout.width ?
			scl_data->viewport_c.width : scl_data->recout.width;

	if (line_size == 0)
		line_size = 1;

	if (line_size_c == 0)
		line_size_c = 1;


	lb_bpc = dpp1_dscl_get_lb_depth_bpc(scl_data->lb_params.depth);
	memory_line_size_y = (line_size * lb_bpc + 71) / 72; /* +71 to ceil */
	memory_line_size_c = (line_size_c * lb_bpc + 71) / 72; /* +71 to ceil */
	memory_line_size_a = (line_size + 5) / 6; /* +5 to ceil */

	if (lb_config == LB_MEMORY_CONFIG_1) {
		lb_memory_size = 816;
		lb_memory_size_c = 816;
		lb_memory_size_a = 984;
	} else if (lb_config == LB_MEMORY_CONFIG_2) {
		lb_memory_size = 1088;
		lb_memory_size_c = 1088;
		lb_memory_size_a = 1312;
	} else if (lb_config == LB_MEMORY_CONFIG_3) {
		/* 420 mode: using 3rd mem from Y, Cr and Cb */
		lb_memory_size = 816 + 1088 + 848 + 848 + 848;
		lb_memory_size_c = 816 + 1088;
		lb_memory_size_a = 984 + 1312 + 456;
	} else {
		lb_memory_size = 816 + 1088 + 848;
		lb_memory_size_c = 816 + 1088 + 848;
		lb_memory_size_a = 984 + 1312 + 456;
	}
	*num_part_y = lb_memory_size / memory_line_size_y;
	*num_part_c = lb_memory_size_c / memory_line_size_c;
	num_partitions_a = lb_memory_size_a / memory_line_size_a;

	if (scl_data->lb_params.alpha_en
			&& (num_partitions_a < *num_part_y))
		*num_part_y = num_partitions_a;

	if (*num_part_y > 64)
		*num_part_y = 64;
	if (*num_part_c > 64)
		*num_part_c = 64;

}

bool dpp1_dscl_is_lb_conf_valid(int ceil_vratio, int num_partitions, int vtaps)
{
	if (ceil_vratio > 2)
		return vtaps <= (num_partitions - ceil_vratio + 2);
	else
		return vtaps <= num_partitions;
}

/*find first match configuration which meets the min required lb size*/
static enum lb_memory_config dpp1_dscl_find_lb_memory_config(struct dcn10_dpp *dpp,
		const struct scaler_data *scl_data)
{
	int num_part_y, num_part_c;
	int vtaps = scl_data->taps.v_taps;
	int vtaps_c = scl_data->taps.v_taps_c;
	int ceil_vratio = dc_fixpt_ceil(scl_data->ratios.vert);
	int ceil_vratio_c = dc_fixpt_ceil(scl_data->ratios.vert_c);

	if (dpp->base.ctx->dc->debug.use_max_lb) {
		if (scl_data->format == PIXEL_FORMAT_420BPP8
				|| scl_data->format == PIXEL_FORMAT_420BPP10)
			return LB_MEMORY_CONFIG_3;
		return LB_MEMORY_CONFIG_0;
	}

	dpp->base.caps->dscl_calc_lb_num_partitions(
			scl_data, LB_MEMORY_CONFIG_1, &num_part_y, &num_part_c);

	if (dpp1_dscl_is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
			&& dpp1_dscl_is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c))
		return LB_MEMORY_CONFIG_1;

	dpp->base.caps->dscl_calc_lb_num_partitions(
			scl_data, LB_MEMORY_CONFIG_2, &num_part_y, &num_part_c);

	if (dpp1_dscl_is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
			&& dpp1_dscl_is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c))
		return LB_MEMORY_CONFIG_2;

	if (scl_data->format == PIXEL_FORMAT_420BPP8
			|| scl_data->format == PIXEL_FORMAT_420BPP10) {
		dpp->base.caps->dscl_calc_lb_num_partitions(
				scl_data, LB_MEMORY_CONFIG_3, &num_part_y, &num_part_c);

		if (dpp1_dscl_is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
				&& dpp1_dscl_is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c))
			return LB_MEMORY_CONFIG_3;
	}

	dpp->base.caps->dscl_calc_lb_num_partitions(
			scl_data, LB_MEMORY_CONFIG_0, &num_part_y, &num_part_c);

	/*Ensure we can support the requested number of vtaps*/
	ASSERT(dpp1_dscl_is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
			&& dpp1_dscl_is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c));

	return LB_MEMORY_CONFIG_0;
}


static void dpp1_dscl_set_manual_ratio_init(
		struct dcn10_dpp *dpp, const struct scaler_data *data)
{
	uint32_t init_frac = 0;
	uint32_t init_int = 0;

	REG_SET(SCL_HORZ_FILTER_SCALE_RATIO, 0,
			SCL_H_SCALE_RATIO, dc_fixpt_u3d19(data->ratios.horz) << 5);

	REG_SET(SCL_VERT_FILTER_SCALE_RATIO, 0,
			SCL_V_SCALE_RATIO, dc_fixpt_u3d19(data->ratios.vert) << 5);

	REG_SET(SCL_HORZ_FILTER_SCALE_RATIO_C, 0,
			SCL_H_SCALE_RATIO_C, dc_fixpt_u3d19(data->ratios.horz_c) << 5);

	REG_SET(SCL_VERT_FILTER_SCALE_RATIO_C, 0,
			SCL_V_SCALE_RATIO_C, dc_fixpt_u3d19(data->ratios.vert_c) << 5);

	/*
	 * 0.24 format for fraction, first five bits zeroed
	 */
	init_frac = dc_fixpt_u0d19(data->inits.h) << 5;
	init_int = dc_fixpt_floor(data->inits.h);
	REG_SET_2(SCL_HORZ_FILTER_INIT, 0,
		SCL_H_INIT_FRAC, init_frac,
		SCL_H_INIT_INT, init_int);

	init_frac = dc_fixpt_u0d19(data->inits.h_c) << 5;
	init_int = dc_fixpt_floor(data->inits.h_c);
	REG_SET_2(SCL_HORZ_FILTER_INIT_C, 0,
		SCL_H_INIT_FRAC_C, init_frac,
		SCL_H_INIT_INT_C, init_int);

	init_frac = dc_fixpt_u0d19(data->inits.v) << 5;
	init_int = dc_fixpt_floor(data->inits.v);
	REG_SET_2(SCL_VERT_FILTER_INIT, 0,
		SCL_V_INIT_FRAC, init_frac,
		SCL_V_INIT_INT, init_int);

	if (REG(SCL_VERT_FILTER_INIT_BOT)) {
		struct fixed31_32 bot = dc_fixpt_add(data->inits.v, data->ratios.vert);

		init_frac = dc_fixpt_u0d19(bot) << 5;
		init_int = dc_fixpt_floor(bot);
		REG_SET_2(SCL_VERT_FILTER_INIT_BOT, 0,
			SCL_V_INIT_FRAC_BOT, init_frac,
			SCL_V_INIT_INT_BOT, init_int);
	}

	init_frac = dc_fixpt_u0d19(data->inits.v_c) << 5;
	init_int = dc_fixpt_floor(data->inits.v_c);
	REG_SET_2(SCL_VERT_FILTER_INIT_C, 0,
		SCL_V_INIT_FRAC_C, init_frac,
		SCL_V_INIT_INT_C, init_int);

	if (REG(SCL_VERT_FILTER_INIT_BOT_C)) {
		struct fixed31_32 bot = dc_fixpt_add(data->inits.v_c, data->ratios.vert_c);

		init_frac = dc_fixpt_u0d19(bot) << 5;
		init_int = dc_fixpt_floor(bot);
		REG_SET_2(SCL_VERT_FILTER_INIT_BOT_C, 0,
			SCL_V_INIT_FRAC_BOT_C, init_frac,
			SCL_V_INIT_INT_BOT_C, init_int);
	}
}

/**
 * dpp1_dscl_set_recout - Set the first pixel of RECOUT in the OTG active area
 *
 * @dpp: DPP data struct
 * @recout: Rectangle information
 *
 * This function sets the MPC RECOUT_START and RECOUT_SIZE registers based on
 * the values specified in the recount parameter.
 *
 * Note: This function only have effect if AutoCal is disabled.
 */
static void dpp1_dscl_set_recout(struct dcn10_dpp *dpp,
				 const struct rect *recout)
{
	int visual_confirm_on = 0;
	unsigned short visual_confirm_rect_height = VISUAL_CONFIRM_RECT_HEIGHT_DEFAULT;

	if (dpp->base.ctx->dc->debug.visual_confirm != VISUAL_CONFIRM_DISABLE)
		visual_confirm_on = 1;

	/* Check bounds to ensure the VC bar height was set to a sane value */
	if ((dpp->base.ctx->dc->debug.visual_confirm_rect_height >= VISUAL_CONFIRM_RECT_HEIGHT_MIN) &&
			(dpp->base.ctx->dc->debug.visual_confirm_rect_height <= VISUAL_CONFIRM_RECT_HEIGHT_MAX)) {
		visual_confirm_rect_height = dpp->base.ctx->dc->debug.visual_confirm_rect_height;
	}

	REG_SET_2(RECOUT_START, 0,
		  /* First pixel of RECOUT in the active OTG area */
		  RECOUT_START_X, recout->x,
		  /* First line of RECOUT in the active OTG area */
		  RECOUT_START_Y, recout->y);

	REG_SET_2(RECOUT_SIZE, 0,
		  /* Number of RECOUT horizontal pixels */
		  RECOUT_WIDTH, recout->width,
		  /* Number of RECOUT vertical lines */
		  RECOUT_HEIGHT, recout->height
			 - visual_confirm_on * 2 * (dpp->base.inst + visual_confirm_rect_height));
}

/**
 * dpp1_dscl_set_scaler_manual_scale - Manually program scaler and line buffer
 *
 * @dpp_base: High level DPP struct
 * @scl_data: scalaer_data info
 *
 * This is the primary function to program scaler and line buffer in manual
 * scaling mode. To execute the required operations for manual scale, we need
 * to disable AutoCal first.
 */
void dpp1_dscl_set_scaler_manual_scale(struct dpp *dpp_base,
				       const struct scaler_data *scl_data)
{
	enum lb_memory_config lb_config;
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	enum dscl_mode_sel dscl_mode = dpp1_dscl_get_dscl_mode(
			dpp_base, scl_data, dpp_base->ctx->dc->debug.always_scale);
	bool ycbcr = scl_data->format >= PIXEL_FORMAT_VIDEO_BEGIN
				&& scl_data->format <= PIXEL_FORMAT_VIDEO_END;

	if (memcmp(&dpp->scl_data, scl_data, sizeof(*scl_data)) == 0)
		return;

	PERF_TRACE();

	dpp->scl_data = *scl_data;

	if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.dscl) {
		if (dscl_mode != DSCL_MODE_DSCL_BYPASS)
			dpp1_power_on_dscl(dpp_base, true);
	}

	/* Autocal off */
	REG_SET_3(DSCL_AUTOCAL, 0,
		AUTOCAL_MODE, AUTOCAL_MODE_OFF,
		AUTOCAL_NUM_PIPE, 0,
		AUTOCAL_PIPE_ID, 0);

	/* Recout */
	dpp1_dscl_set_recout(dpp, &scl_data->recout);

	/* MPC Size */
	REG_SET_2(MPC_SIZE, 0,
		/* Number of horizontal pixels of MPC */
			 MPC_WIDTH, scl_data->h_active,
		/* Number of vertical lines of MPC */
			 MPC_HEIGHT, scl_data->v_active);

	/* SCL mode */
	REG_UPDATE(SCL_MODE, DSCL_MODE, dscl_mode);

	if (dscl_mode == DSCL_MODE_DSCL_BYPASS) {
		if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.dscl)
			dpp1_power_on_dscl(dpp_base, false);
		return;
	}

	/* LB */
	lb_config =  dpp1_dscl_find_lb_memory_config(dpp, scl_data);
	dpp1_dscl_set_lb(dpp, &scl_data->lb_params, lb_config);

	if (dscl_mode == DSCL_MODE_SCALING_444_BYPASS)
		return;

	/* Black offsets */
	if (REG(SCL_BLACK_OFFSET)) {
		if (ycbcr)
			REG_SET_2(SCL_BLACK_OFFSET, 0,
					SCL_BLACK_OFFSET_RGB_Y, BLACK_OFFSET_RGB_Y,
					SCL_BLACK_OFFSET_CBCR, BLACK_OFFSET_CBCR);
		else

			REG_SET_2(SCL_BLACK_OFFSET, 0,
					SCL_BLACK_OFFSET_RGB_Y, BLACK_OFFSET_RGB_Y,
					SCL_BLACK_OFFSET_CBCR, BLACK_OFFSET_RGB_Y);
	}

	/* Manually calculate scale ratio and init values */
	dpp1_dscl_set_manual_ratio_init(dpp, scl_data);

	/* HTaps/VTaps */
	REG_SET_4(SCL_TAP_CONTROL, 0,
		SCL_V_NUM_TAPS, scl_data->taps.v_taps - 1,
		SCL_H_NUM_TAPS, scl_data->taps.h_taps - 1,
		SCL_V_NUM_TAPS_C, scl_data->taps.v_taps_c - 1,
		SCL_H_NUM_TAPS_C, scl_data->taps.h_taps_c - 1);

	dpp1_dscl_set_scl_filter(dpp, scl_data, ycbcr);
	PERF_TRACE();
}
