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

#include "include/grph_object_id.h"
#include "include/fixed31_32.h"
#include "include/logger_interface.h"

#include "reg_helper.h"
#include "dcn10_transform.h"
#include "basics/conversion.h"

#define NUM_PHASES    64
#define HORZ_MAX_TAPS 8
#define VERT_MAX_TAPS 8

#define BLACK_OFFSET_RGB_Y 0x0
#define BLACK_OFFSET_CBCR  0x8000

#define REG(reg)\
	xfm->tf_regs->reg

#define CTX \
	xfm->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	xfm->tf_shift->field_name, xfm->tf_mask->field_name


enum dcn10_coef_filter_type_sel {
	SCL_COEF_LUMA_VERT_FILTER = 0,
	SCL_COEF_LUMA_HORZ_FILTER = 1,
	SCL_COEF_CHROMA_VERT_FILTER = 2,
	SCL_COEF_CHROMA_HORZ_FILTER = 3,
	SCL_COEF_ALPHA_VERT_FILTER = 4,
	SCL_COEF_ALPHA_HORZ_FILTER = 5
};

enum lb_memory_config {
	/* Enable all 3 pieces of memory */
	LB_MEMORY_CONFIG_0 = 0,

	/* Enable only the first piece of memory */
	LB_MEMORY_CONFIG_1 = 1,

	/* Enable only the second piece of memory */
	LB_MEMORY_CONFIG_2 = 2,

	/* Only applicable in 4:2:0 mode, enable all 3 pieces of memory and the
	 * last piece of chroma memory used for the luma storage
	 */
	LB_MEMORY_CONFIG_3 = 3
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

enum gamut_remap_select {
	GAMUT_REMAP_BYPASS = 0,
	GAMUT_REMAP_COEFF,
	GAMUT_REMAP_COMA_COEFF,
	GAMUT_REMAP_COMB_COEFF
};

static void transform_set_overscan(
	struct dcn10_transform *xfm,
	const struct scaler_data *data)
{
	uint32_t left = data->recout.x;
	uint32_t top = data->recout.y;

	int right = data->h_active - data->recout.x - data->recout.width;
	int bottom = data->v_active - data->recout.y - data->recout.height;

	if (right < 0) {
		BREAK_TO_DEBUGGER();
		right = 0;
	}
	if (bottom < 0) {
		BREAK_TO_DEBUGGER();
		bottom = 0;
	}

	REG_SET_2(DSCL_EXT_OVERSCAN_LEFT_RIGHT, 0,
		EXT_OVERSCAN_LEFT, left,
		EXT_OVERSCAN_RIGHT, right);

	REG_SET_2(DSCL_EXT_OVERSCAN_TOP_BOTTOM, 0,
		EXT_OVERSCAN_BOTTOM, bottom,
		EXT_OVERSCAN_TOP, top);
}

static void transform_set_otg_blank(
		struct dcn10_transform *xfm, const struct scaler_data *data)
{
	uint32_t h_blank_start = data->h_active;
	uint32_t h_blank_end = 0;
	uint32_t v_blank_start = data->v_active;
	uint32_t v_blank_end = 0;

	REG_SET_2(OTG_H_BLANK, 0,
			OTG_H_BLANK_START, h_blank_start,
			OTG_H_BLANK_END, h_blank_end);

	REG_SET_2(OTG_V_BLANK, 0,
			OTG_V_BLANK_START, v_blank_start,
			OTG_V_BLANK_END, v_blank_end);
}

static enum dscl_mode_sel get_dscl_mode(
		const struct scaler_data *data, bool dbg_always_scale)
{
	const long long one = dal_fixed31_32_one.value;
	bool ycbcr = false;
	bool format420 = false;

	if (data->format == PIXEL_FORMAT_FP16)
		return DSCL_MODE_DSCL_BYPASS;

	if (data->format >= PIXEL_FORMAT_VIDEO_BEGIN
			&& data->format <= PIXEL_FORMAT_VIDEO_END)
		ycbcr = true;

	if (data->format == PIXEL_FORMAT_420BPP12 ||
			data->format == PIXEL_FORMAT_420BPP15)
		format420 = true;

	if (data->ratios.horz.value == one
			&& data->ratios.vert.value == one
			&& data->ratios.horz_c.value == one
			&& data->ratios.vert_c.value == one
			&& !dbg_always_scale)
		return DSCL_MODE_SCALING_444_BYPASS;

	if (!format420) {
		if (ycbcr)
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

static int get_pixel_depth_val(enum lb_pixel_depth depth)
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

static void transform_set_lb(
	struct dcn10_transform *xfm,
	const struct line_buffer_params *lb_params,
	enum lb_memory_config mem_size_config)
{
	uint32_t pixel_depth = get_pixel_depth_val(lb_params->depth);
	uint32_t dyn_pix_depth = lb_params->dynamic_pixel_depth;
	REG_SET_7(LB_DATA_FORMAT, 0,
		PIXEL_DEPTH, pixel_depth, /* Pixel depth stored in LB */
		PIXEL_EXPAN_MODE, lb_params->pixel_expan_mode, /* Pixel expansion mode */
		PIXEL_REDUCE_MODE, 1, /* Pixel reduction mode: Rounding */
		DYNAMIC_PIXEL_DEPTH, dyn_pix_depth, /* Dynamic expansion pixel depth */
		DITHER_EN, 0, /* Dithering enable: Disabled */
		INTERLEAVE_EN, lb_params->interleave_en, /* Interleave source enable */
		ALPHA_EN, lb_params->alpha_en); /* Alpha enable */

	REG_SET_2(LB_MEMORY_CTRL, 0,
		MEMORY_CONFIG, mem_size_config,
		LB_MAX_PARTITIONS, 63);
}

static void transform_set_scaler_filter(
		struct dcn10_transform *xfm,
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

#if 0
bool transform_set_pixel_storage_depth(
	struct transform *xfm,
	enum lb_pixel_depth depth,
	const struct bit_depth_reduction_params *bit_depth_params)
{
	struct dcn10_transform *xfm110 = TO_DCN10_TRANSFORM(xfm);
	bool ret = true;
	uint32_t value;
	enum dc_color_depth color_depth;

	value = dm_read_reg(xfm->ctx, LB_REG(mmLB_DATA_FORMAT));
	switch (depth) {
	case LB_PIXEL_DEPTH_18BPP:
		color_depth = COLOR_DEPTH_666;
		set_reg_field_value(value, 2, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	case LB_PIXEL_DEPTH_24BPP:
		color_depth = COLOR_DEPTH_888;
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	case LB_PIXEL_DEPTH_30BPP:
		color_depth = COLOR_DEPTH_101010;
		set_reg_field_value(value, 0, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	case LB_PIXEL_DEPTH_36BPP:
		color_depth = COLOR_DEPTH_121212;
		set_reg_field_value(value, 3, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 0, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	default:
		ret = false;
		break;
	}

	if (ret == true) {
		set_denormalization(xfm110, color_depth);
		ret = program_bit_depth_reduction(xfm110, color_depth,
				bit_depth_params);

		set_reg_field_value(value, 0, LB_DATA_FORMAT, ALPHA_EN);
		dm_write_reg(xfm->ctx, LB_REG(mmLB_DATA_FORMAT), value);
		if (!(xfm110->lb_pixel_depth_supported & depth)) {
			/* We should use unsupported capabilities
			 * unless it is required by w/a
			 */
			dm_logger_write(xfm->ctx->logger, LOG_WARNING,
				"%s: Capability not supported",
				__func__);
		}
	}

	return ret;
}
#endif

static const uint16_t *get_filter_coeffs_64p(int taps, struct fixed31_32 ratio)
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
		return filter_2tap_64p;
	else if (taps == 1)
		return NULL;
	else {
		/* should never happen, bug */
		BREAK_TO_DEBUGGER();
		return NULL;
	}
}

static void transform_set_scl_filter(
		struct dcn10_transform *xfm,
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
		&& (scl_data->taps.h_taps > 1 || scl_data->taps.h_taps_c > 1);
	v_2tap_hardcode_coef_en = scl_data->taps.v_taps < 3
					&& scl_data->taps.v_taps_c < 3
		&& (scl_data->taps.v_taps > 1 || scl_data->taps.v_taps_c > 1);

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

		filter_h = get_filter_coeffs_64p(
				scl_data->taps.h_taps, scl_data->ratios.horz);
		filter_v = get_filter_coeffs_64p(
				scl_data->taps.v_taps, scl_data->ratios.vert);

		filter_updated = (filter_h && (filter_h != xfm->filter_h))
				|| (filter_v && (filter_v != xfm->filter_v));

		if (chroma_coef_mode) {
			filter_h_c = get_filter_coeffs_64p(
					scl_data->taps.h_taps_c, scl_data->ratios.horz_c);
			filter_v_c = get_filter_coeffs_64p(
					scl_data->taps.v_taps_c, scl_data->ratios.vert_c);
			filter_updated = filter_updated || (filter_h_c && (filter_h_c != xfm->filter_h_c))
							|| (filter_v_c && (filter_v_c != xfm->filter_v_c));
		}

		if (filter_updated) {
			uint32_t scl_mode = REG_READ(SCL_MODE);

			if (!h_2tap_hardcode_coef_en && filter_h) {
				transform_set_scaler_filter(
					xfm, scl_data->taps.h_taps,
					SCL_COEF_LUMA_HORZ_FILTER, filter_h);
			}
			xfm->filter_h = filter_h;
			if (!v_2tap_hardcode_coef_en && filter_v) {
				transform_set_scaler_filter(
					xfm, scl_data->taps.v_taps,
					SCL_COEF_LUMA_VERT_FILTER, filter_v);
			}
			xfm->filter_v = filter_v;
			if (chroma_coef_mode) {
				if (!h_2tap_hardcode_coef_en && filter_h_c) {
					transform_set_scaler_filter(
						xfm, scl_data->taps.h_taps_c,
						SCL_COEF_CHROMA_HORZ_FILTER, filter_h_c);
				}
				if (!v_2tap_hardcode_coef_en && filter_v_c) {
					transform_set_scaler_filter(
						xfm, scl_data->taps.v_taps_c,
						SCL_COEF_CHROMA_VERT_FILTER, filter_v_c);
				}
			}
			xfm->filter_h_c = filter_h_c;
			xfm->filter_v_c = filter_v_c;

			coef_ram_current = get_reg_field_value_ex(
				scl_mode, xfm->tf_mask->SCL_COEF_RAM_SELECT_CURRENT,
				xfm->tf_shift->SCL_COEF_RAM_SELECT_CURRENT);

			/* Swap coefficient RAM and set chroma coefficient mode */
			REG_SET_2(SCL_MODE, scl_mode,
					SCL_COEF_RAM_SELECT, !coef_ram_current,
					SCL_CHROMA_COEF_MODE, chroma_coef_mode);
		}
	}
}

static void transform_set_viewport(
		struct dcn10_transform *xfm,
		const struct rect *viewport,
		const struct rect *viewport_c)
{
	REG_SET_2(DCSURF_PRI_VIEWPORT_DIMENSION, 0,
			PRI_VIEWPORT_WIDTH, viewport->width,
			PRI_VIEWPORT_HEIGHT, viewport->height);

	REG_SET_2(DCSURF_PRI_VIEWPORT_START, 0,
			PRI_VIEWPORT_X_START, viewport->x,
			PRI_VIEWPORT_Y_START, viewport->y);

	/*for stereo*/
	REG_SET_2(DCSURF_SEC_VIEWPORT_DIMENSION, 0,
				SEC_VIEWPORT_WIDTH, viewport->width,
				SEC_VIEWPORT_HEIGHT, viewport->height);

	REG_SET_2(DCSURF_SEC_VIEWPORT_START, 0,
				SEC_VIEWPORT_X_START, viewport->x,
				SEC_VIEWPORT_Y_START, viewport->y);

	/* DC supports NV12 only at the moment */
	REG_SET_2(DCSURF_PRI_VIEWPORT_DIMENSION_C, 0,
			PRI_VIEWPORT_WIDTH_C, viewport_c->width,
			PRI_VIEWPORT_HEIGHT_C, viewport_c->height);

	REG_SET_2(DCSURF_PRI_VIEWPORT_START_C, 0,
			PRI_VIEWPORT_X_START_C, viewport_c->x,
			PRI_VIEWPORT_Y_START_C, viewport_c->y);
}

static int get_lb_depth_bpc(enum lb_pixel_depth depth)
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

static void calc_lb_num_partitions(
		const struct scaler_data *scl_data,
		enum lb_memory_config lb_config,
		int *num_part_y,
		int *num_part_c)
{
	int line_size = scl_data->viewport.width < scl_data->recout.width ?
			scl_data->viewport.width : scl_data->recout.width;
	int line_size_c = scl_data->viewport_c.width < scl_data->recout.width ?
			scl_data->viewport_c.width : scl_data->recout.width;
	int lb_bpc = get_lb_depth_bpc(scl_data->lb_params.depth);
	int memory_line_size_y = (line_size * lb_bpc + 71) / 72; /* +71 to ceil */
	int memory_line_size_c = (line_size_c * lb_bpc + 71) / 72; /* +71 to ceil */
	int memory_line_size_a = (line_size + 5) / 6; /* +5 to ceil */
	int lb_memory_size, lb_memory_size_c, lb_memory_size_a, num_partitions_a;

	if (lb_config == LB_MEMORY_CONFIG_1) {
		lb_memory_size = 816;
		lb_memory_size_c = 816;
		lb_memory_size_a = 984;
	} else if (lb_config == LB_MEMORY_CONFIG_2) {
		lb_memory_size = 1088;
		lb_memory_size_c = 1088;
		lb_memory_size_a = 1312;
	} else if (lb_config == LB_MEMORY_CONFIG_3) {
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

static bool is_lb_conf_valid(int ceil_vratio, int num_partitions, int vtaps)
{
	if (ceil_vratio > 2)
		return vtaps <= (num_partitions - ceil_vratio + 2);
	else
		return vtaps <= num_partitions;
}

/*find first match configuration which meets the min required lb size*/
static enum lb_memory_config find_lb_memory_config(const struct scaler_data *scl_data)
{
	int num_part_y, num_part_c;
	int vtaps = scl_data->taps.v_taps;
	int vtaps_c = scl_data->taps.v_taps_c;
	int ceil_vratio = dal_fixed31_32_ceil(scl_data->ratios.vert);
	int ceil_vratio_c = dal_fixed31_32_ceil(scl_data->ratios.vert_c);

	calc_lb_num_partitions(
			scl_data, LB_MEMORY_CONFIG_1, &num_part_y, &num_part_c);

	if (is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
			&& is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c))
		return LB_MEMORY_CONFIG_1;

	calc_lb_num_partitions(
			scl_data, LB_MEMORY_CONFIG_2, &num_part_y, &num_part_c);

	if (is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
			&& is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c))
		return LB_MEMORY_CONFIG_2;

	if (scl_data->format == PIXEL_FORMAT_420BPP12
			|| scl_data->format == PIXEL_FORMAT_420BPP15) {
		calc_lb_num_partitions(
				scl_data, LB_MEMORY_CONFIG_3, &num_part_y, &num_part_c);

		if (is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
				&& is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c))
			return LB_MEMORY_CONFIG_3;
	}

	calc_lb_num_partitions(
			scl_data, LB_MEMORY_CONFIG_0, &num_part_y, &num_part_c);

	/*Ensure we can support the requested number of vtaps*/
	ASSERT(is_lb_conf_valid(ceil_vratio, num_part_y, vtaps)
			&& is_lb_conf_valid(ceil_vratio_c, num_part_c, vtaps_c));

	return LB_MEMORY_CONFIG_0;
}

void transform_set_scaler_auto_scale(
	struct transform *xfm_base,
	const struct scaler_data *scl_data)
{
	enum lb_memory_config lb_config;
	struct dcn10_transform *xfm = TO_DCN10_TRANSFORM(xfm_base);
	enum dscl_mode_sel dscl_mode = get_dscl_mode(
			scl_data, xfm_base->ctx->dc->debug.always_scale);
	bool ycbcr = scl_data->format >= PIXEL_FORMAT_VIDEO_BEGIN
				&& scl_data->format <= PIXEL_FORMAT_VIDEO_END;

	transform_set_overscan(xfm, scl_data);

	transform_set_otg_blank(xfm, scl_data);

	REG_UPDATE(SCL_MODE, DSCL_MODE, dscl_mode);

	transform_set_viewport(xfm, &scl_data->viewport, &scl_data->viewport_c);

	if (dscl_mode == DSCL_MODE_DSCL_BYPASS)
		return;

	lb_config =  find_lb_memory_config(scl_data);
	transform_set_lb(xfm, &scl_data->lb_params, lb_config);

	if (dscl_mode == DSCL_MODE_SCALING_444_BYPASS)
		return;

	/* TODO: v_min */
	REG_SET_3(DSCL_AUTOCAL, 0,
		AUTOCAL_MODE, AUTOCAL_MODE_AUTOSCALE,
		AUTOCAL_NUM_PIPE, 0,
		AUTOCAL_PIPE_ID, 0);

	/* Black offsets */
	if (ycbcr)
		REG_SET_2(SCL_BLACK_OFFSET, 0,
				SCL_BLACK_OFFSET_RGB_Y, BLACK_OFFSET_RGB_Y,
				SCL_BLACK_OFFSET_CBCR, BLACK_OFFSET_CBCR);
	else

		REG_SET_2(SCL_BLACK_OFFSET, 0,
				SCL_BLACK_OFFSET_RGB_Y, BLACK_OFFSET_RGB_Y,
				SCL_BLACK_OFFSET_CBCR, BLACK_OFFSET_RGB_Y);

	REG_SET_4(SCL_TAP_CONTROL, 0,
		SCL_V_NUM_TAPS, scl_data->taps.v_taps - 1,
		SCL_H_NUM_TAPS, scl_data->taps.h_taps - 1,
		SCL_V_NUM_TAPS_C, scl_data->taps.v_taps_c - 1,
		SCL_H_NUM_TAPS_C, scl_data->taps.h_taps_c - 1);

	transform_set_scl_filter(xfm, scl_data, ycbcr);
}

/* Program gamut remap in bypass mode */
void transform_set_gamut_remap_bypass(struct dcn10_transform *xfm)
{
	REG_SET(CM_GAMUT_REMAP_CONTROL, 0,
			CM_GAMUT_REMAP_MODE, 0);
	/* Gamut remap in bypass */
}

static void transform_set_recout(
			struct dcn10_transform *xfm, const struct rect *recout)
{
	REG_SET_2(RECOUT_START, 0,
		/* First pixel of RECOUT */
			 RECOUT_START_X, recout->x,
		/* First line of RECOUT */
			 RECOUT_START_Y, recout->y);

	REG_SET_2(RECOUT_SIZE, 0,
		/* Number of RECOUT horizontal pixels */
			 RECOUT_WIDTH, recout->width,
		/* Number of RECOUT vertical lines */
			 RECOUT_HEIGHT, recout->height
			 - xfm->base.ctx->dc->debug.surface_visual_confirm * 2);
}

static void transform_set_manual_ratio_init(
		struct dcn10_transform *xfm, const struct scaler_data *data)
{
	uint32_t init_frac = 0;
	uint32_t init_int = 0;

	REG_SET(SCL_HORZ_FILTER_SCALE_RATIO, 0,
			SCL_H_SCALE_RATIO, dal_fixed31_32_u2d19(data->ratios.horz) << 5);

	REG_SET(SCL_VERT_FILTER_SCALE_RATIO, 0,
			SCL_V_SCALE_RATIO, dal_fixed31_32_u2d19(data->ratios.vert) << 5);

	REG_SET(SCL_HORZ_FILTER_SCALE_RATIO_C, 0,
			SCL_H_SCALE_RATIO_C, dal_fixed31_32_u2d19(data->ratios.horz_c) << 5);

	REG_SET(SCL_VERT_FILTER_SCALE_RATIO_C, 0,
			SCL_V_SCALE_RATIO_C, dal_fixed31_32_u2d19(data->ratios.vert_c) << 5);

	/*
	 * 0.24 format for fraction, first five bits zeroed
	 */
	init_frac = dal_fixed31_32_u0d19(data->inits.h) << 5;
	init_int = dal_fixed31_32_floor(data->inits.h);
	REG_SET_2(SCL_HORZ_FILTER_INIT, 0,
		SCL_H_INIT_FRAC, init_frac,
		SCL_H_INIT_INT, init_int);

	init_frac = dal_fixed31_32_u0d19(data->inits.h_c) << 5;
	init_int = dal_fixed31_32_floor(data->inits.h_c);
	REG_SET_2(SCL_HORZ_FILTER_INIT_C, 0,
		SCL_H_INIT_FRAC_C, init_frac,
		SCL_H_INIT_INT_C, init_int);

	init_frac = dal_fixed31_32_u0d19(data->inits.v) << 5;
	init_int = dal_fixed31_32_floor(data->inits.v);
	REG_SET_2(SCL_VERT_FILTER_INIT, 0,
		SCL_V_INIT_FRAC, init_frac,
		SCL_V_INIT_INT, init_int);

	init_frac = dal_fixed31_32_u0d19(data->inits.v_bot) << 5;
	init_int = dal_fixed31_32_floor(data->inits.v_bot);
	REG_SET_2(SCL_VERT_FILTER_INIT_BOT, 0,
		SCL_V_INIT_FRAC_BOT, init_frac,
		SCL_V_INIT_INT_BOT, init_int);

	init_frac = dal_fixed31_32_u0d19(data->inits.v_c) << 5;
	init_int = dal_fixed31_32_floor(data->inits.v_c);
	REG_SET_2(SCL_VERT_FILTER_INIT_C, 0,
		SCL_V_INIT_FRAC_C, init_frac,
		SCL_V_INIT_INT_C, init_int);

	init_frac = dal_fixed31_32_u0d19(data->inits.v_c_bot) << 5;
	init_int = dal_fixed31_32_floor(data->inits.v_c_bot);
	REG_SET_2(SCL_VERT_FILTER_INIT_BOT_C, 0,
		SCL_V_INIT_FRAC_BOT_C, init_frac,
		SCL_V_INIT_INT_BOT_C, init_int);
}

/* Main function to program scaler and line buffer in manual scaling mode */
static void transform_set_scaler_manual_scale(
	struct transform *xfm_base,
	const struct scaler_data *scl_data)
{
	enum lb_memory_config lb_config;
	struct dcn10_transform *xfm = TO_DCN10_TRANSFORM(xfm_base);
	enum dscl_mode_sel dscl_mode = get_dscl_mode(
			scl_data, xfm_base->ctx->dc->debug.always_scale);
	bool ycbcr = scl_data->format >= PIXEL_FORMAT_VIDEO_BEGIN
				&& scl_data->format <= PIXEL_FORMAT_VIDEO_END;

	/* Recout */
	transform_set_recout(xfm, &scl_data->recout);

	/* MPC Size */
	REG_SET_2(MPC_SIZE, 0,
		/* Number of horizontal pixels of MPC */
			 MPC_WIDTH, scl_data->h_active,
		/* Number of vertical lines of MPC */
			 MPC_HEIGHT, scl_data->v_active);

	/* SCL mode */
	REG_UPDATE(SCL_MODE, DSCL_MODE, dscl_mode);

	/* Viewport */
	transform_set_viewport(xfm, &scl_data->viewport, &scl_data->viewport_c);

	if (dscl_mode == DSCL_MODE_DSCL_BYPASS)
		return;
	/* LB */
	lb_config =  find_lb_memory_config(scl_data);
	transform_set_lb(xfm, &scl_data->lb_params, lb_config);

	if (dscl_mode == DSCL_MODE_SCALING_444_BYPASS)
		return;

	/* Autocal off */
	REG_SET_3(DSCL_AUTOCAL, 0,
		AUTOCAL_MODE, AUTOCAL_MODE_OFF,
		AUTOCAL_NUM_PIPE, 0,
		AUTOCAL_PIPE_ID, 0);

	/* Black offsets */
	if (ycbcr)
		REG_SET_2(SCL_BLACK_OFFSET, 0,
				SCL_BLACK_OFFSET_RGB_Y, BLACK_OFFSET_RGB_Y,
				SCL_BLACK_OFFSET_CBCR, BLACK_OFFSET_CBCR);
	else

		REG_SET_2(SCL_BLACK_OFFSET, 0,
				SCL_BLACK_OFFSET_RGB_Y, BLACK_OFFSET_RGB_Y,
				SCL_BLACK_OFFSET_CBCR, BLACK_OFFSET_RGB_Y);

	/* Manually calculate scale ratio and init values */
	transform_set_manual_ratio_init(xfm, scl_data);

	/* HTaps/VTaps */
	REG_SET_4(SCL_TAP_CONTROL, 0,
		SCL_V_NUM_TAPS, scl_data->taps.v_taps - 1,
		SCL_H_NUM_TAPS, scl_data->taps.h_taps - 1,
		SCL_V_NUM_TAPS_C, scl_data->taps.v_taps_c - 1,
		SCL_H_NUM_TAPS_C, scl_data->taps.h_taps_c - 1);

	transform_set_scl_filter(xfm, scl_data, ycbcr);
}

#define IDENTITY_RATIO(ratio) (dal_fixed31_32_u2d19(ratio) == (1 << 19))


static bool transform_get_optimal_number_of_taps(
		struct transform *xfm,
		struct scaler_data *scl_data,
		const struct scaling_taps *in_taps)
{
	uint32_t pixel_width;

	if (scl_data->viewport.width > scl_data->recout.width)
		pixel_width = scl_data->recout.width;
	else
		pixel_width = scl_data->viewport.width;

	/* TODO: add lb check */

	/* No support for programming ratio of 4, drop to 3.99999.. */
	if (scl_data->ratios.horz.value == (4ll << 32))
		scl_data->ratios.horz.value--;
	if (scl_data->ratios.vert.value == (4ll << 32))
		scl_data->ratios.vert.value--;
	if (scl_data->ratios.horz_c.value == (4ll << 32))
		scl_data->ratios.horz_c.value--;
	if (scl_data->ratios.vert_c.value == (4ll << 32))
		scl_data->ratios.vert_c.value--;

	/* Set default taps if none are provided */
	if (in_taps->h_taps == 0)
		scl_data->taps.h_taps = 4;
	else
		scl_data->taps.h_taps = in_taps->h_taps;
	if (in_taps->v_taps == 0)
		scl_data->taps.v_taps = 4;
	else
		scl_data->taps.v_taps = in_taps->v_taps;
	if (in_taps->v_taps_c == 0)
		scl_data->taps.v_taps_c = 2;
	else
		scl_data->taps.v_taps_c = in_taps->v_taps_c;
	if (in_taps->h_taps_c == 0)
		scl_data->taps.h_taps_c = 2;
	/* Only 1 and even h_taps_c are supported by hw */
	else if ((in_taps->h_taps_c % 2) != 0 && in_taps->h_taps_c != 1)
		scl_data->taps.h_taps_c = in_taps->h_taps_c - 1;
	else
		scl_data->taps.h_taps_c = in_taps->h_taps_c;

	if (!xfm->ctx->dc->debug.always_scale) {
		if (IDENTITY_RATIO(scl_data->ratios.horz))
			scl_data->taps.h_taps = 1;
		if (IDENTITY_RATIO(scl_data->ratios.vert))
			scl_data->taps.v_taps = 1;
		if (IDENTITY_RATIO(scl_data->ratios.horz_c))
			scl_data->taps.h_taps_c = 1;
		if (IDENTITY_RATIO(scl_data->ratios.vert_c))
			scl_data->taps.v_taps_c = 1;
	}

	return true;
}

static void transform_reset(struct transform *xfm_base)
{
	struct dcn10_transform *xfm = TO_DCN10_TRANSFORM(xfm_base);

	xfm->filter_h_c = NULL;
	xfm->filter_v_c = NULL;
	xfm->filter_h = NULL;
	xfm->filter_v = NULL;

	/* set boundary mode to 0 */
	REG_SET(DSCL_CONTROL, 0, SCL_BOUNDARY_MODE, 0);
}

static void program_gamut_remap(
		struct dcn10_transform *xfm,
		const uint16_t *regval,
		enum gamut_remap_select select)
{
	 uint16_t selection = 0;

	if (regval == NULL || select == GAMUT_REMAP_BYPASS) {
		REG_SET(CM_GAMUT_REMAP_CONTROL, 0,
				CM_GAMUT_REMAP_MODE, 0);
		return;
	}
	switch (select) {
	case GAMUT_REMAP_COEFF:
		selection = 1;
		break;
	case GAMUT_REMAP_COMA_COEFF:
		selection = 2;
		break;
	case GAMUT_REMAP_COMB_COEFF:
		selection = 3;
		break;
	default:
		break;
	}


	if (select == GAMUT_REMAP_COEFF) {

		REG_SET_2(CM_GAMUT_REMAP_C11_C12, 0,
				CM_GAMUT_REMAP_C11, regval[0],
				CM_GAMUT_REMAP_C12, regval[1]);
		regval += 2;
		REG_SET_2(CM_GAMUT_REMAP_C13_C14, 0,
				CM_GAMUT_REMAP_C13, regval[0],
				CM_GAMUT_REMAP_C14, regval[1]);
		regval += 2;
		REG_SET_2(CM_GAMUT_REMAP_C21_C22, 0,
				CM_GAMUT_REMAP_C21, regval[0],
				CM_GAMUT_REMAP_C22, regval[1]);
		regval += 2;
		REG_SET_2(CM_GAMUT_REMAP_C23_C24, 0,
				CM_GAMUT_REMAP_C23, regval[0],
				CM_GAMUT_REMAP_C24, regval[1]);
		regval += 2;
		REG_SET_2(CM_GAMUT_REMAP_C31_C32, 0,
				CM_GAMUT_REMAP_C31, regval[0],
				CM_GAMUT_REMAP_C32, regval[1]);
		regval += 2;
		REG_SET_2(CM_GAMUT_REMAP_C33_C34, 0,
				CM_GAMUT_REMAP_C33, regval[0],
				CM_GAMUT_REMAP_C34, regval[1]);

	} else  if (select == GAMUT_REMAP_COMA_COEFF) {
		REG_SET_2(CM_COMA_C11_C12, 0,
				CM_COMA_C11, regval[0],
				CM_COMA_C12, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C13_C14, 0,
				CM_COMA_C13, regval[0],
				CM_COMA_C14, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C21_C22, 0,
				CM_COMA_C21, regval[0],
				CM_COMA_C22, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C23_C24, 0,
				CM_COMA_C23, regval[0],
				CM_COMA_C24, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C31_C32, 0,
				CM_COMA_C31, regval[0],
				CM_COMA_C32, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C33_C34, 0,
				CM_COMA_C33, regval[0],
				CM_COMA_C34, regval[1]);

	} else {
		REG_SET_2(CM_COMB_C11_C12, 0,
				CM_COMB_C11, regval[0],
				CM_COMB_C12, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMB_C13_C14, 0,
				CM_COMB_C13, regval[0],
				CM_COMB_C14, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMB_C21_C22, 0,
				CM_COMB_C21, regval[0],
				CM_COMB_C22, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMB_C23_C24, 0,
				CM_COMB_C23, regval[0],
				CM_COMB_C24, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMB_C31_C32, 0,
				CM_COMB_C31, regval[0],
				CM_COMB_C32, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMB_C33_C34, 0,
				CM_COMB_C33, regval[0],
				CM_COMB_C34, regval[1]);
	}

	REG_SET(
			CM_GAMUT_REMAP_CONTROL, 0,
			CM_GAMUT_REMAP_MODE, selection);

}

static void dcn_transform_set_gamut_remap(
	struct transform *xfm,
	const struct xfm_grph_csc_adjustment *adjust)
{
	struct dcn10_transform *dcn_xfm = TO_DCN10_TRANSFORM(xfm);

	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW)
		/* Bypass if type is bypass or hw */
		program_gamut_remap(dcn_xfm, NULL, GAMUT_REMAP_BYPASS);
	else {
		struct fixed31_32 arr_matrix[12];
		uint16_t arr_reg_val[12];

		arr_matrix[0] = adjust->temperature_matrix[0];
		arr_matrix[1] = adjust->temperature_matrix[1];
		arr_matrix[2] = adjust->temperature_matrix[2];
		arr_matrix[3] = dal_fixed31_32_zero;

		arr_matrix[4] = adjust->temperature_matrix[3];
		arr_matrix[5] = adjust->temperature_matrix[4];
		arr_matrix[6] = adjust->temperature_matrix[5];
		arr_matrix[7] = dal_fixed31_32_zero;

		arr_matrix[8] = adjust->temperature_matrix[6];
		arr_matrix[9] = adjust->temperature_matrix[7];
		arr_matrix[10] = adjust->temperature_matrix[8];
		arr_matrix[11] = dal_fixed31_32_zero;

		convert_float_matrix(
			arr_reg_val, arr_matrix, 12);

		program_gamut_remap(dcn_xfm, arr_reg_val, GAMUT_REMAP_COEFF);
	}
}

static struct transform_funcs dcn10_transform_funcs = {

	.transform_reset = transform_reset,
	.transform_set_scaler = transform_set_scaler_manual_scale,
	.transform_get_optimal_number_of_taps =
			transform_get_optimal_number_of_taps,
	.transform_set_gamut_remap = dcn_transform_set_gamut_remap
};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dcn10_transform_construct(
	struct dcn10_transform *xfm,
	struct dc_context *ctx,
	const struct dcn_transform_registers *tf_regs,
	const struct dcn_transform_shift *tf_shift,
	const struct dcn_transform_mask *tf_mask)
{
	xfm->base.ctx = ctx;

	xfm->base.funcs = &dcn10_transform_funcs;

	xfm->tf_regs = tf_regs;
	xfm->tf_shift = tf_shift;
	xfm->tf_mask = tf_mask;

	xfm->lb_pixel_depth_supported =
		LB_PIXEL_DEPTH_18BPP |
		LB_PIXEL_DEPTH_24BPP |
		LB_PIXEL_DEPTH_30BPP;

	xfm->lb_bits_per_entry = LB_BITS_PER_ENTRY;
	xfm->lb_memory_size = LB_TOTAL_NUMBER_OF_ENTRIES; /*0x1404*/

	return true;
}
