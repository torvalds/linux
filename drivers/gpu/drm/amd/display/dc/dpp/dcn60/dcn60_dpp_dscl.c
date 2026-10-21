// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dm_services.h"

#include "core_types.h"

#include "reg_helper.h"
#include "dcn60/dcn60_dpp.h"
#include "basics/conversion.h"

#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

static void dpp60_power_on_dscl(struct dpp *dpp_base, bool power_on)
{
	struct dcn60_dpp *dpp = TO_DCN60_DPP(dpp_base);

	if (power_on) {
		REG_UPDATE(DSCL_MEM_PWR_CTRL, LUT_MEM_PWR_FORCE, 0);
		REG_UPDATE(DSCL_MEM_PWR_CTRL, LUT_MEM_PWR_DIS, 1);
		REG_WAIT(DSCL_MEM_PWR_STATUS, LUT_MEM_PWR_STATE, 0, 1, 100);
	} else {
		if (dpp->base.ctx->dc->debug.enable_mem_low_power.bits.dscl) {
			dpp->base.ctx->dc->optimized_required = true;
			dpp->base.deferred_reg_writes.bits.disable_dscl = true;
		}
	}
}

void dpp60_dscl_set_lb(
	struct dcn60_dpp *dpp,
	const struct line_buffer_params *lb_params,
	enum lb_memory_config mem_size_config)
{
	/* LB */
	REG_SET(LB_DATA_FORMAT, 0,
			LB_DATA_FORMAT__ALPHA_EN, lb_params->alpha_en); /* Alpha enable */

	REG_SET_2(LB_MEMORY_CTRL, 0,
		MEMORY_CONFIG, mem_size_config,
		LB_MAX_PARTITIONS, dpp->base.caps->max_lb_partitions);
}

void dpp60_dscl_set_manual_ratio_init(
	struct dcn60_dpp *dpp, const struct scaler_data *data)
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

		REG_SET_2(SCL_VERT_FILTER_INIT_C, 0,
			SCL_V_INIT_FRAC_C, data->dscl_prog_data.init.v_filter_init_frac_c,
			SCL_V_INIT_INT_C, data->dscl_prog_data.init.v_filter_init_int_c);

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

	init_frac = dc_fixpt_u0d19(data->inits.v_c) << 5;
	init_int = dc_fixpt_floor(data->inits.v_c);
	REG_SET_2(SCL_VERT_FILTER_INIT_C, 0,
		SCL_V_INIT_FRAC_C, init_frac,
		SCL_V_INIT_INT_C, init_int);
}

/**
 * dpp60_dscl_set_scaler_manual_scale - Manually program scaler and line buffer
 *
 * @dpp_base: High level DPP struct
 * @scl_data: scalaer_data info
 *
 * This is the primary function to program scaler and line buffer in manual
 * scaling mode. To execute the required operations for manual scale, we need
 * to disable AutoCal first.
 */
void dpp60_dscl_set_scaler_manual_scale(struct dpp *dpp_base,
				       const struct scaler_data *scl_data)
{
	enum lb_memory_config lb_config;
	struct dcn60_dpp *dpp = TO_DCN60_DPP(dpp_base);
	struct dcn401_dpp *dpp401 = TO_DCN401_DPP(dpp_base);
	const struct rect *rect = &scl_data->recout;
	uint32_t mpc_width = scl_data->h_active;
	uint32_t mpc_height = scl_data->v_active;
	uint32_t v_num_taps = scl_data->taps.v_taps - 1;
	uint32_t v_num_taps_c = scl_data->taps.v_taps_c - 1;
	uint32_t h_num_taps = scl_data->taps.h_taps - 1;
	uint32_t h_num_taps_c = scl_data->taps.h_taps_c - 1;
	enum dcn401_dscl_mode_sel dscl_mode = dpp401_dscl_get_dscl_mode(
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
		dpp401_dscl_set_isharp_filter(dpp401, scl_data->dscl_prog_data.isharp_delta);
		dpp->scl_data.dscl_prog_data.sharpness_level = scl_data->dscl_prog_data.sharpness_level;
		memcpy(dpp->scl_data.dscl_prog_data.isharp_delta, scl_data->dscl_prog_data.isharp_delta,
			sizeof(uint32_t) * ISHARP_LUT_TABLE_SIZE);

		if (memcmp(&dpp->scl_data, scl_data, sizeof(*scl_data)) == 0)
			return;
		program_isharp_1dlut = true;
	}

	dpp->scl_data = *scl_data;

	if ((dpp->base.ctx->dc->config.use_spl) && (!dpp->base.ctx->dc->debug.disable_spl)) {
		dscl_mode = (enum dcn401_dscl_mode_sel) scl_data->dscl_prog_data.dscl_mode;
		rect = (struct rect *)&scl_data->dscl_prog_data.recout;
		mpc_width = scl_data->dscl_prog_data.mpc_size.width;
		mpc_height = scl_data->dscl_prog_data.mpc_size.height;
		v_num_taps = scl_data->dscl_prog_data.taps.v_taps;
		v_num_taps_c = scl_data->dscl_prog_data.taps.v_taps_c;
		h_num_taps = scl_data->dscl_prog_data.taps.h_taps;
		h_num_taps_c = scl_data->dscl_prog_data.taps.h_taps_c;
	}

	/* Unconditionally power on DSCL - can be in light sleep otherwise. */
	if (dscl_mode != DCN401_DSCL_MODE_DSCL_BYPASS)
		dpp60_power_on_dscl(dpp_base, true);

	/* Autocal off */
	REG_SET_4(DSCL_AUTOCAL, 0,
		AUTOCAL_MODE, 0,
		AUTOCAL_FRAC_MODE, 0,
		AUTOCAL_NUM_PIPE, 0,
		AUTOCAL_PIPE_ID, 0);

	/*clean scaler boundary mode when Autocal off*/
	REG_SET(DSCL_CONTROL, 0,
		SCL_BOUNDARY_MODE, 0);

	/* Recout */
	dpp401_dscl_set_recout(dpp401, rect);

	/* MPC Size */
	REG_SET_2(MPC_SIZE, 0,
		/* Number of horizontal pixels of MPC */
			 MPC_WIDTH, mpc_width,
		/* Number of vertical lines of MPC */
			 MPC_HEIGHT, mpc_height);

	/* SCL mode */
	REG_UPDATE(SCL_MODE, DSCL_MODE, dscl_mode);

	if (dscl_mode == DCN401_DSCL_MODE_DSCL_BYPASS) {
		dpp60_power_on_dscl(dpp_base, false);
		return;
	}

	/* LB */
	lb_config =  dpp401_dscl_find_lb_memory_config(dpp401, scl_data);
	dpp60_dscl_set_lb(dpp, &scl_data->lb_params, lb_config);

	if (dscl_mode == DCN401_DSCL_MODE_SCALING_444_BYPASS) {
		if (dpp->base.ctx->dc->config.prefer_easf)
			dpp401_dscl_disable_easf(dpp_base, scl_data);
		dpp401_dscl_program_isharp(dpp_base, scl_data, program_isharp_1dlut, &bs_coeffs_updated);
		return;
	}

	/* Black color */
	if (ycbcr)
		REG_SET_2(SCL_BLACK_COLOR, 0,
				SCL_BLACK_COLOR_RGB_Y, BLACK_OFFSET_RGB_Y,
				SCL_BLACK_COLOR_CBCR, BLACK_OFFSET_CBCR);
	else
		REG_SET_2(SCL_BLACK_COLOR, 0,
				SCL_BLACK_COLOR_RGB_Y, BLACK_OFFSET_RGB_Y,
				SCL_BLACK_COLOR_CBCR, BLACK_OFFSET_RGB_Y);

	/* Manually calculate scale ratio and init values */
	dpp60_dscl_set_manual_ratio_init(dpp, scl_data);

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

	dpp401_dscl_set_scl_filter(dpp401, scl_data, ycbcr, bs_coeffs_updated);
	/* Edge adaptive scaler function configuration */
	if (dpp->base.ctx->dc->config.prefer_easf)
		dpp401_dscl_program_easf(dpp_base, scl_data);
	PERF_TRACE();
}

/**
 * dpp60_dscl_program_upsp - Manually program UPSP registers
 *
 * @dpp_base: High level DPP struct
 * @dscl_prog_data: dscl_prog_data info
 */
void dpp60_dscl_program_upsp(struct dpp *dpp_base,
		const struct dscl_prog_data *dscl_prog_data)
{
	struct dcn60_dpp *dpp = TO_DCN60_DPP(dpp_base);

	REG_SET_8(UPSP_MODE, 0,
			UPSP_MODE, dscl_prog_data->upsp_mode,
			UPSP_V_NUM_TAPS, dscl_prog_data->upsp_v_num_taps,
			UPSP_V_INIT_INT, dscl_prog_data->upsp_v_init_int,
			UPSP_V_INIT_FRAC, dscl_prog_data->upsp_v_init_frac,
			UPSP_H_NUM_TAPS, dscl_prog_data->upsp_h_num_taps,
			UPSP_H_INIT_INT, dscl_prog_data->upsp_h_init_int,
			UPSP_H_INIT_FRAC, dscl_prog_data->upsp_h_init_frac,
			UPSP_BOUNDARY_MODE, dscl_prog_data->upsp_boundary_mode);
	REG_SET_4(UPSP_V_COEF_P0, 0,
			UPSP_V_COEF_TAP0_P0, dscl_prog_data->upsp_v_coef_tap0_p0,
			UPSP_V_COEF_TAP1_P0, dscl_prog_data->upsp_v_coef_tap1_p0,
			UPSP_V_COEF_TAP2_P0, dscl_prog_data->upsp_v_coef_tap2_p0,
			UPSP_V_COEF_TAP3_P0, dscl_prog_data->upsp_v_coef_tap3_p0);
	REG_SET_4(UPSP_V_COEF_P1, 0,
			UPSP_V_COEF_TAP0_P1, dscl_prog_data->upsp_v_coef_tap0_p1,
			UPSP_V_COEF_TAP1_P1, dscl_prog_data->upsp_v_coef_tap1_p1,
			UPSP_V_COEF_TAP2_P1, dscl_prog_data->upsp_v_coef_tap2_p1,
			UPSP_V_COEF_TAP3_P1, dscl_prog_data->upsp_v_coef_tap3_p1);
	REG_SET_4(UPSP_H_COEF_P0, 0,
			UPSP_H_COEF_TAP0_P0, dscl_prog_data->upsp_h_coef_tap0_p0,
			UPSP_H_COEF_TAP1_P0, dscl_prog_data->upsp_h_coef_tap1_p0,
			UPSP_H_COEF_TAP2_P0, dscl_prog_data->upsp_h_coef_tap2_p0,
			UPSP_H_COEF_TAP3_P0, dscl_prog_data->upsp_h_coef_tap3_p0);
	REG_SET_4(UPSP_H_COEF_P1, 0,
			UPSP_H_COEF_TAP0_P1, dscl_prog_data->upsp_h_coef_tap0_p1,
			UPSP_H_COEF_TAP1_P1, dscl_prog_data->upsp_h_coef_tap1_p1,
			UPSP_H_COEF_TAP2_P1, dscl_prog_data->upsp_h_coef_tap2_p1,
			UPSP_H_COEF_TAP3_P1, dscl_prog_data->upsp_h_coef_tap3_p1);
	REG_SET_2(UPSP_CLAMP, 0,
			UPSP_CLAMP_MAX, dscl_prog_data->upsp_clamp_max,
			UPSP_CLAMP_MIN, dscl_prog_data->upsp_clamp_min);
}
