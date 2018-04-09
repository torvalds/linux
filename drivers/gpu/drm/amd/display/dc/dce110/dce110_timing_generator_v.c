/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#include "dce110_timing_generator_v.h"

#include "timing_generator.h"

#define DC_LOGGER \
	tg->ctx->logger
/** ********************************************************************************
 *
 * DCE11 Timing Generator Implementation
 *
 **********************************************************************************/

/**
* Enable CRTCV
*/

static bool dce110_timing_generator_v_enable_crtc(struct timing_generator *tg)
{
/*
* Set MASTER_UPDATE_MODE to 0
* This is needed for DRR, and also suggested to be default value by Syed.
*/

	uint32_t value;

	value = 0;
	set_reg_field_value(value, 0,
			CRTCV_MASTER_UPDATE_MODE, MASTER_UPDATE_MODE);
	dm_write_reg(tg->ctx,
			mmCRTCV_MASTER_UPDATE_MODE, value);

	/* TODO: may want this on for looking for underflow */
	value = 0;
	dm_write_reg(tg->ctx, mmCRTCV_MASTER_UPDATE_MODE, value);

	value = 0;
	set_reg_field_value(value, 1,
			CRTCV_MASTER_EN, CRTC_MASTER_EN);
	dm_write_reg(tg->ctx,
			mmCRTCV_MASTER_EN, value);

	return true;
}

static bool dce110_timing_generator_v_disable_crtc(struct timing_generator *tg)
{
	uint32_t value;

	value = dm_read_reg(tg->ctx,
			mmCRTCV_CONTROL);
	set_reg_field_value(value, 0,
			CRTCV_CONTROL, CRTC_DISABLE_POINT_CNTL);
	set_reg_field_value(value, 0,
				CRTCV_CONTROL, CRTC_MASTER_EN);
	dm_write_reg(tg->ctx,
			mmCRTCV_CONTROL, value);
	/*
	 * TODO: call this when adding stereo support
	 * tg->funcs->disable_stereo(tg);
	 */
	return true;
}

static void dce110_timing_generator_v_blank_crtc(struct timing_generator *tg)
{
	uint32_t addr = mmCRTCV_BLANK_CONTROL;
	uint32_t value = dm_read_reg(tg->ctx, addr);

	set_reg_field_value(
		value,
		1,
		CRTCV_BLANK_CONTROL,
		CRTC_BLANK_DATA_EN);

	set_reg_field_value(
		value,
		0,
		CRTCV_BLANK_CONTROL,
		CRTC_BLANK_DE_MODE);

	dm_write_reg(tg->ctx, addr, value);
}

static void dce110_timing_generator_v_unblank_crtc(struct timing_generator *tg)
{
	uint32_t addr = mmCRTCV_BLANK_CONTROL;
	uint32_t value = dm_read_reg(tg->ctx, addr);

	set_reg_field_value(
		value,
		0,
		CRTCV_BLANK_CONTROL,
		CRTC_BLANK_DATA_EN);

	set_reg_field_value(
		value,
		0,
		CRTCV_BLANK_CONTROL,
		CRTC_BLANK_DE_MODE);

	dm_write_reg(tg->ctx, addr, value);
}

static bool dce110_timing_generator_v_is_in_vertical_blank(
		struct timing_generator *tg)
{
	uint32_t addr = 0;
	uint32_t value = 0;
	uint32_t field = 0;

	addr = mmCRTCV_STATUS;
	value = dm_read_reg(tg->ctx, addr);
	field = get_reg_field_value(value, CRTCV_STATUS, CRTC_V_BLANK);
	return field == 1;
}

static bool dce110_timing_generator_v_is_counter_moving(struct timing_generator *tg)
{
	uint32_t value;
	uint32_t h1 = 0;
	uint32_t h2 = 0;
	uint32_t v1 = 0;
	uint32_t v2 = 0;

	value = dm_read_reg(tg->ctx, mmCRTCV_STATUS_POSITION);

	h1 = get_reg_field_value(
			value,
			CRTCV_STATUS_POSITION,
			CRTC_HORZ_COUNT);

	v1 = get_reg_field_value(
			value,
			CRTCV_STATUS_POSITION,
			CRTC_VERT_COUNT);

	value = dm_read_reg(tg->ctx, mmCRTCV_STATUS_POSITION);

	h2 = get_reg_field_value(
			value,
			CRTCV_STATUS_POSITION,
			CRTC_HORZ_COUNT);

	v2 = get_reg_field_value(
			value,
			CRTCV_STATUS_POSITION,
			CRTC_VERT_COUNT);

	if (h1 == h2 && v1 == v2)
		return false;
	else
		return true;
}

static void dce110_timing_generator_v_wait_for_vblank(struct timing_generator *tg)
{
	/* We want to catch beginning of VBlank here, so if the first try are
	 * in VBlank, we might be very close to Active, in this case wait for
	 * another frame
	 */
	while (dce110_timing_generator_v_is_in_vertical_blank(tg)) {
		if (!dce110_timing_generator_v_is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}

	while (!dce110_timing_generator_v_is_in_vertical_blank(tg)) {
		if (!dce110_timing_generator_v_is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}
}

/**
* Wait till we are in VActive (anywhere in VActive)
*/
static void dce110_timing_generator_v_wait_for_vactive(struct timing_generator *tg)
{
	while (dce110_timing_generator_v_is_in_vertical_blank(tg)) {
		if (!dce110_timing_generator_v_is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}
}

static void dce110_timing_generator_v_wait_for_state(struct timing_generator *tg,
	enum crtc_state state)
{
	switch (state) {
	case CRTC_STATE_VBLANK:
		dce110_timing_generator_v_wait_for_vblank(tg);
		break;

	case CRTC_STATE_VACTIVE:
		dce110_timing_generator_v_wait_for_vactive(tg);
		break;

	default:
		break;
	}
}

static void dce110_timing_generator_v_program_blanking(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing)
{
	uint32_t vsync_offset = timing->v_border_bottom +
			timing->v_front_porch;
	uint32_t v_sync_start = timing->v_addressable + vsync_offset;

	uint32_t hsync_offset = timing->h_border_right +
			timing->h_front_porch;
	uint32_t h_sync_start = timing->h_addressable + hsync_offset;

	struct dc_context *ctx = tg->ctx;
	uint32_t value = 0;
	uint32_t addr = 0;
	uint32_t tmp = 0;

	addr = mmCRTCV_H_TOTAL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		timing->h_total - 1,
		CRTCV_H_TOTAL,
		CRTC_H_TOTAL);
	dm_write_reg(ctx, addr, value);

	addr = mmCRTCV_V_TOTAL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		timing->v_total - 1,
		CRTCV_V_TOTAL,
		CRTC_V_TOTAL);
	dm_write_reg(ctx, addr, value);

	addr = mmCRTCV_H_BLANK_START_END;
	value = dm_read_reg(ctx, addr);

	tmp = timing->h_total -
		(h_sync_start + timing->h_border_left);

	set_reg_field_value(
		value,
		tmp,
		CRTCV_H_BLANK_START_END,
		CRTC_H_BLANK_END);

	tmp = tmp + timing->h_addressable +
		timing->h_border_left + timing->h_border_right;

	set_reg_field_value(
		value,
		tmp,
		CRTCV_H_BLANK_START_END,
		CRTC_H_BLANK_START);

	dm_write_reg(ctx, addr, value);

	addr = mmCRTCV_V_BLANK_START_END;
	value = dm_read_reg(ctx, addr);

	tmp = timing->v_total - (v_sync_start + timing->v_border_top);

	set_reg_field_value(
		value,
		tmp,
		CRTCV_V_BLANK_START_END,
		CRTC_V_BLANK_END);

	tmp = tmp + timing->v_addressable + timing->v_border_top +
		timing->v_border_bottom;

	set_reg_field_value(
		value,
		tmp,
		CRTCV_V_BLANK_START_END,
		CRTC_V_BLANK_START);

	dm_write_reg(ctx, addr, value);

	addr = mmCRTCV_H_SYNC_A;
	value = 0;
	set_reg_field_value(
		value,
		timing->h_sync_width,
		CRTCV_H_SYNC_A,
		CRTC_H_SYNC_A_END);
	dm_write_reg(ctx, addr, value);

	addr = mmCRTCV_H_SYNC_A_CNTL;
	value = dm_read_reg(ctx, addr);
	if (timing->flags.HSYNC_POSITIVE_POLARITY) {
		set_reg_field_value(
			value,
			0,
			CRTCV_H_SYNC_A_CNTL,
			CRTC_H_SYNC_A_POL);
	} else {
		set_reg_field_value(
			value,
			1,
			CRTCV_H_SYNC_A_CNTL,
			CRTC_H_SYNC_A_POL);
	}
	dm_write_reg(ctx, addr, value);

	addr = mmCRTCV_V_SYNC_A;
	value = 0;
	set_reg_field_value(
		value,
		timing->v_sync_width,
		CRTCV_V_SYNC_A,
		CRTC_V_SYNC_A_END);
	dm_write_reg(ctx, addr, value);

	addr = mmCRTCV_V_SYNC_A_CNTL;
	value = dm_read_reg(ctx, addr);
	if (timing->flags.VSYNC_POSITIVE_POLARITY) {
		set_reg_field_value(
			value,
			0,
			CRTCV_V_SYNC_A_CNTL,
			CRTC_V_SYNC_A_POL);
	} else {
		set_reg_field_value(
			value,
			1,
			CRTCV_V_SYNC_A_CNTL,
			CRTC_V_SYNC_A_POL);
	}
	dm_write_reg(ctx, addr, value);

	addr = mmCRTCV_INTERLACE_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		timing->flags.INTERLACE,
		CRTCV_INTERLACE_CONTROL,
		CRTC_INTERLACE_ENABLE);
	dm_write_reg(ctx, addr, value);
}

static void dce110_timing_generator_v_enable_advanced_request(
	struct timing_generator *tg,
	bool enable,
	const struct dc_crtc_timing *timing)
{
	uint32_t addr = mmCRTCV_START_LINE_CONTROL;
	uint32_t value = dm_read_reg(tg->ctx, addr);

	if (enable) {
		if ((timing->v_sync_width + timing->v_front_porch) <= 3) {
			set_reg_field_value(
				value,
				3,
				CRTCV_START_LINE_CONTROL,
				CRTC_ADVANCED_START_LINE_POSITION);
		} else {
			set_reg_field_value(
				value,
				4,
				CRTCV_START_LINE_CONTROL,
				CRTC_ADVANCED_START_LINE_POSITION);
		}
		set_reg_field_value(
			value,
			0,
			CRTCV_START_LINE_CONTROL,
			CRTC_LEGACY_REQUESTOR_EN);
	} else {
		set_reg_field_value(
			value,
			2,
			CRTCV_START_LINE_CONTROL,
			CRTC_ADVANCED_START_LINE_POSITION);
		set_reg_field_value(
			value,
			1,
			CRTCV_START_LINE_CONTROL,
			CRTC_LEGACY_REQUESTOR_EN);
	}

	dm_write_reg(tg->ctx, addr, value);
}

static void dce110_timing_generator_v_set_blank(struct timing_generator *tg,
		bool enable_blanking)
{
	if (enable_blanking)
		dce110_timing_generator_v_blank_crtc(tg);
	else
		dce110_timing_generator_v_unblank_crtc(tg);
}

static void dce110_timing_generator_v_program_timing(struct timing_generator *tg,
	const struct dc_crtc_timing *timing,
	bool use_vbios)
{
	if (use_vbios)
		dce110_timing_generator_program_timing_generator(tg, timing);
	else
		dce110_timing_generator_v_program_blanking(tg, timing);
}

static void dce110_timing_generator_v_program_blank_color(
		struct timing_generator *tg,
		const struct tg_color *black_color)
{
	uint32_t addr = mmCRTCV_BLACK_COLOR;
	uint32_t value = dm_read_reg(tg->ctx, addr);

	set_reg_field_value(
		value,
		black_color->color_b_cb,
		CRTCV_BLACK_COLOR,
		CRTC_BLACK_COLOR_B_CB);
	set_reg_field_value(
		value,
		black_color->color_g_y,
		CRTCV_BLACK_COLOR,
		CRTC_BLACK_COLOR_G_Y);
	set_reg_field_value(
		value,
		black_color->color_r_cr,
		CRTCV_BLACK_COLOR,
		CRTC_BLACK_COLOR_R_CR);

	dm_write_reg(tg->ctx, addr, value);
}

static void dce110_timing_generator_v_set_overscan_color_black(
	struct timing_generator *tg,
	const struct tg_color *color)
{
	struct dc_context *ctx = tg->ctx;
	uint32_t addr;
	uint32_t value = 0;

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

	addr = mmCRTCV_OVERSCAN_COLOR;
	dm_write_reg(ctx, addr, value);
	addr = mmCRTCV_BLACK_COLOR;
	dm_write_reg(ctx, addr, value);
	/* This is desirable to have a constant DAC output voltage during the
	 * blank time that is higher than the 0 volt reference level that the
	 * DAC outputs when the NBLANK signal
	 * is asserted low, such as for output to an analog TV. */
	addr = mmCRTCV_BLANK_DATA_COLOR;
	dm_write_reg(ctx, addr, value);

	/* TO DO we have to program EXT registers and we need to know LB DATA
	 * format because it is used when more 10 , i.e. 12 bits per color
	 *
	 * m_mmDxCRTC_OVERSCAN_COLOR_EXT
	 * m_mmDxCRTC_BLACK_COLOR_EXT
	 * m_mmDxCRTC_BLANK_DATA_COLOR_EXT
	 */
}

static void dce110_tg_v_program_blank_color(struct timing_generator *tg,
		const struct tg_color *black_color)
{
	uint32_t addr = mmCRTCV_BLACK_COLOR;
	uint32_t value = dm_read_reg(tg->ctx, addr);

	set_reg_field_value(
		value,
		black_color->color_b_cb,
		CRTCV_BLACK_COLOR,
		CRTC_BLACK_COLOR_B_CB);
	set_reg_field_value(
		value,
		black_color->color_g_y,
		CRTCV_BLACK_COLOR,
		CRTC_BLACK_COLOR_G_Y);
	set_reg_field_value(
		value,
		black_color->color_r_cr,
		CRTCV_BLACK_COLOR,
		CRTC_BLACK_COLOR_R_CR);

	dm_write_reg(tg->ctx, addr, value);

	addr = mmCRTCV_BLANK_DATA_COLOR;
	dm_write_reg(tg->ctx, addr, value);
}

static void dce110_timing_generator_v_set_overscan_color(struct timing_generator *tg,
	const struct tg_color *overscan_color)
{
	struct dc_context *ctx = tg->ctx;
	uint32_t value = 0;
	uint32_t addr;

	set_reg_field_value(
		value,
		overscan_color->color_b_cb,
		CRTCV_OVERSCAN_COLOR,
		CRTC_OVERSCAN_COLOR_BLUE);

	set_reg_field_value(
		value,
		overscan_color->color_g_y,
		CRTCV_OVERSCAN_COLOR,
		CRTC_OVERSCAN_COLOR_GREEN);

	set_reg_field_value(
		value,
		overscan_color->color_r_cr,
		CRTCV_OVERSCAN_COLOR,
		CRTC_OVERSCAN_COLOR_RED);

	addr = mmCRTCV_OVERSCAN_COLOR;
	dm_write_reg(ctx, addr, value);
}

static void dce110_timing_generator_v_set_colors(struct timing_generator *tg,
	const struct tg_color *blank_color,
	const struct tg_color *overscan_color)
{
	if (blank_color != NULL)
		dce110_tg_v_program_blank_color(tg, blank_color);
	if (overscan_color != NULL)
		dce110_timing_generator_v_set_overscan_color(tg, overscan_color);
}

static void dce110_timing_generator_v_set_early_control(
		struct timing_generator *tg,
		uint32_t early_cntl)
{
	uint32_t regval;
	uint32_t address = mmCRTC_CONTROL;

	regval = dm_read_reg(tg->ctx, address);
	set_reg_field_value(regval, early_cntl,
			CRTCV_CONTROL, CRTC_HBLANK_EARLY_CONTROL);
	dm_write_reg(tg->ctx, address, regval);
}

static uint32_t dce110_timing_generator_v_get_vblank_counter(struct timing_generator *tg)
{
	uint32_t addr = mmCRTCV_STATUS_FRAME_COUNT;
	uint32_t value = dm_read_reg(tg->ctx, addr);
	uint32_t field = get_reg_field_value(
			value, CRTCV_STATUS_FRAME_COUNT, CRTC_FRAME_COUNT);

	return field;
}

static bool dce110_timing_generator_v_did_triggered_reset_occur(
	struct timing_generator *tg)
{
	DC_LOG_ERROR("Timing Sync not supported on underlay pipe\n");
	return false;
}

static void dce110_timing_generator_v_setup_global_swap_lock(
	struct timing_generator *tg,
	const struct dcp_gsl_params *gsl_params)
{
	DC_LOG_ERROR("Timing Sync not supported on underlay pipe\n");
	return;
}

static void dce110_timing_generator_v_enable_reset_trigger(
	struct timing_generator *tg,
	int source_tg_inst)
{
	DC_LOG_ERROR("Timing Sync not supported on underlay pipe\n");
	return;
}

static void dce110_timing_generator_v_disable_reset_trigger(
	struct timing_generator *tg)
{
	DC_LOG_ERROR("Timing Sync not supported on underlay pipe\n");
	return;
}

static void dce110_timing_generator_v_tear_down_global_swap_lock(
	struct timing_generator *tg)
{
	DC_LOG_ERROR("Timing Sync not supported on underlay pipe\n");
	return;
}

static void dce110_timing_generator_v_disable_vga(
	struct timing_generator *tg)
{
	return;
}

static bool dce110_tg_v_is_blanked(struct timing_generator *tg)
{
	/* Signal comes from the primary pipe, underlay is never blanked. */
	return false;
}

/** ********************************************************************************************
 *
 * DCE11 Timing Generator Constructor / Destructor
 *
 *********************************************************************************************/
static const struct timing_generator_funcs dce110_tg_v_funcs = {
		.validate_timing = dce110_tg_validate_timing,
		.program_timing = dce110_timing_generator_v_program_timing,
		.enable_crtc = dce110_timing_generator_v_enable_crtc,
		.disable_crtc = dce110_timing_generator_v_disable_crtc,
		.is_counter_moving = dce110_timing_generator_v_is_counter_moving,
		.get_position = NULL, /* Not to be implemented for underlay*/
		.get_frame_count = dce110_timing_generator_v_get_vblank_counter,
		.set_early_control = dce110_timing_generator_v_set_early_control,
		.wait_for_state = dce110_timing_generator_v_wait_for_state,
		.set_blank = dce110_timing_generator_v_set_blank,
		.is_blanked = dce110_tg_v_is_blanked,
		.set_colors = dce110_timing_generator_v_set_colors,
		.set_overscan_blank_color =
				dce110_timing_generator_v_set_overscan_color_black,
		.set_blank_color = dce110_timing_generator_v_program_blank_color,
		.disable_vga = dce110_timing_generator_v_disable_vga,
		.did_triggered_reset_occur =
				dce110_timing_generator_v_did_triggered_reset_occur,
		.setup_global_swap_lock =
				dce110_timing_generator_v_setup_global_swap_lock,
		.enable_reset_trigger = dce110_timing_generator_v_enable_reset_trigger,
		.disable_reset_trigger = dce110_timing_generator_v_disable_reset_trigger,
		.tear_down_global_swap_lock =
				dce110_timing_generator_v_tear_down_global_swap_lock,
		.enable_advanced_request =
				dce110_timing_generator_v_enable_advanced_request
};

void dce110_timing_generator_v_construct(
	struct dce110_timing_generator *tg110,
	struct dc_context *ctx)
{
	tg110->controller_id = CONTROLLER_ID_UNDERLAY0;

	tg110->base.funcs = &dce110_tg_v_funcs;

	tg110->base.ctx = ctx;
	tg110->base.bp = ctx->dc_bios;

	tg110->max_h_total = CRTC_H_TOTAL__CRTC_H_TOTAL_MASK + 1;
	tg110->max_v_total = CRTC_V_TOTAL__CRTC_V_TOTAL_MASK + 1;

	tg110->min_h_blank = 56;
	tg110->min_h_front_porch = 4;
	tg110->min_h_back_porch = 4;
}
