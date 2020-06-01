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

#include "dce110/dce110_hw_sequencer.h"
#include "dcn10/dcn10_hw_sequencer.h"
#include "dcn20/dcn20_hwseq.h"
#include "dcn21_hwseq.h"

static const struct hw_sequencer_funcs dcn21_funcs = {
	.program_gamut_remap = dcn10_program_gamut_remap,
	.init_hw = dcn10_init_hw,
	.apply_ctx_to_hw = dce110_apply_ctx_to_hw,
	.apply_ctx_for_surface = NULL,
	.program_front_end_for_ctx = dcn20_program_front_end_for_ctx,
	.post_unlock_program_front_end = dcn20_post_unlock_program_front_end,
	.update_plane_addr = dcn20_update_plane_addr,
	.update_dchub = dcn10_update_dchub,
	.update_pending_status = dcn10_update_pending_status,
	.program_output_csc = dcn20_program_output_csc,
	.enable_accelerated_mode = dce110_enable_accelerated_mode,
	.enable_timing_synchronization = dcn10_enable_timing_synchronization,
	.enable_per_frame_crtc_position_reset = dcn10_enable_per_frame_crtc_position_reset,
	.update_info_frame = dce110_update_info_frame,
	.send_immediate_sdp_message = dcn10_send_immediate_sdp_message,
	.enable_stream = dcn20_enable_stream,
	.disable_stream = dce110_disable_stream,
	.unblank_stream = dcn20_unblank_stream,
	.blank_stream = dce110_blank_stream,
	.enable_audio_stream = dce110_enable_audio_stream,
	.disable_audio_stream = dce110_disable_audio_stream,
	.disable_plane = dcn20_disable_plane,
	.pipe_control_lock = dcn20_pipe_control_lock,
	.interdependent_update_lock = dcn10_lock_all_pipes,
	.prepare_bandwidth = dcn20_prepare_bandwidth,
	.optimize_bandwidth = dcn20_optimize_bandwidth,
	.update_bandwidth = dcn20_update_bandwidth,
	.set_drr = dcn10_set_drr,
	.get_position = dcn10_get_position,
	.set_static_screen_control = dcn10_set_static_screen_control,
	.setup_stereo = dcn10_setup_stereo,
	.set_avmute = dce110_set_avmute,
	.log_hw_state = dcn10_log_hw_state,
	.get_hw_state = dcn10_get_hw_state,
	.clear_status_bits = dcn10_clear_status_bits,
	.wait_for_mpcc_disconnect = dcn10_wait_for_mpcc_disconnect,
	.edp_power_control = dce110_edp_power_control,
	.edp_wait_for_hpd_ready = dce110_edp_wait_for_hpd_ready,
	.set_cursor_position = dcn10_set_cursor_position,
	.set_cursor_attribute = dcn10_set_cursor_attribute,
	.set_cursor_sdr_white_level = dcn10_set_cursor_sdr_white_level,
	.setup_periodic_interrupt = dcn10_setup_periodic_interrupt,
	.set_clock = dcn10_set_clock,
	.get_clock = dcn10_get_clock,
	.program_triplebuffer = dcn20_program_triple_buffer,
	.enable_writeback = dcn20_enable_writeback,
	.disable_writeback = dcn20_disable_writeback,
	.dmdata_status_done = dcn20_dmdata_status_done,
	.program_dmdata_engine = dcn20_program_dmdata_engine,
	.set_dmdata_attributes = dcn20_set_dmdata_attributes,
	.init_sys_ctx = dcn21_init_sys_ctx,
	.init_vm_ctx = dcn20_init_vm_ctx,
	.set_flip_control_gsl = dcn20_set_flip_control_gsl,
	.optimize_pwr_state = dcn21_optimize_pwr_state,
	.exit_optimized_pwr_state = dcn21_exit_optimized_pwr_state,
	.get_vupdate_offset_from_vsync = dcn10_get_vupdate_offset_from_vsync,
	.set_cursor_position = dcn10_set_cursor_position,
	.set_cursor_attribute = dcn10_set_cursor_attribute,
	.set_cursor_sdr_white_level = dcn10_set_cursor_sdr_white_level,
	.optimize_pwr_state = dcn21_optimize_pwr_state,
	.exit_optimized_pwr_state = dcn21_exit_optimized_pwr_state,
};

static const struct hwseq_private_funcs dcn21_private_funcs = {
	.init_pipes = dcn10_init_pipes,
	.update_plane_addr = dcn20_update_plane_addr,
	.plane_atomic_disconnect = dcn10_plane_atomic_disconnect,
	.update_mpcc = dcn20_update_mpcc,
	.set_input_transfer_func = dcn20_set_input_transfer_func,
	.set_output_transfer_func = dcn20_set_output_transfer_func,
	.power_down = dce110_power_down,
	.enable_display_power_gating = dcn10_dummy_display_power_gating,
	.blank_pixel_data = dcn20_blank_pixel_data,
	.reset_hw_ctx_wrap = dcn20_reset_hw_ctx_wrap,
	.enable_stream_timing = dcn20_enable_stream_timing,
	.edp_backlight_control = dce110_edp_backlight_control,
	.is_panel_backlight_on = dce110_is_panel_backlight_on,
	.is_panel_powered_on = dce110_is_panel_powered_on,
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
	.update_odm = dcn20_update_odm,
	.dsc_pg_control = dcn20_dsc_pg_control,
	.get_surface_visual_confirm_color = dcn10_get_surface_visual_confirm_color,
	.get_hdr_visual_confirm_color = dcn10_get_hdr_visual_confirm_color,
	.set_hdr_multiplier = dcn10_set_hdr_multiplier,
	.verify_allow_pstate_change_high = dcn10_verify_allow_pstate_change_high,
	.s0i3_golden_init_wa = dcn21_s0i3_golden_init_wa,
	.wait_for_blank_complete = dcn20_wait_for_blank_complete,
	.dccg_init = dcn20_dccg_init,
	.set_blend_lut = dcn20_set_blend_lut,
	.set_shaper_3dlut = dcn20_set_shaper_3dlut,
	.PLAT_58856_wa = dcn21_PLAT_58856_wa,
};

void dcn21_hw_sequencer_construct(struct dc *dc)
{
	dc->hwss = dcn21_funcs;
	dc->hwseq->funcs = dcn21_private_funcs;

	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		dc->hwss.init_hw = dcn20_fpga_init_hw;
		dc->hwseq->funcs.init_pipes = NULL;
	}
}
