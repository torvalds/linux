/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include "hw_sequencer_private.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dcn10_hw_sequencer.h"

static const struct hw_sequencer_funcs dcn10_funcs = {
	.program_gamut_remap = dcn10_program_gamut_remap,
	.init_hw = dcn10_init_hw,
	.power_down_on_boot = dcn10_power_down_on_boot,
	.apply_ctx_to_hw = dce110_apply_ctx_to_hw,
	.apply_ctx_for_surface = dcn10_apply_ctx_for_surface,
	.post_unlock_program_front_end = dcn10_post_unlock_program_front_end,
	.disconnect_pipes = dcn10_disconnect_pipes,
	.wait_for_pending_cleared = dcn10_wait_for_pending_cleared,
	.update_plane_addr = dcn10_update_plane_addr,
	.update_dchub = dcn10_update_dchub,
	.update_pending_status = dcn10_update_pending_status,
	.program_output_csc = dcn10_program_output_csc,
	.enable_accelerated_mode = dce110_enable_accelerated_mode,
	.enable_timing_synchronization = dcn10_enable_timing_synchronization,
	.enable_per_frame_crtc_position_reset = dcn10_enable_per_frame_crtc_position_reset,
	.update_info_frame = dce110_update_info_frame,
	.send_immediate_sdp_message = dcn10_send_immediate_sdp_message,
	.enable_stream = dce110_enable_stream,
	.disable_stream = dce110_disable_stream,
	.unblank_stream = dcn10_unblank_stream,
	.blank_stream = dce110_blank_stream,
	.enable_audio_stream = dce110_enable_audio_stream,
	.disable_audio_stream = dce110_disable_audio_stream,
	.disable_plane = dcn10_disable_plane,
	.pipe_control_lock = dcn10_pipe_control_lock,
	.cursor_lock = dcn10_cursor_lock,
	.interdependent_update_lock = dcn10_lock_all_pipes,
	.prepare_bandwidth = dcn10_prepare_bandwidth,
	.optimize_bandwidth = dcn10_optimize_bandwidth,
	.set_drr = dcn10_set_drr,
	.get_position = dcn10_get_position,
	.set_static_screen_control = dcn10_set_static_screen_control,
	.setup_stereo = dcn10_setup_stereo,
	.set_avmute = dce110_set_avmute,
	.log_hw_state = dcn10_log_hw_state,
	.get_hw_state = dcn10_get_hw_state,
	.clear_status_bits = dcn10_clear_status_bits,
	.wait_for_mpcc_disconnect = dcn10_wait_for_mpcc_disconnect,
	.edp_backlight_control = dce110_edp_backlight_control,
	.edp_power_control = dce110_edp_power_control,
	.edp_wait_for_hpd_ready = dce110_edp_wait_for_hpd_ready,
	.set_cursor_position = dcn10_set_cursor_position,
	.set_cursor_attribute = dcn10_set_cursor_attribute,
	.set_cursor_sdr_white_level = dcn10_set_cursor_sdr_white_level,
	.setup_periodic_interrupt = dcn10_setup_periodic_interrupt,
	.set_clock = dcn10_set_clock,
	.get_clock = dcn10_get_clock,
	.get_vupdate_offset_from_vsync = dcn10_get_vupdate_offset_from_vsync,
	.calc_vupdate_position = dcn10_calc_vupdate_position,
	.set_backlight_level = dce110_set_backlight_level,
	.set_abm_immediate_disable = dce110_set_abm_immediate_disable,
	.set_pipe = dce110_set_pipe,
};

static const struct hwseq_private_funcs dcn10_private_funcs = {
	.init_pipes = dcn10_init_pipes,
	.update_plane_addr = dcn10_update_plane_addr,
	.plane_atomic_disconnect = dcn10_plane_atomic_disconnect,
	.program_pipe = dcn10_program_pipe,
	.update_mpcc = dcn10_update_mpcc,
	.set_input_transfer_func = dcn10_set_input_transfer_func,
	.set_output_transfer_func = dcn10_set_output_transfer_func,
	.power_down = dce110_power_down,
	.enable_display_power_gating = dcn10_dummy_display_power_gating,
	.blank_pixel_data = dcn10_blank_pixel_data,
	.reset_hw_ctx_wrap = dcn10_reset_hw_ctx_wrap,
	.enable_stream_timing = dcn10_enable_stream_timing,
	.edp_backlight_control = dce110_edp_backlight_control,
	.disable_stream_gating = NULL,
	.enable_stream_gating = NULL,
	.setup_vupdate_interrupt = dcn10_setup_vupdate_interrupt,
	.did_underflow_occur = dcn10_did_underflow_occur,
	.init_blank = NULL,
	.disable_vga = dcn10_disable_vga,
	.bios_golden_init = dcn10_bios_golden_init,
	.plane_atomic_disable = dcn10_plane_atomic_disable,
	.plane_atomic_power_down = dcn10_plane_atomic_power_down,
	.enable_power_gating_plane = dcn10_enable_power_gating_plane,
	.dpp_pg_control = dcn10_dpp_pg_control,
	.hubp_pg_control = dcn10_hubp_pg_control,
	.dsc_pg_control = NULL,
	.get_surface_visual_confirm_color = dcn10_get_surface_visual_confirm_color,
	.get_hdr_visual_confirm_color = dcn10_get_hdr_visual_confirm_color,
	.set_hdr_multiplier = dcn10_set_hdr_multiplier,
	.verify_allow_pstate_change_high = dcn10_verify_allow_pstate_change_high,
};

void dcn10_hw_sequencer_construct(struct dc *dc)
{
	dc->hwss = dcn10_funcs;
	dc->hwseq->funcs = dcn10_private_funcs;
}
