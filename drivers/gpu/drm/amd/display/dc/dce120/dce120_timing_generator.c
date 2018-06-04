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

#include "dce/dce_12_0_offset.h"
#include "dce/dce_12_0_sh_mask.h"
#include "soc15_hw_ip.h"
#include "vega10_ip_offset.h"

#include "dc_types.h"
#include "dc_bios_types.h"

#include "include/grph_object_id.h"
#include "include/logger_interface.h"
#include "dce120_timing_generator.h"

#include "timing_generator.h"

#define CRTC_REG_UPDATE_N(reg_name, n, ...)	\
		generic_reg_update_soc15(tg110->base.ctx, tg110->offsets.crtc, reg_name, n, __VA_ARGS__)

#define CRTC_REG_SET_N(reg_name, n, ...)	\
		generic_reg_set_soc15(tg110->base.ctx, tg110->offsets.crtc, reg_name, n, __VA_ARGS__)

#define CRTC_REG_UPDATE(reg, field, val)	\
		CRTC_REG_UPDATE_N(reg, 1, FD(reg##__##field), val)

#define CRTC_REG_UPDATE_2(reg, field1, val1, field2, val2)	\
		CRTC_REG_UPDATE_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)

#define CRTC_REG_UPDATE_3(reg, field1, val1, field2, val2, field3, val3)	\
		CRTC_REG_UPDATE_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3)

#define CRTC_REG_UPDATE_4(reg, field1, val1, field2, val2, field3, val3, field4, val4)	\
		CRTC_REG_UPDATE_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3, FD(reg##__##field4), val4)

#define CRTC_REG_UPDATE_5(reg, field1, val1, field2, val2, field3, val3, field4, val4, field5, val5)	\
		CRTC_REG_UPDATE_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3, FD(reg##__##field4), val4, FD(reg##__##field5), val5)

#define CRTC_REG_SET(reg, field, val)	\
		CRTC_REG_SET_N(reg, 1, FD(reg##__##field), val)

#define CRTC_REG_SET_2(reg, field1, val1, field2, val2)	\
		CRTC_REG_SET_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)

#define CRTC_REG_SET_3(reg, field1, val1, field2, val2, field3, val3)	\
		CRTC_REG_SET_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3)

/**
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
static bool dce120_timing_generator_is_in_vertical_blank(
		struct timing_generator *tg)
{
	uint32_t field = 0;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = dm_read_reg_soc15(
					tg->ctx,
					mmCRTC0_CRTC_STATUS,
					tg110->offsets.crtc);

	field = get_reg_field_value(value, CRTC0_CRTC_STATUS, CRTC_V_BLANK);
	return field == 1;
}


/* determine if given timing can be supported by TG */
bool dce120_timing_generator_validate_timing(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing,
	enum signal_type signal)
{
	uint32_t interlace_factor = timing->flags.INTERLACE ? 2 : 1;
	uint32_t v_blank =
					(timing->v_total - timing->v_addressable -
					timing->v_border_top - timing->v_border_bottom) *
					interlace_factor;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	if (!dce110_timing_generator_validate_timing(
					tg,
					timing,
					signal))
		return false;


	if (v_blank < tg110->min_v_blank	||
		 timing->h_sync_width  < tg110->min_h_sync_width ||
		 timing->v_sync_width  < tg110->min_v_sync_width)
		return false;

	return true;
}

bool dce120_tg_validate_timing(struct timing_generator *tg,
	const struct dc_crtc_timing *timing)
{
	return dce120_timing_generator_validate_timing(tg, timing, SIGNAL_TYPE_NONE);
}

/******** HW programming ************/
/* Disable/Enable Timing Generator */
bool dce120_timing_generator_enable_crtc(struct timing_generator *tg)
{
	enum bp_result result;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	/* Set MASTER_UPDATE_MODE to 0
	 * This is needed for DRR, and also suggested to be default value by Syed.*/

	CRTC_REG_UPDATE(CRTC0_CRTC_MASTER_UPDATE_MODE,
			MASTER_UPDATE_MODE, 0);

	CRTC_REG_UPDATE(CRTC0_CRTC_MASTER_UPDATE_LOCK,
			UNDERFLOW_UPDATE_LOCK, 0);

	/* TODO API for AtomFirmware didn't change*/
	result = tg->bp->funcs->enable_crtc(tg->bp, tg110->controller_id, true);

	return result == BP_RESULT_OK;
}

void dce120_timing_generator_set_early_control(
		struct timing_generator *tg,
		uint32_t early_cntl)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	CRTC_REG_UPDATE(CRTC0_CRTC_CONTROL,
			CRTC_HBLANK_EARLY_CONTROL, early_cntl);
}

/**************** TG current status ******************/

/* return the current frame counter. Used by Linux kernel DRM */
uint32_t dce120_timing_generator_get_vblank_counter(
		struct timing_generator *tg)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = dm_read_reg_soc15(
				tg->ctx,
				mmCRTC0_CRTC_STATUS_FRAME_COUNT,
				tg110->offsets.crtc);
	uint32_t field = get_reg_field_value(
				value, CRTC0_CRTC_STATUS_FRAME_COUNT, CRTC_FRAME_COUNT);

	return field;
}

/* Get current H and V position */
void dce120_timing_generator_get_crtc_position(
	struct timing_generator *tg,
	struct crtc_position *position)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = dm_read_reg_soc15(
				tg->ctx,
				mmCRTC0_CRTC_STATUS_POSITION,
				tg110->offsets.crtc);

	position->horizontal_count = get_reg_field_value(value,
			CRTC0_CRTC_STATUS_POSITION, CRTC_HORZ_COUNT);

	position->vertical_count = get_reg_field_value(value,
			CRTC0_CRTC_STATUS_POSITION, CRTC_VERT_COUNT);

	value = dm_read_reg_soc15(
				tg->ctx,
				mmCRTC0_CRTC_NOM_VERT_POSITION,
				tg110->offsets.crtc);

	position->nominal_vcount = get_reg_field_value(value,
			CRTC0_CRTC_NOM_VERT_POSITION, CRTC_VERT_COUNT_NOM);
}

/* wait until TG is in beginning of vertical blank region */
void dce120_timing_generator_wait_for_vblank(struct timing_generator *tg)
{
	/* We want to catch beginning of VBlank here, so if the first try are
	 * in VBlank, we might be very close to Active, in this case wait for
	 * another frame
	 */
	while (dce120_timing_generator_is_in_vertical_blank(tg)) {
		if (!tg->funcs->is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}

	while (!dce120_timing_generator_is_in_vertical_blank(tg)) {
		if (!tg->funcs->is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}
}

/* wait until TG is in beginning of active region */
void dce120_timing_generator_wait_for_vactive(struct timing_generator *tg)
{
	while (dce120_timing_generator_is_in_vertical_blank(tg)) {
		if (!tg->funcs->is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}
}

/*********** Timing Generator Synchronization routines ****/

/* Setups Global Swap Lock group, TimingServer or TimingClient*/
void dce120_timing_generator_setup_global_swap_lock(
	struct timing_generator *tg,
	const struct dcp_gsl_params *gsl_params)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value_crtc_vtotal =
							dm_read_reg_soc15(tg->ctx,
							mmCRTC0_CRTC_V_TOTAL,
							tg110->offsets.crtc);
	/* Checkpoint relative to end of frame */
	uint32_t check_point =
							get_reg_field_value(value_crtc_vtotal,
							CRTC0_CRTC_V_TOTAL,
							CRTC_V_TOTAL);


	dm_write_reg_soc15(tg->ctx, mmCRTC0_CRTC_GSL_WINDOW, tg110->offsets.crtc, 0);

	CRTC_REG_UPDATE_N(DCP0_DCP_GSL_CONTROL, 6,
		/* This pipe will belong to GSL Group zero. */
		FD(DCP0_DCP_GSL_CONTROL__DCP_GSL0_EN), 1,
		FD(DCP0_DCP_GSL_CONTROL__DCP_GSL_MASTER_EN), gsl_params->gsl_master == tg->inst,
		FD(DCP0_DCP_GSL_CONTROL__DCP_GSL_HSYNC_FLIP_FORCE_DELAY), HFLIP_READY_DELAY,
		/* Keep signal low (pending high) during 6 lines.
		 * Also defines minimum interval before re-checking signal. */
		FD(DCP0_DCP_GSL_CONTROL__DCP_GSL_HSYNC_FLIP_CHECK_DELAY), HFLIP_CHECK_DELAY,
		/* DCP_GSL_PURPOSE_SURFACE_FLIP */
		FD(DCP0_DCP_GSL_CONTROL__DCP_GSL_SYNC_SOURCE), 0,
		FD(DCP0_DCP_GSL_CONTROL__DCP_GSL_DELAY_SURFACE_UPDATE_PENDING), 1);

	CRTC_REG_SET_2(
			CRTC0_CRTC_GSL_CONTROL,
			CRTC_GSL_CHECK_LINE_NUM, check_point - FLIP_READY_BACK_LOOKUP,
			CRTC_GSL_FORCE_DELAY, VFLIP_READY_DELAY);
}

/* Clear all the register writes done by setup_global_swap_lock */
void dce120_timing_generator_tear_down_global_swap_lock(
	struct timing_generator *tg)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	/* Settig HW default values from reg specs */
	CRTC_REG_SET_N(DCP0_DCP_GSL_CONTROL, 6,
			FD(DCP0_DCP_GSL_CONTROL__DCP_GSL0_EN), 0,
			FD(DCP0_DCP_GSL_CONTROL__DCP_GSL_MASTER_EN), 0,
			FD(DCP0_DCP_GSL_CONTROL__DCP_GSL_HSYNC_FLIP_FORCE_DELAY), HFLIP_READY_DELAY,
			FD(DCP0_DCP_GSL_CONTROL__DCP_GSL_HSYNC_FLIP_CHECK_DELAY), HFLIP_CHECK_DELAY,
			/* DCP_GSL_PURPOSE_SURFACE_FLIP */
			FD(DCP0_DCP_GSL_CONTROL__DCP_GSL_SYNC_SOURCE), 0,
			FD(DCP0_DCP_GSL_CONTROL__DCP_GSL_DELAY_SURFACE_UPDATE_PENDING), 0);

	CRTC_REG_SET_2(CRTC0_CRTC_GSL_CONTROL,
		       CRTC_GSL_CHECK_LINE_NUM, 0,
		       CRTC_GSL_FORCE_DELAY, 0x2); /*TODO Why this value here ?*/
}

/* Reset slave controllers on master VSync */
void dce120_timing_generator_enable_reset_trigger(
	struct timing_generator *tg,
	int source)
{
	enum trigger_source_select trig_src_select = TRIGGER_SOURCE_SELECT_LOGIC_ZERO;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t rising_edge = 0;
	uint32_t falling_edge = 0;
	/* Setup trigger edge */
	uint32_t pol_value = dm_read_reg_soc15(
									tg->ctx,
									mmCRTC0_CRTC_V_SYNC_A_CNTL,
									tg110->offsets.crtc);

	/* Register spec has reversed definition:
	 *	0 for positive, 1 for negative */
	if (get_reg_field_value(pol_value,
			CRTC0_CRTC_V_SYNC_A_CNTL,
			CRTC_V_SYNC_A_POL) == 0) {
		rising_edge = 1;
	} else {
		falling_edge = 1;
	}

	/* TODO What about other sources ?*/
	trig_src_select = TRIGGER_SOURCE_SELECT_GSL_GROUP0;

	CRTC_REG_UPDATE_N(CRTC0_CRTC_TRIGB_CNTL, 7,
		FD(CRTC0_CRTC_TRIGB_CNTL__CRTC_TRIGB_SOURCE_SELECT), trig_src_select,
		FD(CRTC0_CRTC_TRIGB_CNTL__CRTC_TRIGB_POLARITY_SELECT), TRIGGER_POLARITY_SELECT_LOGIC_ZERO,
		FD(CRTC0_CRTC_TRIGB_CNTL__CRTC_TRIGB_RISING_EDGE_DETECT_CNTL), rising_edge,
		FD(CRTC0_CRTC_TRIGB_CNTL__CRTC_TRIGB_FALLING_EDGE_DETECT_CNTL), falling_edge,
		/* send every signal */
		FD(CRTC0_CRTC_TRIGB_CNTL__CRTC_TRIGB_FREQUENCY_SELECT), 0,
		/* no delay */
		FD(CRTC0_CRTC_TRIGB_CNTL__CRTC_TRIGB_DELAY), 0,
		/* clear trigger status */
		FD(CRTC0_CRTC_TRIGB_CNTL__CRTC_TRIGB_CLEAR), 1);

	CRTC_REG_UPDATE_3(
			CRTC0_CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_MODE, 2,
			CRTC_FORCE_COUNT_NOW_TRIG_SEL, 1,
			CRTC_FORCE_COUNT_NOW_CLEAR, 1);
}

/* disabling trigger-reset */
void dce120_timing_generator_disable_reset_trigger(
	struct timing_generator *tg)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	CRTC_REG_UPDATE_2(
		CRTC0_CRTC_FORCE_COUNT_NOW_CNTL,
		CRTC_FORCE_COUNT_NOW_MODE, 0,
		CRTC_FORCE_COUNT_NOW_CLEAR, 1);

	CRTC_REG_UPDATE_3(
		CRTC0_CRTC_TRIGB_CNTL,
		CRTC_TRIGB_SOURCE_SELECT, TRIGGER_SOURCE_SELECT_LOGIC_ZERO,
		CRTC_TRIGB_POLARITY_SELECT, TRIGGER_POLARITY_SELECT_LOGIC_ZERO,
		/* clear trigger status */
		CRTC_TRIGB_CLEAR, 1);

}

/* Checks whether CRTC triggered reset occurred */
bool dce120_timing_generator_did_triggered_reset_occur(
	struct timing_generator *tg)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = dm_read_reg_soc15(
			tg->ctx,
			mmCRTC0_CRTC_FORCE_COUNT_NOW_CNTL,
			tg110->offsets.crtc);

	return get_reg_field_value(value,
			CRTC0_CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_OCCURRED) != 0;
}


/******** Stuff to move to other virtual HW objects *****************/
/* Move to enable accelerated mode */
void dce120_timing_generator_disable_vga(struct timing_generator *tg)
{
	uint32_t offset = 0;
	uint32_t value = 0;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	switch (tg110->controller_id) {
	case CONTROLLER_ID_D0:
		offset = 0;
		break;
	case CONTROLLER_ID_D1:
		offset = mmD2VGA_CONTROL - mmD1VGA_CONTROL;
		break;
	case CONTROLLER_ID_D2:
		offset = mmD3VGA_CONTROL - mmD1VGA_CONTROL;
		break;
	case CONTROLLER_ID_D3:
		offset = mmD4VGA_CONTROL - mmD1VGA_CONTROL;
		break;
	case CONTROLLER_ID_D4:
		offset = mmD5VGA_CONTROL - mmD1VGA_CONTROL;
		break;
	case CONTROLLER_ID_D5:
		offset = mmD6VGA_CONTROL - mmD1VGA_CONTROL;
		break;
	default:
		break;
	}

	value = dm_read_reg_soc15(tg->ctx, mmD1VGA_CONTROL, offset);

	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_MODE_ENABLE);
	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_TIMING_SELECT);
	set_reg_field_value(
			value, 0, D1VGA_CONTROL, D1VGA_SYNC_POLARITY_SELECT);
	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_OVERSCAN_COLOR_EN);

	dm_write_reg_soc15(tg->ctx, mmD1VGA_CONTROL, offset, value);
}
/* TODO: Should we move it to transform */
/* Fully program CRTC timing in timing generator */
void dce120_timing_generator_program_blanking(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing)
{
	uint32_t tmp1 = 0;
	uint32_t tmp2 = 0;
	uint32_t vsync_offset = timing->v_border_bottom +
			timing->v_front_porch;
	uint32_t v_sync_start = timing->v_addressable + vsync_offset;

	uint32_t hsync_offset = timing->h_border_right +
			timing->h_front_porch;
	uint32_t h_sync_start = timing->h_addressable + hsync_offset;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	CRTC_REG_UPDATE(
		CRTC0_CRTC_H_TOTAL,
		CRTC_H_TOTAL,
		timing->h_total - 1);

	CRTC_REG_UPDATE(
		CRTC0_CRTC_V_TOTAL,
		CRTC_V_TOTAL,
		timing->v_total - 1);

	/* In case of V_TOTAL_CONTROL is on, make sure V_TOTAL_MAX and
	 * V_TOTAL_MIN are equal to V_TOTAL.
	 */
	CRTC_REG_UPDATE(
		CRTC0_CRTC_V_TOTAL_MAX,
		CRTC_V_TOTAL_MAX,
		timing->v_total - 1);

	CRTC_REG_UPDATE(
		CRTC0_CRTC_V_TOTAL_MIN,
		CRTC_V_TOTAL_MIN,
		timing->v_total - 1);

	tmp1 = timing->h_total -
			(h_sync_start + timing->h_border_left);
	tmp2 = tmp1 + timing->h_addressable +
			timing->h_border_left + timing->h_border_right;

	CRTC_REG_UPDATE_2(
			CRTC0_CRTC_H_BLANK_START_END,
			CRTC_H_BLANK_END, tmp1,
			CRTC_H_BLANK_START, tmp2);

	tmp1 = timing->v_total - (v_sync_start + timing->v_border_top);
	tmp2 = tmp1 + timing->v_addressable + timing->v_border_top +
			timing->v_border_bottom;

	CRTC_REG_UPDATE_2(
		CRTC0_CRTC_V_BLANK_START_END,
		CRTC_V_BLANK_END, tmp1,
		CRTC_V_BLANK_START, tmp2);
}

/* TODO: Should we move it to opp? */
/* Combine with below and move YUV/RGB color conversion to SW layer */
void dce120_timing_generator_program_blank_color(
	struct timing_generator *tg,
	const struct tg_color *black_color)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	CRTC_REG_UPDATE_3(
		CRTC0_CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_B_CB, black_color->color_b_cb,
		CRTC_BLACK_COLOR_G_Y, black_color->color_g_y,
		CRTC_BLACK_COLOR_R_CR, black_color->color_r_cr);
}
/* Combine with above and move YUV/RGB color conversion to SW layer */
void dce120_timing_generator_set_overscan_color_black(
	struct timing_generator *tg,
	const struct tg_color *color)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = 0;
	CRTC_REG_SET_3(
		CRTC0_CRTC_OVERSCAN_COLOR,
		CRTC_OVERSCAN_COLOR_BLUE, color->color_b_cb,
		CRTC_OVERSCAN_COLOR_GREEN, color->color_g_y,
		CRTC_OVERSCAN_COLOR_RED, color->color_r_cr);

	value = dm_read_reg_soc15(
			tg->ctx,
			mmCRTC0_CRTC_OVERSCAN_COLOR,
			tg110->offsets.crtc);

	dm_write_reg_soc15(
			tg->ctx,
			mmCRTC0_CRTC_BLACK_COLOR,
			tg110->offsets.crtc,
			value);

	/* This is desirable to have a constant DAC output voltage during the
	 * blank time that is higher than the 0 volt reference level that the
	 * DAC outputs when the NBLANK signal
	 * is asserted low, such as for output to an analog TV. */
	dm_write_reg_soc15(
		tg->ctx,
		mmCRTC0_CRTC_BLANK_DATA_COLOR,
		tg110->offsets.crtc,
		value);

	/* TO DO we have to program EXT registers and we need to know LB DATA
	 * format because it is used when more 10 , i.e. 12 bits per color
	 *
	 * m_mmDxCRTC_OVERSCAN_COLOR_EXT
	 * m_mmDxCRTC_BLACK_COLOR_EXT
	 * m_mmDxCRTC_BLANK_DATA_COLOR_EXT
	 */
}

void dce120_timing_generator_set_drr(
	struct timing_generator *tg,
	const struct drr_params *params)
{

	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	if (params != NULL &&
		params->vertical_total_max > 0 &&
		params->vertical_total_min > 0) {

		CRTC_REG_UPDATE(
				CRTC0_CRTC_V_TOTAL_MIN,
				CRTC_V_TOTAL_MIN, params->vertical_total_min - 1);
		CRTC_REG_UPDATE(
				CRTC0_CRTC_V_TOTAL_MAX,
				CRTC_V_TOTAL_MAX, params->vertical_total_max - 1);
		CRTC_REG_SET_N(CRTC0_CRTC_V_TOTAL_CONTROL, 6,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_V_TOTAL_MIN_SEL), 1,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_V_TOTAL_MAX_SEL), 1,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_FORCE_LOCK_ON_EVENT), 0,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_FORCE_LOCK_TO_MASTER_VSYNC), 0,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_SET_V_TOTAL_MIN_MASK_EN), 0,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_SET_V_TOTAL_MIN_MASK), 0);
		CRTC_REG_UPDATE(
				CRTC0_CRTC_STATIC_SCREEN_CONTROL,
				CRTC_STATIC_SCREEN_EVENT_MASK,
				0x180);

	} else {
		CRTC_REG_UPDATE(
				CRTC0_CRTC_V_TOTAL_MIN,
				CRTC_V_TOTAL_MIN, 0);
		CRTC_REG_UPDATE(
				CRTC0_CRTC_V_TOTAL_MAX,
				CRTC_V_TOTAL_MAX, 0);
		CRTC_REG_SET_N(CRTC0_CRTC_V_TOTAL_CONTROL, 5,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_V_TOTAL_MIN_SEL), 0,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_V_TOTAL_MAX_SEL), 0,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_FORCE_LOCK_ON_EVENT), 0,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_FORCE_LOCK_TO_MASTER_VSYNC), 0,
				FD(CRTC0_CRTC_V_TOTAL_CONTROL__CRTC_SET_V_TOTAL_MIN_MASK), 0);
		CRTC_REG_UPDATE(
				CRTC0_CRTC_STATIC_SCREEN_CONTROL,
				CRTC_STATIC_SCREEN_EVENT_MASK,
				0);
	}
}

/**
 *****************************************************************************
 *  Function: dce120_timing_generator_get_position
 *
 *  @brief
 *     Returns CRTC vertical/horizontal counters
 *
 *  @param [out] position
 *****************************************************************************
 */
void dce120_timing_generator_get_position(struct timing_generator *tg,
	struct crtc_position *position)
{
	uint32_t value;
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	value = dm_read_reg_soc15(
			tg->ctx,
			mmCRTC0_CRTC_STATUS_POSITION,
			tg110->offsets.crtc);

	position->horizontal_count = get_reg_field_value(
			value,
			CRTC0_CRTC_STATUS_POSITION,
			CRTC_HORZ_COUNT);

	position->vertical_count = get_reg_field_value(
			value,
			CRTC0_CRTC_STATUS_POSITION,
			CRTC_VERT_COUNT);

	value = dm_read_reg_soc15(
			tg->ctx,
			mmCRTC0_CRTC_NOM_VERT_POSITION,
			tg110->offsets.crtc);

	position->nominal_vcount = get_reg_field_value(
			value,
			CRTC0_CRTC_NOM_VERT_POSITION,
			CRTC_VERT_COUNT_NOM);
}


void dce120_timing_generator_get_crtc_scanoutpos(
	struct timing_generator *tg,
	uint32_t *v_blank_start,
	uint32_t *v_blank_end,
	uint32_t *h_position,
	uint32_t *v_position)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	struct crtc_position position;

	uint32_t v_blank_start_end = dm_read_reg_soc15(
			tg->ctx,
			mmCRTC0_CRTC_V_BLANK_START_END,
			tg110->offsets.crtc);

	*v_blank_start = get_reg_field_value(v_blank_start_end,
					     CRTC0_CRTC_V_BLANK_START_END,
					     CRTC_V_BLANK_START);
	*v_blank_end = get_reg_field_value(v_blank_start_end,
					   CRTC0_CRTC_V_BLANK_START_END,
					   CRTC_V_BLANK_END);

	dce120_timing_generator_get_crtc_position(
			tg, &position);

	*h_position = position.horizontal_count;
	*v_position = position.vertical_count;
}

void dce120_timing_generator_enable_advanced_request(
	struct timing_generator *tg,
	bool enable,
	const struct dc_crtc_timing *timing)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t v_sync_width_and_b_porch =
				timing->v_total - timing->v_addressable -
				timing->v_border_bottom - timing->v_front_porch;
	uint32_t value = dm_read_reg_soc15(
				tg->ctx,
				mmCRTC0_CRTC_START_LINE_CONTROL,
				tg110->offsets.crtc);

	set_reg_field_value(
		value,
		enable ? 0 : 1,
		CRTC0_CRTC_START_LINE_CONTROL,
		CRTC_LEGACY_REQUESTOR_EN);

	/* Program advanced line position acc.to the best case from fetching data perspective to hide MC latency
	 * and prefilling Line Buffer in V Blank (to 10 lines as LB can store max 10 lines)
	 */
	if (v_sync_width_and_b_porch > 10)
		v_sync_width_and_b_porch = 10;

	set_reg_field_value(
		value,
		v_sync_width_and_b_porch,
		CRTC0_CRTC_START_LINE_CONTROL,
		CRTC_ADVANCED_START_LINE_POSITION);

	dm_write_reg_soc15(tg->ctx,
			mmCRTC0_CRTC_START_LINE_CONTROL,
			tg110->offsets.crtc,
			value);
}

void dce120_tg_program_blank_color(struct timing_generator *tg,
	const struct tg_color *black_color)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = 0;

	CRTC_REG_UPDATE_3(
		CRTC0_CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_B_CB, black_color->color_b_cb,
		CRTC_BLACK_COLOR_G_Y, black_color->color_g_y,
		CRTC_BLACK_COLOR_R_CR, black_color->color_r_cr);

	value = dm_read_reg_soc15(
				tg->ctx,
				mmCRTC0_CRTC_BLACK_COLOR,
				tg110->offsets.crtc);
	dm_write_reg_soc15(
		tg->ctx,
		mmCRTC0_CRTC_BLANK_DATA_COLOR,
		tg110->offsets.crtc,
		value);
}

void dce120_tg_set_overscan_color(struct timing_generator *tg,
	const struct tg_color *overscan_color)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	CRTC_REG_SET_3(
		CRTC0_CRTC_OVERSCAN_COLOR,
		CRTC_OVERSCAN_COLOR_BLUE, overscan_color->color_b_cb,
		CRTC_OVERSCAN_COLOR_GREEN, overscan_color->color_g_y,
		CRTC_OVERSCAN_COLOR_RED, overscan_color->color_r_cr);
}

void dce120_tg_program_timing(struct timing_generator *tg,
	const struct dc_crtc_timing *timing,
	bool use_vbios)
{
	if (use_vbios)
		dce110_timing_generator_program_timing_generator(tg, timing);
	else
		dce120_timing_generator_program_blanking(tg, timing);
}

bool dce120_tg_is_blanked(struct timing_generator *tg)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t value = dm_read_reg_soc15(
			tg->ctx,
			mmCRTC0_CRTC_BLANK_CONTROL,
			tg110->offsets.crtc);

	if (get_reg_field_value(
		value,
		CRTC0_CRTC_BLANK_CONTROL,
		CRTC_BLANK_DATA_EN) == 1 &&
	    get_reg_field_value(
		value,
		CRTC0_CRTC_BLANK_CONTROL,
		CRTC_CURRENT_BLANK_STATE) == 1)
			return true;

	return false;
}

void dce120_tg_set_blank(struct timing_generator *tg,
		bool enable_blanking)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	CRTC_REG_SET(
		CRTC0_CRTC_DOUBLE_BUFFER_CONTROL,
		CRTC_BLANK_DATA_DOUBLE_BUFFER_EN, 0);

	if (enable_blanking)
		CRTC_REG_SET(CRTC0_CRTC_BLANK_CONTROL, CRTC_BLANK_DATA_EN, 1);
	else
		dm_write_reg_soc15(tg->ctx, mmCRTC0_CRTC_BLANK_CONTROL,
			tg110->offsets.crtc, 0);
}

bool dce120_tg_validate_timing(struct timing_generator *tg,
	const struct dc_crtc_timing *timing);

void dce120_tg_wait_for_state(struct timing_generator *tg,
	enum crtc_state state)
{
	switch (state) {
	case CRTC_STATE_VBLANK:
		dce120_timing_generator_wait_for_vblank(tg);
		break;

	case CRTC_STATE_VACTIVE:
		dce120_timing_generator_wait_for_vactive(tg);
		break;

	default:
		break;
	}
}

void dce120_tg_set_colors(struct timing_generator *tg,
	const struct tg_color *blank_color,
	const struct tg_color *overscan_color)
{
	if (blank_color != NULL)
		dce120_tg_program_blank_color(tg, blank_color);

	if (overscan_color != NULL)
		dce120_tg_set_overscan_color(tg, overscan_color);
}

static void dce120_timing_generator_set_static_screen_control(
	struct timing_generator *tg,
	uint32_t value)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);

	CRTC_REG_UPDATE_2(CRTC0_CRTC_STATIC_SCREEN_CONTROL,
			CRTC_STATIC_SCREEN_EVENT_MASK, value,
			CRTC_STATIC_SCREEN_FRAME_COUNT, 2);
}

void dce120_timing_generator_set_test_pattern(
	struct timing_generator *tg,
	/* TODO: replace 'controller_dp_test_pattern' by 'test_pattern_mode'
	 * because this is not DP-specific (which is probably somewhere in DP
	 * encoder) */
	enum controller_dp_test_pattern test_pattern,
	enum dc_color_depth color_depth)
{
	struct dc_context *ctx = tg->ctx;
	uint32_t value;
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

		CRTC_REG_UPDATE_2(CRTC0_CRTC_TEST_PATTERN_PARAMETERS,
				CRTC_TEST_PATTERN_VRES, 6,
				CRTC_TEST_PATTERN_HRES, 6);

		CRTC_REG_UPDATE_4(CRTC0_CRTC_TEST_PATTERN_CONTROL,
				CRTC_TEST_PATTERN_EN, 1,
				CRTC_TEST_PATTERN_MODE, mode,
				CRTC_TEST_PATTERN_DYNAMIC_RANGE, dyn_range,
				CRTC_TEST_PATTERN_COLOR_FORMAT, bit_depth);
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

		dm_write_reg_soc15(ctx, mmCRTC0_CRTC_TEST_PATTERN_PARAMETERS, tg110->offsets.crtc, 0);

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
		for (index = 0; index < 6; index++) {
			/* prepare color mask, first write PATTERN_DATA
			 * will have all zeros
			 */
			set_reg_field_value(
				value,
				(1 << index),
				CRTC0_CRTC_TEST_PATTERN_COLOR,
				CRTC_TEST_PATTERN_MASK);
			/* write color component */
			dm_write_reg_soc15(ctx, mmCRTC0_CRTC_TEST_PATTERN_COLOR, tg110->offsets.crtc, value);
			/* prepare next color component,
			 * will be written in the next iteration
			 */
			set_reg_field_value(
				value,
				dst_color[index],
				CRTC0_CRTC_TEST_PATTERN_COLOR,
				CRTC_TEST_PATTERN_DATA);
		}
		/* write last color component,
		 * it's been already prepared in the loop
		 */
		dm_write_reg_soc15(ctx, mmCRTC0_CRTC_TEST_PATTERN_COLOR, tg110->offsets.crtc, value);

		/* enable test pattern */
		CRTC_REG_UPDATE_4(CRTC0_CRTC_TEST_PATTERN_CONTROL,
				CRTC_TEST_PATTERN_EN, 1,
				CRTC_TEST_PATTERN_MODE, mode,
				CRTC_TEST_PATTERN_DYNAMIC_RANGE, 0,
				CRTC_TEST_PATTERN_COLOR_FORMAT, bit_depth);
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

		switch (bit_depth) {
		case TEST_PATTERN_COLOR_FORMAT_BPC_6:
		{
			CRTC_REG_UPDATE_5(CRTC0_CRTC_TEST_PATTERN_PARAMETERS,
					CRTC_TEST_PATTERN_INC0, inc_base,
					CRTC_TEST_PATTERN_INC1, 0,
					CRTC_TEST_PATTERN_HRES, 6,
					CRTC_TEST_PATTERN_VRES, 6,
					CRTC_TEST_PATTERN_RAMP0_OFFSET, 0);
		}
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_8:
		{
			CRTC_REG_UPDATE_5(CRTC0_CRTC_TEST_PATTERN_PARAMETERS,
					CRTC_TEST_PATTERN_INC0, inc_base,
					CRTC_TEST_PATTERN_INC1, 0,
					CRTC_TEST_PATTERN_HRES, 8,
					CRTC_TEST_PATTERN_VRES, 6,
					CRTC_TEST_PATTERN_RAMP0_OFFSET, 0);
		}
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_10:
		{
			CRTC_REG_UPDATE_5(CRTC0_CRTC_TEST_PATTERN_PARAMETERS,
					CRTC_TEST_PATTERN_INC0, inc_base,
					CRTC_TEST_PATTERN_INC1, inc_base + 2,
					CRTC_TEST_PATTERN_HRES, 8,
					CRTC_TEST_PATTERN_VRES, 5,
					CRTC_TEST_PATTERN_RAMP0_OFFSET, 384 << 6);
		}
		break;
		default:
		break;
		}

		dm_write_reg_soc15(ctx, mmCRTC0_CRTC_TEST_PATTERN_COLOR, tg110->offsets.crtc, 0);

		/* enable test pattern */
		dm_write_reg_soc15(ctx, mmCRTC0_CRTC_TEST_PATTERN_CONTROL, tg110->offsets.crtc, 0);

		CRTC_REG_UPDATE_4(CRTC0_CRTC_TEST_PATTERN_CONTROL,
				CRTC_TEST_PATTERN_EN, 1,
				CRTC_TEST_PATTERN_MODE, mode,
				CRTC_TEST_PATTERN_DYNAMIC_RANGE, 0,
				CRTC_TEST_PATTERN_COLOR_FORMAT, bit_depth);
	}
	break;
	case CONTROLLER_DP_TEST_PATTERN_VIDEOMODE:
	{
		value = 0;
		dm_write_reg_soc15(ctx, mmCRTC0_CRTC_TEST_PATTERN_CONTROL, tg110->offsets.crtc,  value);
		dm_write_reg_soc15(ctx, mmCRTC0_CRTC_TEST_PATTERN_COLOR, tg110->offsets.crtc, value);
		dm_write_reg_soc15(ctx, mmCRTC0_CRTC_TEST_PATTERN_PARAMETERS, tg110->offsets.crtc, value);
	}
	break;
	default:
	break;
	}
}

static bool dce120_arm_vert_intr(
		struct timing_generator *tg,
		uint8_t width)
{
	struct dce110_timing_generator *tg110 = DCE110TG_FROM_TG(tg);
	uint32_t v_blank_start, v_blank_end, h_position, v_position;

	tg->funcs->get_scanoutpos(
				tg,
				&v_blank_start,
				&v_blank_end,
				&h_position,
				&v_position);

	if (v_blank_start == 0 || v_blank_end == 0)
		return false;

	CRTC_REG_SET_2(
			CRTC0_CRTC_VERTICAL_INTERRUPT0_POSITION,
			CRTC_VERTICAL_INTERRUPT0_LINE_START, v_blank_start,
			CRTC_VERTICAL_INTERRUPT0_LINE_END, v_blank_start + width);

	return true;
}

static const struct timing_generator_funcs dce120_tg_funcs = {
		.validate_timing = dce120_tg_validate_timing,
		.program_timing = dce120_tg_program_timing,
		.enable_crtc = dce120_timing_generator_enable_crtc,
		.disable_crtc = dce110_timing_generator_disable_crtc,
		/* used by enable_timing_synchronization. Not need for FPGA */
		.is_counter_moving = dce110_timing_generator_is_counter_moving,
		/* never be called */
		.get_position = dce120_timing_generator_get_crtc_position,
		.get_frame_count = dce120_timing_generator_get_vblank_counter,
		.get_scanoutpos = dce120_timing_generator_get_crtc_scanoutpos,
		.set_early_control = dce120_timing_generator_set_early_control,
		/* used by enable_timing_synchronization. Not need for FPGA */
		.wait_for_state = dce120_tg_wait_for_state,
		.set_blank = dce120_tg_set_blank,
		.is_blanked = dce120_tg_is_blanked,
		/* never be called */
		.set_colors = dce120_tg_set_colors,
		.set_overscan_blank_color = dce120_timing_generator_set_overscan_color_black,
		.set_blank_color = dce120_timing_generator_program_blank_color,
		.disable_vga = dce120_timing_generator_disable_vga,
		.did_triggered_reset_occur = dce120_timing_generator_did_triggered_reset_occur,
		.setup_global_swap_lock = dce120_timing_generator_setup_global_swap_lock,
		.enable_reset_trigger = dce120_timing_generator_enable_reset_trigger,
		.disable_reset_trigger = dce120_timing_generator_disable_reset_trigger,
		.tear_down_global_swap_lock = dce120_timing_generator_tear_down_global_swap_lock,
		.enable_advanced_request = dce120_timing_generator_enable_advanced_request,
		.set_drr = dce120_timing_generator_set_drr,
		.set_static_screen_control = dce120_timing_generator_set_static_screen_control,
		.set_test_pattern = dce120_timing_generator_set_test_pattern,
		.arm_vert_intr = dce120_arm_vert_intr,
};


void dce120_timing_generator_construct(
	struct dce110_timing_generator *tg110,
	struct dc_context *ctx,
	uint32_t instance,
	const struct dce110_timing_generator_offsets *offsets)
{
	tg110->controller_id = CONTROLLER_ID_D0 + instance;
	tg110->base.inst = instance;

	tg110->offsets = *offsets;

	tg110->base.funcs = &dce120_tg_funcs;

	tg110->base.ctx = ctx;
	tg110->base.bp = ctx->dc_bios;

	tg110->max_h_total = CRTC0_CRTC_H_TOTAL__CRTC_H_TOTAL_MASK + 1;
	tg110->max_v_total = CRTC0_CRTC_V_TOTAL__CRTC_V_TOTAL_MASK + 1;

	/*//CRTC requires a minimum HBLANK = 32 pixels and o
	 * Minimum HSYNC = 8 pixels*/
	tg110->min_h_blank = 32;
	/*DCE12_CRTC_Block_ARch.doc*/
	tg110->min_h_front_porch = 0;
	tg110->min_h_back_porch = 0;

	tg110->min_h_sync_width = 8;
	tg110->min_v_sync_width = 1;
	tg110->min_v_blank = 3;
}
