/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef __DC_HW_SEQUENCER_H__
#define __DC_HW_SEQUENCER_H__
#include "dc_types.h"
#include "inc/clock_source.h"
#include "inc/hw/timing_generator.h"
#include "inc/hw/opp.h"
#include "inc/hw/link_encoder.h"
#include "inc/core_status.h"
#include "inc/hw/hw_shared.h"
#include "dsc/dsc.h"

struct pipe_ctx;
struct dc_state;
struct dc_stream_status;
struct dc_writeback_info;
struct dchub_init_data;
struct dc_static_screen_params;
struct resource_pool;
struct dc_phy_addr_space_config;
struct dc_virtual_addr_space_config;
struct dpp;
struct dce_hwseq;
struct link_resource;
struct dc_dmub_cmd;
struct pg_block_update;
struct drr_params;
struct dc_underflow_debug_data;
struct dsc_optc_config;
struct vm_system_aperture_param;
struct memory_qos;
struct subvp_pipe_control_lock_fast_params {
	struct dc *dc;
	bool lock;
	bool subvp_immediate_flip;
};

struct pipe_control_lock_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	bool lock;
};

struct set_flip_control_gsl_params {
	struct hubp *hubp;
	bool flip_immediate;
};

struct program_triplebuffer_params {
	const struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	bool enableTripleBuffer;
};

struct update_plane_addr_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct set_input_transfer_func_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	struct dc_plane_state *plane_state;
};

struct program_gamut_remap_params {
	struct pipe_ctx *pipe_ctx;
};

struct program_manual_trigger_params {
	struct pipe_ctx *pipe_ctx;
};

struct send_dmcub_cmd_params {
	struct dc_context *ctx;
	union dmub_rb_cmd *cmd;
	enum dm_dmub_wait_type wait_type;
};

struct setup_dpp_params {
	struct pipe_ctx *pipe_ctx;
};

struct program_bias_and_scale_params {
	struct pipe_ctx *pipe_ctx;
};

struct set_output_transfer_func_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	const struct dc_stream_state *stream;
};

struct update_visual_confirm_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	int mpcc_id;
};

struct power_on_mpc_mem_pwr_params {
	struct mpc *mpc;
	int mpcc_id;
	bool power_on;
};

struct set_output_csc_params {
	struct mpc *mpc;
	int opp_id;
	const uint16_t *regval;
	enum mpc_output_csc_mode ocsc_mode;
};

struct set_ocsc_default_params {
	struct mpc *mpc;
	int opp_id;
	enum dc_color_space color_space;
	enum mpc_output_csc_mode ocsc_mode;
};

struct subvp_save_surf_addr {
	struct dc_dmub_srv *dc_dmub_srv;
	const struct dc_plane_address *addr;
	uint8_t subvp_index;
};

struct wait_for_dcc_meta_propagation_params {
	const struct dc *dc;
	const struct pipe_ctx *top_pipe_to_program;
};

struct dmub_hw_control_lock_fast_params {
	struct dc *dc;
	bool is_required;
	bool lock;
};

struct program_surface_config_params {
	struct hubp *hubp;
	enum surface_pixel_format format;
	struct dc_tiling_info *tiling_info;
	struct plane_size plane_size;
	enum dc_rotation_angle rotation;
	struct dc_plane_dcc_param *dcc;
	bool horizontal_mirror;
	int compat_level;
};

struct program_mcache_id_and_split_coordinate {
	struct hubp *hubp;
	struct dml2_hubp_pipe_mcache_regs *mcache_regs;
};

struct program_cursor_update_now_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct hubp_wait_pipe_read_start_params {
	struct hubp *hubp;
};

struct apply_update_flags_for_phantom_params {
	struct pipe_ctx *pipe_ctx;
};

struct update_phantom_vp_position_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	struct dc_state *context;
};

struct set_odm_combine_params {
	struct timing_generator *tg;
	int opp_inst[MAX_PIPES];
	int opp_head_count;
	int odm_slice_width;
	int last_odm_slice_width;
};

struct set_odm_bypass_params {
	struct timing_generator *tg;
	const struct dc_crtc_timing *timing;
};

struct opp_pipe_clock_control_params {
	struct output_pixel_processor *opp;
	bool enable;
};

struct opp_program_left_edge_extra_pixel_params {
	struct output_pixel_processor *opp;
	enum dc_pixel_encoding pixel_encoding;
	bool is_otg_master;
};

struct dccg_set_dto_dscclk_params {
	struct dccg *dccg;
	int inst;
	int num_slices_h;
};

struct dsc_set_config_params {
	struct display_stream_compressor *dsc;
	struct dsc_config *dsc_cfg;
	struct dsc_optc_config *dsc_optc_cfg;
};

struct dsc_enable_params {
	struct display_stream_compressor *dsc;
	int opp_inst;
};

struct tg_set_dsc_config_params {
	struct timing_generator *tg;
	struct dsc_optc_config *dsc_optc_cfg;
	bool enable;
};

struct dsc_disconnect_params {
	struct display_stream_compressor *dsc;
};

struct dsc_read_state_params {
	struct display_stream_compressor *dsc;
	struct dcn_dsc_state *dsc_state;
};

struct dsc_calculate_and_set_config_params {
	struct pipe_ctx *pipe_ctx;
	struct dsc_optc_config dsc_optc_cfg;
	bool enable;
	int opp_cnt;
};

struct dsc_enable_with_opp_params {
	struct pipe_ctx *pipe_ctx;
};

struct program_tg_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	struct dc_state *context;
};

struct tg_program_global_sync_params {
	struct timing_generator *tg;
	int vready_offset;
	unsigned int vstartup_lines;
	unsigned int vupdate_offset_pixels;
	unsigned int vupdate_vupdate_width_pixels;
	unsigned int pstate_keepout_start_lines;
};

struct tg_wait_for_state_params {
	struct timing_generator *tg;
	enum crtc_state state;
};

struct tg_set_vtg_params_params {
	struct timing_generator *tg;
	struct dc_crtc_timing *timing;
	bool program_fp2;
};

struct tg_set_gsl_params {
	struct timing_generator *tg;
	struct gsl_params gsl;
};

struct tg_set_gsl_source_select_params {
	struct timing_generator *tg;
	int group_idx;
	uint32_t gsl_ready_signal;
};

struct setup_vupdate_interrupt_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct tg_setup_vertical_interrupt2_params {
	struct timing_generator *tg;
	int start_line;
};

struct dpp_set_hdr_multiplier_params {
	struct dpp *dpp;
	uint32_t hw_mult;
};

struct program_det_size_params {
	struct hubbub *hubbub;
	unsigned int hubp_inst;
	unsigned int det_buffer_size_kb;
};

struct program_det_segments_params {
	struct hubbub *hubbub;
	unsigned int hubp_inst;
	unsigned int det_size;
};

struct update_dchubp_dpp_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	struct dc_state *context;
};

struct opp_set_dyn_expansion_params {
	struct output_pixel_processor *opp;
	enum dc_color_space color_space;
	enum dc_color_depth color_depth;
	enum signal_type signal;
};

struct opp_program_fmt_params {
	struct output_pixel_processor *opp;
	struct bit_depth_reduction_params *fmt_bit_depth;
	struct clamping_and_pixel_encoding_params *clamping;
};

struct opp_program_bit_depth_reduction_params {
	struct output_pixel_processor *opp;
	bool use_default_params;
	struct pipe_ctx *pipe_ctx;
};

struct opp_set_disp_pattern_generator_params {
	struct output_pixel_processor *opp;
	enum controller_dp_test_pattern test_pattern;
	enum controller_dp_color_space color_space;
	enum dc_color_depth color_depth;
	struct tg_color solid_color;
	bool use_solid_color;
	int width;
	int height;
	int offset;
};

struct set_abm_pipe_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct set_abm_level_params {
	struct abm *abm;
	unsigned int abm_level;
};

struct set_abm_immediate_disable_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct set_disp_pattern_generator_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	enum controller_dp_test_pattern test_pattern;
	enum controller_dp_color_space color_space;
	enum dc_color_depth color_depth;
	const struct tg_color *solid_color;
	int width;
	int height;
	int offset;
};

struct mpc_update_blending_params {
	struct mpc *mpc;
	struct mpcc_blnd_cfg blnd_cfg;
	int mpcc_id;
};

struct mpc_assert_idle_mpcc_params {
	struct mpc *mpc;
	int mpcc_id;
};

struct mpc_insert_plane_params {
	struct mpc *mpc;
	struct mpc_tree *mpc_tree_params;
	struct mpcc_blnd_cfg blnd_cfg;
	struct mpcc_sm_cfg *sm_cfg;
	struct mpcc *insert_above_mpcc;
	int dpp_id;
	int mpcc_id;
};

struct mpc_remove_mpcc_params {
	struct mpc *mpc;
	struct mpc_tree *mpc_tree_params;
	struct mpcc *mpcc_to_remove;
};

struct opp_set_mpcc_disconnect_pending_params {
	struct output_pixel_processor *opp;
	int mpcc_inst;
	bool pending;
};

struct dc_set_optimized_required_params {
	struct dc *dc;
	bool optimized_required;
};

struct hubp_disconnect_params {
	struct hubp *hubp;
};

struct hubbub_force_pstate_change_control_params {
	struct hubbub *hubbub;
	bool enable;
	bool wait;
};

struct tg_enable_crtc_params {
	struct timing_generator *tg;
};

struct hubp_wait_flip_pending_params {
	struct hubp *hubp;
	unsigned int timeout_us;
	unsigned int polling_interval_us;
};

struct tg_wait_double_buffer_pending_params {
	struct timing_generator *tg;
	unsigned int timeout_us;
	unsigned int polling_interval_us;
};

struct update_force_pstate_params {
	struct dc *dc;
	struct dc_state *context;
};

struct hubbub_apply_dedcn21_147_wa_params {
	struct hubbub *hubbub;
};

struct hubbub_allow_self_refresh_control_params {
	struct hubbub *hubbub;
	bool allow;
	bool *disallow_self_refresh_applied;
};

struct tg_get_frame_count_params {
	struct timing_generator *tg;
	unsigned int *frame_count;
};

struct mpc_set_dwb_mux_params {
	struct mpc *mpc;
	int dwb_id;
	int mpcc_id;
};

struct mpc_disable_dwb_mux_params {
	struct mpc *mpc;
	unsigned int dwb_id;
};

struct mcif_wb_config_buf_params {
	struct mcif_wb *mcif_wb;
	struct mcif_buf_params *mcif_buf_params;
	unsigned int dest_height;
};

struct mcif_wb_config_arb_params {
	struct mcif_wb *mcif_wb;
	struct mcif_arb_params *mcif_arb_params;
};

struct mcif_wb_enable_params {
	struct mcif_wb *mcif_wb;
};

struct mcif_wb_disable_params {
	struct mcif_wb *mcif_wb;
};

struct dwbc_enable_params {
	struct dwbc *dwb;
	struct dc_dwb_params *dwb_params;
};

struct dwbc_disable_params {
	struct dwbc *dwb;
};

struct dwbc_update_params {
	struct dwbc *dwb;
	struct dc_dwb_params *dwb_params;
};

struct hubp_update_mall_sel_params {
	struct hubp *hubp;
	uint32_t mall_sel;
	bool cache_cursor;
};

struct hubp_prepare_subvp_buffering_params {
	struct hubp *hubp;
	bool enable;
};

struct hubp_set_blank_en_params {
	struct hubp *hubp;
	bool enable;
};

struct hubp_disable_control_params {
	struct hubp *hubp;
	bool disable;
};

struct hubbub_soft_reset_params {
	struct hubbub *hubbub;
	void (*hubbub_soft_reset)(struct hubbub *hubbub, bool reset);
	bool reset;
};

struct hubp_clk_cntl_params {
	struct hubp *hubp;
	bool enable;
};

struct hubp_init_params {
	struct hubp *hubp;
};

struct hubp_set_vm_system_aperture_settings_params {
	struct hubp *hubp;
	//struct vm_system_aperture_param apt;
	PHYSICAL_ADDRESS_LOC sys_default;
	PHYSICAL_ADDRESS_LOC sys_low;
	PHYSICAL_ADDRESS_LOC sys_high;
};

struct hubp_set_flip_int_params {
	struct hubp *hubp;
};

struct dpp_dppclk_control_params {
	struct dpp *dpp;
	bool dppclk_div;
	bool enable;
};

struct disable_phantom_crtc_params {
	struct timing_generator *tg;
};

struct dpp_pg_control_params {
	struct dce_hwseq *hws;
	unsigned int dpp_inst;
	bool power_on;
};

struct hubp_pg_control_params {
	struct dce_hwseq *hws;
	unsigned int hubp_inst;
	bool power_on;
};

struct hubp_reset_params {
	struct hubp *hubp;
};

struct dpp_reset_params {
	struct dpp *dpp;
};

struct dpp_root_clock_control_params {
	struct dce_hwseq *hws;
	unsigned int dpp_inst;
	bool clock_on;
};

struct dc_ip_request_cntl_params {
	struct dc *dc;
	bool enable;
};

struct dsc_pg_status_params {
	struct dce_hwseq *hws;
	int dsc_inst;
	bool is_ungated;
};

struct dsc_wait_disconnect_pending_clear_params {
	struct display_stream_compressor *dsc;
	bool *is_ungated;
};

struct dsc_disable_params {
	struct display_stream_compressor *dsc;
	bool *is_ungated;
};

struct dccg_set_ref_dscclk_params {
	struct dccg *dccg;
	int dsc_inst;
	bool *is_ungated;
};

struct dccg_update_dpp_dto_params {
	struct dccg *dccg;
	int dpp_inst;
	int dppclk_khz;
};

struct hubp_vtg_sel_params {
	struct hubp *hubp;
	uint32_t otg_inst;
};

struct hubp_setup2_params {
	struct hubp *hubp;
	struct dml2_dchub_per_pipe_register_set *hubp_regs;
	union dml2_global_sync_programming *global_sync;
	struct dc_crtc_timing *timing;
};

struct hubp_setup_params {
	struct hubp *hubp;
	struct _vcs_dpi_display_dlg_regs_st *dlg_regs;
	struct _vcs_dpi_display_ttu_regs_st *ttu_regs;
	struct _vcs_dpi_display_rq_regs_st *rq_regs;
	struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest;
};

struct hubp_set_unbounded_requesting_params {
	struct hubp *hubp;
	bool unbounded_req;
};

struct hubp_setup_interdependent2_params {
	struct hubp *hubp;
	struct dml2_dchub_per_pipe_register_set *hubp_regs;
};

struct hubp_setup_interdependent_params {
	struct hubp *hubp;
	struct _vcs_dpi_display_dlg_regs_st *dlg_regs;
	struct _vcs_dpi_display_ttu_regs_st *ttu_regs;
};

struct dpp_set_cursor_matrix_params {
	struct dpp *dpp;
	enum dc_color_space color_space;
	struct dc_csc_transform *cursor_csc_color_matrix;
};

struct mpc_update_mpcc_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct dpp_set_scaler_params {
	struct dpp *dpp;
	const struct scaler_data *scl_data;
};

struct hubp_mem_program_viewport_params {
	struct hubp *hubp;
	const struct rect *viewport;
	const struct rect *viewport_c;
};

struct hubp_program_mcache_id_and_split_coordinate_params {
	struct hubp *hubp;
	struct mcache_regs_struct *mcache_regs;
};

struct abort_cursor_offload_update_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct set_cursor_attribute_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct set_cursor_position_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct set_cursor_sdr_white_level_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
};

struct program_output_csc_params {
	struct dc *dc;
	struct pipe_ctx *pipe_ctx;
	enum dc_color_space colorspace;
	uint16_t *matrix;
	int opp_id;
};

struct hubp_set_blank_params {
	struct hubp *hubp;
	bool blank;
};

struct phantom_hubp_post_enable_params {
	struct hubp *hubp;
};

union block_sequence_params {
	struct update_plane_addr_params update_plane_addr_params;
	struct subvp_pipe_control_lock_fast_params subvp_pipe_control_lock_fast_params;
	struct pipe_control_lock_params pipe_control_lock_params;
	struct set_flip_control_gsl_params set_flip_control_gsl_params;
	struct program_triplebuffer_params program_triplebuffer_params;
	struct set_input_transfer_func_params set_input_transfer_func_params;
	struct program_gamut_remap_params program_gamut_remap_params;
	struct program_manual_trigger_params program_manual_trigger_params;
	struct send_dmcub_cmd_params send_dmcub_cmd_params;
	struct setup_dpp_params setup_dpp_params;
	struct program_bias_and_scale_params program_bias_and_scale_params;
	struct set_output_transfer_func_params set_output_transfer_func_params;
	struct update_visual_confirm_params update_visual_confirm_params;
	struct power_on_mpc_mem_pwr_params power_on_mpc_mem_pwr_params;
	struct set_output_csc_params set_output_csc_params;
	struct set_ocsc_default_params set_ocsc_default_params;
	struct subvp_save_surf_addr subvp_save_surf_addr;
	struct wait_for_dcc_meta_propagation_params wait_for_dcc_meta_propagation_params;
	struct dmub_hw_control_lock_fast_params dmub_hw_control_lock_fast_params;
	struct program_surface_config_params program_surface_config_params;
	struct program_mcache_id_and_split_coordinate program_mcache_id_and_split_coordinate;
	struct program_cursor_update_now_params program_cursor_update_now_params;
	struct hubp_wait_pipe_read_start_params hubp_wait_pipe_read_start_params;
	struct apply_update_flags_for_phantom_params apply_update_flags_for_phantom_params;
	struct update_phantom_vp_position_params update_phantom_vp_position_params;
	struct set_odm_combine_params set_odm_combine_params;
	struct set_odm_bypass_params set_odm_bypass_params;
	struct opp_pipe_clock_control_params opp_pipe_clock_control_params;
	struct opp_program_left_edge_extra_pixel_params opp_program_left_edge_extra_pixel_params;
	struct dccg_set_dto_dscclk_params dccg_set_dto_dscclk_params;
	struct dsc_set_config_params dsc_set_config_params;
	struct dsc_enable_params dsc_enable_params;
	struct tg_set_dsc_config_params tg_set_dsc_config_params;
	struct dsc_disconnect_params dsc_disconnect_params;
	struct dsc_read_state_params dsc_read_state_params;
	struct dsc_calculate_and_set_config_params dsc_calculate_and_set_config_params;
	struct dsc_enable_with_opp_params dsc_enable_with_opp_params;
	struct program_tg_params program_tg_params;
	struct tg_program_global_sync_params tg_program_global_sync_params;
	struct tg_wait_for_state_params tg_wait_for_state_params;
	struct tg_set_vtg_params_params tg_set_vtg_params_params;
	struct tg_setup_vertical_interrupt2_params tg_setup_vertical_interrupt2_params;
	struct dpp_set_hdr_multiplier_params dpp_set_hdr_multiplier_params;
	struct tg_set_gsl_params tg_set_gsl_params;
	struct tg_set_gsl_source_select_params tg_set_gsl_source_select_params;
	struct setup_vupdate_interrupt_params setup_vupdate_interrupt_params;
	struct program_det_size_params program_det_size_params;
	struct program_det_segments_params program_det_segments_params;
	struct update_dchubp_dpp_params update_dchubp_dpp_params;
	struct opp_set_dyn_expansion_params opp_set_dyn_expansion_params;
	struct opp_program_fmt_params opp_program_fmt_params;
	struct opp_program_bit_depth_reduction_params opp_program_bit_depth_reduction_params;
	struct opp_set_disp_pattern_generator_params opp_set_disp_pattern_generator_params;
	struct set_abm_pipe_params set_abm_pipe_params;
	struct set_abm_level_params set_abm_level_params;
	struct set_abm_immediate_disable_params set_abm_immediate_disable_params;
	struct set_disp_pattern_generator_params set_disp_pattern_generator_params;
	struct mpc_remove_mpcc_params mpc_remove_mpcc_params;
	struct opp_set_mpcc_disconnect_pending_params opp_set_mpcc_disconnect_pending_params;
	struct dc_set_optimized_required_params dc_set_optimized_required_params;
	struct hubp_disconnect_params hubp_disconnect_params;
	struct hubbub_force_pstate_change_control_params hubbub_force_pstate_change_control_params;
	struct tg_enable_crtc_params tg_enable_crtc_params;
	struct hubp_wait_flip_pending_params hubp_wait_flip_pending_params;
	struct tg_wait_double_buffer_pending_params tg_wait_double_buffer_pending_params;
	struct update_force_pstate_params update_force_pstate_params;
	struct hubbub_apply_dedcn21_147_wa_params hubbub_apply_dedcn21_147_wa_params;
	struct hubbub_allow_self_refresh_control_params hubbub_allow_self_refresh_control_params;
	struct tg_get_frame_count_params tg_get_frame_count_params;
	struct mpc_set_dwb_mux_params mpc_set_dwb_mux_params;
	struct mpc_disable_dwb_mux_params mpc_disable_dwb_mux_params;
	struct mcif_wb_config_buf_params mcif_wb_config_buf_params;
	struct mcif_wb_config_arb_params mcif_wb_config_arb_params;
	struct mcif_wb_enable_params mcif_wb_enable_params;
	struct mcif_wb_disable_params mcif_wb_disable_params;
	struct dwbc_enable_params dwbc_enable_params;
	struct dwbc_disable_params dwbc_disable_params;
	struct dwbc_update_params dwbc_update_params;
	struct hubp_update_mall_sel_params hubp_update_mall_sel_params;
	struct hubp_prepare_subvp_buffering_params hubp_prepare_subvp_buffering_params;
	struct hubp_set_blank_en_params hubp_set_blank_en_params;
	struct hubp_disable_control_params hubp_disable_control_params;
	struct hubbub_soft_reset_params hubbub_soft_reset_params;
	struct hubp_clk_cntl_params hubp_clk_cntl_params;
	struct hubp_init_params hubp_init_params;
	struct hubp_set_vm_system_aperture_settings_params hubp_set_vm_system_aperture_settings_params;
	struct hubp_set_flip_int_params hubp_set_flip_int_params;
	struct dpp_dppclk_control_params dpp_dppclk_control_params;
	struct disable_phantom_crtc_params disable_phantom_crtc_params;
	struct dpp_pg_control_params dpp_pg_control_params;
	struct hubp_pg_control_params hubp_pg_control_params;
	struct hubp_reset_params hubp_reset_params;
	struct dpp_reset_params dpp_reset_params;
	struct dpp_root_clock_control_params dpp_root_clock_control_params;
	struct dc_ip_request_cntl_params dc_ip_request_cntl_params;
	struct dsc_pg_status_params dsc_pg_status_params;
	struct dsc_wait_disconnect_pending_clear_params dsc_wait_disconnect_pending_clear_params;
	struct dsc_disable_params dsc_disable_params;
	struct dccg_set_ref_dscclk_params dccg_set_ref_dscclk_params;
	struct dccg_update_dpp_dto_params dccg_update_dpp_dto_params;
	struct hubp_vtg_sel_params hubp_vtg_sel_params;
	struct hubp_setup2_params hubp_setup2_params;
	struct hubp_setup_params hubp_setup_params;
	struct hubp_set_unbounded_requesting_params hubp_set_unbounded_requesting_params;
	struct hubp_setup_interdependent2_params hubp_setup_interdependent2_params;
	struct hubp_setup_interdependent_params hubp_setup_interdependent_params;
	struct dpp_set_cursor_matrix_params dpp_set_cursor_matrix_params;
	struct mpc_update_mpcc_params mpc_update_mpcc_params;
	struct mpc_update_blending_params mpc_update_blending_params;
	struct mpc_assert_idle_mpcc_params mpc_assert_idle_mpcc_params;
	struct mpc_insert_plane_params mpc_insert_plane_params;
	struct dpp_set_scaler_params dpp_set_scaler_params;
	struct hubp_mem_program_viewport_params hubp_mem_program_viewport_params;
	struct abort_cursor_offload_update_params abort_cursor_offload_update_params;
	struct set_cursor_attribute_params set_cursor_attribute_params;
	struct set_cursor_position_params set_cursor_position_params;
	struct set_cursor_sdr_white_level_params set_cursor_sdr_white_level_params;
	struct program_output_csc_params program_output_csc_params;
	struct hubp_set_blank_params hubp_set_blank_params;
	struct phantom_hubp_post_enable_params phantom_hubp_post_enable_params;
};

enum block_sequence_func {
	DMUB_SUBVP_PIPE_CONTROL_LOCK_FAST = 0,
	OPTC_PIPE_CONTROL_LOCK,
	HUBP_SET_FLIP_CONTROL_GSL,
	HUBP_PROGRAM_TRIPLEBUFFER,
	HUBP_UPDATE_PLANE_ADDR,
	DPP_SET_INPUT_TRANSFER_FUNC,
	DPP_PROGRAM_GAMUT_REMAP,
	OPTC_PROGRAM_MANUAL_TRIGGER,
	DMUB_SEND_DMCUB_CMD,
	DPP_SETUP_DPP,
	DPP_PROGRAM_BIAS_AND_SCALE,
	DPP_SET_OUTPUT_TRANSFER_FUNC,
	DPP_SET_HDR_MULTIPLIER,
	MPC_UPDATE_VISUAL_CONFIRM,
	MPC_POWER_ON_MPC_MEM_PWR,
	MPC_SET_OUTPUT_CSC,
	MPC_SET_OCSC_DEFAULT,
	DMUB_SUBVP_SAVE_SURF_ADDR,
	HUBP_WAIT_FOR_DCC_META_PROP,
	DMUB_HW_CONTROL_LOCK_FAST,
	HUBP_PROGRAM_SURFACE_CONFIG,
	HUBP_PROGRAM_MCACHE_ID,
	PROGRAM_CURSOR_UPDATE_NOW,
	HUBP_WAIT_PIPE_READ_START,
	HWS_APPLY_UPDATE_FLAGS_FOR_PHANTOM,
	HWS_UPDATE_PHANTOM_VP_POSITION,
	OPTC_SET_ODM_COMBINE,
	OPTC_SET_ODM_BYPASS,
	OPP_PIPE_CLOCK_CONTROL,
	OPP_PROGRAM_LEFT_EDGE_EXTRA_PIXEL,
	DCCG_SET_DTO_DSCCLK,
	DSC_SET_CONFIG,
	DSC_ENABLE,
	TG_SET_DSC_CONFIG,
	DSC_DISCONNECT,
	DSC_READ_STATE,
	DSC_CALCULATE_AND_SET_CONFIG,
	DSC_ENABLE_WITH_OPP,
	TG_PROGRAM_GLOBAL_SYNC,
	TG_WAIT_FOR_STATE,
	TG_SET_VTG_PARAMS,
	TG_SETUP_VERTICAL_INTERRUPT2,
	HUBP_PROGRAM_DET_SIZE,
	HUBP_PROGRAM_DET_SEGMENTS,
	OPP_SET_DYN_EXPANSION,
	OPP_PROGRAM_FMT,
	OPP_PROGRAM_BIT_DEPTH_REDUCTION,
	OPP_SET_DISP_PATTERN_GENERATOR,
	ABM_SET_PIPE,
	ABM_SET_LEVEL,
	ABM_SET_IMMEDIATE_DISABLE,
	MPC_REMOVE_MPCC,
	OPP_SET_MPCC_DISCONNECT_PENDING,
	DC_SET_OPTIMIZED_REQUIRED,
	HUBP_DISCONNECT,
	HUBBUB_FORCE_PSTATE_CHANGE_CONTROL,
	TG_ENABLE_CRTC,
	TG_SET_GSL,
	TG_SET_GSL_SOURCE_SELECT,
	HUBP_WAIT_FLIP_PENDING,
	TG_WAIT_DOUBLE_BUFFER_PENDING,
	UPDATE_FORCE_PSTATE,
	PROGRAM_MALL_PIPE_CONFIG,
	HUBBUB_APPLY_DEDCN21_147_WA,
	HUBBUB_ALLOW_SELF_REFRESH_CONTROL,
	TG_GET_FRAME_COUNT,
	MPC_SET_DWB_MUX,
	MPC_DISABLE_DWB_MUX,
	MCIF_WB_CONFIG_BUF,
	MCIF_WB_CONFIG_ARB,
	MCIF_WB_ENABLE,
	MCIF_WB_DISABLE,
	DWBC_ENABLE,
	DWBC_DISABLE,
	DWBC_UPDATE,
	HUBP_UPDATE_MALL_SEL,
	HUBP_PREPARE_SUBVP_BUFFERING,
	HUBP_SET_BLANK_EN,
	HUBP_DISABLE_CONTROL,
	HUBBUB_SOFT_RESET,
	HUBP_CLK_CNTL,
	HUBP_INIT,
	HUBP_SET_VM_SYSTEM_APERTURE_SETTINGS,
	HUBP_SET_FLIP_INT,
	DPP_DPPCLK_CONTROL,
	DISABLE_PHANTOM_CRTC,
	DSC_PG_STATUS,
	DSC_WAIT_DISCONNECT_PENDING_CLEAR,
	DSC_DISABLE,
	DCCG_SET_REF_DSCCLK,
	DPP_PG_CONTROL,
	HUBP_PG_CONTROL,
	HUBP_RESET,
	DPP_RESET,
	DPP_ROOT_CLOCK_CONTROL,
	DC_IP_REQUEST_CNTL,
	DCCG_UPDATE_DPP_DTO,
	HUBP_VTG_SEL,
	HUBP_SETUP2,
	HUBP_SETUP,
	HUBP_SET_UNBOUNDED_REQUESTING,
	HUBP_SETUP_INTERDEPENDENT2,
	HUBP_SETUP_INTERDEPENDENT,
	DPP_SET_CURSOR_MATRIX,
	MPC_UPDATE_BLENDING,
	MPC_ASSERT_IDLE_MPCC,
	MPC_INSERT_PLANE,
	DPP_SET_SCALER,
	HUBP_MEM_PROGRAM_VIEWPORT,
	ABORT_CURSOR_OFFLOAD_UPDATE,
	SET_CURSOR_ATTRIBUTE,
	SET_CURSOR_POSITION,
	SET_CURSOR_SDR_WHITE_LEVEL,
	PROGRAM_OUTPUT_CSC,
	HUBP_SET_LEGACY_TILING_COMPAT_LEVEL,
	HUBP_SET_BLANK,
	PHANTOM_HUBP_POST_ENABLE,
	/* This must be the last value in this enum, add new ones above */
	HWSS_BLOCK_SEQUENCE_FUNC_COUNT
};

struct block_sequence {
	union block_sequence_params params;
	enum block_sequence_func func;
};

struct block_sequence_state {
	struct block_sequence *steps;
	unsigned int *num_steps;
};

#define MAX_HWSS_BLOCK_SEQUENCE_SIZE (HWSS_BLOCK_SEQUENCE_FUNC_COUNT * MAX_PIPES)

struct hw_sequencer_funcs {
	void (*hardware_release)(struct dc *dc);
	/* Embedded Display Related */
	void (*edp_power_control)(struct dc_link *link, bool enable);
	void (*edp_wait_for_hpd_ready)(struct dc_link *link, bool power_up);
	void (*edp_wait_for_T12)(struct dc_link *link);

	/* Pipe Programming Related */
	void (*init_hw)(struct dc *dc);
	void (*power_down_on_boot)(struct dc *dc);
	void (*enable_accelerated_mode)(struct dc *dc,
			struct dc_state *context);
	enum dc_status (*apply_ctx_to_hw)(struct dc *dc,
			struct dc_state *context);
	void (*disable_plane)(struct dc *dc, struct dc_state *state, struct pipe_ctx *pipe_ctx);
	void (*disable_plane_sequence)(struct dc *dc, struct dc_state *state, struct pipe_ctx *pipe_ctx,
		struct block_sequence_state *seq_state);
	void (*disable_pixel_data)(struct dc *dc, struct pipe_ctx *pipe_ctx, bool blank);
	void (*apply_ctx_for_surface)(struct dc *dc,
			const struct dc_stream_state *stream,
			int num_planes, struct dc_state *context);
	void (*program_front_end_for_ctx)(struct dc *dc,
			struct dc_state *context);
	void (*wait_for_pending_cleared)(struct dc *dc,
			struct dc_state *context);
	void (*post_unlock_program_front_end)(struct dc *dc,
			struct dc_state *context);
	void (*update_plane_addr)(const struct dc *dc,
			struct pipe_ctx *pipe_ctx);
	void (*update_dchub)(struct dce_hwseq *hws,
			struct dchub_init_data *dh_data);
	void (*wait_for_mpcc_disconnect)(struct dc *dc,
			struct resource_pool *res_pool,
			struct pipe_ctx *pipe_ctx);
	void (*wait_for_mpcc_disconnect_sequence)(struct dc *dc,
			struct resource_pool *res_pool,
			struct pipe_ctx *pipe_ctx,
			struct block_sequence_state *seq_state);
	void (*edp_backlight_control)(
			struct dc_link *link,
			bool enable);
	void (*program_triplebuffer)(const struct dc *dc,
		struct pipe_ctx *pipe_ctx, bool enableTripleBuffer);
	void (*update_pending_status)(struct pipe_ctx *pipe_ctx);
	void (*update_dsc_pg)(struct dc *dc, struct dc_state *context, bool safe_to_disable);
	void (*clear_surface_dcc_and_tiling)(struct pipe_ctx *pipe_ctx, struct dc_plane_state *plane_state, bool clear_tiling);

	/* Pipe Lock Related */
	void (*pipe_control_lock)(struct dc *dc,
			struct pipe_ctx *pipe, bool lock);
	void (*interdependent_update_lock)(struct dc *dc,
			struct dc_state *context, bool lock);
	void (*set_flip_control_gsl)(struct pipe_ctx *pipe_ctx,
			bool flip_immediate);
	void (*cursor_lock)(struct dc *dc, struct pipe_ctx *pipe, bool lock);

	/* Timing Related */
	void (*get_position)(struct pipe_ctx **pipe_ctx, int num_pipes,
			struct crtc_position *position);
	int (*get_vupdate_offset_from_vsync)(struct pipe_ctx *pipe_ctx);
	void (*calc_vupdate_position)(
			struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			uint32_t *start_line,
			uint32_t *end_line);
	void (*enable_per_frame_crtc_position_reset)(struct dc *dc,
			int group_size, struct pipe_ctx *grouped_pipes[]);
	void (*enable_timing_synchronization)(struct dc *dc,
			struct dc_state *state,
			int group_index, int group_size,
			struct pipe_ctx *grouped_pipes[]);
	void (*enable_vblanks_synchronization)(struct dc *dc,
			int group_index, int group_size,
			struct pipe_ctx *grouped_pipes[]);
	void (*setup_periodic_interrupt)(struct dc *dc,
			struct pipe_ctx *pipe_ctx);
	void (*set_drr)(struct pipe_ctx **pipe_ctx, int num_pipes,
			struct dc_crtc_timing_adjust adjust);
	void (*set_static_screen_control)(struct pipe_ctx **pipe_ctx,
			int num_pipes,
			const struct dc_static_screen_params *events);

	/* Stream Related */
	void (*enable_stream)(struct pipe_ctx *pipe_ctx);
	void (*disable_stream)(struct pipe_ctx *pipe_ctx);
	void (*blank_stream)(struct pipe_ctx *pipe_ctx);
	void (*unblank_stream)(struct pipe_ctx *pipe_ctx,
			struct dc_link_settings *link_settings);

	/* Bandwidth Related */
	void (*prepare_bandwidth)(struct dc *dc, struct dc_state *context);
	bool (*update_bandwidth)(struct dc *dc, struct dc_state *context);
	void (*optimize_bandwidth)(struct dc *dc, struct dc_state *context);

	/* Infopacket Related */
	void (*set_avmute)(struct pipe_ctx *pipe_ctx, bool enable);
	void (*send_immediate_sdp_message)(
			struct pipe_ctx *pipe_ctx,
			const uint8_t *custom_sdp_message,
			unsigned int sdp_message_size);
	void (*update_info_frame)(struct pipe_ctx *pipe_ctx);
	void (*set_dmdata_attributes)(struct pipe_ctx *pipe);
	void (*program_dmdata_engine)(struct pipe_ctx *pipe_ctx);
	bool (*dmdata_status_done)(struct pipe_ctx *pipe_ctx);

	/* Cursor Related */
	void (*set_cursor_position)(struct pipe_ctx *pipe);
	void (*set_cursor_attribute)(struct pipe_ctx *pipe);
	void (*set_cursor_sdr_white_level)(struct pipe_ctx *pipe);
	void (*abort_cursor_offload_update)(struct dc *dc, const struct pipe_ctx *pipe);
	void (*begin_cursor_offload_update)(struct dc *dc, const struct pipe_ctx *pipe);
	void (*commit_cursor_offload_update)(struct dc *dc, const struct pipe_ctx *pipe);
	void (*update_cursor_offload_pipe)(struct dc *dc, const struct pipe_ctx *pipe);
	void (*notify_cursor_offload_drr_update)(struct dc *dc, struct dc_state *context,
						 const struct dc_stream_state *stream);
	void (*program_cursor_offload_now)(struct dc *dc, const struct pipe_ctx *pipe);

	/* Colour Related */
	void (*program_gamut_remap)(struct pipe_ctx *pipe_ctx);
	void (*program_output_csc)(struct dc *dc, struct pipe_ctx *pipe_ctx,
			enum dc_color_space colorspace,
			uint16_t *matrix, int opp_id);
	void (*trigger_3dlut_dma_load)(struct dc *dc, struct pipe_ctx *pipe_ctx);

	/* VM Related */
	int (*init_sys_ctx)(struct dce_hwseq *hws,
			struct dc *dc,
			struct dc_phy_addr_space_config *pa_config);
	void (*init_vm_ctx)(struct dce_hwseq *hws,
			struct dc *dc,
			struct dc_virtual_addr_space_config *va_config,
			int vmid);

	/* Writeback Related */
	void (*update_writeback)(struct dc *dc,
			struct dc_writeback_info *wb_info,
			struct dc_state *context);
	void (*enable_writeback)(struct dc *dc,
			struct dc_writeback_info *wb_info,
			struct dc_state *context);
	void (*disable_writeback)(struct dc *dc,
			unsigned int dwb_pipe_inst);

	/* Clock Related */
	enum dc_status (*set_clock)(struct dc *dc,
			enum dc_clock_type clock_type,
			uint32_t clk_khz, uint32_t stepping);
	void (*get_clock)(struct dc *dc, enum dc_clock_type clock_type,
			struct dc_clock_config *clock_cfg);
	void (*optimize_pwr_state)(const struct dc *dc,
			struct dc_state *context);
	void (*exit_optimized_pwr_state)(const struct dc *dc,
			struct dc_state *context);
	void (*calculate_pix_rate_divider)(struct dc *dc,
			struct dc_state *context,
			const struct dc_stream_state *stream);

	/* Audio Related */
	void (*enable_audio_stream)(struct pipe_ctx *pipe_ctx);
	void (*disable_audio_stream)(struct pipe_ctx *pipe_ctx);

	/* Stereo 3D Related */
	void (*setup_stereo)(struct pipe_ctx *pipe_ctx, struct dc *dc);

	/* HW State Logging Related */
	void (*log_hw_state)(struct dc *dc, struct dc_log_buffer_ctx *log_ctx);
	void (*log_color_state)(struct dc *dc,
				struct dc_log_buffer_ctx *log_ctx);
	void (*get_hw_state)(struct dc *dc, char *pBuf,
			unsigned int bufSize, unsigned int mask);
	void (*clear_status_bits)(struct dc *dc, unsigned int mask);

	bool (*set_backlight_level)(struct pipe_ctx *pipe_ctx,
		struct set_backlight_level_params *params);

	void (*set_abm_immediate_disable)(struct pipe_ctx *pipe_ctx);

	void (*set_pipe)(struct pipe_ctx *pipe_ctx);

	void (*enable_dp_link_output)(struct dc_link *link,
			const struct link_resource *link_res,
			enum signal_type signal,
			enum clock_source_id clock_source,
			const struct dc_link_settings *link_settings);
	void (*enable_tmds_link_output)(struct dc_link *link,
			const struct link_resource *link_res,
			enum signal_type signal,
			enum clock_source_id clock_source,
			enum dc_color_depth color_depth,
			uint32_t pixel_clock);
	void (*enable_lvds_link_output)(struct dc_link *link,
			const struct link_resource *link_res,
			enum clock_source_id clock_source,
			uint32_t pixel_clock);
	void (*disable_link_output)(struct dc_link *link,
			const struct link_resource *link_res,
			enum signal_type signal);

	void (*get_dcc_en_bits)(struct dc *dc, int *dcc_en_bits);

	/* Idle Optimization Related */
	bool (*apply_idle_power_optimizations)(struct dc *dc, bool enable);

	bool (*does_plane_fit_in_mall)(struct dc *dc,
			unsigned int pitch,
			unsigned int height,
			enum surface_pixel_format format,
			struct dc_cursor_attributes *cursor_attr);
	void (*commit_subvp_config)(struct dc *dc, struct dc_state *context);
	void (*enable_phantom_streams)(struct dc *dc, struct dc_state *context);
	void (*disable_phantom_streams)(struct dc *dc, struct dc_state *context);
	void (*subvp_pipe_control_lock)(struct dc *dc,
			struct dc_state *context,
			bool lock,
			bool should_lock_all_pipes,
			struct pipe_ctx *top_pipe_to_program,
			bool subvp_prev_use);
	void (*subvp_pipe_control_lock_fast)(union block_sequence_params *params);

	void (*z10_restore)(const struct dc *dc);
	void (*z10_save_init)(struct dc *dc);
	bool (*is_abm_supported)(struct dc *dc,
			struct dc_state *context, struct dc_stream_state *stream);

	void (*set_disp_pattern_generator)(const struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			enum controller_dp_test_pattern test_pattern,
			enum controller_dp_color_space color_space,
			enum dc_color_depth color_depth,
			const struct tg_color *solid_color,
			int width, int height, int offset);
	void (*blank_phantom)(struct dc *dc,
			struct timing_generator *tg,
			int width,
			int height);
	void (*update_visual_confirm_color)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			int mpcc_id);
	void (*update_phantom_vp_position)(struct dc *dc,
			struct dc_state *context,
			struct pipe_ctx *phantom_pipe);
	void (*apply_update_flags_for_phantom)(struct pipe_ctx *phantom_pipe);

	void (*calc_blocks_to_gate)(struct dc *dc, struct dc_state *context,
		struct pg_block_update *update_state);
	void (*calc_blocks_to_ungate)(struct dc *dc, struct dc_state *context,
		struct pg_block_update *update_state);
	void (*hw_block_power_up)(struct dc *dc,
		struct pg_block_update *update_state);
	void (*hw_block_power_down)(struct dc *dc,
		struct pg_block_update *update_state);
	void (*root_clock_control)(struct dc *dc,
		struct pg_block_update *update_state, bool power_on);
	bool (*is_pipe_topology_transition_seamless)(struct dc *dc,
			const struct dc_state *cur_ctx,
			const struct dc_state *new_ctx);
	void (*wait_for_dcc_meta_propagation)(const struct dc *dc,
		const struct pipe_ctx *top_pipe_to_program);
	void (*dmub_hw_control_lock)(struct dc *dc,
			struct dc_state *context,
			bool lock);
	void (*fams2_update_config)(struct dc *dc,
			struct dc_state *context,
			bool enable);
	void (*dmub_hw_control_lock_fast)(union block_sequence_params *params);
	void (*set_long_vtotal)(struct pipe_ctx **pipe_ctx, int num_pipes, uint32_t v_total_min, uint32_t v_total_max);
	void (*program_outstanding_updates)(struct dc *dc,
			struct dc_state *context);
	void (*setup_hpo_hw_control)(const struct dce_hwseq *hws, bool enable);
	void (*wait_for_all_pending_updates)(const struct pipe_ctx *pipe_ctx);
	void (*detect_pipe_changes)(struct dc_state *old_state,
			struct dc_state *new_state,
			struct pipe_ctx *old_pipe,
			struct pipe_ctx *new_pipe);
	void (*enable_plane)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			struct dc_state *context);
	void (*enable_plane_sequence)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			struct dc_state *context,
			struct block_sequence_state *seq_state);
	void (*update_dchubp_dpp)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			struct dc_state *context);
	void (*update_dchubp_dpp_sequence)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			struct dc_state *context,
			struct block_sequence_state *seq_state);
	void (*post_unlock_reset_opp)(struct dc *dc,
			struct pipe_ctx *opp_head);
	void (*post_unlock_reset_opp_sequence)(
			struct dc *dc,
			struct pipe_ctx *opp_head,
			struct block_sequence_state *seq_state);
	void (*get_underflow_debug_data)(const struct dc *dc,
			struct timing_generator *tg,
			struct dc_underflow_debug_data *out_data);

	/**
	 * measure_memory_qos - Measure memory QoS metrics
	 * @dc: DC structure
	 * @qos: Pointer to memory_qos struct to populate with measured values
	 *
	 * Populates the provided memory_qos struct with peak bandwidth, average bandwidth,
	 * max latency, min latency, and average latency from hardware performance counters.
	 */
	void (*measure_memory_qos)(struct dc *dc, struct memory_qos *qos);

};

void color_space_to_black_color(
	const struct dc *dc,
	enum dc_color_space colorspace,
	struct tg_color *black_color);

bool hwss_wait_for_blank_complete(
		struct timing_generator *tg);

const uint16_t *find_color_matrix(
		enum dc_color_space color_space,
		uint32_t *array_size);

void get_surface_tile_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);
void get_surface_visual_confirm_color(
		const struct pipe_ctx *pipe_ctx,
		struct tg_color *color);

void get_hdr_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);
void get_mpctree_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);
void get_smartmux_visual_confirm_color(
	struct dc *dc,
	struct tg_color *color);
void get_vabc_visual_confirm_color(
	struct pipe_ctx *pipe_ctx,
	struct tg_color *color);
void get_subvp_visual_confirm_color(
	struct pipe_ctx *pipe_ctx,
	struct tg_color *color);
void get_fams2_visual_confirm_color(
	struct dc *dc,
	struct dc_state *context,
	struct pipe_ctx *pipe_ctx,
	struct tg_color *color);

void get_mclk_switch_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);

void get_cursor_visual_confirm_color(
		struct pipe_ctx *pipe_ctx,
		struct tg_color *color);

void get_dcc_visual_confirm_color(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct tg_color *color);

void set_p_state_switch_method(
		struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx);

void set_drr_and_clear_adjust_pending(
		struct pipe_ctx *pipe_ctx,
		struct dc_stream_state *stream,
		struct drr_params *params);

void hwss_execute_sequence(struct dc *dc,
		struct block_sequence block_sequence[MAX_HWSS_BLOCK_SEQUENCE_SIZE],
		int num_steps);

void hwss_build_fast_sequence(struct dc *dc,
		struct dc_dmub_cmd *dc_dmub_cmd,
		unsigned int dmub_cmd_count,
		struct block_sequence block_sequence[MAX_HWSS_BLOCK_SEQUENCE_SIZE],
		unsigned int *num_steps,
		struct pipe_ctx *pipe_ctx,
		struct dc_stream_status *stream_status,
		struct dc_state *context);

void hwss_wait_for_all_blank_complete(struct dc *dc,
		struct dc_state *context);

void hwss_wait_for_odm_update_pending_complete(struct dc *dc,
		struct dc_state *context);

void hwss_wait_for_no_pipes_pending(struct dc *dc,
		struct dc_state *context);

void hwss_wait_for_outstanding_hw_updates(struct dc *dc,
		struct dc_state *dc_context);

void hwss_process_outstanding_hw_updates(struct dc *dc,
		struct dc_state *dc_context);

void hwss_send_dmcub_cmd(union block_sequence_params *params);

void hwss_program_manual_trigger(union block_sequence_params *params);

void hwss_setup_dpp(union block_sequence_params *params);

void hwss_program_bias_and_scale(union block_sequence_params *params);

void hwss_power_on_mpc_mem_pwr(union block_sequence_params *params);

void hwss_set_output_csc(union block_sequence_params *params);

void hwss_set_ocsc_default(union block_sequence_params *params);

void hwss_subvp_save_surf_addr(union block_sequence_params *params);

void hwss_program_surface_config(union block_sequence_params *params);

void hwss_program_mcache_id_and_split_coordinate(union block_sequence_params *params);

void hwss_set_odm_combine(union block_sequence_params *params);

void hwss_set_odm_bypass(union block_sequence_params *params);

void hwss_opp_pipe_clock_control(union block_sequence_params *params);

void hwss_opp_program_left_edge_extra_pixel(union block_sequence_params *params);

void hwss_blank_pixel_data(union block_sequence_params *params);

void hwss_dccg_set_dto_dscclk(union block_sequence_params *params);

void hwss_dsc_set_config(union block_sequence_params *params);

void hwss_dsc_enable(union block_sequence_params *params);

void hwss_tg_set_dsc_config(union block_sequence_params *params);

void hwss_dsc_disconnect(union block_sequence_params *params);

void hwss_dsc_read_state(union block_sequence_params *params);

void hwss_dsc_calculate_and_set_config(union block_sequence_params *params);

void hwss_dsc_enable_with_opp(union block_sequence_params *params);

void hwss_program_tg(union block_sequence_params *params);

void hwss_tg_program_global_sync(union block_sequence_params *params);

void hwss_tg_wait_for_state(union block_sequence_params *params);

void hwss_tg_set_vtg_params(union block_sequence_params *params);

void hwss_tg_setup_vertical_interrupt2(union block_sequence_params *params);

void hwss_dpp_set_hdr_multiplier(union block_sequence_params *params);

void hwss_program_det_size(union block_sequence_params *params);

void hwss_program_det_segments(union block_sequence_params *params);

void hwss_opp_set_dyn_expansion(union block_sequence_params *params);

void hwss_opp_program_fmt(union block_sequence_params *params);

void hwss_opp_program_bit_depth_reduction(union block_sequence_params *params);

void hwss_opp_set_disp_pattern_generator(union block_sequence_params *params);

void hwss_set_abm_pipe(union block_sequence_params *params);

void hwss_set_abm_level(union block_sequence_params *params);

void hwss_set_abm_immediate_disable(union block_sequence_params *params);

void hwss_mpc_remove_mpcc(union block_sequence_params *params);

void hwss_opp_set_mpcc_disconnect_pending(union block_sequence_params *params);

void hwss_dc_set_optimized_required(union block_sequence_params *params);

void hwss_hubp_disconnect(union block_sequence_params *params);

void hwss_hubbub_force_pstate_change_control(union block_sequence_params *params);

void hwss_tg_enable_crtc(union block_sequence_params *params);

void hwss_tg_set_gsl(union block_sequence_params *params);

void hwss_tg_set_gsl_source_select(union block_sequence_params *params);

void hwss_hubp_wait_flip_pending(union block_sequence_params *params);

void hwss_tg_wait_double_buffer_pending(union block_sequence_params *params);

void hwss_update_force_pstate(union block_sequence_params *params);

void hwss_hubbub_apply_dedcn21_147_wa(union block_sequence_params *params);

void hwss_hubbub_allow_self_refresh_control(union block_sequence_params *params);

void hwss_tg_get_frame_count(union block_sequence_params *params);

void hwss_mpc_set_dwb_mux(union block_sequence_params *params);

void hwss_mpc_disable_dwb_mux(union block_sequence_params *params);

void hwss_mcif_wb_config_buf(union block_sequence_params *params);

void hwss_mcif_wb_config_arb(union block_sequence_params *params);

void hwss_mcif_wb_enable(union block_sequence_params *params);

void hwss_mcif_wb_disable(union block_sequence_params *params);

void hwss_dwbc_enable(union block_sequence_params *params);

void hwss_dwbc_disable(union block_sequence_params *params);

void hwss_dwbc_update(union block_sequence_params *params);

void hwss_hubp_update_mall_sel(union block_sequence_params *params);

void hwss_hubp_prepare_subvp_buffering(union block_sequence_params *params);

void hwss_hubp_set_blank_en(union block_sequence_params *params);

void hwss_hubp_disable_control(union block_sequence_params *params);

void hwss_hubbub_soft_reset(union block_sequence_params *params);

void hwss_hubp_clk_cntl(union block_sequence_params *params);

void hwss_hubp_init(union block_sequence_params *params);

void hwss_hubp_set_vm_system_aperture_settings(union block_sequence_params *params);

void hwss_hubp_set_flip_int(union block_sequence_params *params);

void hwss_dpp_dppclk_control(union block_sequence_params *params);

void hwss_disable_phantom_crtc(union block_sequence_params *params);

void hwss_dsc_pg_status(union block_sequence_params *params);

void hwss_dsc_wait_disconnect_pending_clear(union block_sequence_params *params);

void hwss_dsc_disable(union block_sequence_params *params);

void hwss_dccg_set_ref_dscclk(union block_sequence_params *params);

void hwss_dpp_pg_control(union block_sequence_params *params);

void hwss_hubp_pg_control(union block_sequence_params *params);

void hwss_hubp_reset(union block_sequence_params *params);

void hwss_dpp_reset(union block_sequence_params *params);

void hwss_dpp_root_clock_control(union block_sequence_params *params);

void hwss_dc_ip_request_cntl(union block_sequence_params *params);

void hwss_dccg_update_dpp_dto(union block_sequence_params *params);

void hwss_hubp_vtg_sel(union block_sequence_params *params);

void hwss_hubp_setup2(union block_sequence_params *params);

void hwss_hubp_setup(union block_sequence_params *params);

void hwss_hubp_set_unbounded_requesting(union block_sequence_params *params);

void hwss_hubp_setup_interdependent2(union block_sequence_params *params);

void hwss_hubp_setup_interdependent(union block_sequence_params *params);

void hwss_dpp_set_cursor_matrix(union block_sequence_params *params);

void hwss_mpc_update_mpcc(union block_sequence_params *params);

void hwss_mpc_update_blending(union block_sequence_params *params);

void hwss_mpc_assert_idle_mpcc(union block_sequence_params *params);

void hwss_mpc_insert_plane(union block_sequence_params *params);

void hwss_dpp_set_scaler(union block_sequence_params *params);

void hwss_hubp_mem_program_viewport(union block_sequence_params *params);

void hwss_abort_cursor_offload_update(union block_sequence_params *params);

void hwss_set_cursor_attribute(union block_sequence_params *params);

void hwss_set_cursor_position(union block_sequence_params *params);

void hwss_set_cursor_sdr_white_level(union block_sequence_params *params);

void hwss_program_output_csc(union block_sequence_params *params);

void hwss_hubp_set_legacy_tiling_compat_level(union block_sequence_params *params);

void hwss_hubp_set_blank(union block_sequence_params *params);

void hwss_phantom_hubp_post_enable(union block_sequence_params *params);

void hwss_add_optc_pipe_control_lock(struct block_sequence_state *seq_state,
		struct dc *dc, struct pipe_ctx *pipe_ctx, bool lock);

void hwss_add_hubp_set_flip_control_gsl(struct block_sequence_state *seq_state,
		struct hubp *hubp, bool flip_immediate);

void hwss_add_hubp_program_triplebuffer(struct block_sequence_state *seq_state,
		struct dc *dc, struct pipe_ctx *pipe_ctx, bool enableTripleBuffer);

void hwss_add_hubp_update_plane_addr(struct block_sequence_state *seq_state,
		struct dc *dc, struct pipe_ctx *pipe_ctx);

void hwss_add_dpp_set_input_transfer_func(struct block_sequence_state *seq_state,
		struct dc *dc, struct pipe_ctx *pipe_ctx, struct dc_plane_state *plane_state);

void hwss_add_dpp_program_gamut_remap(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx);

void hwss_add_dpp_program_bias_and_scale(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx);

void hwss_add_optc_program_manual_trigger(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx);

void hwss_add_dpp_set_output_transfer_func(struct block_sequence_state *seq_state,
		struct dc *dc, struct pipe_ctx *pipe_ctx, struct dc_stream_state *stream);

void hwss_add_mpc_update_visual_confirm(struct block_sequence_state *seq_state,
		struct dc *dc, struct pipe_ctx *pipe_ctx, int mpcc_id);

void hwss_add_mpc_power_on_mpc_mem_pwr(struct block_sequence_state *seq_state,
		struct mpc *mpc, int mpcc_id, bool power_on);

void hwss_add_mpc_set_output_csc(struct block_sequence_state *seq_state,
		struct mpc *mpc, int opp_id, const uint16_t *regval, enum mpc_output_csc_mode ocsc_mode);

void hwss_add_mpc_set_ocsc_default(struct block_sequence_state *seq_state,
		struct mpc *mpc, int opp_id, enum dc_color_space colorspace, enum mpc_output_csc_mode ocsc_mode);

void hwss_add_dmub_send_dmcub_cmd(struct block_sequence_state *seq_state,
		struct dc_context *ctx, union dmub_rb_cmd *cmd, enum dm_dmub_wait_type wait_type);

void hwss_add_dmub_subvp_save_surf_addr(struct block_sequence_state *seq_state,
		struct dc_dmub_srv *dc_dmub_srv, struct dc_plane_address *addr, uint8_t subvp_index);

void hwss_add_hubp_wait_for_dcc_meta_prop(struct block_sequence_state *seq_state,
		struct dc *dc, struct pipe_ctx *top_pipe_to_program);

void hwss_add_hubp_wait_pipe_read_start(struct block_sequence_state *seq_state,
		struct hubp *hubp);

void hwss_add_hws_apply_update_flags_for_phantom(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx);

void hwss_add_hws_update_phantom_vp_position(struct block_sequence_state *seq_state,
		struct dc *dc, struct dc_state *context, struct pipe_ctx *pipe_ctx);

void hwss_add_optc_set_odm_combine(struct block_sequence_state *seq_state,
		struct timing_generator *tg, int opp_inst[MAX_PIPES], int opp_head_count,
		int odm_slice_width, int last_odm_slice_width);

void hwss_add_optc_set_odm_bypass(struct block_sequence_state *seq_state,
		struct timing_generator *optc, struct dc_crtc_timing *timing);

void hwss_add_tg_program_global_sync(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		int vready_offset,
		unsigned int vstartup_lines,
		unsigned int vupdate_offset_pixels,
		unsigned int vupdate_vupdate_width_pixels,
		unsigned int pstate_keepout_start_lines);

void hwss_add_tg_wait_for_state(struct block_sequence_state *seq_state,
		struct timing_generator *tg, enum crtc_state state);

void hwss_add_tg_set_vtg_params(struct block_sequence_state *seq_state,
		struct timing_generator *tg, struct dc_crtc_timing *dc_crtc_timing, bool program_fp2);

void hwss_add_tg_setup_vertical_interrupt2(struct block_sequence_state *seq_state,
		struct timing_generator *tg, int start_line);

void hwss_add_dpp_set_hdr_multiplier(struct block_sequence_state *seq_state,
		struct dpp *dpp, uint32_t hw_mult);

void hwss_add_hubp_program_det_size(struct block_sequence_state *seq_state,
		struct hubbub *hubbub, unsigned int hubp_inst, unsigned int det_buffer_size_kb);

void hwss_add_hubp_program_mcache_id(struct block_sequence_state *seq_state,
		struct hubp *hubp, struct dml2_hubp_pipe_mcache_regs *mcache_regs);

void hwss_add_hubbub_force_pstate_change_control(struct block_sequence_state *seq_state,
		struct hubbub *hubbub, bool enable, bool wait);

void hwss_add_hubp_program_det_segments(struct block_sequence_state *seq_state,
		struct hubbub *hubbub, unsigned int hubp_inst, unsigned int det_size);

void hwss_add_opp_set_dyn_expansion(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp, enum dc_color_space color_sp,
		enum dc_color_depth color_dpth, enum signal_type signal);

void hwss_add_opp_program_fmt(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp, struct bit_depth_reduction_params *fmt_bit_depth,
		struct clamping_and_pixel_encoding_params *clamping);

void hwss_add_abm_set_pipe(struct block_sequence_state *seq_state,
		struct dc *dc, struct pipe_ctx *pipe_ctx);

void hwss_add_abm_set_level(struct block_sequence_state *seq_state,
		struct abm *abm, uint32_t abm_level);

void hwss_add_tg_enable_crtc(struct block_sequence_state *seq_state,
		struct timing_generator *tg);

void hwss_add_hubp_wait_flip_pending(struct block_sequence_state *seq_state,
		struct hubp *hubp, unsigned int timeout_us, unsigned int polling_interval_us);

void hwss_add_tg_wait_double_buffer_pending(struct block_sequence_state *seq_state,
		struct timing_generator *tg, unsigned int timeout_us, unsigned int polling_interval_us);

void hwss_add_dccg_set_dto_dscclk(struct block_sequence_state *seq_state,
		struct dccg *dccg, int inst, int num_slices_h);

void hwss_add_dsc_calculate_and_set_config(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx, bool enable, int opp_cnt);

void hwss_add_mpc_remove_mpcc(struct block_sequence_state *seq_state,
		struct mpc *mpc, struct mpc_tree *mpc_tree_params, struct mpcc *mpcc_to_remove);

void hwss_add_opp_set_mpcc_disconnect_pending(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp, int mpcc_inst, bool pending);

void hwss_add_hubp_disconnect(struct block_sequence_state *seq_state,
		struct hubp *hubp);

void hwss_add_dsc_enable_with_opp(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx);

void hwss_add_dsc_disconnect(struct block_sequence_state *seq_state,
		struct display_stream_compressor *dsc);

void hwss_add_dc_set_optimized_required(struct block_sequence_state *seq_state,
		struct dc *dc, bool optimized_required);

void hwss_add_abm_set_immediate_disable(struct block_sequence_state *seq_state,
		struct dc *dc, struct pipe_ctx *pipe_ctx);

void hwss_add_opp_set_disp_pattern_generator(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp,
		enum controller_dp_test_pattern test_pattern,
		enum controller_dp_color_space color_space,
		enum dc_color_depth color_depth,
		struct tg_color solid_color,
		bool use_solid_color,
		int width,
		int height,
		int offset);

void hwss_add_opp_program_bit_depth_reduction(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp,
		bool use_default_params,
		struct pipe_ctx *pipe_ctx);

void hwss_add_dc_ip_request_cntl(struct block_sequence_state *seq_state,
		struct dc *dc,
		bool enable);

void hwss_add_dwbc_update(struct block_sequence_state *seq_state,
		struct dwbc *dwb,
		struct dc_dwb_params *dwb_params);

void hwss_add_mcif_wb_config_buf(struct block_sequence_state *seq_state,
		struct mcif_wb *mcif_wb,
		struct mcif_buf_params *mcif_buf_params,
		unsigned int dest_height);

void hwss_add_mcif_wb_config_arb(struct block_sequence_state *seq_state,
		struct mcif_wb *mcif_wb,
		struct mcif_arb_params *mcif_arb_params);

void hwss_add_mcif_wb_enable(struct block_sequence_state *seq_state,
		struct mcif_wb *mcif_wb);

void hwss_add_mcif_wb_disable(struct block_sequence_state *seq_state,
		struct mcif_wb *mcif_wb);

void hwss_add_mpc_set_dwb_mux(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		int dwb_id,
		int mpcc_id);

void hwss_add_mpc_disable_dwb_mux(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		unsigned int dwb_id);

void hwss_add_dwbc_enable(struct block_sequence_state *seq_state,
		struct dwbc *dwb,
		struct dc_dwb_params *dwb_params);

void hwss_add_dwbc_disable(struct block_sequence_state *seq_state,
		struct dwbc *dwb);

void hwss_add_tg_set_gsl(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		struct gsl_params gsl);

void hwss_add_tg_set_gsl_source_select(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		int group_idx,
		uint32_t gsl_ready_signal);

void hwss_add_hubp_update_mall_sel(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		uint32_t mall_sel,
		bool cache_cursor);

void hwss_add_hubp_prepare_subvp_buffering(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool enable);

void hwss_add_hubp_set_blank_en(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool enable);

void hwss_add_hubp_disable_control(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool disable);

void hwss_add_hubbub_soft_reset(struct block_sequence_state *seq_state,
		struct hubbub *hubbub,
		void (*hubbub_soft_reset)(struct hubbub *hubbub, bool reset),
		bool reset);

void hwss_add_hubp_clk_cntl(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool enable);

void hwss_add_dpp_dppclk_control(struct block_sequence_state *seq_state,
		struct dpp *dpp,
		bool dppclk_div,
		bool enable);

void hwss_add_disable_phantom_crtc(struct block_sequence_state *seq_state,
		struct timing_generator *tg);

void hwss_add_dsc_pg_status(struct block_sequence_state *seq_state,
		struct dce_hwseq *hws,
		int dsc_inst,
		bool is_ungated);

void hwss_add_dsc_wait_disconnect_pending_clear(struct block_sequence_state *seq_state,
		struct display_stream_compressor *dsc,
		bool *is_ungated);

void hwss_add_dsc_disable(struct block_sequence_state *seq_state,
		struct display_stream_compressor *dsc,
		bool *is_ungated);

void hwss_add_dccg_set_ref_dscclk(struct block_sequence_state *seq_state,
		struct dccg *dccg,
		int dsc_inst,
		bool *is_ungated);

void hwss_add_dpp_root_clock_control(struct block_sequence_state *seq_state,
		struct dce_hwseq *hws,
		unsigned int dpp_inst,
		bool clock_on);

void hwss_add_dpp_pg_control(struct block_sequence_state *seq_state,
		struct dce_hwseq *hws,
		unsigned int dpp_inst,
		bool power_on);

void hwss_add_hubp_pg_control(struct block_sequence_state *seq_state,
		struct dce_hwseq *hws,
		unsigned int hubp_inst,
		bool power_on);

void hwss_add_hubp_set_blank(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool blank);

void hwss_add_hubp_init(struct block_sequence_state *seq_state,
		struct hubp *hubp);

void hwss_add_hubp_reset(struct block_sequence_state *seq_state,
		struct hubp *hubp);

void hwss_add_dpp_reset(struct block_sequence_state *seq_state,
		struct dpp *dpp);

void hwss_add_opp_pipe_clock_control(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp,
		bool enable);

void hwss_add_hubp_set_vm_system_aperture_settings(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		uint64_t sys_default,
		uint64_t sys_low,
		uint64_t sys_high);

void hwss_add_hubp_set_flip_int(struct block_sequence_state *seq_state,
		struct hubp *hubp);

void hwss_add_dccg_update_dpp_dto(struct block_sequence_state *seq_state,
		struct dccg *dccg,
		int dpp_inst,
		int dppclk_khz);

void hwss_add_hubp_vtg_sel(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		uint32_t otg_inst);

void hwss_add_hubp_setup2(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		struct dml2_dchub_per_pipe_register_set *hubp_regs,
		union dml2_global_sync_programming *global_sync,
		struct dc_crtc_timing *timing);

void hwss_add_hubp_setup(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_regs,
		struct _vcs_dpi_display_ttu_regs_st *ttu_regs,
		struct _vcs_dpi_display_rq_regs_st *rq_regs,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest);

void hwss_add_hubp_set_unbounded_requesting(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		bool unbounded_req);

void hwss_add_hubp_setup_interdependent2(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		struct dml2_dchub_per_pipe_register_set *hubp_regs);

void hwss_add_hubp_setup_interdependent(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_regs,
		struct _vcs_dpi_display_ttu_regs_st *ttu_regs);
void hwss_add_hubp_program_surface_config(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		enum surface_pixel_format format,
		struct dc_tiling_info *tiling_info,
		struct plane_size plane_size,
		enum dc_rotation_angle rotation,
		struct dc_plane_dcc_param *dcc,
		bool horizontal_mirror,
		int compat_level);

void hwss_add_dpp_setup_dpp(struct block_sequence_state *seq_state,
		struct pipe_ctx *pipe_ctx);

void hwss_add_dpp_set_cursor_matrix(struct block_sequence_state *seq_state,
		struct dpp *dpp,
		enum dc_color_space color_space,
		struct dc_csc_transform *cursor_csc_color_matrix);

void hwss_add_mpc_update_blending(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		struct mpcc_blnd_cfg blnd_cfg,
		int mpcc_id);

void hwss_add_mpc_assert_idle_mpcc(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		int mpcc_id);

void hwss_add_mpc_insert_plane(struct block_sequence_state *seq_state,
		struct mpc *mpc,
		struct mpc_tree *mpc_tree_params,
		struct mpcc_blnd_cfg blnd_cfg,
		struct mpcc_sm_cfg *sm_cfg,
		struct mpcc *insert_above_mpcc,
		int dpp_id,
		int mpcc_id);

void hwss_add_dpp_set_scaler(struct block_sequence_state *seq_state,
		struct dpp *dpp,
		const struct scaler_data *scl_data);

void hwss_add_hubp_mem_program_viewport(struct block_sequence_state *seq_state,
		struct hubp *hubp,
		const struct rect *viewport,
		const struct rect *viewport_c);

void hwss_add_abort_cursor_offload_update(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx);

void hwss_add_set_cursor_attribute(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx);

void hwss_add_set_cursor_position(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx);

void hwss_add_set_cursor_sdr_white_level(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx);

void hwss_add_program_output_csc(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix,
		int opp_id);

void hwss_add_phantom_hubp_post_enable(struct block_sequence_state *seq_state,
		struct hubp *hubp);

void hwss_add_update_force_pstate(struct block_sequence_state *seq_state,
		struct dc *dc,
		struct dc_state *context);

void hwss_add_hubbub_apply_dedcn21_147_wa(struct block_sequence_state *seq_state,
		struct hubbub *hubbub);

void hwss_add_hubbub_allow_self_refresh_control(struct block_sequence_state *seq_state,
		struct hubbub *hubbub,
		bool allow,
		bool *disallow_self_refresh_applied);

void hwss_add_tg_get_frame_count(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		unsigned int *frame_count);

void hwss_add_tg_set_dsc_config(struct block_sequence_state *seq_state,
		struct timing_generator *tg,
		struct dsc_optc_config *dsc_optc_cfg,
		bool enable);

void hwss_add_opp_program_left_edge_extra_pixel(struct block_sequence_state *seq_state,
		struct output_pixel_processor *opp,
		enum dc_pixel_encoding pixel_encoding,
		bool is_otg_master);

#endif /* __DC_HW_SEQUENCER_H__ */
