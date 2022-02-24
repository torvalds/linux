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


#include "reg_helper.h"
#include "dcn10_optc.h"
#include "dc.h"

#define REG(reg)\
	optc1->tg_regs->reg

#define CTX \
	optc1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	optc1->tg_shift->field_name, optc1->tg_mask->field_name

#define STATIC_SCREEN_EVENT_MASK_RANGETIMING_DOUBLE_BUFFER_UPDATE_EN 0x100

/**
* apply_front_porch_workaround  TODO FPGA still need?
*
* This is a workaround for a bug that has existed since R5xx and has not been
* fixed keep Front porch at minimum 2 for Interlaced mode or 1 for progressive.
*/
static void apply_front_porch_workaround(struct dc_crtc_timing *timing)
{
	if (timing->flags.INTERLACE == 1) {
		if (timing->v_front_porch < 2)
			timing->v_front_porch = 2;
	} else {
		if (timing->v_front_porch < 1)
			timing->v_front_porch = 1;
	}
}

void optc1_program_global_sync(
		struct timing_generator *optc,
		int vready_offset,
		int vstartup_start,
		int vupdate_offset,
		int vupdate_width)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	optc1->vready_offset = vready_offset;
	optc1->vstartup_start = vstartup_start;
	optc1->vupdate_offset = vupdate_offset;
	optc1->vupdate_width = vupdate_width;

	if (optc1->vstartup_start == 0) {
		BREAK_TO_DEBUGGER();
		return;
	}

	REG_SET(OTG_VSTARTUP_PARAM, 0,
		VSTARTUP_START, optc1->vstartup_start);

	REG_SET_2(OTG_VUPDATE_PARAM, 0,
			VUPDATE_OFFSET, optc1->vupdate_offset,
			VUPDATE_WIDTH, optc1->vupdate_width);

	REG_SET(OTG_VREADY_PARAM, 0,
			VREADY_OFFSET, optc1->vready_offset);
}

static void optc1_disable_stereo(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_STEREO_CONTROL, 0,
		OTG_STEREO_EN, 0);

	REG_SET_2(OTG_3D_STRUCTURE_CONTROL, 0,
		OTG_3D_STRUCTURE_EN, 0,
		OTG_3D_STRUCTURE_STEREO_SEL_OVR, 0);
}

void optc1_setup_vertical_interrupt0(
		struct timing_generator *optc,
		uint32_t start_line,
		uint32_t end_line)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET_2(OTG_VERTICAL_INTERRUPT0_POSITION, 0,
			OTG_VERTICAL_INTERRUPT0_LINE_START, start_line,
			OTG_VERTICAL_INTERRUPT0_LINE_END, end_line);
}

void optc1_setup_vertical_interrupt1(
		struct timing_generator *optc,
		uint32_t start_line)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_VERTICAL_INTERRUPT1_POSITION, 0,
				OTG_VERTICAL_INTERRUPT1_LINE_START, start_line);
}

void optc1_setup_vertical_interrupt2(
		struct timing_generator *optc,
		uint32_t start_line)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_VERTICAL_INTERRUPT2_POSITION, 0,
			OTG_VERTICAL_INTERRUPT2_LINE_START, start_line);
}

/**
 * program_timing_generator   used by mode timing set
 * Program CRTC Timing Registers - OTG_H_*, OTG_V_*, Pixel repetition.
 * Including SYNC. Call BIOS command table to program Timings.
 */
void optc1_program_timing(
	struct timing_generator *optc,
	const struct dc_crtc_timing *dc_crtc_timing,
	int vready_offset,
	int vstartup_start,
	int vupdate_offset,
	int vupdate_width,
	const enum signal_type signal,
	bool use_vbios)
{
	struct dc_crtc_timing patched_crtc_timing;
	uint32_t asic_blank_end;
	uint32_t asic_blank_start;
	uint32_t v_total;
	uint32_t v_sync_end;
	uint32_t h_sync_polarity, v_sync_polarity;
	uint32_t start_point = 0;
	uint32_t field_num = 0;
	enum h_timing_div_mode h_div = H_TIMING_NO_DIV;

	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	optc1->signal = signal;
	optc1->vready_offset = vready_offset;
	optc1->vstartup_start = vstartup_start;
	optc1->vupdate_offset = vupdate_offset;
	optc1->vupdate_width = vupdate_width;
	patched_crtc_timing = *dc_crtc_timing;
	apply_front_porch_workaround(&patched_crtc_timing);

	/* Load horizontal timing */

	/* CRTC_H_TOTAL = vesa.h_total - 1 */
	REG_SET(OTG_H_TOTAL, 0,
			OTG_H_TOTAL,  patched_crtc_timing.h_total - 1);

	/* h_sync_start = 0, h_sync_end = vesa.h_sync_width */
	REG_UPDATE_2(OTG_H_SYNC_A,
			OTG_H_SYNC_A_START, 0,
			OTG_H_SYNC_A_END, patched_crtc_timing.h_sync_width);

	/* blank_start = line end - front porch */
	asic_blank_start = patched_crtc_timing.h_total -
			patched_crtc_timing.h_front_porch;

	/* blank_end = blank_start - active */
	asic_blank_end = asic_blank_start -
			patched_crtc_timing.h_border_right -
			patched_crtc_timing.h_addressable -
			patched_crtc_timing.h_border_left;

	REG_UPDATE_2(OTG_H_BLANK_START_END,
			OTG_H_BLANK_START, asic_blank_start,
			OTG_H_BLANK_END, asic_blank_end);

	/* h_sync polarity */
	h_sync_polarity = patched_crtc_timing.flags.HSYNC_POSITIVE_POLARITY ?
			0 : 1;

	REG_UPDATE(OTG_H_SYNC_A_CNTL,
			OTG_H_SYNC_A_POL, h_sync_polarity);

	v_total = patched_crtc_timing.v_total - 1;

	REG_SET(OTG_V_TOTAL, 0,
			OTG_V_TOTAL, v_total);

	/* In case of V_TOTAL_CONTROL is on, make sure OTG_V_TOTAL_MAX and
	 * OTG_V_TOTAL_MIN are equal to V_TOTAL.
	 */
	REG_SET(OTG_V_TOTAL_MAX, 0,
		OTG_V_TOTAL_MAX, v_total);
	REG_SET(OTG_V_TOTAL_MIN, 0,
		OTG_V_TOTAL_MIN, v_total);

	/* v_sync_start = 0, v_sync_end = v_sync_width */
	v_sync_end = patched_crtc_timing.v_sync_width;

	REG_UPDATE_2(OTG_V_SYNC_A,
			OTG_V_SYNC_A_START, 0,
			OTG_V_SYNC_A_END, v_sync_end);

	/* blank_start = frame end - front porch */
	asic_blank_start = patched_crtc_timing.v_total -
			patched_crtc_timing.v_front_porch;

	/* blank_end = blank_start - active */
	asic_blank_end = asic_blank_start -
			patched_crtc_timing.v_border_bottom -
			patched_crtc_timing.v_addressable -
			patched_crtc_timing.v_border_top;

	REG_UPDATE_2(OTG_V_BLANK_START_END,
			OTG_V_BLANK_START, asic_blank_start,
			OTG_V_BLANK_END, asic_blank_end);

	/* v_sync polarity */
	v_sync_polarity = patched_crtc_timing.flags.VSYNC_POSITIVE_POLARITY ?
			0 : 1;

	REG_UPDATE(OTG_V_SYNC_A_CNTL,
		OTG_V_SYNC_A_POL, v_sync_polarity);

	if (optc1->signal == SIGNAL_TYPE_DISPLAY_PORT ||
			optc1->signal == SIGNAL_TYPE_DISPLAY_PORT_MST ||
			optc1->signal == SIGNAL_TYPE_EDP) {
		start_point = 1;
		if (patched_crtc_timing.flags.INTERLACE == 1)
			field_num = 1;
	}

	/* Interlace */
	if (REG(OTG_INTERLACE_CONTROL)) {
		if (patched_crtc_timing.flags.INTERLACE == 1)
			REG_UPDATE(OTG_INTERLACE_CONTROL,
					OTG_INTERLACE_ENABLE, 1);
		else
			REG_UPDATE(OTG_INTERLACE_CONTROL,
					OTG_INTERLACE_ENABLE, 0);
	}

	/* VTG enable set to 0 first VInit */
	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 0);

	/* original code is using VTG offset to address OTG reg, seems wrong */
	REG_UPDATE_2(OTG_CONTROL,
			OTG_START_POINT_CNTL, start_point,
			OTG_FIELD_NUMBER_CNTL, field_num);

	optc->funcs->program_global_sync(optc,
			vready_offset,
			vstartup_start,
			vupdate_offset,
			vupdate_width);

	optc->funcs->set_vtg_params(optc, dc_crtc_timing, true);

	/* TODO
	 * patched_crtc_timing.flags.HORZ_COUNT_BY_TWO == 1
	 * program_horz_count_by_2
	 * for DVI 30bpp mode, 0 otherwise
	 * program_horz_count_by_2(optc, &patched_crtc_timing);
	 */

	/* Enable stereo - only when we need to pack 3D frame. Other types
	 * of stereo handled in explicit call
	 */

	if (optc1_is_two_pixels_per_containter(&patched_crtc_timing) || optc1->opp_count == 2)
		h_div = H_TIMING_DIV_BY2;

	if (REG(OPTC_DATA_FORMAT_CONTROL) && optc1->tg_mask->OPTC_DATA_FORMAT != 0) {
		uint32_t data_fmt = 0;

		if (patched_crtc_timing.pixel_encoding == PIXEL_ENCODING_YCBCR422)
			data_fmt = 1;
		else if (patched_crtc_timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
			data_fmt = 2;

		REG_UPDATE(OPTC_DATA_FORMAT_CONTROL, OPTC_DATA_FORMAT, data_fmt);
	}

	if (optc1->tg_mask->OTG_H_TIMING_DIV_MODE != 0) {
		if (optc1->opp_count == 4)
			h_div = H_TIMING_DIV_BY4;

		REG_UPDATE(OTG_H_TIMING_CNTL,
		OTG_H_TIMING_DIV_MODE, h_div);
	} else {
		REG_UPDATE(OTG_H_TIMING_CNTL,
		OTG_H_TIMING_DIV_BY2, h_div);
	}
}

void optc1_set_vtg_params(struct timing_generator *optc,
		const struct dc_crtc_timing *dc_crtc_timing, bool program_fp2)
{
	struct dc_crtc_timing patched_crtc_timing;
	uint32_t asic_blank_end;
	uint32_t v_init;
	uint32_t v_fp2 = 0;
	int32_t vertical_line_start;

	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	patched_crtc_timing = *dc_crtc_timing;
	apply_front_porch_workaround(&patched_crtc_timing);

	/* VCOUNT_INIT is the start of blank */
	v_init = patched_crtc_timing.v_total - patched_crtc_timing.v_front_porch;

	/* end of blank = v_init - active */
	asic_blank_end = v_init -
			patched_crtc_timing.v_border_bottom -
			patched_crtc_timing.v_addressable -
			patched_crtc_timing.v_border_top;

	/* if VSTARTUP is before VSYNC, FP2 is the offset, otherwise 0 */
	vertical_line_start = asic_blank_end - optc1->vstartup_start + 1;
	if (vertical_line_start < 0)
		v_fp2 = -vertical_line_start;

	/* Interlace */
	if (REG(OTG_INTERLACE_CONTROL)) {
		if (patched_crtc_timing.flags.INTERLACE == 1) {
			v_init = v_init / 2;
			if ((optc1->vstartup_start/2)*2 > asic_blank_end)
				v_fp2 = v_fp2 / 2;
		}
	}

	if (program_fp2)
		REG_UPDATE_2(CONTROL,
				VTG0_FP2, v_fp2,
				VTG0_VCOUNT_INIT, v_init);
	else
		REG_UPDATE(CONTROL, VTG0_VCOUNT_INIT, v_init);
}

void optc1_set_blank_data_double_buffer(struct timing_generator *optc, bool enable)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	uint32_t blank_data_double_buffer_enable = enable ? 1 : 0;

	REG_UPDATE(OTG_DOUBLE_BUFFER_CONTROL,
			OTG_BLANK_DATA_DOUBLE_BUFFER_EN, blank_data_double_buffer_enable);
}

/**
 * optc1_set_timing_double_buffer() - DRR double buffering control
 *
 * Sets double buffer point for V_TOTAL, H_TOTAL, VTOTAL_MIN,
 * VTOTAL_MAX, VTOTAL_MIN_SEL and VTOTAL_MAX_SEL registers.
 *
 * Options: any time,  start of frame, dp start of frame (range timing)
 */
void optc1_set_timing_double_buffer(struct timing_generator *optc, bool enable)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t mode = enable ? 2 : 0;

	REG_UPDATE(OTG_DOUBLE_BUFFER_CONTROL,
		   OTG_RANGE_TIMING_DBUF_UPDATE_MODE, mode);
}

/**
 * unblank_crtc
 * Call ASIC Control Object to UnBlank CRTC.
 */
static void optc1_unblank_crtc(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE_2(OTG_BLANK_CONTROL,
			OTG_BLANK_DATA_EN, 0,
			OTG_BLANK_DE_MODE, 0);

	/* W/A for automated testing
	 * Automated testing will fail underflow test as there
	 * sporadic underflows which occur during the optc blank
	 * sequence.  As a w/a, clear underflow on unblank.
	 * This prevents the failure, but will not mask actual
	 * underflow that affect real use cases.
	 */
	optc1_clear_optc_underflow(optc);
}

/**
 * blank_crtc
 * Call ASIC Control Object to Blank CRTC.
 */

static void optc1_blank_crtc(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE_2(OTG_BLANK_CONTROL,
			OTG_BLANK_DATA_EN, 1,
			OTG_BLANK_DE_MODE, 0);

	optc1_set_blank_data_double_buffer(optc, false);
}

void optc1_set_blank(struct timing_generator *optc,
		bool enable_blanking)
{
	if (enable_blanking)
		optc1_blank_crtc(optc);
	else
		optc1_unblank_crtc(optc);
}

bool optc1_is_blanked(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t blank_en;
	uint32_t blank_state;

	REG_GET_2(OTG_BLANK_CONTROL,
			OTG_BLANK_DATA_EN, &blank_en,
			OTG_CURRENT_BLANK_STATE, &blank_state);

	return blank_en && blank_state;
}

void optc1_enable_optc_clock(struct timing_generator *optc, bool enable)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	if (enable) {
		REG_UPDATE_2(OPTC_INPUT_CLOCK_CONTROL,
				OPTC_INPUT_CLK_EN, 1,
				OPTC_INPUT_CLK_GATE_DIS, 1);

		REG_WAIT(OPTC_INPUT_CLOCK_CONTROL,
				OPTC_INPUT_CLK_ON, 1,
				1, 1000);

		/* Enable clock */
		REG_UPDATE_2(OTG_CLOCK_CONTROL,
				OTG_CLOCK_EN, 1,
				OTG_CLOCK_GATE_DIS, 1);
		REG_WAIT(OTG_CLOCK_CONTROL,
				OTG_CLOCK_ON, 1,
				1, 1000);
	} else  {
		REG_UPDATE_2(OTG_CLOCK_CONTROL,
				OTG_CLOCK_GATE_DIS, 0,
				OTG_CLOCK_EN, 0);

		REG_UPDATE_2(OPTC_INPUT_CLOCK_CONTROL,
				OPTC_INPUT_CLK_GATE_DIS, 0,
				OPTC_INPUT_CLK_EN, 0);
	}
}

/**
 * Enable CRTC
 * Enable CRTC - call ASIC Control Object to enable Timing generator.
 */
static bool optc1_enable_crtc(struct timing_generator *optc)
{
	/* TODO FPGA wait for answer
	 * OTG_MASTER_UPDATE_MODE != CRTC_MASTER_UPDATE_MODE
	 * OTG_MASTER_UPDATE_LOCK != CRTC_MASTER_UPDATE_LOCK
	 */
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	/* opp instance for OTG. For DCN1.0, ODM is remoed.
	 * OPP and OPTC should 1:1 mapping
	 */
	REG_UPDATE(OPTC_DATA_SOURCE_SELECT,
			OPTC_SRC_SEL, optc->inst);

	/* VTG enable first is for HW workaround */
	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 1);

	REG_SEQ_START();

	/* Enable CRTC */
	REG_UPDATE_2(OTG_CONTROL,
			OTG_DISABLE_POINT_CNTL, 3,
			OTG_MASTER_EN, 1);

	REG_SEQ_SUBMIT();
	REG_SEQ_WAIT_DONE();

	return true;
}

/* disable_crtc - call ASIC Control Object to disable Timing generator. */
bool optc1_disable_crtc(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	/* disable otg request until end of the first line
	 * in the vertical blank region
	 */
	REG_UPDATE_2(OTG_CONTROL,
			OTG_DISABLE_POINT_CNTL, 3,
			OTG_MASTER_EN, 0);

	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 0);

	/* CRTC disabled, so disable  clock. */
	REG_WAIT(OTG_CLOCK_CONTROL,
			OTG_BUSY, 0,
			1, 100000);

	return true;
}


void optc1_program_blank_color(
		struct timing_generator *optc,
		const struct tg_color *black_color)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET_3(OTG_BLACK_COLOR, 0,
			OTG_BLACK_COLOR_B_CB, black_color->color_b_cb,
			OTG_BLACK_COLOR_G_Y, black_color->color_g_y,
			OTG_BLACK_COLOR_R_CR, black_color->color_r_cr);
}

bool optc1_validate_timing(
	struct timing_generator *optc,
	const struct dc_crtc_timing *timing)
{
	uint32_t v_blank;
	uint32_t h_blank;
	uint32_t min_v_blank;
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	ASSERT(timing != NULL);

	v_blank = (timing->v_total - timing->v_addressable -
					timing->v_border_top - timing->v_border_bottom);

	h_blank = (timing->h_total - timing->h_addressable -
		timing->h_border_right -
		timing->h_border_left);

	if (timing->timing_3d_format != TIMING_3D_FORMAT_NONE &&
		timing->timing_3d_format != TIMING_3D_FORMAT_HW_FRAME_PACKING &&
		timing->timing_3d_format != TIMING_3D_FORMAT_TOP_AND_BOTTOM &&
		timing->timing_3d_format != TIMING_3D_FORMAT_SIDE_BY_SIDE &&
		timing->timing_3d_format != TIMING_3D_FORMAT_FRAME_ALTERNATE &&
		timing->timing_3d_format != TIMING_3D_FORMAT_INBAND_FA)
		return false;

	/* Temporarily blocking interlacing mode until it's supported */
	if (timing->flags.INTERLACE == 1)
		return false;

	/* Check maximum number of pixels supported by Timing Generator
	 * (Currently will never fail, in order to fail needs display which
	 * needs more than 8192 horizontal and
	 * more than 8192 vertical total pixels)
	 */
	if (timing->h_total > optc1->max_h_total ||
		timing->v_total > optc1->max_v_total)
		return false;


	if (h_blank < optc1->min_h_blank)
		return false;

	if (timing->h_sync_width  < optc1->min_h_sync_width ||
		 timing->v_sync_width  < optc1->min_v_sync_width)
		return false;

	min_v_blank = timing->flags.INTERLACE?optc1->min_v_blank_interlace:optc1->min_v_blank;

	if (v_blank < min_v_blank)
		return false;

	return true;

}

/*
 * get_vblank_counter
 *
 * @brief
 * Get counter for vertical blanks. use register CRTC_STATUS_FRAME_COUNT which
 * holds the counter of frames.
 *
 * @param
 * struct timing_generator *optc - [in] timing generator which controls the
 * desired CRTC
 *
 * @return
 * Counter of frames, which should equal to number of vblanks.
 */
uint32_t optc1_get_vblank_counter(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t frame_count;

	REG_GET(OTG_STATUS_FRAME_COUNT,
		OTG_FRAME_COUNT, &frame_count);

	return frame_count;
}

void optc1_lock(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t regval = 0;

	regval = REG_READ(OTG_CONTROL);

	/* otg is not running, do not need to be locked */
	if ((regval & 0x1) == 0x0)
		return;

	REG_SET(OTG_GLOBAL_CONTROL0, 0,
			OTG_MASTER_UPDATE_LOCK_SEL, optc->inst);
	REG_SET(OTG_MASTER_UPDATE_LOCK, 0,
			OTG_MASTER_UPDATE_LOCK, 1);

	/* Should be fast, status does not update on maximus */
	if (optc->ctx->dce_environment != DCE_ENV_FPGA_MAXIMUS) {

		REG_WAIT(OTG_MASTER_UPDATE_LOCK,
				UPDATE_LOCK_STATUS, 1,
				1, 10);
	}
}

void optc1_unlock(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_MASTER_UPDATE_LOCK, 0,
			OTG_MASTER_UPDATE_LOCK, 0);
}

bool optc1_is_locked(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t locked;

	REG_GET(OTG_MASTER_UPDATE_LOCK, UPDATE_LOCK_STATUS, &locked);

	return (locked == 1);
}

void optc1_get_position(struct timing_generator *optc,
		struct crtc_position *position)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_GET_2(OTG_STATUS_POSITION,
			OTG_HORZ_COUNT, &position->horizontal_count,
			OTG_VERT_COUNT, &position->vertical_count);

	REG_GET(OTG_NOM_VERT_POSITION,
			OTG_VERT_COUNT_NOM, &position->nominal_vcount);
}

bool optc1_is_counter_moving(struct timing_generator *optc)
{
	struct crtc_position position1, position2;

	optc->funcs->get_position(optc, &position1);
	optc->funcs->get_position(optc, &position2);

	if (position1.horizontal_count == position2.horizontal_count &&
		position1.vertical_count == position2.vertical_count)
		return false;
	else
		return true;
}

bool optc1_did_triggered_reset_occur(
	struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t occurred_force, occurred_vsync;

	REG_GET(OTG_FORCE_COUNT_NOW_CNTL,
		OTG_FORCE_COUNT_NOW_OCCURRED, &occurred_force);

	REG_GET(OTG_VERT_SYNC_CONTROL,
		OTG_FORCE_VSYNC_NEXT_LINE_OCCURRED, &occurred_vsync);

	return occurred_vsync != 0 || occurred_force != 0;
}

void optc1_disable_reset_trigger(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_WRITE(OTG_TRIGA_CNTL, 0);

	REG_SET(OTG_FORCE_COUNT_NOW_CNTL, 0,
		OTG_FORCE_COUNT_NOW_CLEAR, 1);

	REG_SET(OTG_VERT_SYNC_CONTROL, 0,
		OTG_FORCE_VSYNC_NEXT_LINE_CLEAR, 1);
}

void optc1_enable_reset_trigger(struct timing_generator *optc, int source_tg_inst)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t falling_edge;

	REG_GET(OTG_V_SYNC_A_CNTL,
			OTG_V_SYNC_A_POL, &falling_edge);

	if (falling_edge)
		REG_SET_3(OTG_TRIGA_CNTL, 0,
				/* vsync signal from selected OTG pipe based
				 * on OTG_TRIG_SOURCE_PIPE_SELECT setting
				 */
				OTG_TRIGA_SOURCE_SELECT, 20,
				OTG_TRIGA_SOURCE_PIPE_SELECT, source_tg_inst,
				/* always detect falling edge */
				OTG_TRIGA_FALLING_EDGE_DETECT_CNTL, 1);
	else
		REG_SET_3(OTG_TRIGA_CNTL, 0,
				/* vsync signal from selected OTG pipe based
				 * on OTG_TRIG_SOURCE_PIPE_SELECT setting
				 */
				OTG_TRIGA_SOURCE_SELECT, 20,
				OTG_TRIGA_SOURCE_PIPE_SELECT, source_tg_inst,
				/* always detect rising edge */
				OTG_TRIGA_RISING_EDGE_DETECT_CNTL, 1);

	REG_SET(OTG_FORCE_COUNT_NOW_CNTL, 0,
			/* force H count to H_TOTAL and V count to V_TOTAL in
			 * progressive mode and V_TOTAL-1 in interlaced mode
			 */
			OTG_FORCE_COUNT_NOW_MODE, 2);
}

void optc1_enable_crtc_reset(
		struct timing_generator *optc,
		int source_tg_inst,
		struct crtc_trigger_info *crtc_tp)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t falling_edge = 0;
	uint32_t rising_edge = 0;

	switch (crtc_tp->event) {

	case CRTC_EVENT_VSYNC_RISING:
		rising_edge = 1;
		break;

	case CRTC_EVENT_VSYNC_FALLING:
		falling_edge = 1;
		break;
	}

	REG_SET_4(OTG_TRIGA_CNTL, 0,
		 /* vsync signal from selected OTG pipe based
		  * on OTG_TRIG_SOURCE_PIPE_SELECT setting
		  */
		  OTG_TRIGA_SOURCE_SELECT, 20,
		  OTG_TRIGA_SOURCE_PIPE_SELECT, source_tg_inst,
		  /* always detect falling edge */
		  OTG_TRIGA_RISING_EDGE_DETECT_CNTL, rising_edge,
		  OTG_TRIGA_FALLING_EDGE_DETECT_CNTL, falling_edge);

	switch (crtc_tp->delay) {
	case TRIGGER_DELAY_NEXT_LINE:
		REG_SET(OTG_VERT_SYNC_CONTROL, 0,
				OTG_AUTO_FORCE_VSYNC_MODE, 1);
		break;
	case TRIGGER_DELAY_NEXT_PIXEL:
		REG_SET(OTG_FORCE_COUNT_NOW_CNTL, 0,
			/* force H count to H_TOTAL and V count to V_TOTAL in
			 * progressive mode and V_TOTAL-1 in interlaced mode
			 */
			OTG_FORCE_COUNT_NOW_MODE, 2);
		break;
	}
}

void optc1_wait_for_state(struct timing_generator *optc,
		enum crtc_state state)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	switch (state) {
	case CRTC_STATE_VBLANK:
		REG_WAIT(OTG_STATUS,
				OTG_V_BLANK, 1,
				1, 100000); /* 1 vupdate at 10hz */
		break;

	case CRTC_STATE_VACTIVE:
		REG_WAIT(OTG_STATUS,
				OTG_V_ACTIVE_DISP, 1,
				1, 100000); /* 1 vupdate at 10hz */
		break;

	default:
		break;
	}
}

void optc1_set_early_control(
	struct timing_generator *optc,
	uint32_t early_cntl)
{
	/* asic design change, do not need this control
	 * empty for share caller logic
	 */
}


void optc1_set_static_screen_control(
	struct timing_generator *optc,
	uint32_t event_triggers,
	uint32_t num_frames)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	// By register spec, it only takes 8 bit value
	if (num_frames > 0xFF)
		num_frames = 0xFF;

	/* Bit 8 is no longer applicable in RV for PSR case,
	 * set bit 8 to 0 if given
	 */
	if ((event_triggers & STATIC_SCREEN_EVENT_MASK_RANGETIMING_DOUBLE_BUFFER_UPDATE_EN)
			!= 0)
		event_triggers = event_triggers &
		~STATIC_SCREEN_EVENT_MASK_RANGETIMING_DOUBLE_BUFFER_UPDATE_EN;

	REG_SET_2(OTG_STATIC_SCREEN_CONTROL, 0,
			OTG_STATIC_SCREEN_EVENT_MASK, event_triggers,
			OTG_STATIC_SCREEN_FRAME_COUNT, num_frames);
}

static void optc1_setup_manual_trigger(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_GLOBAL_CONTROL2, 0,
			MANUAL_FLOW_CONTROL_SEL, optc->inst);

	REG_SET_8(OTG_TRIGA_CNTL, 0,
			OTG_TRIGA_SOURCE_SELECT, 22,
			OTG_TRIGA_SOURCE_PIPE_SELECT, optc->inst,
			OTG_TRIGA_RISING_EDGE_DETECT_CNTL, 1,
			OTG_TRIGA_FALLING_EDGE_DETECT_CNTL, 0,
			OTG_TRIGA_POLARITY_SELECT, 0,
			OTG_TRIGA_FREQUENCY_SELECT, 0,
			OTG_TRIGA_DELAY, 0,
			OTG_TRIGA_CLEAR, 1);
}

static void optc1_program_manual_trigger(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_MANUAL_FLOW_CONTROL, 0,
			MANUAL_FLOW_CONTROL, 1);

	REG_SET(OTG_MANUAL_FLOW_CONTROL, 0,
			MANUAL_FLOW_CONTROL, 0);
}


/**
 *****************************************************************************
 *  Function: set_drr
 *
 *  @brief
 *     Program dynamic refresh rate registers m_OTGx_OTG_V_TOTAL_*.
 *
 *****************************************************************************
 */
void optc1_set_drr(
	struct timing_generator *optc,
	const struct drr_params *params)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	if (params != NULL &&
		params->vertical_total_max > 0 &&
		params->vertical_total_min > 0) {

		if (params->vertical_total_mid != 0) {

			REG_SET(OTG_V_TOTAL_MID, 0,
				OTG_V_TOTAL_MID, params->vertical_total_mid - 1);

			REG_UPDATE_2(OTG_V_TOTAL_CONTROL,
					OTG_VTOTAL_MID_REPLACING_MAX_EN, 1,
					OTG_VTOTAL_MID_FRAME_NUM,
					(uint8_t)params->vertical_total_mid_frame_num);

		}

		REG_SET(OTG_V_TOTAL_MAX, 0,
			OTG_V_TOTAL_MAX, params->vertical_total_max - 1);

		REG_SET(OTG_V_TOTAL_MIN, 0,
			OTG_V_TOTAL_MIN, params->vertical_total_min - 1);

		REG_UPDATE_5(OTG_V_TOTAL_CONTROL,
				OTG_V_TOTAL_MIN_SEL, 1,
				OTG_V_TOTAL_MAX_SEL, 1,
				OTG_FORCE_LOCK_ON_EVENT, 0,
				OTG_SET_V_TOTAL_MIN_MASK_EN, 0,
				OTG_SET_V_TOTAL_MIN_MASK, 0);

		// Setup manual flow control for EOF via TRIG_A
		optc->funcs->setup_manual_trigger(optc);

	} else {
		REG_UPDATE_4(OTG_V_TOTAL_CONTROL,
				OTG_SET_V_TOTAL_MIN_MASK, 0,
				OTG_V_TOTAL_MIN_SEL, 0,
				OTG_V_TOTAL_MAX_SEL, 0,
				OTG_FORCE_LOCK_ON_EVENT, 0);

		REG_SET(OTG_V_TOTAL_MIN, 0,
			OTG_V_TOTAL_MIN, 0);

		REG_SET(OTG_V_TOTAL_MAX, 0,
			OTG_V_TOTAL_MAX, 0);
	}
}

void optc1_set_vtotal_min_max(struct timing_generator *optc, int vtotal_min, int vtotal_max)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_V_TOTAL_MAX, 0,
		OTG_V_TOTAL_MAX, vtotal_max);

	REG_SET(OTG_V_TOTAL_MIN, 0,
		OTG_V_TOTAL_MIN, vtotal_min);
}

static void optc1_set_test_pattern(
	struct timing_generator *optc,
	/* TODO: replace 'controller_dp_test_pattern' by 'test_pattern_mode'
	 * because this is not DP-specific (which is probably somewhere in DP
	 * encoder) */
	enum controller_dp_test_pattern test_pattern,
	enum dc_color_depth color_depth)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	enum test_pattern_color_format bit_depth;
	enum test_pattern_dyn_range dyn_range;
	enum test_pattern_mode mode;
	uint32_t pattern_mask;
	uint32_t pattern_data;
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

		REG_UPDATE_2(OTG_TEST_PATTERN_PARAMETERS,
				OTG_TEST_PATTERN_VRES, 6,
				OTG_TEST_PATTERN_HRES, 6);

		REG_UPDATE_4(OTG_TEST_PATTERN_CONTROL,
				OTG_TEST_PATTERN_EN, 1,
				OTG_TEST_PATTERN_MODE, mode,
				OTG_TEST_PATTERN_DYNAMIC_RANGE, dyn_range,
				OTG_TEST_PATTERN_COLOR_FORMAT, bit_depth);
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

		REG_WRITE(OTG_TEST_PATTERN_PARAMETERS, 0);

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
		pattern_data = 0;
		for (index = 0; index < 6; index++) {
			/* prepare color mask, first write PATTERN_DATA
			 * will have all zeros
			 */
			pattern_mask = (1 << index);

			/* write color component */
			REG_SET_2(OTG_TEST_PATTERN_COLOR, 0,
					OTG_TEST_PATTERN_MASK, pattern_mask,
					OTG_TEST_PATTERN_DATA, pattern_data);

			/* prepare next color component,
			 * will be written in the next iteration
			 */
			pattern_data = dst_color[index];
		}
		/* write last color component,
		 * it's been already prepared in the loop
		 */
		REG_SET_2(OTG_TEST_PATTERN_COLOR, 0,
				OTG_TEST_PATTERN_MASK, pattern_mask,
				OTG_TEST_PATTERN_DATA, pattern_data);

		/* enable test pattern */
		REG_UPDATE_4(OTG_TEST_PATTERN_CONTROL,
				OTG_TEST_PATTERN_EN, 1,
				OTG_TEST_PATTERN_MODE, mode,
				OTG_TEST_PATTERN_DYNAMIC_RANGE, 0,
				OTG_TEST_PATTERN_COLOR_FORMAT, bit_depth);
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
			REG_UPDATE_5(OTG_TEST_PATTERN_PARAMETERS,
					OTG_TEST_PATTERN_INC0, inc_base,
					OTG_TEST_PATTERN_INC1, 0,
					OTG_TEST_PATTERN_HRES, 6,
					OTG_TEST_PATTERN_VRES, 6,
					OTG_TEST_PATTERN_RAMP0_OFFSET, 0);
		}
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_8:
		{
			REG_UPDATE_5(OTG_TEST_PATTERN_PARAMETERS,
					OTG_TEST_PATTERN_INC0, inc_base,
					OTG_TEST_PATTERN_INC1, 0,
					OTG_TEST_PATTERN_HRES, 8,
					OTG_TEST_PATTERN_VRES, 6,
					OTG_TEST_PATTERN_RAMP0_OFFSET, 0);
		}
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_10:
		{
			REG_UPDATE_5(OTG_TEST_PATTERN_PARAMETERS,
					OTG_TEST_PATTERN_INC0, inc_base,
					OTG_TEST_PATTERN_INC1, inc_base + 2,
					OTG_TEST_PATTERN_HRES, 8,
					OTG_TEST_PATTERN_VRES, 5,
					OTG_TEST_PATTERN_RAMP0_OFFSET, 384 << 6);
		}
		break;
		default:
		break;
		}

		REG_WRITE(OTG_TEST_PATTERN_COLOR, 0);

		/* enable test pattern */
		REG_WRITE(OTG_TEST_PATTERN_CONTROL, 0);

		REG_SET_4(OTG_TEST_PATTERN_CONTROL, 0,
				OTG_TEST_PATTERN_EN, 1,
				OTG_TEST_PATTERN_MODE, mode,
				OTG_TEST_PATTERN_DYNAMIC_RANGE, 0,
				OTG_TEST_PATTERN_COLOR_FORMAT, bit_depth);
	}
	break;
	case CONTROLLER_DP_TEST_PATTERN_VIDEOMODE:
	{
		REG_WRITE(OTG_TEST_PATTERN_CONTROL, 0);
		REG_WRITE(OTG_TEST_PATTERN_COLOR, 0);
		REG_WRITE(OTG_TEST_PATTERN_PARAMETERS, 0);
	}
	break;
	default:
		break;

	}
}

void optc1_get_crtc_scanoutpos(
	struct timing_generator *optc,
	uint32_t *v_blank_start,
	uint32_t *v_blank_end,
	uint32_t *h_position,
	uint32_t *v_position)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	struct crtc_position position;

	REG_GET_2(OTG_V_BLANK_START_END,
			OTG_V_BLANK_START, v_blank_start,
			OTG_V_BLANK_END, v_blank_end);

	optc1_get_position(optc, &position);

	*h_position = position.horizontal_count;
	*v_position = position.vertical_count;
}

static void optc1_enable_stereo(struct timing_generator *optc,
	const struct dc_crtc_timing *timing, struct crtc_stereo_flags *flags)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	if (flags) {
		uint32_t stereo_en;
		stereo_en = flags->FRAME_PACKED == 0 ? 1 : 0;

		if (flags->PROGRAM_STEREO)
			REG_UPDATE_3(OTG_STEREO_CONTROL,
				OTG_STEREO_EN, stereo_en,
				OTG_STEREO_SYNC_OUTPUT_LINE_NUM, 0,
				OTG_STEREO_SYNC_OUTPUT_POLARITY, flags->RIGHT_EYE_POLARITY == 0 ? 0 : 1);

		if (flags->PROGRAM_POLARITY)
			REG_UPDATE(OTG_STEREO_CONTROL,
				OTG_STEREO_EYE_FLAG_POLARITY,
				flags->RIGHT_EYE_POLARITY == 0 ? 0 : 1);

		if (flags->DISABLE_STEREO_DP_SYNC)
			REG_UPDATE(OTG_STEREO_CONTROL,
				OTG_DISABLE_STEREOSYNC_OUTPUT_FOR_DP, 1);

		if (flags->PROGRAM_STEREO)
			REG_UPDATE_2(OTG_3D_STRUCTURE_CONTROL,
				OTG_3D_STRUCTURE_EN, flags->FRAME_PACKED,
				OTG_3D_STRUCTURE_STEREO_SEL_OVR, flags->FRAME_PACKED);

	}
}

void optc1_program_stereo(struct timing_generator *optc,
	const struct dc_crtc_timing *timing, struct crtc_stereo_flags *flags)
{
	if (flags->PROGRAM_STEREO)
		optc1_enable_stereo(optc, timing, flags);
	else
		optc1_disable_stereo(optc);
}


bool optc1_is_stereo_left_eye(struct timing_generator *optc)
{
	bool ret = false;
	uint32_t left_eye = 0;
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_GET(OTG_STEREO_STATUS,
		OTG_STEREO_CURRENT_EYE, &left_eye);
	if (left_eye == 1)
		ret = true;
	else
		ret = false;

	return ret;
}

bool optc1_get_hw_timing(struct timing_generator *tg,
		struct dc_crtc_timing *hw_crtc_timing)
{
	struct dcn_otg_state s = {0};

	if (tg == NULL || hw_crtc_timing == NULL)
		return false;

	optc1_read_otg_state(DCN10TG_FROM_TG(tg), &s);

	hw_crtc_timing->h_total = s.h_total + 1;
	hw_crtc_timing->h_addressable = s.h_total - ((s.h_total - s.h_blank_start) + s.h_blank_end);
	hw_crtc_timing->h_front_porch = s.h_total + 1 - s.h_blank_start;
	hw_crtc_timing->h_sync_width = s.h_sync_a_end - s.h_sync_a_start;

	hw_crtc_timing->v_total = s.v_total + 1;
	hw_crtc_timing->v_addressable = s.v_total - ((s.v_total - s.v_blank_start) + s.v_blank_end);
	hw_crtc_timing->v_front_porch = s.v_total + 1 - s.v_blank_start;
	hw_crtc_timing->v_sync_width = s.v_sync_a_end - s.v_sync_a_start;

	return true;
}


void optc1_read_otg_state(struct optc *optc1,
		struct dcn_otg_state *s)
{
	REG_GET(OTG_CONTROL,
			OTG_MASTER_EN, &s->otg_enabled);

	REG_GET_2(OTG_V_BLANK_START_END,
			OTG_V_BLANK_START, &s->v_blank_start,
			OTG_V_BLANK_END, &s->v_blank_end);

	REG_GET(OTG_V_SYNC_A_CNTL,
			OTG_V_SYNC_A_POL, &s->v_sync_a_pol);

	REG_GET(OTG_V_TOTAL,
			OTG_V_TOTAL, &s->v_total);

	REG_GET(OTG_V_TOTAL_MAX,
			OTG_V_TOTAL_MAX, &s->v_total_max);

	REG_GET(OTG_V_TOTAL_MIN,
			OTG_V_TOTAL_MIN, &s->v_total_min);

	REG_GET(OTG_V_TOTAL_CONTROL,
			OTG_V_TOTAL_MAX_SEL, &s->v_total_max_sel);

	REG_GET(OTG_V_TOTAL_CONTROL,
			OTG_V_TOTAL_MIN_SEL, &s->v_total_min_sel);

	REG_GET_2(OTG_V_SYNC_A,
			OTG_V_SYNC_A_START, &s->v_sync_a_start,
			OTG_V_SYNC_A_END, &s->v_sync_a_end);

	REG_GET_2(OTG_H_BLANK_START_END,
			OTG_H_BLANK_START, &s->h_blank_start,
			OTG_H_BLANK_END, &s->h_blank_end);

	REG_GET_2(OTG_H_SYNC_A,
			OTG_H_SYNC_A_START, &s->h_sync_a_start,
			OTG_H_SYNC_A_END, &s->h_sync_a_end);

	REG_GET(OTG_H_SYNC_A_CNTL,
			OTG_H_SYNC_A_POL, &s->h_sync_a_pol);

	REG_GET(OTG_H_TOTAL,
			OTG_H_TOTAL, &s->h_total);

	REG_GET(OPTC_INPUT_GLOBAL_CONTROL,
			OPTC_UNDERFLOW_OCCURRED_STATUS, &s->underflow_occurred_status);

	REG_GET(OTG_VERTICAL_INTERRUPT2_CONTROL,
			OTG_VERTICAL_INTERRUPT2_INT_ENABLE, &s->vertical_interrupt2_en);

	REG_GET(OTG_VERTICAL_INTERRUPT2_POSITION,
			OTG_VERTICAL_INTERRUPT2_LINE_START, &s->vertical_interrupt2_line);
}

bool optc1_get_otg_active_size(struct timing_generator *optc,
		uint32_t *otg_active_width,
		uint32_t *otg_active_height)
{
	uint32_t otg_enabled;
	uint32_t v_blank_start;
	uint32_t v_blank_end;
	uint32_t h_blank_start;
	uint32_t h_blank_end;
	struct optc *optc1 = DCN10TG_FROM_TG(optc);


	REG_GET(OTG_CONTROL,
			OTG_MASTER_EN, &otg_enabled);

	if (otg_enabled == 0)
		return false;

	REG_GET_2(OTG_V_BLANK_START_END,
			OTG_V_BLANK_START, &v_blank_start,
			OTG_V_BLANK_END, &v_blank_end);

	REG_GET_2(OTG_H_BLANK_START_END,
			OTG_H_BLANK_START, &h_blank_start,
			OTG_H_BLANK_END, &h_blank_end);

	*otg_active_width = v_blank_start - v_blank_end;
	*otg_active_height = h_blank_start - h_blank_end;
	return true;
}

void optc1_clear_optc_underflow(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE(OPTC_INPUT_GLOBAL_CONTROL, OPTC_UNDERFLOW_CLEAR, 1);
}

void optc1_tg_init(struct timing_generator *optc)
{
	optc1_set_blank_data_double_buffer(optc, true);
	optc1_set_timing_double_buffer(optc, true);
	optc1_clear_optc_underflow(optc);
}

bool optc1_is_tg_enabled(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t otg_enabled = 0;

	REG_GET(OTG_CONTROL, OTG_MASTER_EN, &otg_enabled);

	return (otg_enabled != 0);

}

bool optc1_is_optc_underflow_occurred(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t underflow_occurred = 0;

	REG_GET(OPTC_INPUT_GLOBAL_CONTROL,
			OPTC_UNDERFLOW_OCCURRED_STATUS,
			&underflow_occurred);

	return (underflow_occurred == 1);
}

bool optc1_configure_crc(struct timing_generator *optc,
			  const struct crc_params *params)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	/* Cannot configure crc on a CRTC that is disabled */
	if (!optc1_is_tg_enabled(optc))
		return false;

	REG_WRITE(OTG_CRC_CNTL, 0);

	if (!params->enable)
		return true;

	/* Program frame boundaries */
	/* Window A x axis start and end. */
	REG_UPDATE_2(OTG_CRC0_WINDOWA_X_CONTROL,
			OTG_CRC0_WINDOWA_X_START, params->windowa_x_start,
			OTG_CRC0_WINDOWA_X_END, params->windowa_x_end);

	/* Window A y axis start and end. */
	REG_UPDATE_2(OTG_CRC0_WINDOWA_Y_CONTROL,
			OTG_CRC0_WINDOWA_Y_START, params->windowa_y_start,
			OTG_CRC0_WINDOWA_Y_END, params->windowa_y_end);

	/* Window B x axis start and end. */
	REG_UPDATE_2(OTG_CRC0_WINDOWB_X_CONTROL,
			OTG_CRC0_WINDOWB_X_START, params->windowb_x_start,
			OTG_CRC0_WINDOWB_X_END, params->windowb_x_end);

	/* Window B y axis start and end. */
	REG_UPDATE_2(OTG_CRC0_WINDOWB_Y_CONTROL,
			OTG_CRC0_WINDOWB_Y_START, params->windowb_y_start,
			OTG_CRC0_WINDOWB_Y_END, params->windowb_y_end);

	/* Set crc mode and selection, and enable. Only using CRC0*/
	REG_UPDATE_3(OTG_CRC_CNTL,
			OTG_CRC_CONT_EN, params->continuous_mode ? 1 : 0,
			OTG_CRC0_SELECT, params->selection,
			OTG_CRC_EN, 1);

	return true;
}

bool optc1_get_crc(struct timing_generator *optc,
		    uint32_t *r_cr, uint32_t *g_y, uint32_t *b_cb)
{
	uint32_t field = 0;
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_GET(OTG_CRC_CNTL, OTG_CRC_EN, &field);

	/* Early return if CRC is not enabled for this CRTC */
	if (!field)
		return false;

	REG_GET_2(OTG_CRC0_DATA_RG,
			CRC0_R_CR, r_cr,
			CRC0_G_Y, g_y);

	REG_GET(OTG_CRC0_DATA_B,
			CRC0_B_CB, b_cb);

	return true;
}

static const struct timing_generator_funcs dcn10_tg_funcs = {
		.validate_timing = optc1_validate_timing,
		.program_timing = optc1_program_timing,
		.setup_vertical_interrupt0 = optc1_setup_vertical_interrupt0,
		.setup_vertical_interrupt1 = optc1_setup_vertical_interrupt1,
		.setup_vertical_interrupt2 = optc1_setup_vertical_interrupt2,
		.program_global_sync = optc1_program_global_sync,
		.enable_crtc = optc1_enable_crtc,
		.disable_crtc = optc1_disable_crtc,
		/* used by enable_timing_synchronization. Not need for FPGA */
		.is_counter_moving = optc1_is_counter_moving,
		.get_position = optc1_get_position,
		.get_frame_count = optc1_get_vblank_counter,
		.get_scanoutpos = optc1_get_crtc_scanoutpos,
		.get_otg_active_size = optc1_get_otg_active_size,
		.set_early_control = optc1_set_early_control,
		/* used by enable_timing_synchronization. Not need for FPGA */
		.wait_for_state = optc1_wait_for_state,
		.set_blank = optc1_set_blank,
		.is_blanked = optc1_is_blanked,
		.set_blank_color = optc1_program_blank_color,
		.did_triggered_reset_occur = optc1_did_triggered_reset_occur,
		.enable_reset_trigger = optc1_enable_reset_trigger,
		.enable_crtc_reset = optc1_enable_crtc_reset,
		.disable_reset_trigger = optc1_disable_reset_trigger,
		.lock = optc1_lock,
		.is_locked = optc1_is_locked,
		.unlock = optc1_unlock,
		.enable_optc_clock = optc1_enable_optc_clock,
		.set_drr = optc1_set_drr,
		.get_last_used_drr_vtotal = NULL,
		.set_static_screen_control = optc1_set_static_screen_control,
		.set_test_pattern = optc1_set_test_pattern,
		.program_stereo = optc1_program_stereo,
		.is_stereo_left_eye = optc1_is_stereo_left_eye,
		.set_blank_data_double_buffer = optc1_set_blank_data_double_buffer,
		.tg_init = optc1_tg_init,
		.is_tg_enabled = optc1_is_tg_enabled,
		.is_optc_underflow_occurred = optc1_is_optc_underflow_occurred,
		.clear_optc_underflow = optc1_clear_optc_underflow,
		.get_crc = optc1_get_crc,
		.configure_crc = optc1_configure_crc,
		.set_vtg_params = optc1_set_vtg_params,
		.program_manual_trigger = optc1_program_manual_trigger,
		.setup_manual_trigger = optc1_setup_manual_trigger,
		.get_hw_timing = optc1_get_hw_timing,
};

void dcn10_timing_generator_init(struct optc *optc1)
{
	optc1->base.funcs = &dcn10_tg_funcs;

	optc1->max_h_total = optc1->tg_mask->OTG_H_TOTAL + 1;
	optc1->max_v_total = optc1->tg_mask->OTG_V_TOTAL + 1;

	optc1->min_h_blank = 32;
	optc1->min_v_blank = 3;
	optc1->min_v_blank_interlace = 5;
	optc1->min_h_sync_width = 4;
	optc1->min_v_sync_width = 1;
}

/* "Containter" vs. "pixel" is a concept within HW blocks, mostly those closer to the back-end. It works like this:
 *
 * - In most of the formats (RGB or YCbCr 4:4:4, 4:2:2 uncompressed and DSC 4:2:2 Simple) pixel rate is the same as
 *   containter rate.
 *
 * - In 4:2:0 (DSC or uncompressed) there are two pixels per container, hence the target container rate has to be
 *   halved to maintain the correct pixel rate.
 *
 * - Unlike 4:2:2 uncompressed, DSC 4:2:2 Native also has two pixels per container (this happens when DSC is applied
 *   to it) and has to be treated the same as 4:2:0, i.e. target containter rate has to be halved in this case as well.
 *
 */
bool optc1_is_two_pixels_per_containter(const struct dc_crtc_timing *timing)
{
	bool two_pix = timing->pixel_encoding == PIXEL_ENCODING_YCBCR420;

	two_pix = two_pix || (timing->flags.DSC && timing->pixel_encoding == PIXEL_ENCODING_YCBCR422
			&& !timing->dsc_cfg.ycbcr422_simple);
	return two_pix;
}

