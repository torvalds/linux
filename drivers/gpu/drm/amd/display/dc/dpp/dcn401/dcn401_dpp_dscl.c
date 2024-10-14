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
#include "dcn401/dcn401_dpp.h"
#include "basics/conversion.h"


#define NUM_PHASES    64
#define HORZ_MAX_TAPS 8
#define VERT_MAX_TAPS 8
#define NUM_LEVELS    32
#define BLACK_OFFSET_RGB_Y 0x0
#define BLACK_OFFSET_CBCR  0x8000


#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

enum dcn401_coef_filter_type_sel {
	SCL_COEF_LUMA_VERT_FILTER = 0,
	SCL_COEF_LUMA_HORZ_FILTER = 1,
	SCL_COEF_CHROMA_VERT_FILTER = 2,
	SCL_COEF_CHROMA_HORZ_FILTER = 3,
	SCL_COEF_ALPHA_VERT_FILTER = 4,
	SCL_COEF_ALPHA_HORZ_FILTER = 5,
	SCL_COEF_VERTICAL_BLUR_SCALE = SCL_COEF_ALPHA_VERT_FILTER,
	SCL_COEF_HORIZONTAL_BLUR_SCALE = SCL_COEF_ALPHA_HORZ_FILTER
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

static int dpp401_dscl_get_pixel_depth_val(enum lb_pixel_depth depth)
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

static bool dpp401_dscl_is_video_format(enum pixel_format format)
{
	if (format >= PIXEL_FORMAT_VIDEO_BEGIN
			&& format <= PIXEL_FORMAT_VIDEO_END)
		return true;
	else
		return false;
}

static bool dpp401_dscl_is_420_format(enum pixel_format format)
{
	if (format == PIXEL_FORMAT_420BPP8 ||
			format == PIXEL_FORMAT_420BPP10)
		return true;
	else
		return false;
}

static enum dscl_mode_sel dpp401_dscl_get_dscl_mode(
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

	if (!dpp401_dscl_is_420_format(data->format)) {
		if (dpp401_dscl_is_video_format(data->format))
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

static void dpp401_power_on_dscl(
	struct dpp *dpp_base,
	bool power_on)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);

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


static void dpp401_dscl_set_lb(
	struct dcn401_dpp *dpp,
	const struct line_buffer_params *lb_params,
	enum lb_memory_config mem_size_config)
{
	uint32_t max_partitions = 63; /* Currently hardcoded on all ASICs before DCN 3.2 */

	/* LB */
	if (dpp->base.caps->dscl_data_proc_format == DSCL_DATA_PRCESSING_FIXED_FORMAT) {
		/* DSCL caps: pixel data processed in fixed format */
		uint32_t pixel_depth = dpp401_dscl_get_pixel_depth_val(lb_params->depth);
		uint32_t dyn_pix_depth = lb_params->dynamic_pixel_depth;

		REG_SET_7(LB_DATA_FORMAT, 0,
			PIXEL_DEPTH, pixel_depth, /* Pixel depth stored in LB */
			PIXEL_EXPAN_MODE, lb_params->pixel_expan_mode, /* Pixel expansion mode */
			PIXEL_REDUCE_MODE, 1, /* Pixel reduction mode: Rounding */
			DYNAMIC_PIXEL_DEPTH, dyn_pix_depth, /* Dynamic expansion pixel depth */
			DITHER_EN, 0, /* Dithering enable: Disabled */
			INTERLEAVE_EN, lb_params->interleave_en, /* Interleave source enable */
			LB_DATA_FORMAT__ALPHA_EN, lb_params->alpha_en); /* Alpha enable */
	}	else {
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

static const uint16_t *dpp401_dscl_get_filter_coeffs_64p(int taps, struct fixed31_32 ratio)
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

static void dpp401_dscl_set_scaler_filter(
		struct dcn401_dpp *dpp,
		uint32_t taps,
		enum dcn401_coef_filter_type_sel filter_type,
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

static void dpp401_dscl_set_scl_filter(
		struct dcn401_dpp *dpp,
		const struct scaler_data *scl_data,
		bool chroma_coef_mode,
		bool force_coeffs_update)
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

	if ((dpp->base.ctx->dc->config.use_spl) && (!dpp->base.ctx->dc->debug.disable_spl)) {
		filter_h = scl_data->dscl_prog_data.filter_h;
		filter_v = scl_data->dscl_prog_data.filter_v;
		if (chroma_coef_mode) {
			filter_h_c = scl_data->dscl_prog_data.filter_h_c;
			filter_v_c = scl_data->dscl_prog_data.filter_v_c;
		}
	} else {
		filter_h = dpp401_dscl_get_filter_coeffs_64p(
			scl_data->taps.h_taps, scl_data->ratios.horz);
		filter_v = dpp401_dscl_get_filter_coeffs_64p(
			scl_data->taps.v_taps, scl_data->ratios.vert);
		if (chroma_coef_mode) {
			filter_h_c = dpp401_dscl_get_filter_coeffs_64p(
				scl_data->taps.h_taps_c, scl_data->ratios.horz_c);
			filter_v_c = dpp401_dscl_get_filter_coeffs_64p(
				scl_data->taps.v_taps_c, scl_data->ratios.vert_c);
		}
	}

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

		filter_updated = (filter_h && (filter_h != dpp->filter_h))
				|| (filter_v && (filter_v != dpp->filter_v));

		if (chroma_coef_mode) {
			filter_updated = filter_updated || (filter_h_c && (filter_h_c != dpp->filter_h_c))
							|| (filter_v_c && (filter_v_c != dpp->filter_v_c));
		}

		if ((filter_updated) || (force_coeffs_update)) {
			uint32_t scl_mode = REG_READ(SCL_MODE);

			if (!h_2tap_hardcode_coef_en && filter_h) {
				dpp401_dscl_set_scaler_filter(
					dpp, scl_data->taps.h_taps,
					SCL_COEF_LUMA_HORZ_FILTER, filter_h);
			}
			dpp->filter_h = filter_h;
			if (!v_2tap_hardcode_coef_en && filter_v) {
				dpp401_dscl_set_scaler_filter(
					dpp, scl_data->taps.v_taps,
					SCL_COEF_LUMA_VERT_FILTER, filter_v);
			}
			dpp->filter_v = filter_v;
			if (chroma_coef_mode) {
				if (!h_2tap_hardcode_coef_en && filter_h_c) {
					dpp401_dscl_set_scaler_filter(
						dpp, scl_data->taps.h_taps_c,
						SCL_COEF_CHROMA_HORZ_FILTER, filter_h_c);
				}
				if (!v_2tap_hardcode_coef_en && filter_v_c) {
					dpp401_dscl_set_scaler_filter(
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

// TODO: Fix defined but not used error
//static int dpp401_dscl_get_lb_depth_bpc(enum lb_pixel_depth depth)
//{
//	if (depth == LB_PIXEL_DEPTH_30BPP)
//		return 10;
//	else if (depth == LB_PIXEL_DEPTH_24BPP)
//		return 8;
//	else if (depth == LB_PIXEL_DEPTH_18BPP)
//		return 6;
//	else if (depth == LB_PIXEL_DEPTH_36BPP)
//		return 12;
//	else {
//		BREAK_TO_DEBUGGER();
//		return -1; /* Unsupported */
//	}
//}

// TODO: Fix defined but not used error
//void dpp401_dscl_calc_lb_num_partitions(
//		const struct scaler_data *scl_data,
//		enum lb_memory_config lb_config,
//		int *num_part_y,
//		int *num_part_c)
//{
//	int lb_memory_size, lb_memory_size_c, lb_memory_size_a, num_partitions_a,
//	lb_bpc, memory_line_size_y, memory_line_size_c, memory_line_size_a;
//
//	int line_size = scl_data->viewport.width < scl_data->recout.width ?
//			scl_data->viewport.width : scl_data->recout.width;
//	int line_size_c = scl_data->viewport_c.width < scl_data->recout.width ?
//			scl_data->viewport_c.width : scl_data->recout.width;
//
//	if (line_size == 0)
//		line_size = 1;
//
//	if (line_size_c == 0)
//		line_size_c = 1;
//
//
//	lb_bpc = dpp401_dscl_get_lb_depth_bpc(scl_data->lb_params.depth);
//	memory_line_size_y = (line_size * lb_bpc + 71) / 72; /* +71 to ceil */
//	memory_line_size_c = (line_size_c * lb_bpc + 71) / 72; /* +71 to ceil */
//	memory_line_size_a = (line_size + 5) / 6; /* +5 to ceil */
//
//	if (lb_config == LB_MEMORY_CONFIG_1) {
//		lb_memory_size = 816;
//		lb_memory_size_c = 816;
//		lb_memory_size_a = 984;
//	} else if (lb_config == LB_MEMORY_CONFIG_2) {
//		lb_memory_size = 1088;
//		lb_memory_size_c = 1088;
//		lb_memory_size_a = 1312;
//	} else if (lb_config == LB_MEMORY_CONFIG_3) {
//		/* 420 mode: using 3rd mem from Y, Cr and Cb */
//		lb_memory_size = 816 + 1088 + 848 + 848 + 848;
//		lb_memory_size_c = 816 + 1088;
//		lb_memory_size_a = 984 + 1312 + 456;
//	} else {
//		lb_memory_size = 816 + 1088 + 848;
//		lb_memory_size_c = 816 + 1088 + 848;
//		lb_memory_size_a = 984 + 1312 + 456;
//	}
//	*num_part_y = lb_memory_size / memory_line_size_y;
//	*num_part_c = lb_memory_size_c / memory_line_size_c;
//	num_partitions_a = lb_memory_size_a / memory_line_size_a;
//
//	if (scl_data->lb_params.alpha_en
//			&& (num_partitions_a < *num_part_y))
//		*num_part_y = num_partitions_a;
//
//	if (*num_part_y > 64)
//		*num_part_y = 64;
//	if (*num_part_c > 64)
//		*num_part_c = 64;
//
//}

static bool dpp401_dscl_is_lb_conf_valid(int ceil_vratio, int num_partitions, int vtaps)
{
	if (ceil_vratio > 2)
		return vtaps <= (num_partitions - ceil_vratio + 2);
	else
		return vtaps <= num_partitions;
}

/*find first match configuration which meets the min required lb size*/
static enum lb_memory_config dpp401_dscl_find_lb_memory_config(struct dcn401_dpp *dpp,
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

	if (dpp401_dscl_is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
			&& dpp401_dscl_is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c))
		return LB_MEMORY_CONFIG_1;

	dpp->base.caps->dscl_calc_lb_num_partitions(
			scl_data, LB_MEMORY_CONFIG_2, &num_part_y, &num_part_c);

	if (dpp401_dscl_is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
			&& dpp401_dscl_is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c))
		return LB_MEMORY_CONFIG_2;

	if (scl_data->format == PIXEL_FORMAT_420BPP8
			|| scl_data->format == PIXEL_FORMAT_420BPP10) {
		dpp->base.caps->dscl_calc_lb_num_partitions(
				scl_data, LB_MEMORY_CONFIG_3, &num_part_y, &num_part_c);

		if (dpp401_dscl_is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
				&& dpp401_dscl_is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c))
			return LB_MEMORY_CONFIG_3;
	}

	dpp->base.caps->dscl_calc_lb_num_partitions(
			scl_data, LB_MEMORY_CONFIG_0, &num_part_y, &num_part_c);

	/*Ensure we can support the requested number of vtaps*/
	ASSERT(dpp401_dscl_is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
			&& dpp401_dscl_is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c));

	return LB_MEMORY_CONFIG_0;
}


static void dpp401_dscl_set_manual_ratio_init(
		struct dcn401_dpp *dpp, const struct scaler_data *data)
{
	uint32_t init_frac = 0;
	uint32_t init_int = 0;
	if ((dpp->base.ctx->dc->config.use_spl) && (!dpp->base.ctx->dc->debug.disable_spl)) {
		REG_SET(SCL_HORZ_FILTER_SCALE_RATIO, 0,
			SCL_H_SCALE_RATIO, data->dscl_prog_data.ratios.h_scale_ratio);

		REG_SET(SCL_VERT_FILTER_SCALE_RATIO, 0,
			SCL_V_SCALE_RATIO, data->dscl_prog_data.ratios.v_scale_ratio);

		REG_SET(SCL_HORZ_FILTER_SCALE_RATIO_C, 0,
			SCL_H_SCALE_RATIO_C, data->dscl_prog_data.ratios.h_scale_ratio_c);

		REG_SET(SCL_VERT_FILTER_SCALE_RATIO_C, 0,
			SCL_V_SCALE_RATIO_C, data->dscl_prog_data.ratios.v_scale_ratio_c);

		REG_SET_2(SCL_HORZ_FILTER_INIT, 0,
				SCL_H_INIT_FRAC, data->dscl_prog_data.init.h_filter_init_frac,
				SCL_H_INIT_INT, data->dscl_prog_data.init.h_filter_init_int);

		REG_SET_2(SCL_HORZ_FILTER_INIT_C, 0,
				SCL_H_INIT_FRAC_C, data->dscl_prog_data.init.h_filter_init_frac_c,
				SCL_H_INIT_INT_C, data->dscl_prog_data.init.h_filter_init_int_c);

		REG_SET_2(SCL_VERT_FILTER_INIT, 0,
				SCL_V_INIT_FRAC, data->dscl_prog_data.init.v_filter_init_frac,
				SCL_V_INIT_INT, data->dscl_prog_data.init.v_filter_init_int);

		if (REG(SCL_VERT_FILTER_INIT_BOT)) {
			REG_SET_2(SCL_VERT_FILTER_INIT_BOT, 0,
					SCL_V_INIT_FRAC_BOT, data->dscl_prog_data.init.v_filter_init_bot_frac,
					SCL_V_INIT_INT_BOT, data->dscl_prog_data.init.v_filter_init_bot_int);
		}

		REG_SET_2(SCL_VERT_FILTER_INIT_C, 0,
				SCL_V_INIT_FRAC_C, data->dscl_prog_data.init.v_filter_init_frac_c,
				SCL_V_INIT_INT_C, data->dscl_prog_data.init.v_filter_init_int_c);

		if (REG(SCL_VERT_FILTER_INIT_BOT_C)) {
			REG_SET_2(SCL_VERT_FILTER_INIT_BOT_C, 0,
					SCL_V_INIT_FRAC_BOT_C, data->dscl_prog_data.init.v_filter_init_bot_frac_c,
					SCL_V_INIT_INT_BOT_C, data->dscl_prog_data.init.v_filter_init_bot_int_c);
		}
		return;
	}
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
 * dpp401_dscl_set_recout - Set the first pixel of RECOUT in the OTG active area
 *
 * @dpp: DPP data struct
 * @recout: Rectangle information
 *
 * This function sets the MPC RECOUT_START and RECOUT_SIZE registers based on
 * the values specified in the recount parameter.
 *
 * Note: This function only have effect if AutoCal is disabled.
 */
static void dpp401_dscl_set_recout(struct dcn401_dpp *dpp,
				 const struct rect *recout)
{
	REG_SET_2(RECOUT_START, 0,
		  /* First pixel of RECOUT in the active OTG area */
		  RECOUT_START_X, recout->x,
		  /* First line of RECOUT in the active OTG area */
		  RECOUT_START_Y, recout->y);

	REG_SET_2(RECOUT_SIZE, 0,
		  /* Number of RECOUT horizontal pixels */
		  RECOUT_WIDTH, recout->width,
		  /* Number of RECOUT vertical lines */
		  RECOUT_HEIGHT, recout->height);
}
/**
 * dpp401_dscl_program_easf_v - Program EASF_V
 *
 * @dpp_base: High level DPP struct
 * @scl_data: scalaer_data info
 *
 * This is the primary function to program vertical EASF registers
 *
 */
static void dpp401_dscl_program_easf_v(struct dpp *dpp_base, const struct scaler_data *scl_data)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);

	PERF_TRACE();
	/* DSCL_EASF_V_MODE */
	REG_SET_3(DSCL_EASF_V_MODE, 0,
			SCL_EASF_V_EN, scl_data->dscl_prog_data.easf_v_en,
			SCL_EASF_V_2TAP_SHARP_FACTOR, scl_data->dscl_prog_data.easf_v_sharp_factor,
			SCL_EASF_V_RINGEST_FORCE_EN, scl_data->dscl_prog_data.easf_v_ring);

	if (!scl_data->dscl_prog_data.easf_v_en) {
		PERF_TRACE();
		return;
	}

	/* DSCL_EASF_V_BF_CNTL */
	REG_SET_6(DSCL_EASF_V_BF_CNTL, 0,
			SCL_EASF_V_BF1_EN, scl_data->dscl_prog_data.easf_v_bf1_en,
			SCL_EASF_V_BF2_MODE, scl_data->dscl_prog_data.easf_v_bf2_mode,
			SCL_EASF_V_BF3_MODE, scl_data->dscl_prog_data.easf_v_bf3_mode,
			SCL_EASF_V_BF2_FLAT1_GAIN, scl_data->dscl_prog_data.easf_v_bf2_flat1_gain,
			SCL_EASF_V_BF2_FLAT2_GAIN, scl_data->dscl_prog_data.easf_v_bf2_flat2_gain,
			SCL_EASF_V_BF2_ROC_GAIN, scl_data->dscl_prog_data.easf_v_bf2_roc_gain);
	/* DSCL_EASF_V_RINGEST_3TAP_CNTLn */
	REG_SET_2(DSCL_EASF_V_RINGEST_3TAP_CNTL1, 0,
		SCL_EASF_V_RINGEST_3TAP_DNTILT_UPTILT, scl_data->dscl_prog_data.easf_v_ringest_3tap_dntilt_uptilt,
		SCL_EASF_V_RINGEST_3TAP_UPTILT_MAXVAL, scl_data->dscl_prog_data.easf_v_ringest_3tap_uptilt_max);
	REG_SET_2(DSCL_EASF_V_RINGEST_3TAP_CNTL2, 0,
		SCL_EASF_V_RINGEST_3TAP_DNTILT_SLOPE, scl_data->dscl_prog_data.easf_v_ringest_3tap_dntilt_slope,
		SCL_EASF_V_RINGEST_3TAP_UPTILT1_SLOPE, scl_data->dscl_prog_data.easf_v_ringest_3tap_uptilt1_slope);
	REG_SET_2(DSCL_EASF_V_RINGEST_3TAP_CNTL3, 0,
		SCL_EASF_V_RINGEST_3TAP_UPTILT2_SLOPE, scl_data->dscl_prog_data.easf_v_ringest_3tap_uptilt2_slope,
		SCL_EASF_V_RINGEST_3TAP_UPTILT2_OFFSET, scl_data->dscl_prog_data.easf_v_ringest_3tap_uptilt2_offset);
	/* DSCL_EASF_V_RINGEST_EVENTAP_REDUCE */
	REG_SET_2(DSCL_EASF_V_RINGEST_EVENTAP_REDUCE, 0,
		SCL_EASF_V_RINGEST_EVENTAP_REDUCEG1, scl_data->dscl_prog_data.easf_v_ringest_eventap_reduceg1,
		SCL_EASF_V_RINGEST_EVENTAP_REDUCEG2, scl_data->dscl_prog_data.easf_v_ringest_eventap_reduceg2);
	/* DSCL_EASF_V_RINGEST_EVENTAP_GAIN */
	REG_SET_2(DSCL_EASF_V_RINGEST_EVENTAP_GAIN, 0,
		SCL_EASF_V_RINGEST_EVENTAP_GAIN1, scl_data->dscl_prog_data.easf_v_ringest_eventap_gain1,
		SCL_EASF_V_RINGEST_EVENTAP_GAIN2, scl_data->dscl_prog_data.easf_v_ringest_eventap_gain2);
	/* DSCL_EASF_V_BF_FINAL_MAX_MIN */
	REG_SET_4(DSCL_EASF_V_BF_FINAL_MAX_MIN, 0,
			SCL_EASF_V_BF_MAXA, scl_data->dscl_prog_data.easf_v_bf_maxa,
			SCL_EASF_V_BF_MAXB, scl_data->dscl_prog_data.easf_v_bf_maxb,
			SCL_EASF_V_BF_MINA, scl_data->dscl_prog_data.easf_v_bf_mina,
			SCL_EASF_V_BF_MINB, scl_data->dscl_prog_data.easf_v_bf_minb);
	/* DSCL_EASF_V_BF1_PWL_SEGn */
	REG_SET_3(DSCL_EASF_V_BF1_PWL_SEG0, 0,
			SCL_EASF_V_BF1_PWL_IN_SEG0, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg0,
			SCL_EASF_V_BF1_PWL_BASE_SEG0, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg0,
			SCL_EASF_V_BF1_PWL_SLOPE_SEG0, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg0);
	REG_SET_3(DSCL_EASF_V_BF1_PWL_SEG1, 0,
			SCL_EASF_V_BF1_PWL_IN_SEG1, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg1,
			SCL_EASF_V_BF1_PWL_BASE_SEG1, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg1,
			SCL_EASF_V_BF1_PWL_SLOPE_SEG1, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg1);
	REG_SET_3(DSCL_EASF_V_BF1_PWL_SEG2, 0,
			SCL_EASF_V_BF1_PWL_IN_SEG2, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg2,
			SCL_EASF_V_BF1_PWL_BASE_SEG2, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg2,
			SCL_EASF_V_BF1_PWL_SLOPE_SEG2, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg2);
	REG_SET_3(DSCL_EASF_V_BF1_PWL_SEG3, 0,
			SCL_EASF_V_BF1_PWL_IN_SEG3, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg3,
			SCL_EASF_V_BF1_PWL_BASE_SEG3, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg3,
			SCL_EASF_V_BF1_PWL_SLOPE_SEG3, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg3);
	REG_SET_3(DSCL_EASF_V_BF1_PWL_SEG4, 0,
			SCL_EASF_V_BF1_PWL_IN_SEG4, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg4,
			SCL_EASF_V_BF1_PWL_BASE_SEG4, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg4,
			SCL_EASF_V_BF1_PWL_SLOPE_SEG4, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg4);
	REG_SET_3(DSCL_EASF_V_BF1_PWL_SEG5, 0,
			SCL_EASF_V_BF1_PWL_IN_SEG5, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg5,
			SCL_EASF_V_BF1_PWL_BASE_SEG5, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg5,
			SCL_EASF_V_BF1_PWL_SLOPE_SEG5, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg5);
	REG_SET_3(DSCL_EASF_V_BF1_PWL_SEG6, 0,
			SCL_EASF_V_BF1_PWL_IN_SEG6, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg6,
			SCL_EASF_V_BF1_PWL_BASE_SEG6, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg6,
			SCL_EASF_V_BF1_PWL_SLOPE_SEG6, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg6);
	REG_SET_2(DSCL_EASF_V_BF1_PWL_SEG7, 0,
			SCL_EASF_V_BF1_PWL_IN_SEG7, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg7,
			SCL_EASF_V_BF1_PWL_BASE_SEG7, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg7);
	/* DSCL_EASF_V_BF3_PWL_SEGn */
	REG_SET_3(DSCL_EASF_V_BF3_PWL_SEG0, 0,
			SCL_EASF_V_BF3_PWL_IN_SEG0, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set0,
			SCL_EASF_V_BF3_PWL_BASE_SEG0, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set0,
			SCL_EASF_V_BF3_PWL_SLOPE_SEG0, scl_data->dscl_prog_data.easf_v_bf3_pwl_slope_set0);
	REG_SET_3(DSCL_EASF_V_BF3_PWL_SEG1, 0,
			SCL_EASF_V_BF3_PWL_IN_SEG1, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set1,
			SCL_EASF_V_BF3_PWL_BASE_SEG1, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set1,
			SCL_EASF_V_BF3_PWL_SLOPE_SEG1, scl_data->dscl_prog_data.easf_v_bf3_pwl_slope_set1);
	REG_SET_3(DSCL_EASF_V_BF3_PWL_SEG2, 0,
			SCL_EASF_V_BF3_PWL_IN_SEG2, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set2,
			SCL_EASF_V_BF3_PWL_BASE_SEG2, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set2,
			SCL_EASF_V_BF3_PWL_SLOPE_SEG2, scl_data->dscl_prog_data.easf_v_bf3_pwl_slope_set2);
	REG_SET_3(DSCL_EASF_V_BF3_PWL_SEG3, 0,
			SCL_EASF_V_BF3_PWL_IN_SEG3, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set3,
			SCL_EASF_V_BF3_PWL_BASE_SEG3, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set3,
			SCL_EASF_V_BF3_PWL_SLOPE_SEG3, scl_data->dscl_prog_data.easf_v_bf3_pwl_slope_set3);
	REG_SET_3(DSCL_EASF_V_BF3_PWL_SEG4, 0,
			SCL_EASF_V_BF3_PWL_IN_SEG4, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set4,
			SCL_EASF_V_BF3_PWL_BASE_SEG4, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set4,
			SCL_EASF_V_BF3_PWL_SLOPE_SEG4, scl_data->dscl_prog_data.easf_v_bf3_pwl_slope_set4);
	REG_SET_2(DSCL_EASF_V_BF3_PWL_SEG5, 0,
			SCL_EASF_V_BF3_PWL_IN_SEG5, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set5,
			SCL_EASF_V_BF3_PWL_BASE_SEG5, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set5);
	PERF_TRACE();
}
/**
 * dpp401_dscl_program_easf_h - Program EASF_H
 *
 * @dpp_base: High level DPP struct
 * @scl_data: scalaer_data info
 *
 * This is the primary function to program horizontal EASF registers
 *
 */
static void dpp401_dscl_program_easf_h(struct dpp *dpp_base, const struct scaler_data *scl_data)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);

	PERF_TRACE();
	/* DSCL_EASF_H_MODE */
	REG_SET_3(DSCL_EASF_H_MODE, 0,
			SCL_EASF_H_EN, scl_data->dscl_prog_data.easf_h_en,
			SCL_EASF_H_2TAP_SHARP_FACTOR, scl_data->dscl_prog_data.easf_h_sharp_factor,
			SCL_EASF_H_RINGEST_FORCE_EN, scl_data->dscl_prog_data.easf_h_ring);

	if (!scl_data->dscl_prog_data.easf_h_en) {
		PERF_TRACE();
		return;
	}

	/* DSCL_EASF_H_BF_CNTL */
	REG_SET_6(DSCL_EASF_H_BF_CNTL, 0,
			SCL_EASF_H_BF1_EN, scl_data->dscl_prog_data.easf_h_bf1_en,
			SCL_EASF_H_BF2_MODE, scl_data->dscl_prog_data.easf_h_bf2_mode,
			SCL_EASF_H_BF3_MODE, scl_data->dscl_prog_data.easf_h_bf3_mode,
			SCL_EASF_H_BF2_FLAT1_GAIN, scl_data->dscl_prog_data.easf_h_bf2_flat1_gain,
			SCL_EASF_H_BF2_FLAT2_GAIN, scl_data->dscl_prog_data.easf_h_bf2_flat2_gain,
			SCL_EASF_H_BF2_ROC_GAIN, scl_data->dscl_prog_data.easf_h_bf2_roc_gain);
	/* DSCL_EASF_H_RINGEST_EVENTAP_REDUCE */
	REG_SET_2(DSCL_EASF_H_RINGEST_EVENTAP_REDUCE, 0,
			SCL_EASF_H_RINGEST_EVENTAP_REDUCEG1, scl_data->dscl_prog_data.easf_h_ringest_eventap_reduceg1,
			SCL_EASF_H_RINGEST_EVENTAP_REDUCEG2, scl_data->dscl_prog_data.easf_h_ringest_eventap_reduceg2);
	/* DSCL_EASF_H_RINGEST_EVENTAP_GAIN */
	REG_SET_2(DSCL_EASF_H_RINGEST_EVENTAP_GAIN, 0,
			SCL_EASF_H_RINGEST_EVENTAP_GAIN1, scl_data->dscl_prog_data.easf_h_ringest_eventap_gain1,
			SCL_EASF_H_RINGEST_EVENTAP_GAIN2, scl_data->dscl_prog_data.easf_h_ringest_eventap_gain2);
	/* DSCL_EASF_H_BF_FINAL_MAX_MIN */
	REG_SET_4(DSCL_EASF_H_BF_FINAL_MAX_MIN, 0,
			SCL_EASF_H_BF_MAXA, scl_data->dscl_prog_data.easf_h_bf_maxa,
			SCL_EASF_H_BF_MAXB, scl_data->dscl_prog_data.easf_h_bf_maxb,
			SCL_EASF_H_BF_MINA, scl_data->dscl_prog_data.easf_h_bf_mina,
			SCL_EASF_H_BF_MINB, scl_data->dscl_prog_data.easf_h_bf_minb);
	/* DSCL_EASF_H_BF1_PWL_SEGn */
	REG_SET_3(DSCL_EASF_H_BF1_PWL_SEG0, 0,
			SCL_EASF_H_BF1_PWL_IN_SEG0, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg0,
			SCL_EASF_H_BF1_PWL_BASE_SEG0, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg0,
			SCL_EASF_H_BF1_PWL_SLOPE_SEG0, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg0);
	REG_SET_3(DSCL_EASF_H_BF1_PWL_SEG1, 0,
			SCL_EASF_H_BF1_PWL_IN_SEG1, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg1,
			SCL_EASF_H_BF1_PWL_BASE_SEG1, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg1,
			SCL_EASF_H_BF1_PWL_SLOPE_SEG1, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg1);
	REG_SET_3(DSCL_EASF_H_BF1_PWL_SEG2, 0,
			SCL_EASF_H_BF1_PWL_IN_SEG2, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg2,
			SCL_EASF_H_BF1_PWL_BASE_SEG2, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg2,
			SCL_EASF_H_BF1_PWL_SLOPE_SEG2, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg2);
	REG_SET_3(DSCL_EASF_H_BF1_PWL_SEG3, 0,
			SCL_EASF_H_BF1_PWL_IN_SEG3, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg3,
			SCL_EASF_H_BF1_PWL_BASE_SEG3, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg3,
			SCL_EASF_H_BF1_PWL_SLOPE_SEG3, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg3);
	REG_SET_3(DSCL_EASF_H_BF1_PWL_SEG4, 0,
			SCL_EASF_H_BF1_PWL_IN_SEG4, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg4,
			SCL_EASF_H_BF1_PWL_BASE_SEG4, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg4,
			SCL_EASF_H_BF1_PWL_SLOPE_SEG4, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg4);
	REG_SET_3(DSCL_EASF_H_BF1_PWL_SEG5, 0,
			SCL_EASF_H_BF1_PWL_IN_SEG5, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg5,
			SCL_EASF_H_BF1_PWL_BASE_SEG5, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg5,
			SCL_EASF_H_BF1_PWL_SLOPE_SEG5, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg5);
	REG_SET_3(DSCL_EASF_H_BF1_PWL_SEG6, 0,
			SCL_EASF_H_BF1_PWL_IN_SEG6, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg6,
			SCL_EASF_H_BF1_PWL_BASE_SEG6, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg6,
			SCL_EASF_H_BF1_PWL_SLOPE_SEG6, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg6);
	REG_SET_2(DSCL_EASF_H_BF1_PWL_SEG7, 0,
			SCL_EASF_H_BF1_PWL_IN_SEG7, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg7,
			SCL_EASF_H_BF1_PWL_BASE_SEG7, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg7);
	/* DSCL_EASF_H_BF3_PWL_SEGn */
	REG_SET_3(DSCL_EASF_H_BF3_PWL_SEG0, 0,
			SCL_EASF_H_BF3_PWL_IN_SEG0, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set0,
			SCL_EASF_H_BF3_PWL_BASE_SEG0, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set0,
			SCL_EASF_H_BF3_PWL_SLOPE_SEG0, scl_data->dscl_prog_data.easf_h_bf3_pwl_slope_set0);
	REG_SET_3(DSCL_EASF_H_BF3_PWL_SEG1, 0,
			SCL_EASF_H_BF3_PWL_IN_SEG1, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set1,
			SCL_EASF_H_BF3_PWL_BASE_SEG1, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set1,
			SCL_EASF_H_BF3_PWL_SLOPE_SEG1, scl_data->dscl_prog_data.easf_h_bf3_pwl_slope_set1);
	REG_SET_3(DSCL_EASF_H_BF3_PWL_SEG2, 0,
			SCL_EASF_H_BF3_PWL_IN_SEG2, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set2,
			SCL_EASF_H_BF3_PWL_BASE_SEG2, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set2,
			SCL_EASF_H_BF3_PWL_SLOPE_SEG2, scl_data->dscl_prog_data.easf_h_bf3_pwl_slope_set2);
	REG_SET_3(DSCL_EASF_H_BF3_PWL_SEG3, 0,
			SCL_EASF_H_BF3_PWL_IN_SEG3, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set3,
			SCL_EASF_H_BF3_PWL_BASE_SEG3, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set3,
			SCL_EASF_H_BF3_PWL_SLOPE_SEG3, scl_data->dscl_prog_data.easf_h_bf3_pwl_slope_set3);
	REG_SET_3(DSCL_EASF_H_BF3_PWL_SEG4, 0,
			SCL_EASF_H_BF3_PWL_IN_SEG4, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set4,
			SCL_EASF_H_BF3_PWL_BASE_SEG4, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set4,
			SCL_EASF_H_BF3_PWL_SLOPE_SEG4, scl_data->dscl_prog_data.easf_h_bf3_pwl_slope_set4);
	REG_SET_2(DSCL_EASF_H_BF3_PWL_SEG5, 0,
			SCL_EASF_H_BF3_PWL_IN_SEG5, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set5,
			SCL_EASF_H_BF3_PWL_BASE_SEG5, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set5);
	PERF_TRACE();
}
/**
 * dpp401_dscl_program_easf - Program EASF
 *
 * @dpp_base: High level DPP struct
 * @scl_data: scalaer_data info
 *
 * This is the primary function to program EASF
 *
 */
static void dpp401_dscl_program_easf(struct dpp *dpp_base, const struct scaler_data *scl_data)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);

	PERF_TRACE();
	/* DSCL_SC_MODE */
	REG_SET_2(DSCL_SC_MODE, 0,
			SCL_SC_MATRIX_MODE, scl_data->dscl_prog_data.easf_matrix_mode,
			SCL_SC_LTONL_EN, scl_data->dscl_prog_data.easf_ltonl_en);
	/* DSCL_EASF_SC_MATRIX_C0C1, DSCL_EASF_SC_MATRIX_C2C3 */
	REG_SET_2(DSCL_SC_MATRIX_C0C1, 0,
			SCL_SC_MATRIX_C0, scl_data->dscl_prog_data.easf_matrix_c0,
			SCL_SC_MATRIX_C1, scl_data->dscl_prog_data.easf_matrix_c1);
	REG_SET_2(DSCL_SC_MATRIX_C2C3, 0,
			SCL_SC_MATRIX_C2, scl_data->dscl_prog_data.easf_matrix_c2,
			SCL_SC_MATRIX_C3, scl_data->dscl_prog_data.easf_matrix_c3);
	dpp401_dscl_program_easf_v(dpp_base, scl_data);
	dpp401_dscl_program_easf_h(dpp_base, scl_data);
	PERF_TRACE();
}
/**
 * dpp401_dscl_disable_easf - Disable EASF when no scaling (1:1)
 *
 * @dpp_base: High level DPP struct
 * @scl_data: scalaer_data info
 *
 * When we have 1:1 scaling, we need to disable EASF
 *
 */
static void dpp401_dscl_disable_easf(struct dpp *dpp_base, const struct scaler_data *scl_data)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);

	PERF_TRACE();
	/* DSCL_EASF_V_MODE */
	REG_UPDATE(DSCL_EASF_V_MODE,
			SCL_EASF_V_EN, scl_data->dscl_prog_data.easf_v_en);
	/* DSCL_EASF_H_MODE */
	REG_UPDATE(DSCL_EASF_H_MODE,
			SCL_EASF_H_EN, scl_data->dscl_prog_data.easf_h_en);
	PERF_TRACE();
}
static void dpp401_dscl_set_isharp_filter(
	struct dcn401_dpp *dpp, const uint32_t *filter)
{
	int level;
	uint32_t filter_data;
	if (filter == NULL)
		return;

	REG_UPDATE(ISHARP_DELTA_CTRL,
		ISHARP_DELTA_LUT_HOST_SELECT, 0);
	/* LUT data write is auto-indexed.  Write index once */
	REG_SET(ISHARP_DELTA_INDEX, 0,
			ISHARP_DELTA_INDEX, 0);
	for (level = 0; level < NUM_LEVELS; level++)	{
		filter_data = filter[level];
		REG_SET(ISHARP_DELTA_DATA, 0,
				ISHARP_DELTA_DATA, filter_data);
	}
} // dpp401_dscl_set_isharp_filter
/**
 * dpp401_dscl_program_isharp - Program isharp
 *
 * @dpp_base: High level DPP struct
 * @scl_data: scalaer_data info
 * @program_isharp_1dlut: flag to program isharp 1D LUT
 * @bs_coeffs_updated: Blur and Scale Coefficients update flag
 *
 * This is the primary function to program isharp
 *
 */
static void dpp401_dscl_program_isharp(struct dpp *dpp_base,
		const struct scaler_data *scl_data,
		bool program_isharp_1dlut,
		bool *bs_coeffs_updated)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);
	*bs_coeffs_updated = false;

	PERF_TRACE();
	/* ISHARP_MODE */
	REG_SET_6(ISHARP_MODE, 0,
		ISHARP_EN, scl_data->dscl_prog_data.isharp_en,
		ISHARP_NOISEDET_EN, scl_data->dscl_prog_data.isharp_noise_det.enable,
		ISHARP_NOISEDET_MODE, scl_data->dscl_prog_data.isharp_noise_det.mode,
		ISHARP_LBA_MODE, scl_data->dscl_prog_data.isharp_lba.mode,
		ISHARP_FMT_MODE, scl_data->dscl_prog_data.isharp_fmt.mode,
		ISHARP_FMT_NORM, scl_data->dscl_prog_data.isharp_fmt.norm);

	/* Skip remaining register programming if ISHARP is disabled */
	if (!scl_data->dscl_prog_data.isharp_en) {
		PERF_TRACE();
		return;
	}

	/* ISHARP_NOISEDET_THRESHOLD */
	REG_SET_2(ISHARP_NOISEDET_THRESHOLD, 0,
		ISHARP_NOISEDET_UTHRE, scl_data->dscl_prog_data.isharp_noise_det.uthreshold,
		ISHARP_NOISEDET_DTHRE, scl_data->dscl_prog_data.isharp_noise_det.dthreshold);

	/* ISHARP_NOISE_GAIN_PWL */
	REG_SET_3(ISHARP_NOISE_GAIN_PWL, 0,
		ISHARP_NOISEDET_PWL_START_IN, scl_data->dscl_prog_data.isharp_noise_det.pwl_start_in,
		ISHARP_NOISEDET_PWL_END_IN, scl_data->dscl_prog_data.isharp_noise_det.pwl_end_in,
		ISHARP_NOISEDET_PWL_SLOPE, scl_data->dscl_prog_data.isharp_noise_det.pwl_slope);

	/* ISHARP_LBA: IN_SEG, BASE_SEG, SLOPE_SEG */
	REG_SET_3(ISHARP_LBA_PWL_SEG0, 0,
		ISHARP_LBA_PWL_IN_SEG0, scl_data->dscl_prog_data.isharp_lba.in_seg[0],
		ISHARP_LBA_PWL_BASE_SEG0, scl_data->dscl_prog_data.isharp_lba.base_seg[0],
		ISHARP_LBA_PWL_SLOPE_SEG0, scl_data->dscl_prog_data.isharp_lba.slope_seg[0]);
	REG_SET_3(ISHARP_LBA_PWL_SEG1, 0,
		ISHARP_LBA_PWL_IN_SEG1, scl_data->dscl_prog_data.isharp_lba.in_seg[1],
		ISHARP_LBA_PWL_BASE_SEG1, scl_data->dscl_prog_data.isharp_lba.base_seg[1],
		ISHARP_LBA_PWL_SLOPE_SEG1, scl_data->dscl_prog_data.isharp_lba.slope_seg[1]);
	REG_SET_3(ISHARP_LBA_PWL_SEG2, 0,
		ISHARP_LBA_PWL_IN_SEG2, scl_data->dscl_prog_data.isharp_lba.in_seg[2],
		ISHARP_LBA_PWL_BASE_SEG2, scl_data->dscl_prog_data.isharp_lba.base_seg[2],
		ISHARP_LBA_PWL_SLOPE_SEG2, scl_data->dscl_prog_data.isharp_lba.slope_seg[2]);
	REG_SET_3(ISHARP_LBA_PWL_SEG3, 0,
		ISHARP_LBA_PWL_IN_SEG3, scl_data->dscl_prog_data.isharp_lba.in_seg[3],
		ISHARP_LBA_PWL_BASE_SEG3, scl_data->dscl_prog_data.isharp_lba.base_seg[3],
		ISHARP_LBA_PWL_SLOPE_SEG3, scl_data->dscl_prog_data.isharp_lba.slope_seg[3]);
	REG_SET_3(ISHARP_LBA_PWL_SEG4, 0,
		ISHARP_LBA_PWL_IN_SEG4, scl_data->dscl_prog_data.isharp_lba.in_seg[4],
		ISHARP_LBA_PWL_BASE_SEG4, scl_data->dscl_prog_data.isharp_lba.base_seg[4],
		ISHARP_LBA_PWL_SLOPE_SEG4, scl_data->dscl_prog_data.isharp_lba.slope_seg[4]);
	REG_SET_2(ISHARP_LBA_PWL_SEG5, 0,
		ISHARP_LBA_PWL_IN_SEG5, scl_data->dscl_prog_data.isharp_lba.in_seg[5],
		ISHARP_LBA_PWL_BASE_SEG5, scl_data->dscl_prog_data.isharp_lba.base_seg[5]);

	/* ISHARP_DELTA_LUT */
	if (!program_isharp_1dlut)
		dpp401_dscl_set_isharp_filter(dpp, scl_data->dscl_prog_data.isharp_delta);

	/* ISHARP_NLDELTA_SOFT_CLIP */
	REG_SET_6(ISHARP_NLDELTA_SOFT_CLIP, 0,
		ISHARP_NLDELTA_SCLIP_EN_P, scl_data->dscl_prog_data.isharp_nldelta_sclip.enable_p,
		ISHARP_NLDELTA_SCLIP_PIVOT_P, scl_data->dscl_prog_data.isharp_nldelta_sclip.pivot_p,
		ISHARP_NLDELTA_SCLIP_SLOPE_P, scl_data->dscl_prog_data.isharp_nldelta_sclip.slope_p,
		ISHARP_NLDELTA_SCLIP_EN_N, scl_data->dscl_prog_data.isharp_nldelta_sclip.enable_n,
		ISHARP_NLDELTA_SCLIP_PIVOT_N, scl_data->dscl_prog_data.isharp_nldelta_sclip.pivot_n,
		ISHARP_NLDELTA_SCLIP_SLOPE_N, scl_data->dscl_prog_data.isharp_nldelta_sclip.slope_n);

	/* Blur and Scale Coefficients - SCL_COEF_RAM_TAP_SELECT */
	if (scl_data->dscl_prog_data.isharp_en) {
		if (scl_data->dscl_prog_data.filter_blur_scale_v) {
			dpp401_dscl_set_scaler_filter(
				dpp, scl_data->taps.v_taps,
				SCL_COEF_VERTICAL_BLUR_SCALE,
				scl_data->dscl_prog_data.filter_blur_scale_v);
			*bs_coeffs_updated = true;
		}
		if (scl_data->dscl_prog_data.filter_blur_scale_h) {
			dpp401_dscl_set_scaler_filter(
				dpp, scl_data->taps.h_taps,
				SCL_COEF_HORIZONTAL_BLUR_SCALE,
				scl_data->dscl_prog_data.filter_blur_scale_h);
			*bs_coeffs_updated = true;
		}
	}
	PERF_TRACE();
} // dpp401_dscl_program_isharp
/**
 * dpp401_dscl_set_scaler_manual_scale - Manually program scaler and line buffer
 *
 * @dpp_base: High level DPP struct
 * @scl_data: scalaer_data info
 *
 * This is the primary function to program scaler and line buffer in manual
 * scaling mode. To execute the required operations for manual scale, we need
 * to disable AutoCal first.
 */
void dpp401_dscl_set_scaler_manual_scale(struct dpp *dpp_base,
				       const struct scaler_data *scl_data)
{
	enum lb_memory_config lb_config;
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);
	const struct rect *rect = &scl_data->recout;
	uint32_t mpc_width = scl_data->h_active;
	uint32_t mpc_height = scl_data->v_active;
	uint32_t v_num_taps = scl_data->taps.v_taps - 1;
	uint32_t v_num_taps_c = scl_data->taps.v_taps_c - 1;
	uint32_t h_num_taps = scl_data->taps.h_taps - 1;
	uint32_t h_num_taps_c = scl_data->taps.h_taps_c - 1;
	enum dscl_mode_sel dscl_mode = dpp401_dscl_get_dscl_mode(
			dpp_base, scl_data, dpp_base->ctx->dc->debug.always_scale);
	bool ycbcr = scl_data->format >= PIXEL_FORMAT_VIDEO_BEGIN
				&& scl_data->format <= PIXEL_FORMAT_VIDEO_END;
	bool program_isharp_1dlut = false;
	bool bs_coeffs_updated = false;


	if (memcmp(&dpp->scl_data, scl_data, sizeof(*scl_data)) == 0)
		return;

	PERF_TRACE();

	/* If only sharpness has changed, then only update 1dlut, then return */
	if (scl_data->dscl_prog_data.isharp_en &&
		(dpp->scl_data.dscl_prog_data.sharpness_level
		!= scl_data->dscl_prog_data.sharpness_level)) {
		/* ISHARP_DELTA_LUT */
		dpp401_dscl_set_isharp_filter(dpp, scl_data->dscl_prog_data.isharp_delta);
		dpp->scl_data.dscl_prog_data.sharpness_level = scl_data->dscl_prog_data.sharpness_level;
		dpp->scl_data.dscl_prog_data.isharp_delta = scl_data->dscl_prog_data.isharp_delta;

		if (memcmp(&dpp->scl_data, scl_data, sizeof(*scl_data)) == 0)
			return;
		program_isharp_1dlut = true;
	}

	dpp->scl_data = *scl_data;

	if ((dpp->base.ctx->dc->config.use_spl) && (!dpp->base.ctx->dc->debug.disable_spl)) {
		dscl_mode = (enum dscl_mode_sel) scl_data->dscl_prog_data.dscl_mode;
		rect = (struct rect *)&scl_data->dscl_prog_data.recout;
		mpc_width = scl_data->dscl_prog_data.mpc_size.width;
		mpc_height = scl_data->dscl_prog_data.mpc_size.height;
		v_num_taps = scl_data->dscl_prog_data.taps.v_taps;
		v_num_taps_c = scl_data->dscl_prog_data.taps.v_taps_c;
		h_num_taps = scl_data->dscl_prog_data.taps.h_taps;
		h_num_taps_c = scl_data->dscl_prog_data.taps.h_taps_c;
	}
	if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.dscl) {
		if (dscl_mode != DSCL_MODE_DSCL_BYPASS)
			dpp401_power_on_dscl(dpp_base, true);
	}

	/* Autocal off */
	REG_SET_3(DSCL_AUTOCAL, 0,
		AUTOCAL_MODE, AUTOCAL_MODE_OFF,
		AUTOCAL_NUM_PIPE, 0,
		AUTOCAL_PIPE_ID, 0);

	/*clean scaler boundary mode when Autocal off*/
	REG_SET(DSCL_CONTROL, 0,
		SCL_BOUNDARY_MODE, 0);

	/* Recout */
	dpp401_dscl_set_recout(dpp, rect);

	/* MPC Size */
	REG_SET_2(MPC_SIZE, 0,
		/* Number of horizontal pixels of MPC */
			 MPC_WIDTH, mpc_width,
		/* Number of vertical lines of MPC */
			 MPC_HEIGHT, mpc_height);

	/* SCL mode */
	REG_UPDATE(SCL_MODE, DSCL_MODE, dscl_mode);

	if (dscl_mode == DSCL_MODE_DSCL_BYPASS) {
		if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.dscl)
			dpp401_power_on_dscl(dpp_base, false);
		return;
	}

	/* LB */
	lb_config =  dpp401_dscl_find_lb_memory_config(dpp, scl_data);
	dpp401_dscl_set_lb(dpp, &scl_data->lb_params, lb_config);

	if (dscl_mode == DSCL_MODE_SCALING_444_BYPASS) {
		if (dpp->base.ctx->dc->config.prefer_easf)
			dpp401_dscl_disable_easf(dpp_base, scl_data);
		dpp401_dscl_program_isharp(dpp_base, scl_data, program_isharp_1dlut, &bs_coeffs_updated);
		return;
	}

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
	dpp401_dscl_set_manual_ratio_init(dpp, scl_data);

	/* HTaps/VTaps */
	REG_SET_4(SCL_TAP_CONTROL, 0,
		SCL_V_NUM_TAPS, v_num_taps,
		SCL_H_NUM_TAPS, h_num_taps,
		SCL_V_NUM_TAPS_C, v_num_taps_c,
		SCL_H_NUM_TAPS_C, h_num_taps_c);

	/* ISharp configuration
	 * - B&S coeffs are written to same coeff RAM as WB scaler coeffs
	 * - coeff RAM toggle is in EASF programming
	 * - if we are only programming B&S coeffs, then need to reprogram
	 *   WB scaler coeffs and toggle coeff RAM together
	 */
	//if (dpp->base.ctx->dc->config.prefer_easf)
	dpp401_dscl_program_isharp(dpp_base, scl_data, program_isharp_1dlut, &bs_coeffs_updated);

	dpp401_dscl_set_scl_filter(dpp, scl_data, ycbcr, bs_coeffs_updated);
	/* Edge adaptive scaler function configuration */
	if (dpp->base.ctx->dc->config.prefer_easf)
		dpp401_dscl_program_easf(dpp_base, scl_data);
	PERF_TRACE();
}
