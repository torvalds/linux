/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "dce110/dce110_hwseq.h"
#include "dcn10/dcn10_hwseq.h"
#include "dcn20/dcn20_hwseq.h"
#include "dcn21/dcn21_hwseq.h"
#include "dcn30/dcn30_hwseq.h"
#include "dcn301/dcn301_hwseq.h"
#include "dcn31/dcn31_hwseq.h"
#include "dcn32/dcn32_hwseq.h"
#include "dcn35/dcn35_hwseq.h"
#include "dcn351/dcn351_hwseq.h"

#include "dcn351_init.h"

static const struct hw_sequencer_funcs dcn351_funcs = {
	.program_gamut_remap = dcn30_program_gamut_remap,
	.init_hw = dcn35_init_hw,
	.power_down_on_boot = dcn35_power_down_on_boot,
	.apply_ctx_to_hw = dce110_apply_ctx_to_hw,
	.apply_ctx_for_surface = NULL,
	.program_front_end_for_ctx = dcn20_program_front_end_for_ctx,
	.clear_surface_dcc_and_tiling = dcn10_reset_surface_dcc_and_tiling,
	.wait_for_pending_cleared = dcn10_wait_for_pending_cleared,
	.post_unlock_program_front_end = dcn20_post_unlock_program_front_end,
	.update_plane_addr = dcn20_update_plane_addr,
	.update_dchub = dcn10_update_dchub,
	.update_pending_status = dcn10_update_pending_status,
	.program_output_csc = dcn20_program_output_csc,
	.enable_accelerated_mode = dce110_enable_accelerated_mode,
	.enable_timing_synchronization = dcn10_enable_timing_synchronization,
	.enable_per_frame_crtc_position_reset = dcn10_enable_per_frame_crtc_position_reset,
	.update_info_frame = dcn31_update_info_frame,
	.send_immediate_sdp_message = dcn10_send_immediate_sdp_message,
	.enable_stream = dcn20_enable_stream,
	.disable_stream = dce110_disable_stream,
	.unblank_stream = dcn32_unblank_stream,
	.blank_stream = dce110_blank_stream,
	.enable_audio_stream = dce110_enable_audio_stream,
	.disable_audio_stream = dce110_disable_audio_stream,
	.disable_plane = dcn35_disable_plane,
	.disable_pixel_data = dcn20_disable_pixel_data,
	.pipe_control_lock = dcn20_pipe_control_lock,
	.interdependent_update_lock = dcn10_lock_all_pipes,
	.cursor_lock = dcn10_cursor_lock,
	.prepare_bandwidth = dcn35_prepare_bandwidth,
	.optimize_bandwidth = dcn35_optimize_bandwidth,
	.update_bandwidth = dcn20_update_bandwidth,
	.set_drr = dcn35_set_drr,
	.get_position = dcn10_get_position,
	.set_static_screen_control = dcn35_set_static_screen_control,
	.setup_stereo = dcn10_setup_stereo,
	.set_avmute = dcn30_set_avmute,
	.log_hw_state = dcn10_log_hw_state,
	.get_hw_state = dcn10_get_hw_state,
	.clear_status_bits = dcn10_clear_status_bits,
	.wait_for_mpcc_disconnect = dcn10_wait_for_mpcc_disconnect,
	.edp_backlight_control = dce110_edp_backlight_control,
	.edp_power_control = dce110_edp_power_control,
	.edp_wait_for_T12 = dce110_edp_wait_for_T12,
	.edp_wait_for_hpd_ready = dce110_edp_wait_for_hpd_ready,
	.set_cursor_position = dcn10_set_cursor_position,
	.set_cursor_attribute = dcn10_set_cursor_attribute,
	.set_cursor_sdr_white_level = dcn10_set_cursor_sdr_white_level,
	.setup_periodic_interrupt = dcn10_setup_periodic_interrupt,
	.set_clock = dcn10_set_clock,
	.get_clock = dcn10_get_clock,
	.program_triplebuffer = dcn20_program_triple_buffer,
	.enable_writeback = dcn30_enable_writeback,
	.disable_writeback = dcn30_disable_writeback,
	.update_writeback = dcn30_update_writeback,
	.dmdata_status_done = dcn20_dmdata_status_done,
	.program_dmdata_engine = dcn30_program_dmdata_engine,
	.set_dmdata_attributes = dcn20_set_dmdata_attributes,
	.init_sys_ctx = dcn31_init_sys_ctx,
	.init_vm_ctx = dcn20_init_vm_ctx,
	.set_flip_control_gsl = dcn20_set_flip_control_gsl,
	.get_vupdate_offset_from_vsync = dcn10_get_vupdate_offset_from_vsync,
	.calc_vupdate_position = dcn10_calc_vupdate_position,
	.set_backlight_level = dcn31_set_backlight_level,
	.set_abm_immediate_disable = dcn21_set_abm_immediate_disable,
	.set_pipe = dcn21_set_pipe,
	.enable_lvds_link_output = dce110_enable_lvds_link_output,
	.enable_tmds_link_output = dce110_enable_tmds_link_output,
	.enable_dp_link_output = dce110_enable_dp_link_output,
	.disable_link_output = dcn32_disable_link_output,
	.z10_restore = dcn35_z10_restore,
	.z10_save_init = dcn31_z10_save_init,
	.set_disp_pattern_generator = dcn30_set_disp_pattern_generator,
	.optimize_pwr_state = dcn21_optimize_pwr_state,
	.exit_optimized_pwr_state = dcn21_exit_optimized_pwr_state,
	.update_visual_confirm_color = dcn10_update_visual_confirm_color,
	.apply_idle_power_optimizations = dcn35_apply_idle_power_optimizations,
	.calc_blocks_to_gate = dcn351_calc_blocks_to_gate,
	.calc_blocks_to_ungate = dcn351_calc_blocks_to_ungate,
	.hw_block_power_up = dcn351_hw_block_power_up,
	.hw_block_power_down = dcn351_hw_block_power_down,
	.root_clock_control = dcn35_root_clock_control,
	.set_long_vtotal = dcn35_set_long_vblank,
	.calculate_pix_rate_divider = dcn32_calculate_pix_rate_divider,
	.setup_hpo_hw_control = dcn35_setup_hpo_hw_control,
	.get_underflow_debug_data = dcn30_get_underflow_debug_data,
};

static const struct hwseq_private_funcs dcn351_private_funcs = {
	.init_pipes = dcn35_init_pipes,
	.plane_atomic_disconnect = dcn10_plane_atomic_disconnect,
	.update_mpcc = dcn20_update_mpcc,
	.set_input_transfer_func = dcn32_set_input_transfer_func,
	.set_output_transfer_func = dcn32_set_output_transfer_func,
	.power_down = dce110_power_down,
	.enable_display_power_gating = dcn10_dummy_display_power_gating,
	.blank_pixel_data = dcn20_blank_pixel_data,
	.reset_hw_ctx_wrap = dcn31_reset_hw_ctx_wrap,
	.enable_stream_timing = dcn20_enable_stream_timing,
	.edp_backlight_control = dce110_edp_backlight_control,
	.setup_vupdate_interrupt = dcn20_setup_vupdate_interrupt,
	.did_underflow_occur = dcn10_did_underflow_occur,
	.init_blank = dcn20_init_blank,
	.disable_vga = NULL,
	.bios_golden_init = dcn10_bios_golden_init,
	.plane_atomic_disable = dcn35_plane_atomic_disable,
	//.plane_atomic_disable = dcn20_plane_atomic_disable,/*todo*/
	//.hubp_pg_control = dcn35_hubp_pg_control,
	.dpp_root_clock_control = dcn35_dpp_root_clock_control,
	.dpstream_root_clock_control = dcn35_dpstream_root_clock_control,
	.physymclk_root_clock_control = dcn35_physymclk_root_clock_control,
	.program_all_writeback_pipes_in_tree = dcn30_program_all_writeback_pipes_in_tree,
	.update_odm = dcn35_update_odm,
	.set_hdr_multiplier = dcn10_set_hdr_multiplier,
	.verify_allow_pstate_change_high = dcn10_verify_allow_pstate_change_high,
	.wait_for_blank_complete = dcn20_wait_for_blank_complete,
	.dccg_init = dcn20_dccg_init,
	.set_mcm_luts = dcn32_set_mcm_luts,
	.setup_hpo_hw_control = dcn35_setup_hpo_hw_control,
	.calculate_dccg_k1_k2_values = dcn32_calculate_dccg_k1_k2_values,
	.is_dp_dig_pixel_rate_div_policy = dcn35_is_dp_dig_pixel_rate_div_policy,
	.dsc_pg_status = dcn32_dsc_pg_status,
	.enable_plane = dcn35_enable_plane,
	.wait_for_pipe_update_if_needed = dcn10_wait_for_pipe_update_if_needed,
	.set_wait_for_update_needed_for_pipe = dcn10_set_wait_for_update_needed_for_pipe,
};

void dcn351_hw_sequencer_construct(struct dc *dc)
{
	dc->hwss = dcn351_funcs;
	dc->hwseq->funcs = dcn351_private_funcs;

}
