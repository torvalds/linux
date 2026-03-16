// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dcn42_optc.h"
#include "dcn30/dcn30_optc.h"
#include "dcn31/dcn31_optc.h"
#include "dcn32/dcn32_optc.h"
#include "dcn35/dcn35_optc.h"
#include "dcn401/dcn401_optc.h"
#include "reg_helper.h"
#include "dc.h"
#include "dcn_calc_math.h"
#include "dc_dmub_srv.h"
#include "dc_trace.h"

#define REG(reg)\
	optc1->tg_regs->reg

#define CTX \
	optc1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	optc1->tg_shift->field_name, optc1->tg_mask->field_name

/*
 * optc42_get_crc - Capture CRC result per component
 *
 * @optc: timing_generator instance.
 * @r_cr: 16-bit primary CRC signature for red data.
 * @g_y: 16-bit primary CRC signature for green data.
 * @b_cb: 16-bit primary CRC signature for blue data.
 *
 * This function reads the CRC signature from the OPTC registers. Notice that
 * we have three registers to keep the CRC result per color component (RGB).
 *
 * Returns:
 * If CRC is disabled, return false; otherwise, return true, and the CRC
 * results in the parameters.
 */

static bool optc42_get_crc(struct timing_generator *optc, uint8_t idx,
		   uint32_t *r_cr, uint32_t *g_y, uint32_t *b_cb)
{
	uint32_t field = 0;
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_GET(OTG_CRC_CNTL, OTG_CRC_EN, &field);

	/* Early return if CRC is not enabled for this CRTC */
	if (!field)
		return false;


	switch (idx) {
	case 0:
		/* OTG_CRC0_DATA_RG has the CRC16 results for the red component */
		REG_GET(OTG_CRC0_DATA_R,
			CRC0_R_CR, r_cr);

		/* OTG_CRC0_DATA_RG has the CRC16 results for the green component */
		REG_GET(OTG_CRC0_DATA_G,
			CRC0_G_Y, g_y);

		/* OTG_CRC0_DATA_B has the CRC16 results for the blue component */
		REG_GET(OTG_CRC0_DATA_B,
			CRC0_B_CB, b_cb);
		break;
	case 1:
		/* OTG_CRC1_DATA_RG has the CRC16 results for the red component */
		REG_GET(OTG_CRC1_DATA_R,
			CRC0_R_CR, r_cr);

		/* OTG_CRC1_DATA_RG has the CRC16 results for the green component */
		REG_GET(OTG_CRC1_DATA_G,
			CRC0_G_Y, g_y);

		/* OTG_CRC1_DATA_B has the CRC16 results for the blue component */
		REG_GET(OTG_CRC1_DATA_B,
			CRC0_B_CB, b_cb);
		break;
	default:
		return false;
	}
	return true;
}

void optc42_enable_pwa(struct timing_generator *optc, struct otc_pwa_frame_sync *pwa_sync_param)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	/*VCOUNT_MODE :
	00	OTG_PWA_FRAME_SYNC_VCOUNT_0	Vcount counting from VSYNC.
	01	OTG_PWA_FRAME_SYNC_VCOUNT_1	Vcount counting from VSTARTUP.*/

	if (pwa_sync_param == NULL)
		return;
	if (optc1->base.ctx->dc->debug.enable_otg_frame_sync_pwa) {
		/*take mode 1, use line number from vstartup to get output frame as earlier as possible*/
		REG_UPDATE_3(OTG_PWA_FRAME_SYNC_CONTROL,
			OTG_PWA_FRAME_SYNC_EN, 1,
			OTG_PWA_FRAME_SYNC_VCOUNT_MODE, pwa_sync_param->pwa_sync_mode,
			OTG_PWA_FRAME_SYNC_LINE, pwa_sync_param->pwa_frame_sync_line_offset);
	}
}
void  optc42_disable_pwa(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE(OTG_PWA_FRAME_SYNC_CONTROL,
			OTG_PWA_FRAME_SYNC_EN, 0);
}
void optc42_clear_optc_underflow(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);

	REG_UPDATE(OPTC_INPUT_GLOBAL_CONTROL, OPTC_UNDERFLOW_CLEAR, 1);
	REG_UPDATE(OPTC_RSMU_UNDERFLOW, OPTC_RSMU_UNDERFLOW_CLEAR, 1);
}
bool optc42_is_optc_underflow_occurred(struct timing_generator *optc)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t underflow_occurred = 0, rsmu_underflow_occurred = 0;

	REG_GET(OPTC_INPUT_GLOBAL_CONTROL,
			OPTC_UNDERFLOW_OCCURRED_STATUS,
			&underflow_occurred);

	REG_GET(OPTC_RSMU_UNDERFLOW,
			OPTC_RSMU_UNDERFLOW_OCCURRED_STATUS,
			&rsmu_underflow_occurred);
	return (underflow_occurred == 1 || rsmu_underflow_occurred);
}
/* disable_crtc */
bool optc42_disable_crtc(struct timing_generator *optc)
{
	optc401_disable_crtc(optc);
	optc42_clear_optc_underflow(optc);

	return true;
}
static void optc42_set_timing_double_buffer(struct timing_generator *optc, bool enable)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	uint32_t mode = enable ? 2 : 0;
	/* actually we have 4 modes now, use as the same as previous dcn3x
	 * 00	OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE_0	Double buffer update occurs at any time in a frame.
	 * 01	OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE_1	Double buffer update occurs at OTG start of frame.
	 * 02	OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE_2	Double buffer occurs DP start of frame.
	 * 03	OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE_3	Reserved.
	 */

	REG_UPDATE(OTG_DOUBLE_BUFFER_CONTROL,
		   OTG_DRR_TIMING_DBUF_UPDATE_MODE, mode);
}
void optc42_tg_init(struct timing_generator *optc)
{
	optc42_set_timing_double_buffer(optc, true);
	optc42_clear_optc_underflow(optc);
}

void optc42_lock_doublebuffer_enable(struct timing_generator *optc)
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
		MASTER_UPDATE_LOCK_DB_END_Y, v_blank_start);
	REG_UPDATE_2(OTG_GLOBAL_CONTROL4,
		DIG_UPDATE_POSITION_X, 20,
		DIG_UPDATE_POSITION_Y, v_blank_start);
	REG_UPDATE_3(OTG_GLOBAL_CONTROL0,
		MASTER_UPDATE_LOCK_DB_START_X, h_blank_start - 200 - 1,
		MASTER_UPDATE_LOCK_DB_END_X, h_blank_end,
		MASTER_UPDATE_LOCK_DB_EN, 1);
	REG_UPDATE(OTG_GLOBAL_CONTROL2, GLOBAL_UPDATE_LOCK_EN, 1);

	REG_SET_3(OTG_VUPDATE_KEEPOUT, 0,
		MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_START_OFFSET, 0,
		MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_END_OFFSET, 100,
		OTG_MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_EN, 1);

	TRACE_OPTC_LOCK_UNLOCK_STATE(optc1, optc->inst, true);
}

static struct timing_generator_funcs dcn42_tg_funcs = {
		.validate_timing = optc1_validate_timing,
		.program_timing = optc1_program_timing,
		.setup_vertical_interrupt0 = optc1_setup_vertical_interrupt0,
		.setup_vertical_interrupt1 = optc1_setup_vertical_interrupt1,
		.setup_vertical_interrupt2 = optc1_setup_vertical_interrupt2,
		.program_global_sync = optc401_program_global_sync,
		.enable_crtc = optc401_enable_crtc,
		.disable_crtc = optc42_disable_crtc,
		.phantom_crtc_post_enable = optc401_phantom_crtc_post_enable,
		.disable_phantom_crtc = optc401_disable_phantom_otg,
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
		.lock_doublebuffer_enable = optc42_lock_doublebuffer_enable,
		.lock_doublebuffer_disable = optc3_lock_doublebuffer_disable,
		.enable_optc_clock = optc1_enable_optc_clock,
		.set_drr = optc401_set_drr,
		.get_last_used_drr_vtotal = optc2_get_last_used_drr_vtotal,
		.set_vtotal_min_max = optc401_set_vtotal_min_max,
		.set_static_screen_control = optc1_set_static_screen_control,
		.program_stereo = optc1_program_stereo,
		.is_stereo_left_eye = optc1_is_stereo_left_eye,
		.tg_init = optc42_tg_init,
		.is_tg_enabled = optc1_is_tg_enabled,
		.is_optc_underflow_occurred = optc42_is_optc_underflow_occurred,
		.clear_optc_underflow = optc42_clear_optc_underflow,
		.setup_global_swap_lock = NULL,
		.get_crc = optc42_get_crc,
		.configure_crc = optc35_configure_crc,
		.set_dsc_config = optc3_set_dsc_config,
		.get_dsc_status = optc2_get_dsc_status,
		.set_dwb_source = NULL,
		.set_odm_bypass = optc401_set_odm_bypass,
		.set_odm_combine = optc401_set_odm_combine,
		.wait_odm_doublebuffer_pending_clear = optc32_wait_odm_doublebuffer_pending_clear,
		.set_h_timing_div_manual_mode = optc401_set_h_timing_div_manual_mode,
		.get_optc_source = optc2_get_optc_source,
		.wait_otg_disable = optc35_wait_otg_disable,
		.set_out_mux = optc401_set_out_mux,
		.set_drr_trigger_window = optc3_set_drr_trigger_window,
		.set_vtotal_change_limit = optc3_set_vtotal_change_limit,
		.set_gsl = optc2_set_gsl,
		.set_gsl_source_select = optc2_set_gsl_source_select,
		.set_vtg_params = optc1_set_vtg_params,
		.program_manual_trigger = optc2_program_manual_trigger,
		.setup_manual_trigger = optc2_setup_manual_trigger,
		.get_hw_timing = optc1_get_hw_timing,
		.init_odm = optc3_init_odm,
		.set_long_vtotal = optc35_set_long_vtotal,
		.is_two_pixels_per_container = optc1_is_two_pixels_per_container,
		.get_optc_double_buffer_pending = optc3_get_optc_double_buffer_pending,
		.get_otg_double_buffer_pending = optc3_get_otg_update_pending,
		.get_pipe_update_pending = optc3_get_pipe_update_pending,
		.set_vupdate_keepout = optc401_set_vupdate_keepout,
		.wait_update_lock_status = optc401_wait_update_lock_status,
		.optc_read_reg_state = optc31_read_reg_state,
		.read_otg_state = optc31_read_otg_state,
		.enable_otg_pwa = optc42_enable_pwa,
		.disable_otg_pwa = optc42_disable_pwa,
};

void dcn42_timing_generator_init(struct optc *optc1)
{
	optc1->base.funcs = &dcn42_tg_funcs;

	optc1->max_h_total = optc1->tg_mask->OTG_H_TOTAL + 1;
	optc1->max_v_total = optc1->tg_mask->OTG_V_TOTAL + 1;

	optc1->min_h_blank = 32;
	optc1->min_v_blank = 3;
	optc1->min_v_blank_interlace = 5;
	optc1->min_h_sync_width = 4;
	optc1->min_v_sync_width = 1;
	optc1->max_frame_count = 0xFFFFFF;

	dcn35_timing_generator_set_fgcg(
		optc1, CTX->dc->debug.enable_fine_grain_clock_gating.bits.optc);
}

