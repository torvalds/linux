/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#ifndef __DISPLAY_MODE_STRUCTS_H__
#define __DISPLAY_MODE_STRUCTS_H__

struct _vcs_dpi_voltage_scaling_st {
	double dcfclk_mhz;
	double dispclk_mhz;
	double dppclk_mhz;
	double dram_bw_per_chan_gbps;
	double phyclk_mhz;
	double socclk_mhz;
};

struct _vcs_dpi_soc_bounding_box_st {
	double sr_exit_time_us;
	double sr_enter_plus_exit_time_us;
	double urgent_latency_us;
	double writeback_latency_us;
	double ideal_dram_bw_after_urgent_percent;
	unsigned int max_request_size_bytes;
	struct _vcs_dpi_voltage_scaling_st vmin;
	struct _vcs_dpi_voltage_scaling_st vmid;
	struct _vcs_dpi_voltage_scaling_st vnom;
	struct _vcs_dpi_voltage_scaling_st vmax;
	double downspread_percent;
	double dram_page_open_time_ns;
	double dram_rw_turnaround_time_ns;
	double dram_return_buffer_per_channel_bytes;
	unsigned int round_trip_ping_latency_dcfclk_cycles;
	unsigned int urgent_out_of_order_return_per_channel_bytes;
	unsigned int channel_interleave_bytes;
	unsigned int num_banks;
	unsigned int num_chans;
	unsigned int vmm_page_size_bytes;
	double dram_clock_change_latency_us;
	double writeback_dram_clock_change_latency_us;
	unsigned int return_bus_width_bytes;
};

struct _vcs_dpi_ip_params_st {
	unsigned int rob_buffer_size_kbytes;
	unsigned int det_buffer_size_kbytes;
	unsigned int dpte_buffer_size_in_pte_reqs;
	unsigned int dpp_output_buffer_pixels;
	unsigned int opp_output_buffer_lines;
	unsigned int pixel_chunk_size_kbytes;
	unsigned char pte_enable;
	unsigned int pte_chunk_size_kbytes;
	unsigned int meta_chunk_size_kbytes;
	unsigned int writeback_chunk_size_kbytes;
	unsigned int line_buffer_size_bits;
	unsigned int max_line_buffer_lines;
	unsigned int IsLineBufferBppFixed;
	unsigned int LineBufferFixedBpp;
	unsigned int writeback_luma_buffer_size_kbytes;
	unsigned int writeback_chroma_buffer_size_kbytes;
	unsigned int max_num_dpp;
	unsigned int max_num_wb;
	unsigned int max_dchub_pscl_bw_pix_per_clk;
	unsigned int max_pscl_lb_bw_pix_per_clk;
	unsigned int max_lb_vscl_bw_pix_per_clk;
	unsigned int max_vscl_hscl_bw_pix_per_clk;
	double max_hscl_ratio;
	double max_vscl_ratio;
	unsigned int hscl_mults;
	unsigned int vscl_mults;
	unsigned int max_hscl_taps;
	unsigned int max_vscl_taps;
	double dispclk_ramp_margin_percent;
	double underscan_factor;
	unsigned int min_vblank_lines;
	unsigned int dppclk_delay_subtotal;
	unsigned int dispclk_delay_subtotal;
	unsigned int dcfclk_cstate_latency;
	unsigned int max_inter_dcn_tile_repeaters;
	unsigned int can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one;
	unsigned int bug_forcing_LC_req_same_size_fixed;
};

struct _vcs_dpi_display_pipe_source_params_st {
	int source_format;
	unsigned char dcc;
	unsigned int dcc_rate;
	unsigned char vm;
	int source_scan;
	int sw_mode;
	int macro_tile_size;
	unsigned char is_display_sw;
	unsigned int viewport_width;
	unsigned int viewport_height;
	unsigned int viewport_width_c;
	unsigned int viewport_height_c;
	unsigned int data_pitch;
	unsigned int data_pitch_c;
	unsigned int meta_pitch;
	unsigned int meta_pitch_c;
	unsigned int cur0_src_width;
	int cur0_bpp;
	unsigned char is_hsplit;
	unsigned int hsplit_grp;
};

struct _vcs_dpi_display_output_params_st {
	int output_bpc;
	int output_type;
	int output_format;
	int output_standard;
};

struct _vcs_dpi_display_bandwidth_st {
	double total_bw_consumed_gbps;
	double guaranteed_urgent_return_bw_gbps;
};

struct _vcs_dpi_scaler_ratio_depth_st {
	double hscl_ratio;
	double vscl_ratio;
	double hscl_ratio_c;
	double vscl_ratio_c;
	double vinit;
	double vinit_c;
	double vinit_bot;
	double vinit_bot_c;
	int lb_depth;
};

struct _vcs_dpi_scaler_taps_st {
	unsigned int htaps;
	unsigned int vtaps;
	unsigned int htaps_c;
	unsigned int vtaps_c;
};

struct _vcs_dpi_display_pipe_dest_params_st {
	unsigned int recout_width;
	unsigned int recout_height;
	unsigned int full_recout_width;
	unsigned int full_recout_height;
	unsigned int hblank_start;
	unsigned int hblank_end;
	unsigned int vblank_start;
	unsigned int vblank_end;
	unsigned int htotal;
	unsigned int vtotal;
	unsigned int vactive;
	unsigned int vstartup_start;
	unsigned int vupdate_offset;
	unsigned int vupdate_width;
	unsigned int vready_offset;
	unsigned int vsync_plus_back_porch;
	unsigned char interlaced;
	unsigned char underscan;
	double pixel_rate_mhz;
	unsigned char syncronized_vblank_all_planes;
};

struct _vcs_dpi_display_pipe_params_st {
	struct _vcs_dpi_display_pipe_source_params_st src;
	struct _vcs_dpi_display_pipe_dest_params_st dest;
	struct _vcs_dpi_scaler_ratio_depth_st scale_ratio_depth;
	struct _vcs_dpi_scaler_taps_st scale_taps;
};

struct _vcs_dpi_display_clocks_and_cfg_st {
	int voltage;
	double dppclk_mhz;
	double refclk_mhz;
	double dispclk_mhz;
	double dcfclk_mhz;
	double socclk_mhz;
};

struct _vcs_dpi_display_e2e_pipe_params_st {
	struct _vcs_dpi_display_pipe_params_st pipe;
	struct _vcs_dpi_display_output_params_st dout;
	struct _vcs_dpi_display_clocks_and_cfg_st clks_cfg;
};

struct _vcs_dpi_dchub_buffer_sizing_st {
	unsigned int swath_width_y;
	unsigned int swath_height_y;
	unsigned int swath_height_c;
	unsigned int detail_buffer_size_y;
};

struct _vcs_dpi_watermarks_perf_st {
	double stutter_eff_in_active_region_percent;
	double urgent_latency_supported_us;
	double non_urgent_latency_supported_us;
	double dram_clock_change_margin_us;
	double dram_access_eff_percent;
};

struct _vcs_dpi_cstate_pstate_watermarks_st {
	double cstate_exit_us;
	double cstate_enter_plus_exit_us;
	double pstate_change_us;
};

struct _vcs_dpi_wm_calc_pipe_params_st {
	unsigned int num_dpp;
	int voltage;
	int output_type;
	double dcfclk_mhz;
	double socclk_mhz;
	double dppclk_mhz;
	double pixclk_mhz;
	unsigned char interlace_en;
	unsigned char pte_enable;
	unsigned char dcc_enable;
	double dcc_rate;
	double bytes_per_pixel_c;
	double bytes_per_pixel_y;
	unsigned int swath_width_y;
	unsigned int swath_height_y;
	unsigned int swath_height_c;
	unsigned int det_buffer_size_y;
	double h_ratio;
	double v_ratio;
	unsigned int h_taps;
	unsigned int h_total;
	unsigned int v_total;
	unsigned int v_active;
	unsigned int e2e_index;
	double display_pipe_line_delivery_time;
	double read_bw;
	unsigned int lines_in_det_y;
	unsigned int lines_in_det_y_rounded_down_to_swath;
	double full_det_buffering_time;
	double dcfclk_deepsleep_mhz_per_plane;
};

struct _vcs_dpi_vratio_pre_st {
	double vratio_pre_l;
	double vratio_pre_c;
};

struct _vcs_dpi_display_data_rq_misc_params_st {
	unsigned int full_swath_bytes;
	unsigned int stored_swath_bytes;
	unsigned int blk256_height;
	unsigned int blk256_width;
	unsigned int req_height;
	unsigned int req_width;
};

struct _vcs_dpi_display_data_rq_sizing_params_st {
	unsigned int chunk_bytes;
	unsigned int min_chunk_bytes;
	unsigned int meta_chunk_bytes;
	unsigned int min_meta_chunk_bytes;
	unsigned int mpte_group_bytes;
	unsigned int dpte_group_bytes;
};

struct _vcs_dpi_display_data_rq_dlg_params_st {
	unsigned int swath_width_ub;
	unsigned int swath_height;
	unsigned int req_per_swath_ub;
	unsigned int meta_pte_bytes_per_frame_ub;
	unsigned int dpte_req_per_row_ub;
	unsigned int dpte_groups_per_row_ub;
	unsigned int dpte_row_height;
	unsigned int dpte_bytes_per_row_ub;
	unsigned int meta_chunks_per_row_ub;
	unsigned int meta_req_per_row_ub;
	unsigned int meta_row_height;
	unsigned int meta_bytes_per_row_ub;
};

struct _vcs_dpi_display_cur_rq_dlg_params_st {
	unsigned char enable;
	unsigned int swath_height;
	unsigned int req_per_line;
};

struct _vcs_dpi_display_rq_dlg_params_st {
	struct _vcs_dpi_display_data_rq_dlg_params_st rq_l;
	struct _vcs_dpi_display_data_rq_dlg_params_st rq_c;
	struct _vcs_dpi_display_cur_rq_dlg_params_st rq_cur0;
};

struct _vcs_dpi_display_rq_sizing_params_st {
	struct _vcs_dpi_display_data_rq_sizing_params_st rq_l;
	struct _vcs_dpi_display_data_rq_sizing_params_st rq_c;
};

struct _vcs_dpi_display_rq_misc_params_st {
	struct _vcs_dpi_display_data_rq_misc_params_st rq_l;
	struct _vcs_dpi_display_data_rq_misc_params_st rq_c;
};

struct _vcs_dpi_display_rq_params_st {
	unsigned char yuv420;
	unsigned char yuv420_10bpc;
	struct _vcs_dpi_display_rq_misc_params_st misc;
	struct _vcs_dpi_display_rq_sizing_params_st sizing;
	struct _vcs_dpi_display_rq_dlg_params_st dlg;
};

struct _vcs_dpi_display_dlg_regs_st {
	unsigned int refcyc_h_blank_end;
	unsigned int dlg_vblank_end;
	unsigned int min_dst_y_next_start;
	unsigned int refcyc_per_htotal;
	unsigned int refcyc_x_after_scaler;
	unsigned int dst_y_after_scaler;
	unsigned int dst_y_prefetch;
	unsigned int dst_y_per_vm_vblank;
	unsigned int dst_y_per_row_vblank;
	unsigned int ref_freq_to_pix_freq;
	unsigned int vratio_prefetch;
	unsigned int vratio_prefetch_c;
	unsigned int refcyc_per_pte_group_vblank_l;
	unsigned int refcyc_per_pte_group_vblank_c;
	unsigned int refcyc_per_meta_chunk_vblank_l;
	unsigned int refcyc_per_meta_chunk_vblank_c;
	unsigned int dst_y_per_pte_row_nom_l;
	unsigned int dst_y_per_pte_row_nom_c;
	unsigned int refcyc_per_pte_group_nom_l;
	unsigned int refcyc_per_pte_group_nom_c;
	unsigned int dst_y_per_meta_row_nom_l;
	unsigned int dst_y_per_meta_row_nom_c;
	unsigned int refcyc_per_meta_chunk_nom_l;
	unsigned int refcyc_per_meta_chunk_nom_c;
	unsigned int refcyc_per_line_delivery_pre_l;
	unsigned int refcyc_per_line_delivery_pre_c;
	unsigned int refcyc_per_line_delivery_l;
	unsigned int refcyc_per_line_delivery_c;
	unsigned int chunk_hdl_adjust_cur0;
};

struct _vcs_dpi_display_ttu_regs_st {
	unsigned int qos_level_low_wm;
	unsigned int qos_level_high_wm;
	unsigned int min_ttu_vblank;
	unsigned int qos_level_flip;
	unsigned int refcyc_per_req_delivery_l;
	unsigned int refcyc_per_req_delivery_c;
	unsigned int refcyc_per_req_delivery_cur0;
	unsigned int refcyc_per_req_delivery_pre_l;
	unsigned int refcyc_per_req_delivery_pre_c;
	unsigned int refcyc_per_req_delivery_pre_cur0;
	unsigned int qos_level_fixed_l;
	unsigned int qos_level_fixed_c;
	unsigned int qos_level_fixed_cur0;
	unsigned int qos_ramp_disable_l;
	unsigned int qos_ramp_disable_c;
	unsigned int qos_ramp_disable_cur0;
};

struct _vcs_dpi_display_data_rq_regs_st {
	unsigned int chunk_size;
	unsigned int min_chunk_size;
	unsigned int meta_chunk_size;
	unsigned int min_meta_chunk_size;
	unsigned int dpte_group_size;
	unsigned int mpte_group_size;
	unsigned int swath_height;
	unsigned int pte_row_height_linear;
};

struct _vcs_dpi_display_rq_regs_st {
	struct _vcs_dpi_display_data_rq_regs_st rq_regs_l;
	struct _vcs_dpi_display_data_rq_regs_st rq_regs_c;
	unsigned int drq_expansion_mode;
	unsigned int prq_expansion_mode;
	unsigned int mrq_expansion_mode;
	unsigned int crq_expansion_mode;
	unsigned int plane1_base_address;
};

struct _vcs_dpi_display_dlg_sys_params_st {
	double t_mclk_wm_us;
	double t_urg_wm_us;
	double t_sr_wm_us;
	double t_extra_us;
	double t_srx_delay_us;
	double deepsleep_dcfclk_mhz;
	double total_flip_bw;
	unsigned int total_flip_bytes;
};

struct _vcs_dpi_display_dlg_prefetch_param_st {
	double prefetch_bw;
	unsigned int flip_bytes;
};

struct _vcs_dpi_display_pipe_clock_st {
	double dcfclk_mhz;
	double dispclk_mhz;
	double dppclk_mhz[4];
	unsigned char dppclk_div[4];
};

struct _vcs_dpi_display_arb_params_st {
	int max_req_outstanding;
	int min_req_outstanding;
	int sat_level_us;
};

struct _vcs_dpi_mode_evaluation_st {
	int voltage_override;
};

#endif /*__DISPLAY_MODE_STRUCTS_H__*/
