/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "dc_types.h"
#include "dc_bios_types.h"
#include "dc.h"

#include "include/grph_object_id.h"
#include "include/logger_interface.h"
#include "dce110_timing_generator.h"

#include "timing_generator.h"


#define NUMBER_OF_FRAME_TO_WAIT_ON_TRIGGERED_RESET 10

#define MAX_H_TOTAL (CRTC_H_TOTAL__CRTC_H_TOTAL_MASK + 1)
#define MAX_V_TOTAL (CRTC_V_TOTAL__CRTC_V_TOTAL_MASKhw + 1)

#define CRTC_REG(reg) (reg + tg110->offsets.crtc)
#define DCP_REG(reg) (reg + tg110->offsets.dcp)

/* Flowing register offsets are same in files of
 * dce/dce_11_0_d.h
 * dce/vi_polaris10_p/vi_polaris10_d.h
 *
 * So we can create dce110 timing generator to use it.
 */


/*
* apply_front_porch_workaround
*
* This is a workaround for a bug that has existed since R5xx and has not been
* fixed keep Front porch at minimum 2 for Interlaced mode or 1 for progressive.
*/
static void dce110_timing_generator_apply_front_porch_workaround(
	struct timing_generator *tg,
	struct dc_crtc_timing *timing)
{
	if (timing->flags.INTERLACE == 1) {
		if (timing->v_front_porch < 2)
			timing->v_front_porch = 2;
	} else {
		if (timing->v_front_porch < 1)
			timing->v_front_porch = 1;
	}
}

/*
 *****************************************************************************
 *  Function: is_in_vertical_blank
 *
 *  @brief
 *     check the current status of CRTC to check if we are in Vertical Blank
 *     regioneased" state
 *
 *  @return
 *     true if currently in blank region, false otherwise
 *
 *****************************************************************************
 */
static bool dce110_timing_generator_is_in_vertical_blank(
		struct timing_generator *tg)
{
	uint32_t addr = 0;
	uint32_t value = 0;
	uint32_t field = 0;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	addr = CRTC_REG(mmCRTC_STATUS);
	value = dm_read_reg(tg->ctx, addr);
	field = get_reg_field_value(value, CRTC_STATUS, CRTC_V_BLANK);
	return field == 1;
}

void dce110_timing_generator_set_early_control(
		struct timing_generator *tg,
		uint32_t early_cntl)
{
	uint32_t regval;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t address = CRTC_REG(mmCRTC_CONTROL);

	regval = dm_read_reg(tg->ctx, address);
	set_reg_field_value(regval, early_cntl,
			CRTC_CONTROL, CRTC_HBLANK_EARLY_CONTROL);
	dm_write_reg(tg->ctx, address, regval);
}

/*
 * Enable CRTC
 * Enable CRTC - call ASIC Control Object to enable Timing generator.
 */
bool dce110_timing_generator_enable_crtc(struct timing_generator *tg)
{
	enum bp_result result;

	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = 0;

	/*
	 * 3 is used to make sure V_UPDATE occurs at the beginning of the first
	 * line of vertical front porch
	 */
	set_reg_field_value(
		value,
		0,
		CRTC_MASTER_UPDATE_MODE,
		MASTER_UPDATE_MODE);

	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_MASTER_UPDATE_MODE), value);

	/* TODO: may want this on to catch underflow */
	value = 0;
	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_MASTER_UPDATE_LOCK), value);

	result = tg->bp->funcs->enable_crtc(tg->bp, tg110->controller_id, true);

	return result == BP_RESULT_OK;
}

void dce110_timing_generator_program_blank_color(
		struct timing_generator *tg,
		const struct tg_color *black_color)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t addr = CRTC_REG(mmCRTC_BLACK_COLOR);
	uint32_t value = dm_read_reg(tg->ctx, addr);

	set_reg_field_value(
		value,
		black_color->color_b_cb,
		CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_B_CB);
	set_reg_field_value(
		value,
		black_color->color_g_y,
		CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_G_Y);
	set_reg_field_value(
		value,
		black_color->color_r_cr,
		CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_R_CR);

	dm_write_reg(tg->ctx, addr, value);
}

/*
 *****************************************************************************
 *  Function: disable_stereo
 *
 *  @brief
 *     Disables active stereo on controller
 *     Frame Packing need to be disabled in vBlank or when CRTC not running
 *****************************************************************************
 */
#if 0
@TODOSTEREO
static void disable_stereo(struct timing_generator *tg)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t addr = CRTC_REG(mmCRTC_3D_STRUCTURE_CONTROL);
	uint32_t value = 0;
	uint32_t test = 0;
	uint32_t field = 0;
	uint32_t struc_en = 0;
	uint32_t struc_stereo_sel_ovr = 0;

	value = dm_read_reg(tg->ctx, addr);
	struc_en = get_reg_field_value(
			value,
			CRTC_3D_STRUCTURE_CONTROL,
			CRTC_3D_STRUCTURE_EN);

	struc_stereo_sel_ovr = get_reg_field_value(
			value,
			CRTC_3D_STRUCTURE_CONTROL,
			CRTC_3D_STRUCTURE_STEREO_SEL_OVR);

	/*
	 * When disabling Frame Packing in 2 step mode, we need to program both
	 * registers at the same frame
	 * Programming it in the beginning of VActive makes sure we are ok
	 */

	if (struc_en != 0 && struc_stereo_sel_ovr == 0) {
		tg->funcs->wait_for_vblank(tg);
		tg->funcs->wait_for_vactive(tg);
	}

	value = 0;
	dm_write_reg(tg->ctx, addr, value);

	addr = tg->regs[IDX_CRTC_STEREO_CONTROL];
	dm_write_reg(tg->ctx, addr, value);
}
#endif

/*
 * disable_crtc - call ASIC Control Object to disable Timing generator.
 */
bool dce110_timing_generator_disable_crtc(struct timing_generator *tg)
{
	enum bp_result result;

	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	result = tg->bp->funcs->enable_crtc(tg->bp, tg110->controller_id, false);

	/* Need to make sure stereo is disabled according to the DCE5.0 spec */

	/*
	 * @TODOSTEREO call this when adding stereo support
	 * tg->funcs->disable_stereo(tg);
	 */

	return result == BP_RESULT_OK;
}

/*
 * program_horz_count_by_2
 * Programs DxCRTC_HORZ_COUNT_BY2_EN - 1 for DVI 30bpp mode, 0 otherwise
 */
static void program_horz_count_by_2(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing)
{
	uint32_t regval;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	regval = dm_read_reg(tg->ctx,
			CRTC_REG(mmCRTC_COUNT_CONTROL));

	set_reg_field_value(regval, 0, CRTC_COUNT_CONTROL,
			CRTC_HORZ_COUNT_BY2_EN);

	if (timing->flags.HORZ_COUNT_BY_TWO)
		set_reg_field_value(regval, 1, CRTC_COUNT_CONTROL,
					CRTC_HORZ_COUNT_BY2_EN);

	dm_write_reg(tg->ctx,
			CRTC_REG(mmCRTC_COUNT_CONTROL), regval);
}

/*
 * program_timing_generator
 * Program CRTC Timing Registers - DxCRTC_H_*, DxCRTC_V_*, Pixel repetition.
 * Call ASIC Control Object to program Timings.
 */
bool dce110_timing_generator_program_timing_generator(
	struct timing_generator *tg,
	const struct dc_crtc_timing *dc_crtc_timing)
{
	enum bp_result result;
	struct bp_hw_crtc_timing_parameters bp_params;
	struct dc_crtc_timing patched_crtc_timing;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	uint32_t vsync_offset = dc_crtc_timing->v_border_bottom +
			dc_crtc_timing->v_front_porch;
	uint32_t v_sync_start =dc_crtc_timing->v_addressable + vsync_offset;

	uint32_t hsync_offset = dc_crtc_timing->h_border_right +
			dc_crtc_timing->h_front_porch;
	uint32_t h_sync_start = dc_crtc_timing->h_addressable + hsync_offset;

	memset(&bp_params, 0, sizeof(struct bp_hw_crtc_timing_parameters));

	/* Due to an asic bug we need to apply the Front Porch workaround prior
	 * to programming the timing.
	 */

	patched_crtc_timing = *dc_crtc_timing;

	dce110_timing_generator_apply_front_porch_workaround(tg, &patched_crtc_timing);

	bp_params.controller_id = tg110->controller_id;

	bp_params.h_total = patched_crtc_timing.h_total;
	bp_params.h_addressable =
		patched_crtc_timing.h_addressable;
	bp_params.v_total = patched_crtc_timing.v_total;
	bp_params.v_addressable = patched_crtc_timing.v_addressable;

	bp_params.h_sync_start = h_sync_start;
	bp_params.h_sync_width = patched_crtc_timing.h_sync_width;
	bp_params.v_sync_start = v_sync_start;
	bp_params.v_sync_width = patched_crtc_timing.v_sync_width;

	/* Set overscan */
	bp_params.h_overscan_left =
		patched_crtc_timing.h_border_left;
	bp_params.h_overscan_right =
		patched_crtc_timing.h_border_right;
	bp_params.v_overscan_top = patched_crtc_timing.v_border_top;
	bp_params.v_overscan_bottom =
		patched_crtc_timing.v_border_bottom;

	/* Set flags */
	if (patched_crtc_timing.flags.HSYNC_POSITIVE_POLARITY == 1)
		bp_params.flags.HSYNC_POSITIVE_POLARITY = 1;

	if (patched_crtc_timing.flags.VSYNC_POSITIVE_POLARITY == 1)
		bp_params.flags.VSYNC_POSITIVE_POLARITY = 1;

	if (patched_crtc_timing.flags.INTERLACE == 1)
		bp_params.flags.INTERLACE = 1;

	if (patched_crtc_timing.flags.HORZ_COUNT_BY_TWO == 1)
		bp_params.flags.HORZ_COUNT_BY_TWO = 1;

	result = tg->bp->funcs->program_crtc_timing(tg->bp, &bp_params);

	program_horz_count_by_2(tg, &patched_crtc_timing);

	tg110->base.funcs->enable_advanced_request(tg, true, &patched_crtc_timing);

	/* Enable stereo - only when we need to pack 3D frame. Other types
	 * of stereo handled in explicit call */

	return result == BP_RESULT_OK;
}

/*
 *****************************************************************************
 *  Function: set_drr
 *
 *  @brief
 *     Program dynamic refresh rate registers m_DxCRTC_V_TOTAL_*.
 *
 *  @param [in] pHwCrtcTiming: point to H
 *  wCrtcTiming struct
 *****************************************************************************
 */
void dce110_timing_generator_set_drr(
	struct timing_generator *tg,
	const struct drr_params *params)
{
	/* register values */
	uint32_t v_total_min = 0;
	uint32_t v_total_max = 0;
	uint32_t v_total_cntl = 0;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	uint32_t addr = 0;

	addr = CRTC_REG(mmCRTC_V_TOTAL_MIN);
	v_total_min = dm_read_reg(tg->ctx, addr);

	addr = CRTC_REG(mmCRTC_V_TOTAL_MAX);
	v_total_max = dm_read_reg(tg->ctx, addr);

	addr = CRTC_REG(mmCRTC_V_TOTAL_CONTROL);
	v_total_cntl = dm_read_reg(tg->ctx, addr);

	if (params != NULL &&
		params->vertical_total_max > 0 &&
		params->vertical_total_min > 0) {

		set_reg_field_value(v_total_max,
				params->vertical_total_max - 1,
				CRTC_V_TOTAL_MAX,
				CRTC_V_TOTAL_MAX);

		set_reg_field_value(v_total_min,
				params->vertical_total_min - 1,
				CRTC_V_TOTAL_MIN,
				CRTC_V_TOTAL_MIN);

		set_reg_field_value(v_total_cntl,
				1,
				CRTC_V_TOTAL_CONTROL,
				CRTC_V_TOTAL_MIN_SEL);

		set_reg_field_value(v_total_cntl,
				1,
				CRTC_V_TOTAL_CONTROL,
				CRTC_V_TOTAL_MAX_SEL);

		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_FORCE_LOCK_ON_EVENT);
		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_FORCE_LOCK_TO_MASTER_VSYNC);

		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_SET_V_TOTAL_MIN_MASK_EN);

		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_SET_V_TOTAL_MIN_MASK);
	} else {
		set_reg_field_value(v_total_cntl,
			0,
			CRTC_V_TOTAL_CONTROL,
			CRTC_SET_V_TOTAL_MIN_MASK);
		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_V_TOTAL_MIN_SEL);
		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_V_TOTAL_MAX_SEL);
		set_reg_field_value(v_total_min,
				0,
				CRTC_V_TOTAL_MIN,
				CRTC_V_TOTAL_MIN);
		set_reg_field_value(v_total_max,
				0,
				CRTC_V_TOTAL_MAX,
				CRTC_V_TOTAL_MAX);
		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_FORCE_LOCK_ON_EVENT);
		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_FORCE_LOCK_TO_MASTER_VSYNC);
	}

	addr = CRTC_REG(mmCRTC_V_TOTAL_MIN);
	dm_write_reg(tg->ctx, addr, v_total_min);

	addr = CRTC_REG(mmCRTC_V_TOTAL_MAX);
	dm_write_reg(tg->ctx, addr, v_total_max);

	addr = CRTC_REG(mmCRTC_V_TOTAL_CONTROL);
	dm_write_reg(tg->ctx, addr, v_total_cntl);
}

void dce110_timing_generator_set_static_screen_control(
	struct timing_generator *tg,
	uint32_t event_triggers,
	uint32_t num_frames)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t static_screen_cntl = 0;
	uint32_t addr = 0;

	// By register spec, it only takes 8 bit value
	if (num_frames > 0xFF)
		num_frames = 0xFF;

	addr = CRTC_REG(mmCRTC_STATIC_SCREEN_CONTROL);
	static_screen_cntl = dm_read_reg(tg->ctx, addr);

	set_reg_field_value(static_screen_cntl,
				event_triggers,
				CRTC_STATIC_SCREEN_CONTROL,
				CRTC_STATIC_SCREEN_EVENT_MASK);

	set_reg_field_value(static_screen_cntl,
				num_frames,
				CRTC_STATIC_SCREEN_CONTROL,
				CRTC_STATIC_SCREEN_FRAME_COUNT);

	dm_write_reg(tg->ctx, addr, static_screen_cntl);
}

/*
 * get_vblank_counter
 *
 * @brief
 * Get counter for vertical blanks. use register CRTC_STATUS_FRAME_COUNT which
 * holds the counter of frames.
 *
 * @param
 * struct timing_generator *tg - [in] timing generator which controls the
 * desired CRTC
 *
 * @return
 * Counter of frames, which should equal to number of vblanks.
 */
uint32_t dce110_timing_generator_get_vblank_counter(struct timing_generator *tg)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t addr = CRTC_REG(mmCRTC_STATUS_FRAME_COUNT);
	uint32_t value = dm_read_reg(tg->ctx, addr);
	uint32_t field = get_reg_field_value(
			value, CRTC_STATUS_FRAME_COUNT, CRTC_FRAME_COUNT);

	return field;
}

/*
 *****************************************************************************
 *  Function: dce110_timing_generator_get_position
 *
 *  @brief
 *     Returns CRTC vertical/horizontal counters
 *
 *  @param [out] position
 *****************************************************************************
 */
void dce110_timing_generator_get_position(struct timing_generator *tg,
	struct crtc_position *position)
{
	uint32_t value;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_STATUS_POSITION));

	position->horizontal_count = get_reg_field_value(
			value,
			CRTC_STATUS_POSITION,
			CRTC_HORZ_COUNT);

	position->vertical_count = get_reg_field_value(
			value,
			CRTC_STATUS_POSITION,
			CRTC_VERT_COUNT);

	value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_NOM_VERT_POSITION));

	position->nominal_vcount = get_reg_field_value(
			value,
			CRTC_NOM_VERT_POSITION,
			CRTC_VERT_COUNT_NOM);
}

/*
 *****************************************************************************
 *  Function: get_crtc_scanoutpos
 *
 *  @brief
 *     Returns CRTC vertical/horizontal counters
 *
 *  @param [out] vpos, hpos
 *****************************************************************************
 */
void dce110_timing_generator_get_crtc_scanoutpos(
	struct timing_generator *tg,
	uint32_t *v_blank_start,
	uint32_t *v_blank_end,
	uint32_t *h_position,
	uint32_t *v_position)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	struct crtc_position position;

	uint32_t value  = dm_read_reg(tg->ctx,
			CRTC_REG(mmCRTC_V_BLANK_START_END));

	*v_blank_start = get_reg_field_value(value,
					     CRTC_V_BLANK_START_END,
					     CRTC_V_BLANK_START);
	*v_blank_end = get_reg_field_value(value,
					   CRTC_V_BLANK_START_END,
					   CRTC_V_BLANK_END);

	dce110_timing_generator_get_position(
			tg, &position);

	*h_position = position.horizontal_count;
	*v_position = position.vertical_count;
}

/* TODO: is it safe to assume that mask/shift of Primary and Underlay
 * are the same?
 * For example: today CRTC_H_TOTAL == CRTCV_H_TOTAL but is it always
 * guaranteed? */
void dce110_timing_generator_program_blanking(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing)
{
	uint32_t vsync_offset = timing->v_border_bottom +
			timing->v_front_porch;
	uint32_t v_sync_start =timing->v_addressable + vsync_offset;

	uint32_t hsync_offset = timing->h_border_right +
			timing->h_front_porch;
	uint32_t h_sync_start = timing->h_addressable + hsync_offset;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	struct dc_context *ctx = tg->ctx;
	uint32_t value = 0;
	uint32_t addr = 0;
	uint32_t tmp = 0;

	addr = CRTC_REG(mmCRTC_H_TOTAL);
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		timing->h_total - 1,
		CRTC_H_TOTAL,
		CRTC_H_TOTAL);
	dm_write_reg(ctx, addr, value);

	addr = CRTC_REG(mmCRTC_V_TOTAL);
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		timing->v_total - 1,
		CRTC_V_TOTAL,
		CRTC_V_TOTAL);
	dm_write_reg(ctx, addr, value);

	/* In case of V_TOTAL_CONTROL is on, make sure V_TOTAL_MAX and
	 * V_TOTAL_MIN are equal to V_TOTAL.
	 */
	addr = CRTC_REG(mmCRTC_V_TOTAL_MAX);
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		timing->v_total - 1,
		CRTC_V_TOTAL_MAX,
		CRTC_V_TOTAL_MAX);
	dm_write_reg(ctx, addr, value);

	addr = CRTC_REG(mmCRTC_V_TOTAL_MIN);
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		timing->v_total - 1,
		CRTC_V_TOTAL_MIN,
		CRTC_V_TOTAL_MIN);
	dm_write_reg(ctx, addr, value);

	addr = CRTC_REG(mmCRTC_H_BLANK_START_END);
	value = dm_read_reg(ctx, addr);

	tmp = timing->h_total -
		(h_sync_start + timing->h_border_left);

	set_reg_field_value(
		value,
		tmp,
		CRTC_H_BLANK_START_END,
		CRTC_H_BLANK_END);

	tmp = tmp + timing->h_addressable +
		timing->h_border_left + timing->h_border_right;

	set_reg_field_value(
		value,
		tmp,
		CRTC_H_BLANK_START_END,
		CRTC_H_BLANK_START);

	dm_write_reg(ctx, addr, value);

	addr = CRTC_REG(mmCRTC_V_BLANK_START_END);
	value = dm_read_reg(ctx, addr);

	tmp = timing->v_total - (v_sync_start + timing->v_border_top);

	set_reg_field_value(
		value,
		tmp,
		CRTC_V_BLANK_START_END,
		CRTC_V_BLANK_END);

	tmp = tmp + timing->v_addressable + timing->v_border_top +
		timing->v_border_bottom;

	set_reg_field_value(
		value,
		tmp,
		CRTC_V_BLANK_START_END,
		CRTC_V_BLANK_START);

	dm_write_reg(ctx, addr, value);
}

void dce110_timing_generator_set_test_pattern(
	struct timing_generator *tg,
	/* TODO: replace 'controller_dp_test_pattern' by 'test_pattern_mode'
	 * because this is not DP-specific (which is probably somewhere in DP
	 * encoder) */
	enum controller_dp_test_pattern test_pattern,
	enum dc_color_depth color_depth)
{
	struct dc_context *ctx = tg->ctx;
	uint32_t value;
	uint32_t addr;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	enum test_pattern_color_format bit_depth;
	enum test_pattern_dyn_range dyn_range;
	enum test_pattern_mode mode;
	/* color ramp generator mixes 16-bits color */
	uint32_t src_bpc = 16;
	/* requested bpc */
	uint32_t dst_bpc;
	uint32_t index;
	/* RGB values of the color bars.
	 * Produce two RGB colors: RGB0 - white (all Fs)
	 * and RGB1 - black (all 0s)
	 * (three RGB components for two colors)
	 */
	uint16_t src_color[6] = {0xFFFF, 0xFFFF, 0xFFFF, 0x0000,
						0x0000, 0x0000};
	/* dest color (converted to the specified color format) */
	uint16_t dst_color[6];
	uint32_t inc_base;

	/* translate to bit depth */
	switch (color_depth) {
	case COLOR_DEPTH_666:
		bit_depth = TEST_PATTERN_COLOR_FORMAT_BPC_6;
	break;
	case COLOR_DEPTH_888:
		bit_depth = TEST_PATTERN_COLOR_FORMAT_BPC_8;
	break;
	case COLOR_DEPTH_101010:
		bit_depth = TEST_PATTERN_COLOR_FORMAT_BPC_10;
	break;
	case COLOR_DEPTH_121212:
		bit_depth = TEST_PATTERN_COLOR_FORMAT_BPC_12;
	break;
	default:
		bit_depth = TEST_PATTERN_COLOR_FORMAT_BPC_8;
	break;
	}

	switch (test_pattern) {
	case CONTROLLER_DP_TEST_PATTERN_COLORSQUARES:
	case CONTROLLER_DP_TEST_PATTERN_COLORSQUARES_CEA:
	{
		dyn_range = (test_pattern ==
				CONTROLLER_DP_TEST_PATTERN_COLORSQUARES_CEA ?
				TEST_PATTERN_DYN_RANGE_CEA :
				TEST_PATTERN_DYN_RANGE_VESA);
		mode = TEST_PATTERN_MODE_COLORSQUARES_RGB;
		value = 0;
		addr = CRTC_REG(mmCRTC_TEST_PATTERN_PARAMETERS);

		set_reg_field_value(
			value,
			6,
			CRTC_TEST_PATTERN_PARAMETERS,
			CRTC_TEST_PATTERN_VRES);
		set_reg_field_value(
			value,
			6,
			CRTC_TEST_PATTERN_PARAMETERS,
			CRTC_TEST_PATTERN_HRES);

		dm_write_reg(ctx, addr, value);

		addr = CRTC_REG(mmCRTC_TEST_PATTERN_CONTROL);
		value = 0;

		set_reg_field_value(
			value,
			1,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_EN);

		set_reg_field_value(
			value,
			mode,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_MODE);

		set_reg_field_value(
			value,
			dyn_range,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_DYNAMIC_RANGE);
		set_reg_field_value(
			value,
			bit_depth,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_COLOR_FORMAT);
		dm_write_reg(ctx, addr, value);
	}
	break;

	case CONTROLLER_DP_TEST_PATTERN_VERTICALBARS:
	case CONTROLLER_DP_TEST_PATTERN_HORIZONTALBARS:
	{
		mode = (test_pattern ==
			CONTROLLER_DP_TEST_PATTERN_VERTICALBARS ?
			TEST_PATTERN_MODE_VERTICALBARS :
			TEST_PATTERN_MODE_HORIZONTALBARS);

		switch (bit_depth) {
		case TEST_PATTERN_COLOR_FORMAT_BPC_6:
			dst_bpc = 6;
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_8:
			dst_bpc = 8;
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_10:
			dst_bpc = 10;
		break;
		default:
			dst_bpc = 8;
		break;
		}

		/* adjust color to the required colorFormat */
		for (index = 0; index < 6; index++) {
			/* dst = 2^dstBpc * src / 2^srcBpc = src >>
			 * (srcBpc - dstBpc);
			 */
			dst_color[index] =
				src_color[index] >> (src_bpc - dst_bpc);
		/* CRTC_TEST_PATTERN_DATA has 16 bits,
		 * lowest 6 are hardwired to ZERO
		 * color bits should be left aligned aligned to MSB
		 * XXXXXXXXXX000000 for 10 bit,
		 * XXXXXXXX00000000 for 8 bit and XXXXXX0000000000 for 6
		 */
			dst_color[index] <<= (16 - dst_bpc);
		}

		value = 0;
		addr = CRTC_REG(mmCRTC_TEST_PATTERN_PARAMETERS);
		dm_write_reg(ctx, addr, value);

		/* We have to write the mask before data, similar to pipeline.
		 * For example, for 8 bpc, if we want RGB0 to be magenta,
		 * and RGB1 to be cyan,
		 * we need to make 7 writes:
		 * MASK   DATA
		 * 000001 00000000 00000000                     set mask to R0
		 * 000010 11111111 00000000     R0 255, 0xFF00, set mask to G0
		 * 000100 00000000 00000000     G0 0,   0x0000, set mask to B0
		 * 001000 11111111 00000000     B0 255, 0xFF00, set mask to R1
		 * 010000 00000000 00000000     R1 0,   0x0000, set mask to G1
		 * 100000 11111111 00000000     G1 255, 0xFF00, set mask to B1
		 * 100000 11111111 00000000     B1 255, 0xFF00
		 *
		 * we will make a loop of 6 in which we prepare the mask,
		 * then write, then prepare the color for next write.
		 * first iteration will write mask only,
		 * but each next iteration color prepared in
		 * previous iteration will be written within new mask,
		 * the last component will written separately,
		 * mask is not changing between 6th and 7th write
		 * and color will be prepared by last iteration
		 */

		/* write color, color values mask in CRTC_TEST_PATTERN_MASK
		 * is B1, G1, R1, B0, G0, R0
		 */
		value = 0;
		addr = CRTC_REG(mmCRTC_TEST_PATTERN_COLOR);
		for (index = 0; index < 6; index++) {
			/* prepare color mask, first write PATTERN_DATA
			 * will have all zeros
			 */
			set_reg_field_value(
				value,
				(1 << index),
				CRTC_TEST_PATTERN_COLOR,
				CRTC_TEST_PATTERN_MASK);
			/* write color component */
			dm_write_reg(ctx, addr, value);
			/* prepare next color component,
			 * will be written in the next iteration
			 */
			set_reg_field_value(
				value,
				dst_color[index],
				CRTC_TEST_PATTERN_COLOR,
				CRTC_TEST_PATTERN_DATA);
		}
		/* write last color component,
		 * it's been already prepared in the loop
		 */
		dm_write_reg(ctx, addr, value);

		/* enable test pattern */
		addr = CRTC_REG(mmCRTC_TEST_PATTERN_CONTROL);
		value = 0;

		set_reg_field_value(
			value,
			1,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_EN);

		set_reg_field_value(
			value,
			mode,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_MODE);

		set_reg_field_value(
			value,
			0,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_DYNAMIC_RANGE);

		set_reg_field_value(
			value,
			bit_depth,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_COLOR_FORMAT);

		dm_write_reg(ctx, addr, value);
	}
	break;

	case CONTROLLER_DP_TEST_PATTERN_COLORRAMP:
	{
		mode = (bit_depth ==
			TEST_PATTERN_COLOR_FORMAT_BPC_10 ?
			TEST_PATTERN_MODE_DUALRAMP_RGB :
			TEST_PATTERN_MODE_SINGLERAMP_RGB);

		switch (bit_depth) {
		case TEST_PATTERN_COLOR_FORMAT_BPC_6:
			dst_bpc = 6;
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_8:
			dst_bpc = 8;
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_10:
			dst_bpc = 10;
		break;
		default:
			dst_bpc = 8;
		break;
		}

		/* increment for the first ramp for one color gradation
		 * 1 gradation for 6-bit color is 2^10
		 * gradations in 16-bit color
		 */
		inc_base = (src_bpc - dst_bpc);

		value = 0;
		addr = CRTC_REG(mmCRTC_TEST_PATTERN_PARAMETERS);

		switch (bit_depth) {
		case TEST_PATTERN_COLOR_FORMAT_BPC_6:
		{
			set_reg_field_value(
				value,
				inc_base,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_INC0);
			set_reg_field_value(
				value,
				0,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_INC1);
			set_reg_field_value(
				value,
				6,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_HRES);
			set_reg_field_value(
				value,
				6,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_VRES);
			set_reg_field_value(
				value,
				0,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_RAMP0_OFFSET);
		}
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_8:
		{
			set_reg_field_value(
				value,
				inc_base,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_INC0);
			set_reg_field_value(
				value,
				0,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_INC1);
			set_reg_field_value(
				value,
				8,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_HRES);
			set_reg_field_value(
				value,
				6,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_VRES);
			set_reg_field_value(
				value,
				0,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_RAMP0_OFFSET);
		}
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_10:
		{
			set_reg_field_value(
				value,
				inc_base,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_INC0);
			set_reg_field_value(
				value,
				inc_base + 2,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_INC1);
			set_reg_field_value(
				value,
				8,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_HRES);
			set_reg_field_value(
				value,
				5,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_VRES);
			set_reg_field_value(
				value,
				384 << 6,
				CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_RAMP0_OFFSET);
		}
		break;
		default:
		break;
		}
		dm_write_reg(ctx, addr, value);

		value = 0;
		addr = CRTC_REG(mmCRTC_TEST_PATTERN_COLOR);
		dm_write_reg(ctx, addr, value);

		/* enable test pattern */
		addr = CRTC_REG(mmCRTC_TEST_PATTERN_CONTROL);
		value = 0;

		set_reg_field_value(
			value,
			1,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_EN);

		set_reg_field_value(
			value,
			mode,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_MODE);

		set_reg_field_value(
			value,
			0,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_DYNAMIC_RANGE);
		/* add color depth translation here */
		set_reg_field_value(
			value,
			bit_depth,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_COLOR_FORMAT);

		dm_write_reg(ctx, addr, value);
	}
	break;
	case CONTROLLER_DP_TEST_PATTERN_VIDEOMODE:
	{
		value = 0;
		dm_write_reg(ctx, CRTC_REG(mmCRTC_TEST_PATTERN_CONTROL), value);
		dm_write_reg(ctx, CRTC_REG(mmCRTC_TEST_PATTERN_COLOR), value);
		dm_write_reg(ctx, CRTC_REG(mmCRTC_TEST_PATTERN_PARAMETERS),
				value);
	}
	break;
	default:
	break;
	}
}

/*
 * dce110_timing_generator_validate_timing
 * The timing generators support a maximum display size of is 8192 x 8192 pixels,
 * including both active display and blanking periods. Check H Total and V Total.
 */
bool dce110_timing_generator_validate_timing(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing,
	enum signal_type signal)
{
	uint32_t h_blank;
	uint32_t h_back_porch, hsync_offset, h_sync_start;

	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	ASSERT(timing != NULL);

	if (!timing)
		return false;

	hsync_offset = timing->h_border_right + timing->h_front_porch;
	h_sync_start = timing->h_addressable + hsync_offset;

	/* Currently we don't support 3D, so block all 3D timings */
	if (timing->timing_3d_format != TIMING_3D_FORMAT_NONE)
		return false;

	/* Temporarily blocking interlacing mode until it's supported */
	if (timing->flags.INTERLACE == 1)
		return false;

	/* Check maximum number of pixels supported by Timing Generator
	 * (Currently will never fail, in order to fail needs display which
	 * needs more than 8192 horizontal and
	 * more than 8192 vertical total pixels)
	 */
	if (timing->h_total > tg110->max_h_total ||
		timing->v_total > tg110->max_v_total)
		return false;

	h_blank = (timing->h_total - timing->h_addressable -
		timing->h_border_right -
		timing->h_border_left);

	if (h_blank < tg110->min_h_blank)
		return false;

	if (timing->h_front_porch < tg110->min_h_front_porch)
		return false;

	h_back_porch = h_blank - (h_sync_start -
		timing->h_addressable -
		timing->h_border_right -
		timing->h_sync_width);

	if (h_back_porch < tg110->min_h_back_porch)
		return false;

	return true;
}

/*
 * Wait till we are at the beginning of VBlank.
 */
void dce110_timing_generator_wait_for_vblank(struct timing_generator *tg)
{
	/* We want to catch beginning of VBlank here, so if the first try are
	 * in VBlank, we might be very close to Active, in this case wait for
	 * another frame
	 */
	while (dce110_timing_generator_is_in_vertical_blank(tg)) {
		if (!dce110_timing_generator_is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}

	while (!dce110_timing_generator_is_in_vertical_blank(tg)) {
		if (!dce110_timing_generator_is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}
}

/*
 * Wait till we are in VActive (anywhere in VActive)
 */
void dce110_timing_generator_wait_for_vactive(struct timing_generator *tg)
{
	while (dce110_timing_generator_is_in_vertical_blank(tg)) {
		if (!dce110_timing_generator_is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}
}

/*
 *****************************************************************************
 *  Function: dce110_timing_generator_setup_global_swap_lock
 *
 *  @brief
 *     Setups Global Swap Lock group for current pipe
 *     Pipe can join or leave GSL group, become a TimingServer or TimingClient
 *
 *  @param [in] gsl_params: setup data
 *****************************************************************************
 */
void dce110_timing_generator_setup_global_swap_lock(
	struct timing_generator *tg,
	const struct dcp_gsl_params *gsl_params)
{
	uint32_t value;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t address = DCP_REG(mmDCP_GSL_CONTROL);
	uint32_t check_point = FLIP_READY_BACK_LOOKUP;

	value = dm_read_reg(tg->ctx, address);

	/* This pipe will belong to GSL Group zero. */
	set_reg_field_value(value,
			    1,
			    DCP_GSL_CONTROL,
			    DCP_GSL0_EN);

	set_reg_field_value(value,
			    gsl_params->gsl_master == tg->inst,
			    DCP_GSL_CONTROL,
			    DCP_GSL_MASTER_EN);

	set_reg_field_value(value,
			    HFLIP_READY_DELAY,
			    DCP_GSL_CONTROL,
			    DCP_GSL_HSYNC_FLIP_FORCE_DELAY);

	/* Keep signal low (pending high) during 6 lines.
	 * Also defines minimum interval before re-checking signal. */
	set_reg_field_value(value,
			    HFLIP_CHECK_DELAY,
			    DCP_GSL_CONTROL,
			    DCP_GSL_HSYNC_FLIP_CHECK_DELAY);

	dm_write_reg(tg->ctx, CRTC_REG(mmDCP_GSL_CONTROL), value);
	value = 0;

	set_reg_field_value(value,
			    gsl_params->gsl_master,
			    DCIO_GSL0_CNTL,
			    DCIO_GSL0_VSYNC_SEL);

	set_reg_field_value(value,
			    0,
			    DCIO_GSL0_CNTL,
			    DCIO_GSL0_TIMING_SYNC_SEL);

	set_reg_field_value(value,
			    0,
			    DCIO_GSL0_CNTL,
			    DCIO_GSL0_GLOBAL_UNLOCK_SEL);

	dm_write_reg(tg->ctx, CRTC_REG(mmDCIO_GSL0_CNTL), value);


	{
		uint32_t value_crtc_vtotal;

		value_crtc_vtotal = dm_read_reg(tg->ctx,
				CRTC_REG(mmCRTC_V_TOTAL));

		set_reg_field_value(value,
				    0,/* DCP_GSL_PURPOSE_SURFACE_FLIP */
				    DCP_GSL_CONTROL,
				    DCP_GSL_SYNC_SOURCE);

		/* Checkpoint relative to end of frame */
		check_point = get_reg_field_value(value_crtc_vtotal,
						  CRTC_V_TOTAL,
						  CRTC_V_TOTAL);

		dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_GSL_WINDOW), 0);
	}

	set_reg_field_value(value,
			    1,
			    DCP_GSL_CONTROL,
			    DCP_GSL_DELAY_SURFACE_UPDATE_PENDING);

	dm_write_reg(tg->ctx, address, value);

	/********************************************************************/
	address = CRTC_REG(mmCRTC_GSL_CONTROL);

	value = dm_read_reg(tg->ctx, address);
	set_reg_field_value(value,
			    check_point - FLIP_READY_BACK_LOOKUP,
			    CRTC_GSL_CONTROL,
			    CRTC_GSL_CHECK_LINE_NUM);

	set_reg_field_value(value,
			    VFLIP_READY_DELAY,
			    CRTC_GSL_CONTROL,
			    CRTC_GSL_FORCE_DELAY);

	dm_write_reg(tg->ctx, address, value);
}

void dce110_timing_generator_tear_down_global_swap_lock(
	struct timing_generator *tg)
{
	/* Clear all the register writes done by
	 * dce110_timing_generator_setup_global_swap_lock
	 */

	uint32_t value;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t address = DCP_REG(mmDCP_GSL_CONTROL);

	value = 0;

	/* This pipe will belong to GSL Group zero. */
	/* Settig HW default values from reg specs */
	set_reg_field_value(value,
			0,
			DCP_GSL_CONTROL,
			DCP_GSL0_EN);

	set_reg_field_value(value,
			0,
			DCP_GSL_CONTROL,
			DCP_GSL_MASTER_EN);

	set_reg_field_value(value,
			0x2,
			DCP_GSL_CONTROL,
			DCP_GSL_HSYNC_FLIP_FORCE_DELAY);

	set_reg_field_value(value,
			0x6,
			DCP_GSL_CONTROL,
			DCP_GSL_HSYNC_FLIP_CHECK_DELAY);

	/* Restore DCP_GSL_PURPOSE_SURFACE_FLIP */
	{
		dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_V_TOTAL));

		set_reg_field_value(value,
				0,
				DCP_GSL_CONTROL,
				DCP_GSL_SYNC_SOURCE);
	}

	set_reg_field_value(value,
			0,
			DCP_GSL_CONTROL,
			DCP_GSL_DELAY_SURFACE_UPDATE_PENDING);

	dm_write_reg(tg->ctx, address, value);

	/********************************************************************/
	address = CRTC_REG(mmCRTC_GSL_CONTROL);

	value = 0;
	set_reg_field_value(value,
			0,
			CRTC_GSL_CONTROL,
			CRTC_GSL_CHECK_LINE_NUM);

	set_reg_field_value(value,
			0x2,
			CRTC_GSL_CONTROL,
			CRTC_GSL_FORCE_DELAY);

	dm_write_reg(tg->ctx, address, value);
}
/*
 *****************************************************************************
 *  Function: is_counter_moving
 *
 *  @brief
 *     check if the timing generator is currently going
 *
 *  @return
 *     true if currently going, false if currently paused or stopped.
 *
 *****************************************************************************
 */
bool dce110_timing_generator_is_counter_moving(struct timing_generator *tg)
{
	struct crtc_position position1, position2;

	tg->funcs->get_position(tg, &position1);
	tg->funcs->get_position(tg, &position2);

	if (position1.horizontal_count == position2.horizontal_count &&
		position1.vertical_count == position2.vertical_count)
		return false;
	else
		return true;
}

void dce110_timing_generator_enable_advanced_request(
	struct timing_generator *tg,
	bool enable,
	const struct dc_crtc_timing *timing)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t addr = CRTC_REG(mmCRTC_START_LINE_CONTROL);
	uint32_t value = dm_read_reg(tg->ctx, addr);

	if (enable) {
		set_reg_field_value(
			value,
			0,
			CRTC_START_LINE_CONTROL,
			CRTC_LEGACY_REQUESTOR_EN);
	} else {
		set_reg_field_value(
			value,
			1,
			CRTC_START_LINE_CONTROL,
			CRTC_LEGACY_REQUESTOR_EN);
	}

	if ((timing->v_sync_width + timing->v_front_porch) <= 3) {
		set_reg_field_value(
			value,
			3,
			CRTC_START_LINE_CONTROL,
			CRTC_ADVANCED_START_LINE_POSITION);
		set_reg_field_value(
			value,
			0,
			CRTC_START_LINE_CONTROL,
			CRTC_PREFETCH_EN);
	} else {
		set_reg_field_value(
			value,
			4,
			CRTC_START_LINE_CONTROL,
			CRTC_ADVANCED_START_LINE_POSITION);
		set_reg_field_value(
			value,
			1,
			CRTC_START_LINE_CONTROL,
			CRTC_PREFETCH_EN);
	}

	set_reg_field_value(
		value,
		1,
		CRTC_START_LINE_CONTROL,
		CRTC_PROGRESSIVE_START_LINE_EARLY);

	set_reg_field_value(
		value,
		1,
		CRTC_START_LINE_CONTROL,
		CRTC_INTERLACE_START_LINE_EARLY);

	dm_write_reg(tg->ctx, addr, value);
}

/*TODO: Figure out if we need this function. */
void dce110_timing_generator_set_lock_master(struct timing_generator *tg,
		bool lock)
{
	struct dc_context *ctx = tg->ctx;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t addr = CRTC_REG(mmCRTC_MASTER_UPDATE_LOCK);
	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		lock ? 1 : 0,
		CRTC_MASTER_UPDATE_LOCK,
		MASTER_UPDATE_LOCK);

	dm_write_reg(ctx, addr, value);
}

void dce110_timing_generator_enable_reset_trigger(
	struct timing_generator *tg,
	int source_tg_inst)
{
	uint32_t value;
	uint32_t rising_edge = 0;
	uint32_t falling_edge = 0;
	enum trigger_source_select trig_src_select = TRIGGER_SOURCE_SELECT_LOGIC_ZERO;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	/* Setup trigger edge */
	{
		uint32_t pol_value = dm_read_reg(tg->ctx,
				CRTC_REG(mmCRTC_V_SYNC_A_CNTL));

		/* Register spec has reversed definition:
		 *	0 for positive, 1 for negative */
		if (get_reg_field_value(pol_value,
				CRTC_V_SYNC_A_CNTL,
				CRTC_V_SYNC_A_POL) == 0) {
			rising_edge = 1;
		} else {
			falling_edge = 1;
		}
	}

	value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_TRIGB_CNTL));

	trig_src_select = TRIGGER_SOURCE_SELECT_GSL_GROUP0;

	set_reg_field_value(value,
			trig_src_select,
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_SOURCE_SELECT);

	set_reg_field_value(value,
			TRIGGER_POLARITY_SELECT_LOGIC_ZERO,
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_POLARITY_SELECT);

	set_reg_field_value(value,
			rising_edge,
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_RISING_EDGE_DETECT_CNTL);

	set_reg_field_value(value,
			falling_edge,
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_FALLING_EDGE_DETECT_CNTL);

	set_reg_field_value(value,
			0, /* send every signal */
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_FREQUENCY_SELECT);

	set_reg_field_value(value,
			0, /* no delay */
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_DELAY);

	set_reg_field_value(value,
			1, /* clear trigger status */
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_CLEAR);

	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_TRIGB_CNTL), value);

	/**************************************************************/

	value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_FORCE_COUNT_NOW_CNTL));

	set_reg_field_value(value,
			2, /* force H count to H_TOTAL and V count to V_TOTAL */
			CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_MODE);

	set_reg_field_value(value,
			1, /* TriggerB - we never use TriggerA */
			CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_TRIG_SEL);

	set_reg_field_value(value,
			1, /* clear trigger status */
			CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_CLEAR);

	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_FORCE_COUNT_NOW_CNTL), value);
}

void dce110_timing_generator_enable_crtc_reset(
		struct timing_generator *tg,
		int source_tg_inst,
		struct crtc_trigger_info *crtc_tp)
{
	uint32_t value = 0;
	uint32_t rising_edge = 0;
	uint32_t falling_edge = 0;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	/* Setup trigger edge */
	switch (crtc_tp->event) {
	case CRTC_EVENT_VSYNC_RISING:
			rising_edge = 1;
			break;

	case CRTC_EVENT_VSYNC_FALLING:
		falling_edge = 1;
		break;
	}

	value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_TRIGB_CNTL));

	set_reg_field_value(value,
			    source_tg_inst,
			    CRTC_TRIGB_CNTL,
			    CRTC_TRIGB_SOURCE_SELECT);

	set_reg_field_value(value,
			    TRIGGER_POLARITY_SELECT_LOGIC_ZERO,
			    CRTC_TRIGB_CNTL,
			    CRTC_TRIGB_POLARITY_SELECT);

	set_reg_field_value(value,
			    rising_edge,
			    CRTC_TRIGB_CNTL,
			    CRTC_TRIGB_RISING_EDGE_DETECT_CNTL);

	set_reg_field_value(value,
			    falling_edge,
			    CRTC_TRIGB_CNTL,
			    CRTC_TRIGB_FALLING_EDGE_DETECT_CNTL);

	set_reg_field_value(value,
			    1, /* clear trigger status */
			    CRTC_TRIGB_CNTL,
			    CRTC_TRIGB_CLEAR);

	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_TRIGB_CNTL), value);

	/**************************************************************/

	switch (crtc_tp->delay) {
	case TRIGGER_DELAY_NEXT_LINE:
		value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_FORCE_COUNT_NOW_CNTL));

		set_reg_field_value(value,
				    0, /* force H count to H_TOTAL and V count to V_TOTAL */
				    CRTC_FORCE_COUNT_NOW_CNTL,
				    CRTC_FORCE_COUNT_NOW_MODE);

		set_reg_field_value(value,
				    0, /* TriggerB - we never use TriggerA */
				    CRTC_FORCE_COUNT_NOW_CNTL,
				    CRTC_FORCE_COUNT_NOW_TRIG_SEL);

		set_reg_field_value(value,
				    1, /* clear trigger status */
				    CRTC_FORCE_COUNT_NOW_CNTL,
				    CRTC_FORCE_COUNT_NOW_CLEAR);

		dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_FORCE_COUNT_NOW_CNTL), value);

		value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_VERT_SYNC_CONTROL));

		set_reg_field_value(value,
				    1,
				    CRTC_VERT_SYNC_CONTROL,
				    CRTC_FORCE_VSYNC_NEXT_LINE_CLEAR);

		set_reg_field_value(value,
				    2,
				    CRTC_VERT_SYNC_CONTROL,
				    CRTC_AUTO_FORCE_VSYNC_MODE);

		break;

	case TRIGGER_DELAY_NEXT_PIXEL:
		value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_VERT_SYNC_CONTROL));

		set_reg_field_value(value,
				    1,
				    CRTC_VERT_SYNC_CONTROL,
				    CRTC_FORCE_VSYNC_NEXT_LINE_CLEAR);

		set_reg_field_value(value,
				    0,
				    CRTC_VERT_SYNC_CONTROL,
				    CRTC_AUTO_FORCE_VSYNC_MODE);

		dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_VERT_SYNC_CONTROL), value);

		value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_FORCE_COUNT_NOW_CNTL));

		set_reg_field_value(value,
				    2, /* force H count to H_TOTAL and V count to V_TOTAL */
				    CRTC_FORCE_COUNT_NOW_CNTL,
				    CRTC_FORCE_COUNT_NOW_MODE);

		set_reg_field_value(value,
				    1, /* TriggerB - we never use TriggerA */
				    CRTC_FORCE_COUNT_NOW_CNTL,
				    CRTC_FORCE_COUNT_NOW_TRIG_SEL);

		set_reg_field_value(value,
				    1, /* clear trigger status */
				    CRTC_FORCE_COUNT_NOW_CNTL,
				    CRTC_FORCE_COUNT_NOW_CLEAR);

		dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_FORCE_COUNT_NOW_CNTL), value);
		break;
	}

	value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_MASTER_UPDATE_MODE));

	set_reg_field_value(value,
			    2,
			    CRTC_MASTER_UPDATE_MODE,
			    MASTER_UPDATE_MODE);

	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_MASTER_UPDATE_MODE), value);
}
void dce110_timing_generator_disable_reset_trigger(
	struct timing_generator *tg)
{
	uint32_t value;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_FORCE_COUNT_NOW_CNTL));

	set_reg_field_value(value,
			    0, /* force counter now mode is disabled */
			    CRTC_FORCE_COUNT_NOW_CNTL,
			    CRTC_FORCE_COUNT_NOW_MODE);

	set_reg_field_value(value,
			    1, /* clear trigger status */
			    CRTC_FORCE_COUNT_NOW_CNTL,
			    CRTC_FORCE_COUNT_NOW_CLEAR);

	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_FORCE_COUNT_NOW_CNTL), value);

	value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_VERT_SYNC_CONTROL));

	set_reg_field_value(value,
			    1,
			    CRTC_VERT_SYNC_CONTROL,
			    CRTC_FORCE_VSYNC_NEXT_LINE_CLEAR);

	set_reg_field_value(value,
			    0,
			    CRTC_VERT_SYNC_CONTROL,
			    CRTC_AUTO_FORCE_VSYNC_MODE);

	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_VERT_SYNC_CONTROL), value);

	/********************************************************************/
	value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_TRIGB_CNTL));

	set_reg_field_value(value,
			    TRIGGER_SOURCE_SELECT_LOGIC_ZERO,
			    CRTC_TRIGB_CNTL,
			    CRTC_TRIGB_SOURCE_SELECT);

	set_reg_field_value(value,
			    TRIGGER_POLARITY_SELECT_LOGIC_ZERO,
			    CRTC_TRIGB_CNTL,
			    CRTC_TRIGB_POLARITY_SELECT);

	set_reg_field_value(value,
			    1, /* clear trigger status */
			    CRTC_TRIGB_CNTL,
			    CRTC_TRIGB_CLEAR);

	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_TRIGB_CNTL), value);
}

/*
 *****************************************************************************
 *  @brief
 *     Checks whether CRTC triggered reset occurred
 *
 *  @return
 *     true if triggered reset occurred, false otherwise
 *****************************************************************************
 */
bool dce110_timing_generator_did_triggered_reset_occur(
	struct timing_generator *tg)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = dm_read_reg(tg->ctx,
			CRTC_REG(mmCRTC_FORCE_COUNT_NOW_CNTL));
	uint32_t value1 = dm_read_reg(tg->ctx,
			CRTC_REG(mmCRTC_VERT_SYNC_CONTROL));
	bool force = get_reg_field_value(value,
					 CRTC_FORCE_COUNT_NOW_CNTL,
					 CRTC_FORCE_COUNT_NOW_OCCURRED) != 0;
	bool vert_sync = get_reg_field_value(value1,
					     CRTC_VERT_SYNC_CONTROL,
					     CRTC_FORCE_VSYNC_NEXT_LINE_OCCURRED) != 0;

	return (force || vert_sync);
}

/*
 * dce110_timing_generator_disable_vga
 * Turn OFF VGA Mode and Timing  - DxVGA_CONTROL
 * VGA Mode and VGA Timing is used by VBIOS on CRT Monitors;
 */
void dce110_timing_generator_disable_vga(
	struct timing_generator *tg)
{
	uint32_t addr = 0;
	uint32_t value = 0;

	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	switch (tg110->controller_id) {
	case CONTROLLER_ID_D0:
		addr = mmD1VGA_CONTROL;
		break;
	case CONTROLLER_ID_D1:
		addr = mmD2VGA_CONTROL;
		break;
	case CONTROLLER_ID_D2:
		addr = mmD3VGA_CONTROL;
		break;
	case CONTROLLER_ID_D3:
		addr = mmD4VGA_CONTROL;
		break;
	case CONTROLLER_ID_D4:
		addr = mmD5VGA_CONTROL;
		break;
	case CONTROLLER_ID_D5:
		addr = mmD6VGA_CONTROL;
		break;
	default:
		break;
	}
	value = dm_read_reg(tg->ctx, addr);

	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_MODE_ENABLE);
	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_TIMING_SELECT);
	set_reg_field_value(
			value, 0, D1VGA_CONTROL, D1VGA_SYNC_POLARITY_SELECT);
	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_OVERSCAN_COLOR_EN);

	dm_write_reg(tg->ctx, addr, value);
}

/*
 * set_overscan_color_black
 *
 * @param :black_color is one of the color space
 *    :this routine will set overscan black color according to the color space.
 * @return none
 */
void dce110_timing_generator_set_overscan_color_black(
	struct timing_generator *tg,
	const struct tg_color *color)
{
	struct dc_context *ctx = tg->ctx;
	uint32_t addr;
	uint32_t value = 0;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	set_reg_field_value(
			value,
			color->color_b_cb,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

	set_reg_field_value(
			value,
			color->color_r_cr,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);

	set_reg_field_value(
			value,
			color->color_g_y,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

	addr = CRTC_REG(mmCRTC_OVERSCAN_COLOR);
	dm_write_reg(ctx, addr, value);
	addr = CRTC_REG(mmCRTC_BLACK_COLOR);
	dm_write_reg(ctx, addr, value);
	/* This is desirable to have a constant DAC output voltage during the
	 * blank time that is higher than the 0 volt reference level that the
	 * DAC outputs when the NBLANK signal
	 * is asserted low, such as for output to an analog TV. */
	addr = CRTC_REG(mmCRTC_BLANK_DATA_COLOR);
	dm_write_reg(ctx, addr, value);

	/* TO DO we have to program EXT registers and we need to know LB DATA
	 * format because it is used when more 10 , i.e. 12 bits per color
	 *
	 * m_mmDxCRTC_OVERSCAN_COLOR_EXT
	 * m_mmDxCRTC_BLACK_COLOR_EXT
	 * m_mmDxCRTC_BLANK_DATA_COLOR_EXT
	 */

}

void dce110_tg_program_blank_color(struct timing_generator *tg,
		const struct tg_color *black_color)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t addr = CRTC_REG(mmCRTC_BLACK_COLOR);
	uint32_t value = dm_read_reg(tg->ctx, addr);

	set_reg_field_value(
		value,
		black_color->color_b_cb,
		CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_B_CB);
	set_reg_field_value(
		value,
		black_color->color_g_y,
		CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_G_Y);
	set_reg_field_value(
		value,
		black_color->color_r_cr,
		CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_R_CR);

	dm_write_reg(tg->ctx, addr, value);

	addr = CRTC_REG(mmCRTC_BLANK_DATA_COLOR);
	dm_write_reg(tg->ctx, addr, value);
}

void dce110_tg_set_overscan_color(struct timing_generator *tg,
	const struct tg_color *overscan_color)
{
	struct dc_context *ctx = tg->ctx;
	uint32_t value = 0;
	uint32_t addr;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	set_reg_field_value(
		value,
		overscan_color->color_b_cb,
		CRTC_OVERSCAN_COLOR,
		CRTC_OVERSCAN_COLOR_BLUE);

	set_reg_field_value(
		value,
		overscan_color->color_g_y,
		CRTC_OVERSCAN_COLOR,
		CRTC_OVERSCAN_COLOR_GREEN);

	set_reg_field_value(
		value,
		overscan_color->color_r_cr,
		CRTC_OVERSCAN_COLOR,
		CRTC_OVERSCAN_COLOR_RED);

	addr = CRTC_REG(mmCRTC_OVERSCAN_COLOR);
	dm_write_reg(ctx, addr, value);
}

void dce110_tg_program_timing(struct timing_generator *tg,
	const struct dc_crtc_timing *timing,
	int vready_offset,
	int vstartup_start,
	int vupdate_offset,
	int vupdate_width,
	const enum signal_type signal,
	bool use_vbios)
{
	if (use_vbios)
		dce110_timing_generator_program_timing_generator(tg, timing);
	else
		dce110_timing_generator_program_blanking(tg, timing);
}

bool dce110_tg_is_blanked(struct timing_generator *tg)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = dm_read_reg(tg->ctx, CRTC_REG(mmCRTC_BLANK_CONTROL));

	if (get_reg_field_value(
			value,
			CRTC_BLANK_CONTROL,
			CRTC_BLANK_DATA_EN) == 1 &&
		get_reg_field_value(
			value,
			CRTC_BLANK_CONTROL,
			CRTC_CURRENT_BLANK_STATE) == 1)
		return true;
	return false;
}

void dce110_tg_set_blank(struct timing_generator *tg,
		bool enable_blanking)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = 0;

	set_reg_field_value(
		value,
		1,
		CRTC_DOUBLE_BUFFER_CONTROL,
		CRTC_BLANK_DATA_DOUBLE_BUFFER_EN);

	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_DOUBLE_BUFFER_CONTROL), value);
	value = 0;

	if (enable_blanking) {
		set_reg_field_value(
			value,
			1,
			CRTC_BLANK_CONTROL,
			CRTC_BLANK_DATA_EN);

		dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_BLANK_CONTROL), value);

	} else
		dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_BLANK_CONTROL), 0);
}

bool dce110_tg_validate_timing(struct timing_generator *tg,
	const struct dc_crtc_timing *timing)
{
	return dce110_timing_generator_validate_timing(tg, timing, SIGNAL_TYPE_NONE);
}

void dce110_tg_wait_for_state(struct timing_generator *tg,
	enum crtc_state state)
{
	switch (state) {
	case CRTC_STATE_VBLANK:
		dce110_timing_generator_wait_for_vblank(tg);
		break;

	case CRTC_STATE_VACTIVE:
		dce110_timing_generator_wait_for_vactive(tg);
		break;

	default:
		break;
	}
}

void dce110_tg_set_colors(struct timing_generator *tg,
	const struct tg_color *blank_color,
	const struct tg_color *overscan_color)
{
	if (blank_color != NULL)
		dce110_tg_program_blank_color(tg, blank_color);
	if (overscan_color != NULL)
		dce110_tg_set_overscan_color(tg, overscan_color);
}

/* Gets first line of blank region of the display timing for CRTC
 * and programms is as a trigger to fire vertical interrupt
 */
bool dce110_arm_vert_intr(struct timing_generator *tg, uint8_t width)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t v_blank_start = 0;
	uint32_t v_blank_end = 0;
	uint32_t val = 0;
	uint32_t h_position, v_position;

	tg->funcs->get_scanoutpos(
			tg,
			&v_blank_start,
			&v_blank_end,
			&h_position,
			&v_position);

	if (v_blank_start == 0 || v_blank_end == 0)
		return false;

	set_reg_field_value(
		val,
		v_blank_start,
		CRTC_VERTICAL_INTERRUPT0_POSITION,
		CRTC_VERTICAL_INTERRUPT0_LINE_START);

	/* Set interval width for interrupt to fire to 1 scanline */
	set_reg_field_value(
		val,
		v_blank_start + width,
		CRTC_VERTICAL_INTERRUPT0_POSITION,
		CRTC_VERTICAL_INTERRUPT0_LINE_END);

	dm_write_reg(tg->ctx, CRTC_REG(mmCRTC_VERTICAL_INTERRUPT0_POSITION), val);

	return true;
}

static bool dce110_is_tg_enabled(struct timing_generator *tg)
{
	uint32_t addr = 0;
	uint32_t value = 0;
	uint32_t field = 0;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	addr = CRTC_REG(mmCRTC_CONTROL);
	value = dm_read_reg(tg->ctx, addr);
	field = get_reg_field_value(value, CRTC_CONTROL,
				    CRTC_CURRENT_MASTER_EN_STATE);
	return field == 1;
}

bool dce110_configure_crc(struct timing_generator *tg,
			  const struct crc_params *params)
{
	uint32_t cntl_addr = 0;
	uint32_t addr = 0;
	uint32_t value;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	/* Cannot configure crc on a CRTC that is disabled */
	if (!dce110_is_tg_enabled(tg))
		return false;

	cntl_addr = CRTC_REG(mmCRTC_CRC_CNTL);

	/* First, disable CRC before we configure it. */
	dm_write_reg(tg->ctx, cntl_addr, 0);

	if (!params->enable)
		return true;

	/* Program frame boundaries */
	/* Window A x axis start and end. */
	value = 0;
	addr = CRTC_REG(mmCRTC_CRC0_WINDOWA_X_CONTROL);
	set_reg_field_value(value, params->windowa_x_start,
			    CRTC_CRC0_WINDOWA_X_CONTROL,
			    CRTC_CRC0_WINDOWA_X_START);
	set_reg_field_value(value, params->windowa_x_end,
			    CRTC_CRC0_WINDOWA_X_CONTROL,
			    CRTC_CRC0_WINDOWA_X_END);
	dm_write_reg(tg->ctx, addr, value);

	/* Window A y axis start and end. */
	value = 0;
	addr = CRTC_REG(mmCRTC_CRC0_WINDOWA_Y_CONTROL);
	set_reg_field_value(value, params->windowa_y_start,
			    CRTC_CRC0_WINDOWA_Y_CONTROL,
			    CRTC_CRC0_WINDOWA_Y_START);
	set_reg_field_value(value, params->windowa_y_end,
			    CRTC_CRC0_WINDOWA_Y_CONTROL,
			    CRTC_CRC0_WINDOWA_Y_END);
	dm_write_reg(tg->ctx, addr, value);

	/* Window B x axis start and end. */
	value = 0;
	addr = CRTC_REG(mmCRTC_CRC0_WINDOWB_X_CONTROL);
	set_reg_field_value(value, params->windowb_x_start,
			    CRTC_CRC0_WINDOWB_X_CONTROL,
			    CRTC_CRC0_WINDOWB_X_START);
	set_reg_field_value(value, params->windowb_x_end,
			    CRTC_CRC0_WINDOWB_X_CONTROL,
			    CRTC_CRC0_WINDOWB_X_END);
	dm_write_reg(tg->ctx, addr, value);

	/* Window B y axis start and end. */
	value = 0;
	addr = CRTC_REG(mmCRTC_CRC0_WINDOWB_Y_CONTROL);
	set_reg_field_value(value, params->windowb_y_start,
			    CRTC_CRC0_WINDOWB_Y_CONTROL,
			    CRTC_CRC0_WINDOWB_Y_START);
	set_reg_field_value(value, params->windowb_y_end,
			    CRTC_CRC0_WINDOWB_Y_CONTROL,
			    CRTC_CRC0_WINDOWB_Y_END);
	dm_write_reg(tg->ctx, addr, value);

	/* Set crc mode and selection, and enable. Only using CRC0*/
	value = 0;
	set_reg_field_value(value, params->continuous_mode ? 1 : 0,
			    CRTC_CRC_CNTL, CRTC_CRC_CONT_EN);
	set_reg_field_value(value, params->selection,
			    CRTC_CRC_CNTL, CRTC_CRC0_SELECT);
	set_reg_field_value(value, 1, CRTC_CRC_CNTL, CRTC_CRC_EN);
	dm_write_reg(tg->ctx, cntl_addr, value);

	return true;
}

bool dce110_get_crc(struct timing_generator *tg,
		    uint32_t *r_cr, uint32_t *g_y, uint32_t *b_cb)
{
	uint32_t addr = 0;
	uint32_t value = 0;
	uint32_t field = 0;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	addr = CRTC_REG(mmCRTC_CRC_CNTL);
	value = dm_read_reg(tg->ctx, addr);
	field = get_reg_field_value(value, CRTC_CRC_CNTL, CRTC_CRC_EN);

	/* Early return if CRC is not enabled for this CRTC */
	if (!field)
		return false;

	addr = CRTC_REG(mmCRTC_CRC0_DATA_RG);
	value = dm_read_reg(tg->ctx, addr);
	*r_cr = get_reg_field_value(value, CRTC_CRC0_DATA_RG, CRC0_R_CR);
	*g_y = get_reg_field_value(value, CRTC_CRC0_DATA_RG, CRC0_G_Y);

	addr = CRTC_REG(mmCRTC_CRC0_DATA_B);
	value = dm_read_reg(tg->ctx, addr);
	*b_cb = get_reg_field_value(value, CRTC_CRC0_DATA_B, CRC0_B_CB);

	return true;
}

static const struct timing_generator_funcs dce110_tg_funcs = {
		.validate_timing = dce110_tg_validate_timing,
		.program_timing = dce110_tg_program_timing,
		.enable_crtc = dce110_timing_generator_enable_crtc,
		.disable_crtc = dce110_timing_generator_disable_crtc,
		.is_counter_moving = dce110_timing_generator_is_counter_moving,
		.get_position = dce110_timing_generator_get_position,
		.get_frame_count = dce110_timing_generator_get_vblank_counter,
		.get_scanoutpos = dce110_timing_generator_get_crtc_scanoutpos,
		.set_early_control = dce110_timing_generator_set_early_control,
		.wait_for_state = dce110_tg_wait_for_state,
		.set_blank = dce110_tg_set_blank,
		.is_blanked = dce110_tg_is_blanked,
		.set_colors = dce110_tg_set_colors,
		.set_overscan_blank_color =
				dce110_timing_generator_set_overscan_color_black,
		.set_blank_color = dce110_timing_generator_program_blank_color,
		.disable_vga = dce110_timing_generator_disable_vga,
		.did_triggered_reset_occur =
				dce110_timing_generator_did_triggered_reset_occur,
		.setup_global_swap_lock =
				dce110_timing_generator_setup_global_swap_lock,
		.enable_reset_trigger = dce110_timing_generator_enable_reset_trigger,
		.enable_crtc_reset = dce110_timing_generator_enable_crtc_reset,
		.disable_reset_trigger = dce110_timing_generator_disable_reset_trigger,
		.tear_down_global_swap_lock =
				dce110_timing_generator_tear_down_global_swap_lock,
		.enable_advanced_request =
				dce110_timing_generator_enable_advanced_request,
		.set_drr =
				dce110_timing_generator_set_drr,
		.set_static_screen_control =
			dce110_timing_generator_set_static_screen_control,
		.set_test_pattern = dce110_timing_generator_set_test_pattern,
		.arm_vert_intr = dce110_arm_vert_intr,
		.is_tg_enabled = dce110_is_tg_enabled,
		.configure_crc = dce110_configure_crc,
		.get_crc = dce110_get_crc,
};

void dce110_timing_generator_construct(
	struct dce110_timing_generator *tg110,
	struct dc_context *ctx,
	uint32_t instance,
	const struct dce110_timing_generator_offsets *offsets)
{
	tg110->controller_id = CONTROLLER_ID_D0 + instance;
	tg110->base.inst = instance;

	tg110->offsets = *offsets;

	tg110->base.funcs = &dce110_tg_funcs;

	tg110->base.ctx = ctx;
	tg110->base.bp = ctx->dc_bios;

	tg110->max_h_total = CRTC_H_TOTAL__CRTC_H_TOTAL_MASK + 1;
	tg110->max_v_total = CRTC_V_TOTAL__CRTC_V_TOTAL_MASK + 1;

	tg110->min_h_blank = 56;
	tg110->min_h_front_porch = 4;
	tg110->min_h_back_porch = 4;
}
