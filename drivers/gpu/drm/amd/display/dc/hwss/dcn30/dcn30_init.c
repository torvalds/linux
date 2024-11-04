/*
 * Copyright 2016-2020 Advanced Micro Devices, Inc.
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

#include "dcn30_init.h"

static const struct hw_sequencer_funcs dcn30_funcs = {
	.program_gamut_remap = dcn30_program_gamut_remap,
	.init_hw = dcn30_init_hw,
	.apply_ctx_to_hw = dce110_apply_ctx_to_hw,
	.apply_ctx_for_surface = NULL,
	.program_front_end_for_ctx = dcn20_program_front_end_for_ctx,
	.wait_for_pending_cleared = dcn10_wait_for_pending_cleared,
	.post_unlock_program_front_end = dcn20_post_unlock_program_front_end,
	.update_plane_addr = dcn20_update_plane_addr,
	.update_dchub = dcn10_update_dchub,
	.update_pending_status = dcn10_update_pending_status,
	.program_output_csc = dcn20_program_output_csc,
	.enable_accelerated_mode = dce110_enable_accelerated_mode,
	.enable_timing_synchronization = dcn10_enable_timing_synchronization,
	.enable_per_frame_crtc_position_reset = dcn10_enable_per_frame_crtc_position_reset,
	.update_info_frame = dcn30_update_info_frame,
	.send_immediate_sdp_message = dcn10_send_immediate_sdp_message,
	.enable_stream = dcn20_enable_stream,
	.disable_stream = dce110_disable_stream,
	.unblank_stream = dcn20_unblank_stream,
	.blank_stream = dce110_blank_stream,
	.enable_audio_stream = dce110_enable_audio_stream,
	.disable_audio_stream = dce110_disable_audio_stream,
	.disable_plane = dcn20_disable_plane,
	.disable_pixel_data = dcn20_disable_pixel_data,
	.pipe_control_lock = dcn20_pipe_control_lock,
	.interdependent_update_lock = dcn10_lock_all_pipes,
	.cursor_lock = dcn10_cursor_lock,
	.prepare_bandwidth = dcn30_prepare_bandwidth,
	.optimize_bandwidth = dcn20_optimize_bandwidth,
	.update_bandwidth = dcn20_update_bandwidth,
	.set_drr = dcn10_set_drr,
	.get_position = dcn10_get_position,
	.set_static_screen_control = dcn10_set_static_screen_control,
	.setup_stereo = dcn10_setup_stereo,
	.set_avmute = dcn30_set_avmute,
	.log_hw_state = dcn10_log_hw_state,
	.log_color_state = dcn30_log_color_state,
	.get_hw_state = dcn10_get_hw_state,
	.clear_status_bits = dcn10_clear_status_bits,
	.wait_for_mpcc_disconnect = dcn10_wait_for_mpcc_disconnect,
	.edp_backlight_control = dce110_edp_backlight_control,
	.edp_power_control = dce110_edp_power_control,
	.edp_wait_for_hpd_ready = dce110_edp_wait_for_hpd_ready,
	.edp_wait_for_T12 = dce110_edp_wait_for_T12,
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
	.init_sys_ctx = dcn20_init_sys_ctx,
	.init_vm_ctx = dcn20_init_vm_ctx,
	.set_flip_control_gsl = dcn20_set_flip_control_gsl,
	.get_vupdate_offset_from_vsync = dcn10_get_vupdate_offset_from_vsync,
	.calc_vupdate_position = dcn10_calc_vupdate_position,
	.apply_idle_power_optimizations = dcn30_apply_idle_power_optimizations,
	.does_plane_fit_in_mall = dcn30_does_plane_fit_in_mall,
	.set_backlight_level = dcn21_set_backlight_level,
	.set_abm_immediate_disable = dcn21_set_abm_immediate_disable,
	.hardware_release = dcn30_hardware_release,
	.set_pipe = dcn21_set_pipe,
	.enable_lvds_link_output = dce110_enable_lvds_link_output,
	.enable_tmds_link_output = dce110_enable_tmds_link_output,
	.enable_dp_link_output = dce110_enable_dp_link_output,
	.disable_link_output = dce110_disable_link_output,
	.set_disp_pattern_generator = dcn30_set_disp_pattern_generator,
	.get_dcc_en_bits = dcn10_get_dcc_en_bits,
	.update_visual_confirm_color = dcn10_update_visual_confirm_color,
	.is_abm_supported = dcn21_is_abm_supported,
	.wait_for_all_pending_updates = dcn30_wait_for_all_pending_updates,
};

static const struct hwseq_private_funcs dcn30_private_funcs = {
	.init_pipes = dcn10_init_pipes,
	.plane_atomic_disconnect = dcn10_plane_atomic_disconnect,
	.update_mpcc = dcn20_update_mpcc,
	.set_input_transfer_func = dcn30_set_input_transfer_func,
	.set_output_transfer_func = dcn30_set_output_transfer_func,
	.power_down = dce110_power_down,
	.enable_display_power_gating = dcn10_dummy_display_power_gating,
	.blank_pixel_data = dcn20_blank_pixel_data,
	.reset_hw_ctx_wrap = dcn20_reset_hw_ctx_wrap,
	.enable_stream_timing = dcn20_enable_stream_timing,
	.edp_backlight_control = dce110_edp_backlight_control,
	.disable_stream_gating = dcn20_disable_stream_gating,
	.enable_stream_gating = dcn20_enable_stream_gating,
	.setup_vupdate_interrupt = dcn20_setup_vupdate_interrupt,
	.did_underflow_occur = dcn10_did_underflow_occur,
	.init_blank = dcn20_init_blank,
	.disable_vga = dcn20_disable_vga,
	.bios_golden_init = dcn10_bios_golden_init,
	.plane_atomic_disable = dcn20_plane_atomic_disable,
	.plane_atomic_power_down = dcn10_plane_atomic_power_down,
	.enable_power_gating_plane = dcn20_enable_power_gating_plane,
	.dpp_pg_control = dcn20_dpp_pg_control,
	.hubp_pg_control = dcn20_hubp_pg_control,
	.program_all_writeback_pipes_in_tree = dcn30_program_all_writeback_pipes_in_tree,
	.update_odm = dcn20_update_odm,
	.dsc_pg_control = dcn20_dsc_pg_control,
	.set_hdr_multiplier = dcn10_set_hdr_multiplier,
	.verify_allow_pstate_change_high = dcn10_verify_allow_pstate_change_high,
	.wait_for_blank_complete = dcn20_wait_for_blank_complete,
	.dccg_init = dcn20_dccg_init,
	.set_blend_lut = dcn30_set_blend_lut,
	.set_shaper_3dlut = dcn20_set_shaper_3dlut,
};

void dcn30_hw_sequencer_construct(struct dc *dc)
{
	dc->hwss = dcn30_funcs;
	dc->hwseq->funcs = dcn30_private_funcs;

}
