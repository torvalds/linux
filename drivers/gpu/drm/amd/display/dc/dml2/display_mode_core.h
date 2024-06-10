/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef __DISPLAY_MODE_CORE_H__
#define __DISPLAY_MODE_CORE_H__

#include "display_mode_core_structs.h"

struct display_mode_lib_st;

dml_bool_t dml_core_mode_support(struct display_mode_lib_st *mode_lib);
void dml_core_mode_support_partial(struct display_mode_lib_st *mode_lib);
void dml_core_mode_programming(struct display_mode_lib_st *mode_lib, const struct dml_clk_cfg_st *clk_cfg);

void dml_core_get_row_heights(
						dml_uint_t *dpte_row_height,
						dml_uint_t *meta_row_height,
						const struct display_mode_lib_st *mode_lib,
						dml_bool_t is_plane1,
						enum dml_source_format_class SourcePixelFormat,
						enum dml_swizzle_mode SurfaceTiling,
						enum dml_rotation_angle ScanDirection,
						dml_uint_t pitch,
						dml_uint_t GPUVMMinPageSizeKBytes);

dml_float_t dml_get_return_bw_mbps_vm_only(
									const struct soc_bounding_box_st *soc,
									dml_bool_t use_ideal_dram_bw_strobe,
									dml_bool_t HostVMEnable,
									dml_float_t DCFCLK,
									dml_float_t FabricClock,
									dml_float_t DRAMSpeed);

dml_float_t dml_get_return_bw_mbps(
							const struct soc_bounding_box_st *soc,
							dml_bool_t use_ideal_dram_bw_strobe,
							dml_bool_t HostVMEnable,
							dml_float_t DCFCLK,
							dml_float_t FabricClock,
							dml_float_t DRAMSpeed);

dml_bool_t dml_mode_support(
	struct display_mode_lib_st *mode_lib,
	dml_uint_t state_idx,
	const struct dml_display_cfg_st *display_cfg);

dml_bool_t dml_mode_programming(
	struct display_mode_lib_st *mode_lib,
	dml_uint_t state_idx,
	const struct dml_display_cfg_st *display_cfg,
	bool call_standalone);

dml_uint_t dml_mode_support_ex(
	struct dml_mode_support_ex_params_st *in_out_params);

dml_bool_t dml_get_is_phantom_pipe(struct display_mode_lib_st *mode_lib, dml_uint_t pipe_idx);

#define dml_get_per_surface_var_decl(variable, type) type dml_get_##variable(struct display_mode_lib_st *mode_lib, dml_uint_t surface_idx)
#define dml_get_var_decl(var, type) type dml_get_##var(struct display_mode_lib_st *mode_lib)

dml_get_var_decl(wm_urgent, dml_float_t);
dml_get_var_decl(wm_stutter_exit, dml_float_t);
dml_get_var_decl(wm_stutter_enter_exit, dml_float_t);
dml_get_var_decl(wm_memory_trip, dml_float_t);
dml_get_var_decl(wm_dram_clock_change, dml_float_t);
dml_get_var_decl(wm_z8_stutter_enter_exit, dml_float_t);
dml_get_var_decl(wm_z8_stutter, dml_float_t);
dml_get_var_decl(urgent_latency, dml_float_t);
dml_get_var_decl(clk_dcf_deepsleep, dml_float_t);
dml_get_var_decl(wm_fclk_change, dml_float_t);
dml_get_var_decl(wm_usr_retraining, dml_float_t);
dml_get_var_decl(urgent_latency, dml_float_t);

dml_get_var_decl(wm_writeback_dram_clock_change, dml_float_t);
dml_get_var_decl(wm_writeback_urgent, dml_float_t);
dml_get_var_decl(stutter_efficiency_no_vblank, dml_float_t);
dml_get_var_decl(stutter_efficiency, dml_float_t);
dml_get_var_decl(stutter_efficiency_z8, dml_float_t);
dml_get_var_decl(stutter_num_bursts_z8, dml_float_t);
dml_get_var_decl(stutter_period, dml_float_t);
dml_get_var_decl(stutter_efficiency_z8_bestcase, dml_float_t);
dml_get_var_decl(stutter_num_bursts_z8_bestcase, dml_float_t);
dml_get_var_decl(stutter_period_bestcase, dml_float_t);
dml_get_var_decl(urgent_latency, dml_float_t);
dml_get_var_decl(urgent_extra_latency, dml_float_t);
dml_get_var_decl(nonurgent_latency, dml_float_t);
dml_get_var_decl(dispclk_calculated, dml_float_t);
dml_get_var_decl(total_data_read_bw, dml_float_t);
dml_get_var_decl(return_bw, dml_float_t);
dml_get_var_decl(tcalc, dml_float_t);
dml_get_var_decl(fraction_of_urgent_bandwidth, dml_float_t);
dml_get_var_decl(fraction_of_urgent_bandwidth_imm_flip, dml_float_t);
dml_get_var_decl(comp_buffer_size_kbytes, dml_uint_t);
dml_get_var_decl(pixel_chunk_size_in_kbyte, dml_uint_t);
dml_get_var_decl(alpha_pixel_chunk_size_in_kbyte, dml_uint_t);
dml_get_var_decl(meta_chunk_size_in_kbyte, dml_uint_t);
dml_get_var_decl(min_pixel_chunk_size_in_byte, dml_uint_t);
dml_get_var_decl(min_meta_chunk_size_in_byte, dml_uint_t);
dml_get_var_decl(total_immediate_flip_bytes, dml_uint_t);

dml_get_per_surface_var_decl(dsc_delay, dml_uint_t);
dml_get_per_surface_var_decl(dppclk_calculated, dml_float_t);
dml_get_per_surface_var_decl(dscclk_calculated, dml_float_t);
dml_get_per_surface_var_decl(min_ttu_vblank_in_us, dml_float_t);
dml_get_per_surface_var_decl(vratio_prefetch_l, dml_float_t);
dml_get_per_surface_var_decl(vratio_prefetch_c, dml_float_t);
dml_get_per_surface_var_decl(dst_x_after_scaler, dml_uint_t);
dml_get_per_surface_var_decl(dst_y_after_scaler, dml_uint_t);
dml_get_per_surface_var_decl(dst_y_per_vm_vblank, dml_float_t);
dml_get_per_surface_var_decl(dst_y_per_row_vblank, dml_float_t);
dml_get_per_surface_var_decl(dst_y_prefetch, dml_float_t);
dml_get_per_surface_var_decl(dst_y_per_vm_flip, dml_float_t);
dml_get_per_surface_var_decl(dst_y_per_row_flip, dml_float_t);
dml_get_per_surface_var_decl(dst_y_per_pte_row_nom_l, dml_float_t);
dml_get_per_surface_var_decl(dst_y_per_pte_row_nom_c, dml_float_t);
dml_get_per_surface_var_decl(dst_y_per_meta_row_nom_l, dml_float_t);
dml_get_per_surface_var_decl(dst_y_per_meta_row_nom_c, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_vm_group_vblank_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_vm_group_flip_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_vm_req_vblank_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_vm_req_flip_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_vm_dmdata_in_us, dml_float_t);
dml_get_per_surface_var_decl(dmdata_dl_delta_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_line_delivery_l_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_line_delivery_c_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_line_delivery_pre_l_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_line_delivery_pre_c_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_req_delivery_l_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_req_delivery_c_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_req_delivery_pre_l_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_req_delivery_pre_c_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_cursor_req_delivery_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_cursor_req_delivery_pre_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_meta_chunk_nom_l_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_meta_chunk_nom_c_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_meta_chunk_vblank_l_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_meta_chunk_vblank_c_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_meta_chunk_flip_l_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_meta_chunk_flip_c_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_pte_group_nom_l_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_pte_group_nom_c_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_pte_group_vblank_l_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_pte_group_vblank_c_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_pte_group_flip_l_in_us, dml_float_t);
dml_get_per_surface_var_decl(refcyc_per_pte_group_flip_c_in_us, dml_float_t);

dml_get_per_surface_var_decl(dpte_group_size_in_bytes, dml_uint_t);
dml_get_per_surface_var_decl(vm_group_size_in_bytes, dml_uint_t);
dml_get_per_surface_var_decl(swath_height_l, dml_uint_t);
dml_get_per_surface_var_decl(swath_height_c, dml_uint_t);
dml_get_per_surface_var_decl(dpte_row_height_l, dml_uint_t);
dml_get_per_surface_var_decl(dpte_row_height_c, dml_uint_t);
dml_get_per_surface_var_decl(dpte_row_height_linear_l, dml_uint_t);
dml_get_per_surface_var_decl(dpte_row_height_linear_c, dml_uint_t);
dml_get_per_surface_var_decl(meta_row_height_l, dml_uint_t);
dml_get_per_surface_var_decl(meta_row_height_c, dml_uint_t);
dml_get_per_surface_var_decl(vstartup_calculated, dml_uint_t);
dml_get_per_surface_var_decl(vupdate_offset, dml_uint_t);
dml_get_per_surface_var_decl(vupdate_width, dml_uint_t);
dml_get_per_surface_var_decl(vready_offset, dml_uint_t);
dml_get_per_surface_var_decl(vready_at_or_after_vsync, dml_uint_t);
dml_get_per_surface_var_decl(min_dst_y_next_start, dml_uint_t);
dml_get_per_surface_var_decl(det_stored_buffer_size_l_bytes, dml_uint_t);
dml_get_per_surface_var_decl(det_stored_buffer_size_c_bytes, dml_uint_t);
dml_get_per_surface_var_decl(use_mall_for_static_screen, dml_uint_t);
dml_get_per_surface_var_decl(surface_size_for_mall, dml_uint_t);
dml_get_per_surface_var_decl(dcc_max_uncompressed_block_l, dml_uint_t);
dml_get_per_surface_var_decl(dcc_max_uncompressed_block_c, dml_uint_t);
dml_get_per_surface_var_decl(dcc_max_compressed_block_l, dml_uint_t);
dml_get_per_surface_var_decl(dcc_max_compressed_block_c, dml_uint_t);
dml_get_per_surface_var_decl(dcc_independent_block_l, dml_uint_t);
dml_get_per_surface_var_decl(dcc_independent_block_c, dml_uint_t);
dml_get_per_surface_var_decl(max_active_dram_clock_change_latency_supported, dml_uint_t);
dml_get_per_surface_var_decl(pte_buffer_mode, dml_uint_t);
dml_get_per_surface_var_decl(bigk_fragment_size, dml_uint_t);
dml_get_per_surface_var_decl(dpte_bytes_per_row, dml_uint_t);
dml_get_per_surface_var_decl(meta_bytes_per_row, dml_uint_t);
dml_get_per_surface_var_decl(det_buffer_size_kbytes, dml_uint_t);

#endif
