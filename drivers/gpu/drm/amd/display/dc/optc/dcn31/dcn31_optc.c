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
/*
 * Immediate_Disable_Crtc - this is to temp disable Timing generator without reset ODM.
 */
bool optc31_immediate_disable_crtc(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE_2(OTG_CONTROL,
			OTG_DISABLE_POINT_CNTL, 0,
			OTG_MASTER_EN, 0);

	REG_UPDATE(CONTROL,
			VTG0_ENABLE, 0);

	/* CRTC disabled, so disable  clock. */
	if (optc->ctx->dce_environment != DCE_ENV_DIAG)
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

void optc31_read_otg_state(struct timing_generator *optc,
		struct dcn_otg_state *s)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

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

	REG_GET(OTG_VERTICAL_INTERRUPT1_CONTROL,
			OTG_VERTICAL_INTERRUPT1_INT_ENABLE, &s->vertical_interrupt1_en);

	REG_GET(OTG_VERTICAL_INTERRUPT1_POSITION,
				OTG_VERTICAL_INTERRUPT1_LINE_START, &s->vertical_interrupt1_line);

	REG_GET(OTG_VERTICAL_INTERRUPT2_CONTROL,
			OTG_VERTICAL_INTERRUPT2_INT_ENABLE, &s->vertical_interrupt2_en);

	REG_GET(OTG_VERTICAL_INTERRUPT2_POSITION,
			OTG_VERTICAL_INTERRUPT2_LINE_START, &s->vertical_interrupt2_line);

	REG_GET(INTERRUPT_DEST,
			OTG0_IHC_OTG_VERTICAL_INTERRUPT2_DEST, &s->vertical_interrupt2_dest);

	s->otg_master_update_lock = REG_READ(OTG_MASTER_UPDATE_LOCK);
	s->otg_double_buffer_control = REG_READ(OTG_DOUBLE_BUFFER_CONTROL);
}

void optc31_read_reg_state(struct timing_generator *optc, struct dcn_optc_reg_state *optc_reg_state)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	optc_reg_state->optc_bytes_per_pixel = REG_READ(OPTC_BYTES_PER_PIXEL);
	optc_reg_state->optc_data_format_control = REG_READ(OPTC_DATA_FORMAT_CONTROL);
	optc_reg_state->optc_data_source_select = REG_READ(OPTC_DATA_SOURCE_SELECT);
	optc_reg_state->optc_input_clock_control = REG_READ(OPTC_INPUT_CLOCK_CONTROL);
	optc_reg_state->optc_input_global_control = REG_READ(OPTC_INPUT_GLOBAL_CONTROL);
	optc_reg_state->optc_input_spare_register = REG_READ(OPTC_INPUT_SPARE_REGISTER);
	optc_reg_state->optc_memory_config = REG_READ(OPTC_MEMORY_CONFIG);
	optc_reg_state->optc_rsmu_underflow = REG_READ(OPTC_RSMU_UNDERFLOW);
	optc_reg_state->optc_underflow_threshold = REG_READ(OPTC_UNDERFLOW_THRESHOLD);
	optc_reg_state->optc_width_control = REG_READ(OPTC_WIDTH_CONTROL);
	optc_reg_state->otg_3d_structure_control = REG_READ(OTG_3D_STRUCTURE_CONTROL);
	optc_reg_state->otg_clock_control = REG_READ(OTG_CLOCK_CONTROL);
	optc_reg_state->otg_control = REG_READ(OTG_CONTROL);
	optc_reg_state->otg_count_control = REG_READ(OTG_COUNT_CONTROL);
	optc_reg_state->otg_count_reset = REG_READ(OTG_COUNT_RESET);
	optc_reg_state->otg_crc_cntl = REG_READ(OTG_CRC_CNTL);
	optc_reg_state->otg_crc_sig_blue_control_mask = REG_READ(OTG_CRC_SIG_BLUE_CONTROL_MASK);
	optc_reg_state->otg_crc_sig_red_green_mask = REG_READ(OTG_CRC_SIG_RED_GREEN_MASK);
	optc_reg_state->otg_crc0_data_b = REG_READ(OTG_CRC0_DATA_B);
	optc_reg_state->otg_crc0_data_rg = REG_READ(OTG_CRC0_DATA_RG);
	optc_reg_state->otg_crc0_windowa_x_control = REG_READ(OTG_CRC0_WINDOWA_X_CONTROL);
	optc_reg_state->otg_crc0_windowa_x_control_readback = REG_READ(OTG_CRC0_WINDOWA_X_CONTROL_READBACK);
	optc_reg_state->otg_crc0_windowa_y_control = REG_READ(OTG_CRC0_WINDOWA_Y_CONTROL);
	optc_reg_state->otg_crc0_windowa_y_control_readback = REG_READ(OTG_CRC0_WINDOWA_Y_CONTROL_READBACK);
	optc_reg_state->otg_crc0_windowb_x_control = REG_READ(OTG_CRC0_WINDOWB_X_CONTROL);
	optc_reg_state->otg_crc0_windowb_x_control_readback = REG_READ(OTG_CRC0_WINDOWB_X_CONTROL_READBACK);
	optc_reg_state->otg_crc0_windowb_y_control = REG_READ(OTG_CRC0_WINDOWB_Y_CONTROL);
	optc_reg_state->otg_crc0_windowb_y_control_readback = REG_READ(OTG_CRC0_WINDOWB_Y_CONTROL_READBACK);
	optc_reg_state->otg_crc1_data_b = REG_READ(OTG_CRC1_DATA_B);
	optc_reg_state->otg_crc1_data_rg = REG_READ(OTG_CRC1_DATA_RG);
	optc_reg_state->otg_crc1_windowa_x_control = REG_READ(OTG_CRC1_WINDOWA_X_CONTROL);
	optc_reg_state->otg_crc1_windowa_x_control_readback = REG_READ(OTG_CRC1_WINDOWA_X_CONTROL_READBACK);
	optc_reg_state->otg_crc1_windowa_y_control = REG_READ(OTG_CRC1_WINDOWA_Y_CONTROL);
	optc_reg_state->otg_crc1_windowa_y_control_readback = REG_READ(OTG_CRC1_WINDOWA_Y_CONTROL_READBACK);
	optc_reg_state->otg_crc1_windowb_x_control = REG_READ(OTG_CRC1_WINDOWB_X_CONTROL);
	optc_reg_state->otg_crc1_windowb_x_control_readback = REG_READ(OTG_CRC1_WINDOWB_X_CONTROL_READBACK);
	optc_reg_state->otg_crc1_windowb_y_control = REG_READ(OTG_CRC1_WINDOWB_Y_CONTROL);
	optc_reg_state->otg_crc1_windowb_y_control_readback = REG_READ(OTG_CRC1_WINDOWB_Y_CONTROL_READBACK);
	optc_reg_state->otg_crc2_data_b = REG_READ(OTG_CRC2_DATA_B);
	optc_reg_state->otg_crc2_data_rg = REG_READ(OTG_CRC2_DATA_RG);
	optc_reg_state->otg_crc3_data_b = REG_READ(OTG_CRC3_DATA_B);
	optc_reg_state->otg_crc3_data_rg = REG_READ(OTG_CRC3_DATA_RG);
	optc_reg_state->otg_dlpc_control = REG_READ(OTG_DLPC_CONTROL);
	optc_reg_state->otg_double_buffer_control = REG_READ(OTG_DOUBLE_BUFFER_CONTROL);
	optc_reg_state->otg_drr_control2 = REG_READ(OTG_DRR_CONTROL2);
	optc_reg_state->otg_drr_control = REG_READ(OTG_DRR_CONTROL);
	optc_reg_state->otg_drr_timing_int_status = REG_READ(OTG_DRR_TIMING_INT_STATUS);
	optc_reg_state->otg_drr_trigger_window = REG_READ(OTG_DRR_TRIGGER_WINDOW);
	optc_reg_state->otg_drr_v_total_change = REG_READ(OTG_DRR_V_TOTAL_CHANGE);
	optc_reg_state->otg_dsc_start_position = REG_READ(OTG_DSC_START_POSITION);
	optc_reg_state->otg_force_count_now_cntl = REG_READ(OTG_FORCE_COUNT_NOW_CNTL);
	optc_reg_state->otg_global_control0 = REG_READ(OTG_GLOBAL_CONTROL0);
	optc_reg_state->otg_global_control1 = REG_READ(OTG_GLOBAL_CONTROL1);
	optc_reg_state->otg_global_control2 = REG_READ(OTG_GLOBAL_CONTROL2);
	optc_reg_state->otg_global_control3 = REG_READ(OTG_GLOBAL_CONTROL3);
	optc_reg_state->otg_global_control4 = REG_READ(OTG_GLOBAL_CONTROL4);
	optc_reg_state->otg_global_sync_status = REG_READ(OTG_GLOBAL_SYNC_STATUS);
	optc_reg_state->otg_gsl_control = REG_READ(OTG_GSL_CONTROL);
	optc_reg_state->otg_gsl_vsync_gap = REG_READ(OTG_GSL_VSYNC_GAP);
	optc_reg_state->otg_gsl_window_x = REG_READ(OTG_GSL_WINDOW_X);
	optc_reg_state->otg_gsl_window_y = REG_READ(OTG_GSL_WINDOW_Y);
	optc_reg_state->otg_h_blank_start_end = REG_READ(OTG_H_BLANK_START_END);
	optc_reg_state->otg_h_sync_a = REG_READ(OTG_H_SYNC_A);
	optc_reg_state->otg_h_sync_a_cntl = REG_READ(OTG_H_SYNC_A_CNTL);
	optc_reg_state->otg_h_timing_cntl = REG_READ(OTG_H_TIMING_CNTL);
	optc_reg_state->otg_h_total = REG_READ(OTG_H_TOTAL);
	optc_reg_state->otg_interlace_control = REG_READ(OTG_INTERLACE_CONTROL);
	optc_reg_state->otg_interlace_status = REG_READ(OTG_INTERLACE_STATUS);
	optc_reg_state->otg_interrupt_control = REG_READ(OTG_INTERRUPT_CONTROL);
	optc_reg_state->otg_long_vblank_status = REG_READ(OTG_LONG_VBLANK_STATUS);
	optc_reg_state->otg_m_const_dto0 = REG_READ(OTG_M_CONST_DTO0);
	optc_reg_state->otg_m_const_dto1 = REG_READ(OTG_M_CONST_DTO1);
	optc_reg_state->otg_manual_force_vsync_next_line = REG_READ(OTG_MANUAL_FORCE_VSYNC_NEXT_LINE);
	optc_reg_state->otg_master_en = REG_READ(OTG_MASTER_EN);
	optc_reg_state->otg_master_update_lock = REG_READ(OTG_MASTER_UPDATE_LOCK);
	optc_reg_state->otg_master_update_mode = REG_READ(OTG_MASTER_UPDATE_MODE);
	optc_reg_state->otg_nom_vert_position = REG_READ(OTG_NOM_VERT_POSITION);
	optc_reg_state->otg_pipe_update_status = REG_READ(OTG_PIPE_UPDATE_STATUS);
	optc_reg_state->otg_pixel_data_readback0 = REG_READ(OTG_PIXEL_DATA_READBACK0);
	optc_reg_state->otg_pixel_data_readback1 = REG_READ(OTG_PIXEL_DATA_READBACK1);
	optc_reg_state->otg_request_control = REG_READ(OTG_REQUEST_CONTROL);
	optc_reg_state->otg_snapshot_control = REG_READ(OTG_SNAPSHOT_CONTROL);
	optc_reg_state->otg_snapshot_frame = REG_READ(OTG_SNAPSHOT_FRAME);
	optc_reg_state->otg_snapshot_position = REG_READ(OTG_SNAPSHOT_POSITION);
	optc_reg_state->otg_snapshot_status = REG_READ(OTG_SNAPSHOT_STATUS);
	optc_reg_state->otg_spare_register = REG_READ(OTG_SPARE_REGISTER);
	optc_reg_state->otg_static_screen_control = REG_READ(OTG_STATIC_SCREEN_CONTROL);
	optc_reg_state->otg_status = REG_READ(OTG_STATUS);
	optc_reg_state->otg_status_frame_count = REG_READ(OTG_STATUS_FRAME_COUNT);
	optc_reg_state->otg_status_hv_count = REG_READ(OTG_STATUS_HV_COUNT);
	optc_reg_state->otg_status_position = REG_READ(OTG_STATUS_POSITION);
	optc_reg_state->otg_status_vf_count = REG_READ(OTG_STATUS_VF_COUNT);
	optc_reg_state->otg_stereo_control = REG_READ(OTG_STEREO_CONTROL);
	optc_reg_state->otg_stereo_force_next_eye = REG_READ(OTG_STEREO_FORCE_NEXT_EYE);
	optc_reg_state->otg_stereo_status = REG_READ(OTG_STEREO_STATUS);
	optc_reg_state->otg_trig_manual_control = REG_READ(OTG_TRIG_MANUAL_CONTROL);
	optc_reg_state->otg_triga_cntl = REG_READ(OTG_TRIGA_CNTL);
	optc_reg_state->otg_triga_manual_trig = REG_READ(OTG_TRIGA_MANUAL_TRIG);
	optc_reg_state->otg_trigb_cntl = REG_READ(OTG_TRIGB_CNTL);
	optc_reg_state->otg_trigb_manual_trig = REG_READ(OTG_TRIGB_MANUAL_TRIG);
	optc_reg_state->otg_update_lock = REG_READ(OTG_UPDATE_LOCK);
	optc_reg_state->otg_v_blank_start_end = REG_READ(OTG_V_BLANK_START_END);
	optc_reg_state->otg_v_count_stop_control = REG_READ(OTG_V_COUNT_STOP_CONTROL);
	optc_reg_state->otg_v_count_stop_control2 = REG_READ(OTG_V_COUNT_STOP_CONTROL2);
	optc_reg_state->otg_v_sync_a = REG_READ(OTG_V_SYNC_A);
	optc_reg_state->otg_v_sync_a_cntl = REG_READ(OTG_V_SYNC_A_CNTL);
	optc_reg_state->otg_v_total = REG_READ(OTG_V_TOTAL);
	optc_reg_state->otg_v_total_control = REG_READ(OTG_V_TOTAL_CONTROL);
	optc_reg_state->otg_v_total_int_status = REG_READ(OTG_V_TOTAL_INT_STATUS);
	optc_reg_state->otg_v_total_max = REG_READ(OTG_V_TOTAL_MAX);
	optc_reg_state->otg_v_total_mid = REG_READ(OTG_V_TOTAL_MID);
	optc_reg_state->otg_v_total_min = REG_READ(OTG_V_TOTAL_MIN);
	optc_reg_state->otg_vert_sync_control = REG_READ(OTG_VERT_SYNC_CONTROL);
	optc_reg_state->otg_vertical_interrupt0_control = REG_READ(OTG_VERTICAL_INTERRUPT0_CONTROL);
	optc_reg_state->otg_vertical_interrupt0_position = REG_READ(OTG_VERTICAL_INTERRUPT0_POSITION);
	optc_reg_state->otg_vertical_interrupt1_control = REG_READ(OTG_VERTICAL_INTERRUPT1_CONTROL);
	optc_reg_state->otg_vertical_interrupt1_position = REG_READ(OTG_VERTICAL_INTERRUPT1_POSITION);
	optc_reg_state->otg_vertical_interrupt2_control = REG_READ(OTG_VERTICAL_INTERRUPT2_CONTROL);
	optc_reg_state->otg_vertical_interrupt2_position = REG_READ(OTG_VERTICAL_INTERRUPT2_POSITION);
	optc_reg_state->otg_vready_param = REG_READ(OTG_VREADY_PARAM);
	optc_reg_state->otg_vstartup_param = REG_READ(OTG_VSTARTUP_PARAM);
	optc_reg_state->otg_vsync_nom_int_status = REG_READ(OTG_VSYNC_NOM_INT_STATUS);
	optc_reg_state->otg_vupdate_keepout = REG_READ(OTG_VUPDATE_KEEPOUT);
	optc_reg_state->otg_vupdate_param = REG_READ(OTG_VUPDATE_PARAM);
}

static const struct timing_generator_funcs dcn31_tg_funcs = {
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
		.read_otg_state = optc31_read_otg_state,
		.optc_read_reg_state = optc31_read_reg_state,
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

