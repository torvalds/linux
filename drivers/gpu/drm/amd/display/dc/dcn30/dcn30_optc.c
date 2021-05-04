/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "dcn30_optc.h"
#include "dc.h"
#include "dcn_calc_math.h"

#define REG(reg)\
	optc1->tg_regs->reg

#define CTX \
	optc1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	optc1->tg_shift->field_name, optc1->tg_mask->field_name

void optc3_triplebuffer_lock(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE(OTG_GLOBAL_CONTROL2,
		OTG_MASTER_UPDATE_LOCK_SEL, optc->inst);

	REG_SET(OTG_VUPDATE_KEEPOUT, 0,
		OTG_MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_EN, 1);

	REG_SET(OTG_MASTER_UPDATE_LOCK, 0,
		OTG_MASTER_UPDATE_LOCK, 1);

	if (optc->ctx->dce_environment != DCE_ENV_FPGA_MAXIMUS)
		REG_WAIT(OTG_MASTER_UPDATE_LOCK,
				UPDATE_LOCK_STATUS, 1,
				1, 10);
}

void optc3_lock_doublebuffer_enable(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t v_blank_start = 0;
	uint32_t v_blank_end = 0;
	uint32_t h_blank_start = 0;
	uint32_t h_blank_end = 0;

	REG_GET_2(OTG_V_BLANK_START_END,
		OTG_V_BLANK_START, &v_blank_start,
		OTG_V_BLANK_END, &v_blank_end);
	REG_GET_2(OTG_H_BLANK_START_END,
		OTG_H_BLANK_START, &h_blank_start,
		OTG_H_BLANK_END, &h_blank_end);

	REG_UPDATE_2(OTG_GLOBAL_CONTROL1,
		MASTER_UPDATE_LOCK_DB_START_Y, v_blank_start,
		MASTER_UPDATE_LOCK_DB_END_Y, v_blank_end);
	REG_UPDATE_2(OTG_GLOBAL_CONTROL4,
		DIG_UPDATE_POSITION_X, 20,
		DIG_UPDATE_POSITION_Y, v_blank_start);
	REG_UPDATE_3(OTG_GLOBAL_CONTROL0,
		MASTER_UPDATE_LOCK_DB_START_X, h_blank_start - 200 - 1,
		MASTER_UPDATE_LOCK_DB_END_X, h_blank_end,
		MASTER_UPDATE_LOCK_DB_EN, 1);
	REG_UPDATE(OTG_GLOBAL_CONTROL2, GLOBAL_UPDATE_LOCK_EN, 1);
}

void optc3_lock_doublebuffer_disable(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE_2(OTG_GLOBAL_CONTROL0,
		MASTER_UPDATE_LOCK_DB_START_X, 0,
		MASTER_UPDATE_LOCK_DB_END_X, 0);
	REG_UPDATE_2(OTG_GLOBAL_CONTROL1,
		MASTER_UPDATE_LOCK_DB_START_Y, 0,
		MASTER_UPDATE_LOCK_DB_END_Y, 0);

	REG_UPDATE(OTG_GLOBAL_CONTROL2, GLOBAL_UPDATE_LOCK_EN, 0);
	REG_UPDATE(OTG_GLOBAL_CONTROL0, MASTER_UPDATE_LOCK_DB_EN, 0);
}

void optc3_lock(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE(OTG_GLOBAL_CONTROL2,
		OTG_MASTER_UPDATE_LOCK_SEL, optc->inst);
	REG_SET(OTG_MASTER_UPDATE_LOCK, 0,
		OTG_MASTER_UPDATE_LOCK, 1);

	/* Should be fast, status does not update on maximus */
	if (optc->ctx->dce_environment != DCE_ENV_FPGA_MAXIMUS)
		REG_WAIT(OTG_MASTER_UPDATE_LOCK,
				UPDATE_LOCK_STATUS, 1,
				1, 10);
}

void optc3_set_out_mux(struct timing_generator *optc, enum otg_out_mux_dest dest)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE(OTG_CONTROL, OTG_OUT_MUX, dest);
}

void optc3_program_blank_color(struct timing_generator *optc,
		const struct tg_color *blank_color)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET_3(OTG_BLANK_DATA_COLOR, 0,
		OTG_BLANK_DATA_COLOR_BLUE_CB, blank_color->color_b_cb,
		OTG_BLANK_DATA_COLOR_GREEN_Y, blank_color->color_g_y,
		OTG_BLANK_DATA_COLOR_RED_CR, blank_color->color_r_cr);

	REG_SET_3(OTG_BLANK_DATA_COLOR_EXT, 0,
		OTG_BLANK_DATA_COLOR_BLUE_CB_EXT, blank_color->color_b_cb >> 10,
		OTG_BLANK_DATA_COLOR_GREEN_Y_EXT, blank_color->color_g_y >> 10,
		OTG_BLANK_DATA_COLOR_RED_CR_EXT, blank_color->color_r_cr >> 10);
}

void optc3_set_drr_trigger_window(struct timing_generator *optc,
		uint32_t window_start, uint32_t window_end)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET_2(OTG_DRR_TRIGGER_WINDOW, 0,
		OTG_DRR_TRIGGER_WINDOW_START_X, window_start,
		OTG_DRR_TRIGGER_WINDOW_END_X, window_end);
}

void optc3_set_vtotal_change_limit(struct timing_generator *optc,
		uint32_t limit)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);


	REG_SET(OTG_DRR_V_TOTAL_CHANGE, 0,
		OTG_DRR_V_TOTAL_CHANGE_LIMIT, limit);
}


/* Set DSC-related configuration.
 *   dsc_mode: 0 disables DSC, other values enable DSC in specified format
 *   sc_bytes_per_pixel: Bytes per pixel in u3.28 format
 *   dsc_slice_width: Slice width in pixels
 */
void optc3_set_dsc_config(struct timing_generator *optc,
		enum optc_dsc_mode dsc_mode,
		uint32_t dsc_bytes_per_pixel,
		uint32_t dsc_slice_width)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	optc2_set_dsc_config(optc, dsc_mode, dsc_bytes_per_pixel,
		dsc_slice_width);

		REG_UPDATE(OTG_V_SYNC_A_CNTL, OTG_V_SYNC_MODE, 0);

}

void optc3_set_odm_bypass(struct timing_generator *optc,
		const struct dc_crtc_timing *dc_crtc_timing)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	enum h_timing_div_mode h_div = H_TIMING_NO_DIV;

	REG_SET_5(OPTC_DATA_SOURCE_SELECT, 0,
			OPTC_NUM_OF_INPUT_SEGMENT, 0,
			OPTC_SEG0_SRC_SEL, optc->inst,
			OPTC_SEG1_SRC_SEL, 0xf,
			OPTC_SEG2_SRC_SEL, 0xf,
			OPTC_SEG3_SRC_SEL, 0xf
			);

	h_div = optc1_is_two_pixels_per_containter(dc_crtc_timing);
	REG_SET(OTG_H_TIMING_CNTL, 0,
			OTG_H_TIMING_DIV_MODE, h_div);

	REG_SET(OPTC_MEMORY_CONFIG, 0,
			OPTC_MEM_SEL, 0);
	optc1->opp_count = 1;
}

static void optc3_set_odm_combine(struct timing_generator *optc, int *opp_id, int opp_cnt,
		struct dc_crtc_timing *timing)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	int mpcc_hactive = (timing->h_addressable + timing->h_border_left + timing->h_border_right)
			/ opp_cnt;
	uint32_t memory_mask = 0;

	/* TODO: In pseudocode but does not affect maximus, delete comment if we dont need on asic
	 * REG_SET(OTG_GLOBAL_CONTROL2, 0, GLOBAL_UPDATE_LOCK_EN, 1);
	 * Program OTG register MASTER_UPDATE_LOCK_DB_X/Y to the position before DP frame start
	 * REG_SET_2(OTG_GLOBAL_CONTROL1, 0,
	 *		MASTER_UPDATE_LOCK_DB_X, 160,
	 *		MASTER_UPDATE_LOCK_DB_Y, 240);
	 */

	ASSERT(opp_cnt == 2 || opp_cnt == 4);

	/* 2 pieces of memory required for up to 5120 displays, 4 for up to 8192,
	 * however, for ODM combine we can simplify by always using 4.
	 */
	if (opp_cnt == 2) {
		/* To make sure there's no memory overlap, each instance "reserves" 2
		 * memories and they are uniquely combined here.
		 */
		memory_mask = 0x3 << (opp_id[0] * 2) | 0x3 << (opp_id[1] * 2);
	} else if (opp_cnt == 4) {
		/* To make sure there's no memory overlap, each instance "reserves" 1
		 * memory and they are uniquely combined here.
		 */
		memory_mask = 0x1 << (opp_id[0] * 2) | 0x1 << (opp_id[1] * 2) | 0x1 << (opp_id[2] * 2) | 0x1 << (opp_id[3] * 2);
	}

	if (REG(OPTC_MEMORY_CONFIG))
		REG_SET(OPTC_MEMORY_CONFIG, 0,
			OPTC_MEM_SEL, memory_mask);

	if (opp_cnt == 2) {
		REG_SET_3(OPTC_DATA_SOURCE_SELECT, 0,
				OPTC_NUM_OF_INPUT_SEGMENT, 1,
				OPTC_SEG0_SRC_SEL, opp_id[0],
				OPTC_SEG1_SRC_SEL, opp_id[1]);
	} else if (opp_cnt == 4) {
		REG_SET_5(OPTC_DATA_SOURCE_SELECT, 0,
				OPTC_NUM_OF_INPUT_SEGMENT, 3,
				OPTC_SEG0_SRC_SEL, opp_id[0],
				OPTC_SEG1_SRC_SEL, opp_id[1],
				OPTC_SEG2_SRC_SEL, opp_id[2],
				OPTC_SEG3_SRC_SEL, opp_id[3]);
	}

	REG_UPDATE(OPTC_WIDTH_CONTROL,
			OPTC_SEGMENT_WIDTH, mpcc_hactive);

	REG_SET(OTG_H_TIMING_CNTL, 0, OTG_H_TIMING_DIV_MODE, opp_cnt - 1);
	optc1->opp_count = opp_cnt;
}

/**
 * optc3_set_timing_double_buffer() - DRR double buffering control
 *
 * Sets double buffer point for V_TOTAL, H_TOTAL, VTOTAL_MIN,
 * VTOTAL_MAX, VTOTAL_MIN_SEL and VTOTAL_MAX_SEL registers.
 *
 * Options: any time,  start of frame, dp start of frame (range timing)
 */
static void optc3_set_timing_double_buffer(struct timing_generator *optc, bool enable)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t mode = enable ? 2 : 0;

	REG_UPDATE(OTG_DOUBLE_BUFFER_CONTROL,
		   OTG_DRR_TIMING_DBUF_UPDATE_MODE, mode);
}

void optc3_tg_init(struct timing_generator *optc)
{
	optc3_set_timing_double_buffer(optc, true);
	optc1_clear_optc_underflow(optc);
}

static struct timing_generator_funcs dcn30_tg_funcs = {
		.validate_timing = optc1_validate_timing,
		.program_timing = optc1_program_timing,
		.setup_vertical_interrupt0 = optc1_setup_vertical_interrupt0,
		.setup_vertical_interrupt1 = optc1_setup_vertical_interrupt1,
		.setup_vertical_interrupt2 = optc1_setup_vertical_interrupt2,
		.program_global_sync = optc1_program_global_sync,
		.enable_crtc = optc2_enable_crtc,
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
		.set_blank_color = optc3_program_blank_color,
		.did_triggered_reset_occur = optc1_did_triggered_reset_occur,
		.triplebuffer_lock = optc3_triplebuffer_lock,
		.triplebuffer_unlock = optc2_triplebuffer_unlock,
		.enable_reset_trigger = optc1_enable_reset_trigger,
		.enable_crtc_reset = optc1_enable_crtc_reset,
		.disable_reset_trigger = optc1_disable_reset_trigger,
		.lock = optc3_lock,
		.is_locked = optc1_is_locked,
		.unlock = optc1_unlock,
		.lock_doublebuffer_enable = optc3_lock_doublebuffer_enable,
		.lock_doublebuffer_disable = optc3_lock_doublebuffer_disable,
		.enable_optc_clock = optc1_enable_optc_clock,
		.set_drr = optc1_set_drr,
		.get_last_used_drr_vtotal = optc2_get_last_used_drr_vtotal,
		.set_static_screen_control = optc1_set_static_screen_control,
		.program_stereo = optc1_program_stereo,
		.is_stereo_left_eye = optc1_is_stereo_left_eye,
		.tg_init = optc3_tg_init,
		.is_tg_enabled = optc1_is_tg_enabled,
		.is_optc_underflow_occurred = optc1_is_optc_underflow_occurred,
		.clear_optc_underflow = optc1_clear_optc_underflow,
		.setup_global_swap_lock = NULL,
		.get_crc = optc1_get_crc,
		.configure_crc = optc2_configure_crc,
		.set_dsc_config = optc3_set_dsc_config,
		.set_dwb_source = NULL,
		.set_odm_bypass = optc3_set_odm_bypass,
		.set_odm_combine = optc3_set_odm_combine,
		.get_optc_source = optc2_get_optc_source,
		.set_out_mux = optc3_set_out_mux,
		.set_drr_trigger_window = optc3_set_drr_trigger_window,
		.set_vtotal_change_limit = optc3_set_vtotal_change_limit,
		.set_gsl = optc2_set_gsl,
		.set_gsl_source_select = optc2_set_gsl_source_select,
		.set_vtg_params = optc1_set_vtg_params,
		.program_manual_trigger = optc2_program_manual_trigger,
		.setup_manual_trigger = optc2_setup_manual_trigger,
		.get_hw_timing = optc1_get_hw_timing,
};

void dcn30_timing_generator_init(struct optc *optc1)
{
	optc1->base.funcs = &dcn30_tg_funcs;

	optc1->max_h_total = optc1->tg_mask->OTG_H_TOTAL + 1;
	optc1->max_v_total = optc1->tg_mask->OTG_V_TOTAL + 1;

	optc1->min_h_blank = 32;
	optc1->min_v_blank = 3;
	optc1->min_v_blank_interlace = 5;
	optc1->min_h_sync_width = 4;
	optc1->min_v_sync_width = 1;
}

