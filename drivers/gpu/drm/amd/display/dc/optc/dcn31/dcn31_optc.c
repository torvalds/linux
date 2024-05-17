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

#include "dcn31_optc.h"

#include "dcn30/dcn30_optc.h"
#include "reg_helper.h"
#include "dc.h"
#include "dcn_calc_math.h"

#define REG(reg)\
	optc1->tg_regs->reg

#define CTX \
	optc1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	optc1->tg_shift->field_name, optc1->tg_mask->field_name

static void optc31_set_odm_combine(struct timing_generator *optc, int *opp_id, int opp_cnt,
		int segment_width, int last_segment_width)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t memory_mask = 0;
	int mem_count_per_opp = (segment_width + 2559) / 2560;

	/* Assume less than 6 pipes */
	if (opp_cnt == 4) {
		if (mem_count_per_opp == 1)
			memory_mask = 0xf;
		else {
			ASSERT(mem_count_per_opp == 2);
			memory_mask = 0xff;
		}
	} else if (mem_count_per_opp == 1)
		memory_mask = 0x1 << (opp_id[0] * 2) | 0x1 << (opp_id[1] * 2);
	else if (mem_count_per_opp == 2)
		memory_mask = 0x3 << (opp_id[0] * 2) | 0x3 << (opp_id[1] * 2);
	else if (mem_count_per_opp == 3)
		memory_mask = 0x77;
	else if (mem_count_per_opp == 4)
		memory_mask = 0xff;

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
			OPTC_SEGMENT_WIDTH, segment_width);

	REG_SET(OTG_H_TIMING_CNTL, 0, OTG_H_TIMING_DIV_MODE, opp_cnt - 1);
	optc1->opp_count = opp_cnt;
}

/*
 * Enable CRTC - call ASIC Control Object to enable Timing generator.
 */
static bool optc31_enable_crtc(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	/* opp instance for OTG, 1 to 1 mapping and odm will adjust */
	REG_UPDATE(OPTC_DATA_SOURCE_SELECT,
			OPTC_SEG0_SRC_SEL, optc->inst);

	/* VTG enable first is for HW workaround */
	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 1);

	REG_SEQ_START();

	/* Enable CRTC */
	REG_UPDATE_2(OTG_CONTROL,
			OTG_DISABLE_POINT_CNTL, 2,
			OTG_MASTER_EN, 1);

	REG_SEQ_SUBMIT();
	REG_SEQ_WAIT_DONE();

	return true;
}

/* disable_crtc - call ASIC Control Object to disable Timing generator. */
static bool optc31_disable_crtc(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE_5(OPTC_DATA_SOURCE_SELECT,
			OPTC_SEG0_SRC_SEL, 0xf,
			OPTC_SEG1_SRC_SEL, 0xf,
			OPTC_SEG2_SRC_SEL, 0xf,
			OPTC_SEG3_SRC_SEL, 0xf,
			OPTC_NUM_OF_INPUT_SEGMENT, 0);

	REG_UPDATE(OPTC_MEMORY_CONFIG,
			OPTC_MEM_SEL, 0);

	/* disable otg request until end of the first line
	 * in the vertical blank region
	 */
	REG_UPDATE(OTG_CONTROL,
			OTG_MASTER_EN, 0);

	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 0);

	/* CRTC disabled, so disable  clock. */
	REG_WAIT(OTG_CLOCK_CONTROL,
			OTG_BUSY, 0,
			1, 100000);
	optc1_clear_optc_underflow(optc);

	return true;
}

bool optc31_immediate_disable_crtc(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE_2(OTG_CONTROL,
			OTG_DISABLE_POINT_CNTL, 0,
			OTG_MASTER_EN, 0);

	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 0);

	/* CRTC disabled, so disable  clock. */
	REG_WAIT(OTG_CLOCK_CONTROL,
			OTG_BUSY, 0,
			1, 100000);

	/* clear the false state */
	optc1_clear_optc_underflow(optc);

	return true;
}

void optc31_set_drr(
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

		optc->funcs->set_vtotal_min_max(optc, params->vertical_total_min - 1, params->vertical_total_max - 1);

		/*
		 * MIN_MASK_EN is gone and MASK is now always enabled.
		 *
		 * To get it to it work with manual trigger we need to make sure
		 * we program the correct bit.
		 */
		REG_UPDATE_4(OTG_V_TOTAL_CONTROL,
				OTG_V_TOTAL_MIN_SEL, 1,
				OTG_V_TOTAL_MAX_SEL, 1,
				OTG_FORCE_LOCK_ON_EVENT, 0,
				OTG_SET_V_TOTAL_MIN_MASK, (1 << 1)); /* TRIGA */

		// Setup manual flow control for EOF via TRIG_A
		optc->funcs->setup_manual_trigger(optc);
	} else {
		REG_UPDATE_4(OTG_V_TOTAL_CONTROL,
				OTG_SET_V_TOTAL_MIN_MASK, 0,
				OTG_V_TOTAL_MIN_SEL, 0,
				OTG_V_TOTAL_MAX_SEL, 0,
				OTG_FORCE_LOCK_ON_EVENT, 0);

		optc->funcs->set_vtotal_min_max(optc, 0, 0);
	}
}

void optc3_init_odm(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET_5(OPTC_DATA_SOURCE_SELECT, 0,
			OPTC_NUM_OF_INPUT_SEGMENT, 0,
			OPTC_SEG0_SRC_SEL, optc->inst,
			OPTC_SEG1_SRC_SEL, 0xf,
			OPTC_SEG2_SRC_SEL, 0xf,
			OPTC_SEG3_SRC_SEL, 0xf
			);

	REG_SET(OTG_H_TIMING_CNTL, 0,
			OTG_H_TIMING_DIV_MODE, 0);

	REG_SET(OPTC_MEMORY_CONFIG, 0,
			OPTC_MEM_SEL, 0);
	optc1->opp_count = 1;
}

static struct timing_generator_funcs dcn31_tg_funcs = {
		.validate_timing = optc1_validate_timing,
		.program_timing = optc1_program_timing,
		.setup_vertical_interrupt0 = optc1_setup_vertical_interrupt0,
		.setup_vertical_interrupt1 = optc1_setup_vertical_interrupt1,
		.setup_vertical_interrupt2 = optc1_setup_vertical_interrupt2,
		.program_global_sync = optc1_program_global_sync,
		.enable_crtc = optc31_enable_crtc,
		.disable_crtc = optc31_disable_crtc,
		.immediate_disable_crtc = optc31_immediate_disable_crtc,
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
		.unlock = optc1_unlock,
		.lock_doublebuffer_enable = optc3_lock_doublebuffer_enable,
		.lock_doublebuffer_disable = optc3_lock_doublebuffer_disable,
		.enable_optc_clock = optc1_enable_optc_clock,
		.set_drr = optc31_set_drr,
		.get_last_used_drr_vtotal = optc2_get_last_used_drr_vtotal,
		.set_vtotal_min_max = optc1_set_vtotal_min_max,
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
		.get_dsc_status = optc2_get_dsc_status,
		.set_dwb_source = NULL,
		.set_odm_bypass = optc3_set_odm_bypass,
		.set_odm_combine = optc31_set_odm_combine,
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
		.init_odm = optc3_init_odm,
		.is_two_pixels_per_container = optc1_is_two_pixels_per_container,
};

void dcn31_timing_generator_init(struct optc *optc1)
{
	optc1->base.funcs = &dcn31_tg_funcs;

	optc1->max_h_total = optc1->tg_mask->OTG_H_TOTAL + 1;
	optc1->max_v_total = optc1->tg_mask->OTG_V_TOTAL + 1;

	optc1->min_h_blank = 32;
	optc1->min_v_blank = 3;
	optc1->min_v_blank_interlace = 5;
	optc1->min_h_sync_width = 4;
	optc1->min_v_sync_width = 1;
}

