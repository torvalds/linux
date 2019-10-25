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
#include "dcn20_optc.h"
#include "dc.h"

#define REG(reg)\
	optc1->tg_regs->reg

#define CTX \
	optc1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	optc1->tg_shift->field_name, optc1->tg_mask->field_name

/**
 * Enable CRTC
 * Enable CRTC - call ASIC Control Object to enable Timing generator.
 */
bool optc2_enable_crtc(struct timing_generator *optc)
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
			OPTC_SEG0_SRC_SEL, optc->inst);

	/* VTG enable first is for HW workaround */
	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 1);

	/* Enable CRTC */
	REG_UPDATE_2(OTG_CONTROL,
			OTG_DISABLE_POINT_CNTL, 3,
			OTG_MASTER_EN, 1);

	return true;
}

/**
 * DRR double buffering control to select buffer point
 * for V_TOTAL, H_TOTAL, VTOTAL_MIN, VTOTAL_MAX, VTOTAL_MIN_SEL and VTOTAL_MAX_SEL registers
 * Options: anytime, start of frame, dp start of frame (range timing)
 */
void optc2_set_timing_db_mode(struct timing_generator *optc, bool enable)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	uint32_t blank_data_double_buffer_enable = enable ? 1 : 0;

	REG_UPDATE(OTG_DOUBLE_BUFFER_CONTROL,
		OTG_RANGE_TIMING_DBUF_UPDATE_MODE, blank_data_double_buffer_enable);
}

/**
 *For the below, I'm not sure how your GSL parameters are stored in your env,
 * so I will assume a gsl_params struct for now
 */
void optc2_set_gsl(struct timing_generator *optc,
		   const struct gsl_params *params)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

/**
 * There are (MAX_OPTC+1)/2 gsl groups available for use.
 * In each group (assign an OTG to a group by setting OTG_GSLX_EN = 1,
 * set one of the OTGs to be the master (OTG_GSL_MASTER_EN = 1) and the rest are slaves.
 */
	REG_UPDATE_5(OTG_GSL_CONTROL,
		OTG_GSL0_EN, params->gsl0_en,
		OTG_GSL1_EN, params->gsl1_en,
		OTG_GSL2_EN, params->gsl2_en,
		OTG_GSL_MASTER_EN, params->gsl_master_en,
		OTG_GSL_MASTER_MODE, params->gsl_master_mode);
}


/* Use the gsl allow flip as the master update lock */
void optc2_use_gsl_as_master_update_lock(struct timing_generator *optc,
		   const struct gsl_params *params)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE(OTG_GSL_CONTROL,
		OTG_MASTER_UPDATE_LOCK_GSL_EN, params->master_update_lock_gsl_en);
}

/* You can control the GSL timing by limiting GSL to a window (X,Y) */
void optc2_set_gsl_window(struct timing_generator *optc,
		   const struct gsl_params *params)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET_2(OTG_GSL_WINDOW_X, 0,
		OTG_GSL_WINDOW_START_X, params->gsl_window_start_x,
		OTG_GSL_WINDOW_END_X, params->gsl_window_end_x);
	REG_SET_2(OTG_GSL_WINDOW_Y, 0,
		OTG_GSL_WINDOW_START_Y, params->gsl_window_start_y,
		OTG_GSL_WINDOW_END_Y, params->gsl_window_end_y);
}

/**
 * Vupdate keepout can be set to a window to block the update lock for that pipe from changing.
 * Start offset begins with vstartup and goes for x number of clocks,
 * end offset starts from end of vupdate to x number of clocks.
 */
void optc2_set_vupdate_keepout(struct timing_generator *optc,
		   const struct vupdate_keepout_params *params)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET_3(OTG_VUPDATE_KEEPOUT, 0,
		MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_START_OFFSET, params->start_offset,
		MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_END_OFFSET, params->end_offset,
		OTG_MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_EN, params->enable);
}

void optc2_set_gsl_source_select(
		struct timing_generator *optc,
		int group_idx,
		uint32_t gsl_ready_signal)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	switch (group_idx) {
	case 1:
		REG_UPDATE(GSL_SOURCE_SELECT, GSL0_READY_SOURCE_SEL, gsl_ready_signal);
		break;
	case 2:
		REG_UPDATE(GSL_SOURCE_SELECT, GSL1_READY_SOURCE_SEL, gsl_ready_signal);
		break;
	case 3:
		REG_UPDATE(GSL_SOURCE_SELECT, GSL2_READY_SOURCE_SEL, gsl_ready_signal);
		break;
	default:
		break;
	}
}

#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
/* DSC encoder frame start controls: x = h position, line_num = # of lines from vstartup */
void optc2_set_dsc_encoder_frame_start(struct timing_generator *optc,
					int x_position,
					int line_num)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET_2(OTG_DSC_START_POSITION, 0,
			OTG_DSC_START_POSITION_X, x_position,
			OTG_DSC_START_POSITION_LINE_NUM, line_num);
}

/* Set DSC-related configuration.
 *   dsc_mode: 0 disables DSC, other values enable DSC in specified format
 *   sc_bytes_per_pixel: Bytes per pixel in u3.28 format
 *   dsc_slice_width: Slice width in pixels
 */
void optc2_set_dsc_config(struct timing_generator *optc,
					enum optc_dsc_mode dsc_mode,
					uint32_t dsc_bytes_per_pixel,
					uint32_t dsc_slice_width)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE(OPTC_DATA_FORMAT_CONTROL,
		OPTC_DSC_MODE, dsc_mode);

	REG_SET(OPTC_BYTES_PER_PIXEL, 0,
		OPTC_DSC_BYTES_PER_PIXEL, dsc_bytes_per_pixel);

	REG_UPDATE(OPTC_WIDTH_CONTROL,
		OPTC_DSC_SLICE_WIDTH, dsc_slice_width);
}
#endif

/**
 * PTI i think is already done somewhere else for 2ka
 * (opp?, please double check.
 * OPTC side only has 1 register to set for PTI_ENABLE)
 */

void optc2_set_odm_bypass(struct timing_generator *optc,
		const struct dc_crtc_timing *dc_crtc_timing)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t h_div_2 = 0;

	REG_SET_3(OPTC_DATA_SOURCE_SELECT, 0,
			OPTC_NUM_OF_INPUT_SEGMENT, 0,
			OPTC_SEG0_SRC_SEL, optc->inst,
			OPTC_SEG1_SRC_SEL, 0xf);
	REG_WRITE(OTG_H_TIMING_CNTL, 0);

	h_div_2 = optc1_is_two_pixels_per_containter(dc_crtc_timing);
	REG_UPDATE(OTG_H_TIMING_CNTL,
			OTG_H_TIMING_DIV_BY2, h_div_2);
	REG_SET(OPTC_MEMORY_CONFIG, 0,
			OPTC_MEM_SEL, 0);
	optc1->opp_count = 1;
}

void optc2_set_odm_combine(struct timing_generator *optc, int *opp_id, int opp_cnt,
		struct dc_crtc_timing *timing)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	/* 2 pieces of memory required for up to 5120 displays, 4 for up to 8192 */
	int mpcc_hactive = (timing->h_addressable + timing->h_border_left + timing->h_border_right)
			/ opp_cnt;
	int memory_mask = mpcc_hactive <= 2560 ? 0x3 : 0xf;
	uint32_t data_fmt = 0;

	/* TODO: In pseudocode but does not affect maximus, delete comment if we dont need on asic
	 * REG_SET(OTG_GLOBAL_CONTROL2, 0, GLOBAL_UPDATE_LOCK_EN, 1);
	 * Program OTG register MASTER_UPDATE_LOCK_DB_X/Y to the position before DP frame start
	 * REG_SET_2(OTG_GLOBAL_CONTROL1, 0,
	 *		MASTER_UPDATE_LOCK_DB_X, 160,
	 *		MASTER_UPDATE_LOCK_DB_Y, 240);
	 */
	if (REG(OPTC_MEMORY_CONFIG))
		REG_SET(OPTC_MEMORY_CONFIG, 0,
			OPTC_MEM_SEL, memory_mask << (optc->inst * 4));

	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422)
		data_fmt = 1;
	else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		data_fmt = 2;

	REG_UPDATE(OPTC_DATA_FORMAT_CONTROL, OPTC_DATA_FORMAT, data_fmt);

	ASSERT(opp_cnt == 2);
	REG_SET_3(OPTC_DATA_SOURCE_SELECT, 0,
			OPTC_NUM_OF_INPUT_SEGMENT, 1,
			OPTC_SEG0_SRC_SEL, opp_id[0],
			OPTC_SEG1_SRC_SEL, opp_id[1]);

	REG_UPDATE(OPTC_WIDTH_CONTROL,
			OPTC_SEGMENT_WIDTH, mpcc_hactive);

	REG_SET(OTG_H_TIMING_CNTL, 0, OTG_H_TIMING_DIV_BY2, 1);
	optc1->opp_count = opp_cnt;
}

void optc2_get_optc_source(struct timing_generator *optc,
		uint32_t *num_of_src_opp,
		uint32_t *src_opp_id_0,
		uint32_t *src_opp_id_1)
{
	uint32_t num_of_input_segments;
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_GET_3(OPTC_DATA_SOURCE_SELECT,
			OPTC_NUM_OF_INPUT_SEGMENT, &num_of_input_segments,
			OPTC_SEG0_SRC_SEL, src_opp_id_0,
			OPTC_SEG1_SRC_SEL, src_opp_id_1);

	if (num_of_input_segments == 1)
		*num_of_src_opp = 2;
	else
		*num_of_src_opp = 1;

	/* Work around VBIOS not updating OPTC_NUM_OF_INPUT_SEGMENT */
	if (*src_opp_id_1 == 0xf)
		*num_of_src_opp = 1;
}

void optc2_set_dwb_source(struct timing_generator *optc,
		uint32_t dwb_pipe_inst)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	if (dwb_pipe_inst == 0)
		REG_UPDATE(DWB_SOURCE_SELECT,
				OPTC_DWB0_SOURCE_SELECT, optc->inst);
	else if (dwb_pipe_inst == 1)
		REG_UPDATE(DWB_SOURCE_SELECT,
				OPTC_DWB1_SOURCE_SELECT, optc->inst);
}

void optc2_triplebuffer_lock(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_GLOBAL_CONTROL0, 0,
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

void optc2_triplebuffer_unlock(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_MASTER_UPDATE_LOCK, 0,
		OTG_MASTER_UPDATE_LOCK, 0);

	REG_SET(OTG_VUPDATE_KEEPOUT, 0,
		OTG_MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_EN, 0);

}

void optc2_lock_doublebuffer_enable(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t v_blank_start = 0;
	uint32_t h_blank_start = 0;

	REG_UPDATE(OTG_GLOBAL_CONTROL1, MASTER_UPDATE_LOCK_DB_EN, 1);

	REG_UPDATE_2(OTG_GLOBAL_CONTROL2, GLOBAL_UPDATE_LOCK_EN, 1,
			DIG_UPDATE_LOCATION, 20);

	REG_GET(OTG_V_BLANK_START_END, OTG_V_BLANK_START, &v_blank_start);

	REG_GET(OTG_H_BLANK_START_END, OTG_H_BLANK_START, &h_blank_start);

	REG_UPDATE_2(OTG_GLOBAL_CONTROL1,
			MASTER_UPDATE_LOCK_DB_X,
			h_blank_start - 200 - 1,
			MASTER_UPDATE_LOCK_DB_Y,
			v_blank_start - 1);
}

void optc2_lock_doublebuffer_disable(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE_2(OTG_GLOBAL_CONTROL1,
				MASTER_UPDATE_LOCK_DB_X,
				0,
				MASTER_UPDATE_LOCK_DB_Y,
				0);

	REG_UPDATE_2(OTG_GLOBAL_CONTROL2, GLOBAL_UPDATE_LOCK_EN, 0,
				DIG_UPDATE_LOCATION, 0);

	REG_UPDATE(OTG_GLOBAL_CONTROL1, MASTER_UPDATE_LOCK_DB_EN, 0);
}

void optc2_setup_manual_trigger(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_MANUAL_FLOW_CONTROL, 0,
			MANUAL_FLOW_CONTROL, 1);

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

void optc2_program_manual_trigger(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_SET(OTG_TRIGA_MANUAL_TRIG, 0,
			OTG_TRIGA_MANUAL_TRIG, 1);
}

static struct timing_generator_funcs dcn20_tg_funcs = {
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
		.set_blank = optc1_set_blank,
		.is_blanked = optc1_is_blanked,
		.set_blank_color = optc1_program_blank_color,
		.enable_reset_trigger = optc1_enable_reset_trigger,
		.enable_crtc_reset = optc1_enable_crtc_reset,
		.did_triggered_reset_occur = optc1_did_triggered_reset_occur,
		.triplebuffer_lock = optc2_triplebuffer_lock,
		.triplebuffer_unlock = optc2_triplebuffer_unlock,
		.disable_reset_trigger = optc1_disable_reset_trigger,
		.lock = optc1_lock,
		.unlock = optc1_unlock,
		.lock_doublebuffer_enable = optc2_lock_doublebuffer_enable,
		.lock_doublebuffer_disable = optc2_lock_doublebuffer_disable,
		.enable_optc_clock = optc1_enable_optc_clock,
		.set_drr = optc1_set_drr,
		.set_static_screen_control = optc1_set_static_screen_control,
		.program_stereo = optc1_program_stereo,
		.is_stereo_left_eye = optc1_is_stereo_left_eye,
		.set_blank_data_double_buffer = optc1_set_blank_data_double_buffer,
		.tg_init = optc1_tg_init,
		.is_tg_enabled = optc1_is_tg_enabled,
		.is_optc_underflow_occurred = optc1_is_optc_underflow_occurred,
		.clear_optc_underflow = optc1_clear_optc_underflow,
		.setup_global_swap_lock = NULL,
		.get_crc = optc1_get_crc,
		.configure_crc = optc1_configure_crc,
#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
		.set_dsc_config = optc2_set_dsc_config,
#endif
		.set_dwb_source = optc2_set_dwb_source,
		.set_odm_bypass = optc2_set_odm_bypass,
		.set_odm_combine = optc2_set_odm_combine,
		.get_optc_source = optc2_get_optc_source,
		.set_gsl = optc2_set_gsl,
		.set_gsl_source_select = optc2_set_gsl_source_select,
		.set_vtg_params = optc1_set_vtg_params,
		.program_manual_trigger = optc2_program_manual_trigger,
		.setup_manual_trigger = optc2_setup_manual_trigger,
		.is_matching_timing = optc1_is_matching_timing
};

void dcn20_timing_generator_init(struct optc *optc1)
{
	optc1->base.funcs = &dcn20_tg_funcs;

	optc1->max_h_total = optc1->tg_mask->OTG_H_TOTAL + 1;
	optc1->max_v_total = optc1->tg_mask->OTG_V_TOTAL + 1;

	optc1->min_h_blank = 32;
	optc1->min_v_blank = 3;
	optc1->min_v_blank_interlace = 5;
	optc1->min_h_sync_width = 4;//	Minimum HSYNC = 8 pixels asked By HW in the first place for no actual reason. Oculus Rift S will not light up with 8 as it's hsyncWidth is 6. Changing it to 4 to fix that issue.
	optc1->min_v_sync_width = 1;
}

