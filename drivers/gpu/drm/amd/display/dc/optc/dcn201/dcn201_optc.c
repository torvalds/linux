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
#include "dcn201_optc.h"
#include "dcn10/dcn10_optc.h"
#include "dc.h"

#define REG(reg)\
	optc1->tg_regs->reg

#define CTX \
	optc1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	optc1->tg_shift->field_name, optc1->tg_mask->field_name

static void optc201_triplebuffer_lock(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_GLOBAL_CONTROL0, 0,
		OTG_MASTER_UPDATE_LOCK_SEL, optc->inst);
	REG_SET(OTG_VUPDATE_KEEPOUT, 0,
		OTG_MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_EN, 1);
	REG_SET(OTG_MASTER_UPDATE_LOCK, 0,
		OTG_MASTER_UPDATE_LOCK, 1);

	REG_WAIT(OTG_MASTER_UPDATE_LOCK,
			UPDATE_LOCK_STATUS, 1,
			1, 10);
}

static void optc201_triplebuffer_unlock(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_MASTER_UPDATE_LOCK, 0,
		OTG_MASTER_UPDATE_LOCK, 0);
	REG_SET(OTG_VUPDATE_KEEPOUT, 0,
		OTG_MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_EN, 0);

}

static bool optc201_validate_timing(
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

static void optc201_get_optc_source(struct timing_generator *optc,
		uint32_t *num_of_src_opp,
		uint32_t *src_opp_id_0,
		uint32_t *src_opp_id_1)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_GET(OPTC_DATA_SOURCE_SELECT,
			OPTC_SEG0_SRC_SEL, src_opp_id_0);

	*num_of_src_opp = 1;
}

static const struct timing_generator_funcs dcn201_tg_funcs = {
		.validate_timing = optc201_validate_timing,
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
		.set_blank = optc1_set_blank,
		.is_blanked = optc1_is_blanked,
		.set_blank_color = optc1_program_blank_color,
		.did_triggered_reset_occur = optc1_did_triggered_reset_occur,
		.enable_reset_trigger = optc1_enable_reset_trigger,
		.enable_crtc_reset = optc1_enable_crtc_reset,
		.disable_reset_trigger = optc1_disable_reset_trigger,
		.triplebuffer_lock = optc201_triplebuffer_lock,
		.triplebuffer_unlock = optc201_triplebuffer_unlock,
		.lock = optc1_lock,
		.unlock = optc1_unlock,
		.enable_optc_clock = optc1_enable_optc_clock,
		.set_drr = optc1_set_drr,
		.get_last_used_drr_vtotal = NULL,
		.set_vtotal_min_max = optc1_set_vtotal_min_max,
		.set_static_screen_control = optc1_set_static_screen_control,
		.program_stereo = optc1_program_stereo,
		.is_stereo_left_eye = optc1_is_stereo_left_eye,
		.set_blank_data_double_buffer = optc1_set_blank_data_double_buffer,
		.tg_init = optc1_tg_init,
		.is_tg_enabled = optc1_is_tg_enabled,
		.is_optc_underflow_occurred = optc1_is_optc_underflow_occurred,
		.clear_optc_underflow = optc1_clear_optc_underflow,
		.get_crc = optc1_get_crc,
		.configure_crc = optc2_configure_crc,
		.set_dsc_config = optc2_set_dsc_config,
		.set_dwb_source = NULL,
		.get_optc_source = optc201_get_optc_source,
		.set_vtg_params = optc1_set_vtg_params,
		.program_manual_trigger = optc2_program_manual_trigger,
		.setup_manual_trigger = optc2_setup_manual_trigger,
		.get_hw_timing = optc1_get_hw_timing,
		.is_two_pixels_per_container = optc1_is_two_pixels_per_container,
		.read_otg_state = optc1_read_otg_state,
};

void dcn201_timing_generator_init(struct optc *optc1)
{
	optc1->base.funcs = &dcn201_tg_funcs;

	optc1->max_h_total = optc1->tg_mask->OTG_H_TOTAL + 1;
	optc1->max_v_total = optc1->tg_mask->OTG_V_TOTAL + 1;

	optc1->min_h_blank = 32;
	optc1->min_v_blank = 3;
	optc1->min_v_blank_interlace = 5;
	optc1->min_h_sync_width = 8;
	optc1->min_v_sync_width = 1;
}
