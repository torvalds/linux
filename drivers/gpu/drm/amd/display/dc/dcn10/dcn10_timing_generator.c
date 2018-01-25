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
#include "dcn10_timing_generator.h"
#include "dc.h"

#define REG(reg)\
	tgn10->tg_regs->reg

#define CTX \
	tgn10->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	tgn10->tg_shift->field_name, tgn10->tg_mask->field_name

#define STATIC_SCREEN_EVENT_MASK_RANGETIMING_DOUBLE_BUFFER_UPDATE_EN 0x100

/**
* apply_front_porch_workaround  TODO FPGA still need?
*
* This is a workaround for a bug that has existed since R5xx and has not been
* fixed keep Front porch at minimum 2 for Interlaced mode or 1 for progressive.
*/
static void tgn10_apply_front_porch_workaround(
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

static void tgn10_program_global_sync(
		struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	if (tg->dlg_otg_param.vstartup_start == 0) {
		BREAK_TO_DEBUGGER();
		return;
	}

	REG_SET(OTG_VSTARTUP_PARAM, 0,
		VSTARTUP_START, tg->dlg_otg_param.vstartup_start);

	REG_SET_2(OTG_VUPDATE_PARAM, 0,
			VUPDATE_OFFSET, tg->dlg_otg_param.vupdate_offset,
			VUPDATE_WIDTH, tg->dlg_otg_param.vupdate_width);

	REG_SET(OTG_VREADY_PARAM, 0,
			VREADY_OFFSET, tg->dlg_otg_param.vready_offset);
}

static void tgn10_disable_stereo(struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	REG_SET(OTG_STEREO_CONTROL, 0,
		OTG_STEREO_EN, 0);

	REG_SET_3(OTG_3D_STRUCTURE_CONTROL, 0,
		OTG_3D_STRUCTURE_EN, 0,
		OTG_3D_STRUCTURE_V_UPDATE_MODE, 0,
		OTG_3D_STRUCTURE_STEREO_SEL_OVR, 0);

	REG_UPDATE(OPPBUF_CONTROL,
		OPPBUF_ACTIVE_WIDTH, 0);
	REG_UPDATE(OPPBUF_3D_PARAMETERS_0,
		OPPBUF_3D_VACT_SPACE1_SIZE, 0);
}

/**
 * program_timing_generator   used by mode timing set
 * Program CRTC Timing Registers - OTG_H_*, OTG_V_*, Pixel repetition.
 * Including SYNC. Call BIOS command table to program Timings.
 */
static void tgn10_program_timing(
	struct timing_generator *tg,
	const struct dc_crtc_timing *dc_crtc_timing,
	bool use_vbios)
{
	struct dc_crtc_timing patched_crtc_timing;
	uint32_t vesa_sync_start;
	uint32_t asic_blank_end;
	uint32_t asic_blank_start;
	uint32_t v_total;
	uint32_t v_sync_end;
	uint32_t v_init, v_fp2;
	uint32_t h_sync_polarity, v_sync_polarity;
	uint32_t interlace_factor;
	uint32_t start_point = 0;
	uint32_t field_num = 0;
	uint32_t h_div_2;
	int32_t vertical_line_start;

	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	patched_crtc_timing = *dc_crtc_timing;
	tgn10_apply_front_porch_workaround(tg, &patched_crtc_timing);

	/* Load horizontal timing */

	/* CRTC_H_TOTAL = vesa.h_total - 1 */
	REG_SET(OTG_H_TOTAL, 0,
			OTG_H_TOTAL,  patched_crtc_timing.h_total - 1);

	/* h_sync_start = 0, h_sync_end = vesa.h_sync_width */
	REG_UPDATE_2(OTG_H_SYNC_A,
			OTG_H_SYNC_A_START, 0,
			OTG_H_SYNC_A_END, patched_crtc_timing.h_sync_width);

	/* asic_h_blank_end = HsyncWidth + HbackPorch =
	 * vesa. usHorizontalTotal - vesa. usHorizontalSyncStart -
	 * vesa.h_left_border
	 */
	vesa_sync_start = patched_crtc_timing.h_addressable +
			patched_crtc_timing.h_border_right +
			patched_crtc_timing.h_front_porch;

	asic_blank_end = patched_crtc_timing.h_total -
			vesa_sync_start -
			patched_crtc_timing.h_border_left;

	/* h_blank_start = v_blank_end + v_active */
	asic_blank_start = asic_blank_end +
			patched_crtc_timing.h_border_left +
			patched_crtc_timing.h_addressable +
			patched_crtc_timing.h_border_right;

	REG_UPDATE_2(OTG_H_BLANK_START_END,
			OTG_H_BLANK_START, asic_blank_start,
			OTG_H_BLANK_END, asic_blank_end);

	/* h_sync polarity */
	h_sync_polarity = patched_crtc_timing.flags.HSYNC_POSITIVE_POLARITY ?
			0 : 1;

	REG_UPDATE(OTG_H_SYNC_A_CNTL,
			OTG_H_SYNC_A_POL, h_sync_polarity);

	/* Load vertical timing */

	/* CRTC_V_TOTAL = v_total - 1 */
	if (patched_crtc_timing.flags.INTERLACE) {
		interlace_factor = 2;
		v_total = 2 * patched_crtc_timing.v_total;
	} else {
		interlace_factor = 1;
		v_total = patched_crtc_timing.v_total - 1;
	}
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
	v_sync_end = patched_crtc_timing.v_sync_width * interlace_factor;

	REG_UPDATE_2(OTG_V_SYNC_A,
			OTG_V_SYNC_A_START, 0,
			OTG_V_SYNC_A_END, v_sync_end);

	vesa_sync_start = patched_crtc_timing.v_addressable +
			patched_crtc_timing.v_border_bottom +
			patched_crtc_timing.v_front_porch;

	asic_blank_end = (patched_crtc_timing.v_total -
			vesa_sync_start -
			patched_crtc_timing.v_border_top)
			* interlace_factor;

	/* v_blank_start = v_blank_end + v_active */
	asic_blank_start = asic_blank_end +
			(patched_crtc_timing.v_border_top +
			patched_crtc_timing.v_addressable +
			patched_crtc_timing.v_border_bottom)
			* interlace_factor;

	REG_UPDATE_2(OTG_V_BLANK_START_END,
			OTG_V_BLANK_START, asic_blank_start,
			OTG_V_BLANK_END, asic_blank_end);

	/* Use OTG_VERTICAL_INTERRUPT2 replace VUPDATE interrupt,
	 * program the reg for interrupt postition.
	 */
	vertical_line_start = asic_blank_end - tg->dlg_otg_param.vstartup_start + 1;
	if (vertical_line_start < 0) {
		ASSERT(0);
		vertical_line_start = 0;
	}
	REG_SET(OTG_VERTICAL_INTERRUPT2_POSITION, 0,
			OTG_VERTICAL_INTERRUPT2_LINE_START, vertical_line_start);

	/* v_sync polarity */
	v_sync_polarity = patched_crtc_timing.flags.VSYNC_POSITIVE_POLARITY ?
			0 : 1;

	REG_UPDATE(OTG_V_SYNC_A_CNTL,
			OTG_V_SYNC_A_POL, v_sync_polarity);

	v_init = asic_blank_start;
	if (tg->dlg_otg_param.signal == SIGNAL_TYPE_DISPLAY_PORT ||
		tg->dlg_otg_param.signal == SIGNAL_TYPE_DISPLAY_PORT_MST ||
		tg->dlg_otg_param.signal == SIGNAL_TYPE_EDP) {
		start_point = 1;
		if (patched_crtc_timing.flags.INTERLACE == 1)
			field_num = 1;
	}
	v_fp2 = 0;
	if (tg->dlg_otg_param.vstartup_start > asic_blank_end)
		v_fp2 = tg->dlg_otg_param.vstartup_start > asic_blank_end;

	/* Interlace */
	if (patched_crtc_timing.flags.INTERLACE == 1) {
		REG_UPDATE(OTG_INTERLACE_CONTROL,
				OTG_INTERLACE_ENABLE, 1);
		v_init = v_init / 2;
		if ((tg->dlg_otg_param.vstartup_start/2)*2 > asic_blank_end)
			v_fp2 = v_fp2 / 2;
	}
	else
		REG_UPDATE(OTG_INTERLACE_CONTROL,
				OTG_INTERLACE_ENABLE, 0);


	/* VTG enable set to 0 first VInit */
	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 0);

	REG_UPDATE_2(CONTROL,
			VTG0_FP2, v_fp2,
			VTG0_VCOUNT_INIT, v_init);

	/* original code is using VTG offset to address OTG reg, seems wrong */
	REG_UPDATE_2(OTG_CONTROL,
			OTG_START_POINT_CNTL, start_point,
			OTG_FIELD_NUMBER_CNTL, field_num);

	tgn10_program_global_sync(tg);

	/* TODO
	 * patched_crtc_timing.flags.HORZ_COUNT_BY_TWO == 1
	 * program_horz_count_by_2
	 * for DVI 30bpp mode, 0 otherwise
	 * program_horz_count_by_2(tg, &patched_crtc_timing);
	 */

	/* Enable stereo - only when we need to pack 3D frame. Other types
	 * of stereo handled in explicit call
	 */
	h_div_2 = (dc_crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR420) ?
			1 : 0;

	REG_UPDATE(OTG_H_TIMING_CNTL,
			OTG_H_TIMING_DIV_BY2, h_div_2);

}

/**
 * unblank_crtc
 * Call ASIC Control Object to UnBlank CRTC.
 */
static void tgn10_unblank_crtc(struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);
	uint32_t vertical_interrupt_enable = 0;

	REG_GET(OTG_VERTICAL_INTERRUPT2_CONTROL,
			OTG_VERTICAL_INTERRUPT2_INT_ENABLE, &vertical_interrupt_enable);

	/* temporary work around for vertical interrupt, once vertical interrupt enabled,
	 * this check will be removed.
	 */
	if (vertical_interrupt_enable)
		REG_UPDATE(OTG_DOUBLE_BUFFER_CONTROL,
				OTG_BLANK_DATA_DOUBLE_BUFFER_EN, 1);

	REG_UPDATE_2(OTG_BLANK_CONTROL,
			OTG_BLANK_DATA_EN, 0,
			OTG_BLANK_DE_MODE, 0);
}

/**
 * blank_crtc
 * Call ASIC Control Object to Blank CRTC.
 */

static void tgn10_blank_crtc(struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	REG_UPDATE_2(OTG_BLANK_CONTROL,
			OTG_BLANK_DATA_EN, 1,
			OTG_BLANK_DE_MODE, 0);

	/* todo: why are we waiting for BLANK_DATA_EN?  shouldn't we be waiting
	 * for status?
	 */
	REG_WAIT(OTG_BLANK_CONTROL,
			OTG_BLANK_DATA_EN, 1,
			1, 100000);

	REG_UPDATE(OTG_DOUBLE_BUFFER_CONTROL,
			OTG_BLANK_DATA_DOUBLE_BUFFER_EN, 0);
}

static void tgn10_set_blank(struct timing_generator *tg,
		bool enable_blanking)
{
	if (enable_blanking)
		tgn10_blank_crtc(tg);
	else
		tgn10_unblank_crtc(tg);
}

static bool tgn10_is_blanked(struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);
	uint32_t blank_en;
	uint32_t blank_state;

	REG_GET_2(OTG_BLANK_CONTROL,
			OTG_BLANK_DATA_EN, &blank_en,
			OTG_CURRENT_BLANK_STATE, &blank_state);

	return blank_en && blank_state;
}

static void tgn10_enable_optc_clock(struct timing_generator *tg, bool enable)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

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

		if (tg->ctx->dce_environment != DCE_ENV_FPGA_MAXIMUS)
			REG_WAIT(OTG_CLOCK_CONTROL,
					OTG_CLOCK_ON, 0,
					1, 1000);

		REG_UPDATE_2(OPTC_INPUT_CLOCK_CONTROL,
				OPTC_INPUT_CLK_GATE_DIS, 0,
				OPTC_INPUT_CLK_EN, 0);

		if (tg->ctx->dce_environment != DCE_ENV_FPGA_MAXIMUS)
			REG_WAIT(OPTC_INPUT_CLOCK_CONTROL,
					OPTC_INPUT_CLK_ON, 0,
					1, 1000);
	}
}

/**
 * Enable CRTC
 * Enable CRTC - call ASIC Control Object to enable Timing generator.
 */
static bool tgn10_enable_crtc(struct timing_generator *tg)
{
	/* TODO FPGA wait for answer
	 * OTG_MASTER_UPDATE_MODE != CRTC_MASTER_UPDATE_MODE
	 * OTG_MASTER_UPDATE_LOCK != CRTC_MASTER_UPDATE_LOCK
	 */
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	/* opp instance for OTG. For DCN1.0, ODM is remoed.
	 * OPP and OPTC should 1:1 mapping
	 */
	REG_UPDATE(OPTC_DATA_SOURCE_SELECT,
			OPTC_SRC_SEL, tg->inst);

	/* VTG enable first is for HW workaround */
	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 1);

	/* Enable CRTC */
	REG_UPDATE_2(OTG_CONTROL,
			OTG_DISABLE_POINT_CNTL, 3,
			OTG_MASTER_EN, 1);

	return true;
}

/* disable_crtc - call ASIC Control Object to disable Timing generator. */
static bool tgn10_disable_crtc(struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

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


static void tgn10_program_blank_color(
		struct timing_generator *tg,
		const struct tg_color *black_color)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	REG_SET_3(OTG_BLACK_COLOR, 0,
			OTG_BLACK_COLOR_B_CB, black_color->color_b_cb,
			OTG_BLACK_COLOR_G_Y, black_color->color_g_y,
			OTG_BLACK_COLOR_R_CR, black_color->color_r_cr);
}

static bool tgn10_validate_timing(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing)
{
	uint32_t interlace_factor;
	uint32_t v_blank;
	uint32_t h_blank;
	uint32_t min_v_blank;
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	ASSERT(timing != NULL);

	interlace_factor = timing->flags.INTERLACE ? 2 : 1;
	v_blank = (timing->v_total - timing->v_addressable -
					timing->v_border_top - timing->v_border_bottom) *
					interlace_factor;

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

	if (timing->timing_3d_format != TIMING_3D_FORMAT_NONE &&
		tg->ctx->dc->debug.disable_stereo_support)
		return false;
	/* Temporarily blocking interlacing mode until it's supported */
	if (timing->flags.INTERLACE == 1)
		return false;

	/* Check maximum number of pixels supported by Timing Generator
	 * (Currently will never fail, in order to fail needs display which
	 * needs more than 8192 horizontal and
	 * more than 8192 vertical total pixels)
	 */
	if (timing->h_total > tgn10->max_h_total ||
		timing->v_total > tgn10->max_v_total)
		return false;


	if (h_blank < tgn10->min_h_blank)
		return false;

	if (timing->h_sync_width  < tgn10->min_h_sync_width ||
		 timing->v_sync_width  < tgn10->min_v_sync_width)
		return false;

	min_v_blank = timing->flags.INTERLACE?tgn10->min_v_blank_interlace:tgn10->min_v_blank;

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
 * struct timing_generator *tg - [in] timing generator which controls the
 * desired CRTC
 *
 * @return
 * Counter of frames, which should equal to number of vblanks.
 */
static uint32_t tgn10_get_vblank_counter(struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);
	uint32_t frame_count;

	REG_GET(OTG_STATUS_FRAME_COUNT,
		OTG_FRAME_COUNT, &frame_count);

	return frame_count;
}

static void tgn10_lock(struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	REG_SET(OTG_GLOBAL_CONTROL0, 0,
			OTG_MASTER_UPDATE_LOCK_SEL, tg->inst);
	REG_SET(OTG_MASTER_UPDATE_LOCK, 0,
			OTG_MASTER_UPDATE_LOCK, 1);

	if (tg->ctx->dce_environment != DCE_ENV_FPGA_MAXIMUS)
		REG_WAIT(OTG_MASTER_UPDATE_LOCK,
				UPDATE_LOCK_STATUS, 1,
				1, 100);
}

static void tgn10_unlock(struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	REG_SET(OTG_MASTER_UPDATE_LOCK, 0,
			OTG_MASTER_UPDATE_LOCK, 0);

	/* why are we waiting here? */
	REG_WAIT(OTG_DOUBLE_BUFFER_CONTROL,
			OTG_UPDATE_PENDING, 0,
			1, 100000);
}

static void tgn10_get_position(struct timing_generator *tg,
		struct crtc_position *position)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	REG_GET_2(OTG_STATUS_POSITION,
			OTG_HORZ_COUNT, &position->horizontal_count,
			OTG_VERT_COUNT, &position->vertical_count);

	REG_GET(OTG_NOM_VERT_POSITION,
			OTG_VERT_COUNT_NOM, &position->nominal_vcount);
}

static bool tgn10_is_counter_moving(struct timing_generator *tg)
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

static bool tgn10_did_triggered_reset_occur(
	struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);
	uint32_t occurred;

	REG_GET(OTG_FORCE_COUNT_NOW_CNTL,
		OTG_FORCE_COUNT_NOW_OCCURRED, &occurred);

	return occurred != 0;
}

static void tgn10_enable_reset_trigger(struct timing_generator *tg, int source_tg_inst)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);
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

static void tgn10_disable_reset_trigger(struct timing_generator *tg)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	REG_WRITE(OTG_TRIGA_CNTL, 0);

	REG_SET(OTG_FORCE_COUNT_NOW_CNTL, 0,
			OTG_FORCE_COUNT_NOW_CLEAR, 1);
}

static void tgn10_wait_for_state(struct timing_generator *tg,
		enum crtc_state state)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

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

static void tgn10_set_early_control(
	struct timing_generator *tg,
	uint32_t early_cntl)
{
	/* asic design change, do not need this control
	 * empty for share caller logic
	 */
}


static void tgn10_set_static_screen_control(
	struct timing_generator *tg,
	uint32_t value)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	/* Bit 8 is no longer applicable in RV for PSR case,
	 * set bit 8 to 0 if given
	 */
	if ((value & STATIC_SCREEN_EVENT_MASK_RANGETIMING_DOUBLE_BUFFER_UPDATE_EN)
			!= 0)
		value = value &
		~STATIC_SCREEN_EVENT_MASK_RANGETIMING_DOUBLE_BUFFER_UPDATE_EN;

	REG_SET_2(OTG_STATIC_SCREEN_CONTROL, 0,
			OTG_STATIC_SCREEN_EVENT_MASK, value,
			OTG_STATIC_SCREEN_FRAME_COUNT, 2);
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
static void tgn10_set_drr(
	struct timing_generator *tg,
	const struct drr_params *params)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	if (params != NULL &&
		params->vertical_total_max > 0 &&
		params->vertical_total_min > 0) {

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
	} else {
		REG_SET(OTG_V_TOTAL_MIN, 0,
			OTG_V_TOTAL_MIN, 0);

		REG_SET(OTG_V_TOTAL_MAX, 0,
			OTG_V_TOTAL_MAX, 0);

		REG_UPDATE_4(OTG_V_TOTAL_CONTROL,
				OTG_SET_V_TOTAL_MIN_MASK, 0,
				OTG_V_TOTAL_MIN_SEL, 0,
				OTG_V_TOTAL_MAX_SEL, 0,
				OTG_FORCE_LOCK_ON_EVENT, 0);
	}
}

static void tgn10_set_test_pattern(
	struct timing_generator *tg,
	/* TODO: replace 'controller_dp_test_pattern' by 'test_pattern_mode'
	 * because this is not DP-specific (which is probably somewhere in DP
	 * encoder) */
	enum controller_dp_test_pattern test_pattern,
	enum dc_color_depth color_depth)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);
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

static void tgn10_get_crtc_scanoutpos(
	struct timing_generator *tg,
	uint32_t *v_blank_start,
	uint32_t *v_blank_end,
	uint32_t *h_position,
	uint32_t *v_position)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);
	struct crtc_position position;

	REG_GET_2(OTG_V_BLANK_START_END,
			OTG_V_BLANK_START, v_blank_start,
			OTG_V_BLANK_END, v_blank_end);

	tgn10_get_position(tg, &position);

	*h_position = position.horizontal_count;
	*v_position = position.vertical_count;
}



static void tgn10_enable_stereo(struct timing_generator *tg,
	const struct dc_crtc_timing *timing, struct crtc_stereo_flags *flags)
{
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	uint32_t active_width = timing->h_addressable;
	uint32_t space1_size = timing->v_total - timing->v_addressable;

	if (flags) {
		uint32_t stereo_en;
		stereo_en = flags->FRAME_PACKED == 0 ? 1 : 0;

		if (flags->PROGRAM_STEREO)
			REG_UPDATE_3(OTG_STEREO_CONTROL,
				OTG_STEREO_EN, stereo_en,
				OTG_STEREO_SYNC_OUTPUT_LINE_NUM, 0,
				OTG_STEREO_SYNC_OUTPUT_POLARITY, 0);

		if (flags->PROGRAM_POLARITY)
			REG_UPDATE(OTG_STEREO_CONTROL,
				OTG_STEREO_EYE_FLAG_POLARITY,
				flags->RIGHT_EYE_POLARITY == 0 ? 0 : 1);

		if (flags->DISABLE_STEREO_DP_SYNC)
			REG_UPDATE(OTG_STEREO_CONTROL,
				OTG_DISABLE_STEREOSYNC_OUTPUT_FOR_DP, 1);

		if (flags->PROGRAM_STEREO)
			REG_UPDATE_3(OTG_3D_STRUCTURE_CONTROL,
				OTG_3D_STRUCTURE_EN, flags->FRAME_PACKED,
				OTG_3D_STRUCTURE_V_UPDATE_MODE, flags->FRAME_PACKED,
				OTG_3D_STRUCTURE_STEREO_SEL_OVR, flags->FRAME_PACKED);

	}

	REG_UPDATE(OPPBUF_CONTROL,
		OPPBUF_ACTIVE_WIDTH, active_width);

	REG_UPDATE(OPPBUF_3D_PARAMETERS_0,
		OPPBUF_3D_VACT_SPACE1_SIZE, space1_size);
}

static void tgn10_program_stereo(struct timing_generator *tg,
	const struct dc_crtc_timing *timing, struct crtc_stereo_flags *flags)
{
	if (flags->PROGRAM_STEREO)
		tgn10_enable_stereo(tg, timing, flags);
	else
		tgn10_disable_stereo(tg);
}


static bool tgn10_is_stereo_left_eye(struct timing_generator *tg)
{
	bool ret = false;
	uint32_t left_eye = 0;
	struct dcn10_timing_generator *tgn10 = DCN10TG_FROM_TG(tg);

	REG_GET(OTG_STEREO_STATUS,
		OTG_STEREO_CURRENT_EYE, &left_eye);
	if (left_eye == 1)
		ret = true;
	else
		ret = false;

	return ret;
}

void tgn10_read_otg_state(struct dcn10_timing_generator *tgn10,
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
}


static const struct timing_generator_funcs dcn10_tg_funcs = {
		.validate_timing = tgn10_validate_timing,
		.program_timing = tgn10_program_timing,
		.program_global_sync = tgn10_program_global_sync,
		.enable_crtc = tgn10_enable_crtc,
		.disable_crtc = tgn10_disable_crtc,
		/* used by enable_timing_synchronization. Not need for FPGA */
		.is_counter_moving = tgn10_is_counter_moving,
		.get_position = tgn10_get_position,
		.get_frame_count = tgn10_get_vblank_counter,
		.get_scanoutpos = tgn10_get_crtc_scanoutpos,
		.set_early_control = tgn10_set_early_control,
		/* used by enable_timing_synchronization. Not need for FPGA */
		.wait_for_state = tgn10_wait_for_state,
		.set_blank = tgn10_set_blank,
		.is_blanked = tgn10_is_blanked,
		.set_blank_color = tgn10_program_blank_color,
		.did_triggered_reset_occur = tgn10_did_triggered_reset_occur,
		.enable_reset_trigger = tgn10_enable_reset_trigger,
		.disable_reset_trigger = tgn10_disable_reset_trigger,
		.lock = tgn10_lock,
		.unlock = tgn10_unlock,
		.enable_optc_clock = tgn10_enable_optc_clock,
		.set_drr = tgn10_set_drr,
		.set_static_screen_control = tgn10_set_static_screen_control,
		.set_test_pattern = tgn10_set_test_pattern,
		.program_stereo = tgn10_program_stereo,
		.is_stereo_left_eye = tgn10_is_stereo_left_eye
};

void dcn10_timing_generator_init(struct dcn10_timing_generator *tgn10)
{
	tgn10->base.funcs = &dcn10_tg_funcs;

	tgn10->max_h_total = tgn10->tg_mask->OTG_H_TOTAL + 1;
	tgn10->max_v_total = tgn10->tg_mask->OTG_V_TOTAL + 1;

	tgn10->min_h_blank = 32;
	tgn10->min_v_blank = 3;
	tgn10->min_v_blank_interlace = 5;
	tgn10->min_h_sync_width = 8;
	tgn10->min_v_sync_width = 1;
}
