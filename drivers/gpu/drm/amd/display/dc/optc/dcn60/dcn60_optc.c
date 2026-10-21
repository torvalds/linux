// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dcn30/dcn30_optc.h"
#include "dcn31/dcn31_optc.h"
#include "dcn32/dcn32_optc.h"
#include "dcn401/dcn401_optc.h"
#include "dcn42/dcn42_optc.h"
#include "dcn60_optc.h"
#include "reg_helper.h"
#include "dc.h"
#include "dcn_calc_math.h"
#include "dc_dmub_srv.h"

#define REG(reg)\
	optc1->tg_regs->reg

#define CTX \
	optc1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	optc1->tg_shift->field_name, optc1->tg_mask->field_name

static const struct timing_generator_funcs dcn60_tg_funcs = {
		.validate_timing = optc1_validate_timing,
		.program_timing = optc1_program_timing,
		.setup_vertical_interrupt0 = optc1_setup_vertical_interrupt0,
		.setup_vertical_interrupt1 = optc1_setup_vertical_interrupt1,
		.setup_vertical_interrupt2 = optc1_setup_vertical_interrupt2,
		.program_global_sync = optc401_program_global_sync,
		.enable_crtc = optc401_enable_crtc,
		.disable_crtc = optc401_disable_crtc,
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
		.set_drr = optc401_set_drr,
		.get_last_used_drr_vtotal = optc2_get_last_used_drr_vtotal,
		.set_vtotal_min_max = optc401_set_vtotal_min_max,
		.set_static_screen_control = optc1_set_static_screen_control,
		.program_stereo = optc1_program_stereo,
		.is_stereo_left_eye = optc1_is_stereo_left_eye,
		.tg_init = optc3_tg_init,
		.is_tg_enabled = optc1_is_tg_enabled,
		.is_optc_underflow_occurred = optc1_is_optc_underflow_occurred,
		.clear_optc_underflow = optc1_clear_optc_underflow,
		.get_crc = optc42_get_crc,
		.configure_crc = optc1_configure_crc,
		.set_dsc_config = optc3_set_dsc_config,
		.get_dsc_status = optc2_get_dsc_status,
		.set_odm_bypass = optc401_set_odm_bypass,
		.set_odm_combine = optc401_set_odm_combine,
		.wait_odm_doublebuffer_pending_clear = optc32_wait_odm_doublebuffer_pending_clear,
		.set_h_timing_div_manual_mode = optc401_set_h_timing_div_manual_mode,
		.get_optc_source = optc2_get_optc_source,
		.set_out_mux = optc401_set_out_mux,
		.set_drr_trigger_window = optc3_set_drr_trigger_window,
		.set_vtotal_change_limit = optc3_set_vtotal_change_limit,
		.set_gsl = optc2_set_gsl,
		.set_gsl_source_select = NULL,
		.set_vtg_params = optc1_set_vtg_params,
		.program_manual_trigger = optc2_program_manual_trigger,
		.setup_manual_trigger = optc2_setup_manual_trigger,
		.get_hw_timing = optc1_get_hw_timing,
		.is_two_pixels_per_container = optc1_is_two_pixels_per_container,
		.get_optc_double_buffer_pending = optc3_get_optc_double_buffer_pending,
		.get_otg_double_buffer_pending = optc3_get_otg_update_pending,
		.get_pipe_update_pending = optc3_get_pipe_update_pending,
		.set_vupdate_keepout = optc401_set_vupdate_keepout,
		.wait_update_lock_status = optc401_wait_update_lock_status,
		.read_otg_state = optc31_read_otg_state,
		.optc_read_reg_state = optc31_read_reg_state,
};

void dcn60_timing_generator_init(struct optc *optc1)
{
	optc1->base.funcs = &dcn60_tg_funcs;

	optc1->max_h_total = optc1->tg_mask->OTG_H_TOTAL + 1;
	optc1->max_v_total = optc1->tg_mask->OTG_V_TOTAL + 1;

	optc1->min_h_blank = 32;
	optc1->min_v_blank = 3;
	optc1->min_v_blank_interlace = 5;
	optc1->min_h_sync_width = 4;
	optc1->min_v_sync_width = 1;
}
