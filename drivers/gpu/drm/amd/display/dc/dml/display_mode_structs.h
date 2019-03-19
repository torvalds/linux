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

typedef struct _vcs_dpi_voltage_scaling_st voltage_scaling_st;
typedef struct _vcs_dpi_soc_bounding_box_st soc_bounding_box_st;
typedef struct _vcs_dpi_ip_params_st ip_params_st;
typedef struct _vcs_dpi_display_pipe_source_params_st display_pipe_source_params_st;
typedef struct _vcs_dpi_display_output_params_st display_output_params_st;
typedef struct _vcs_dpi_scaler_ratio_depth_st scaler_ratio_depth_st;
typedef struct _vcs_dpi_scaler_taps_st scaler_taps_st;
typedef struct _vcs_dpi_display_pipe_dest_params_st display_pipe_dest_params_st;
typedef struct _vcs_dpi_display_pipe_params_st display_pipe_params_st;
typedef struct _vcs_dpi_display_clocks_and_cfg_st display_clocks_and_cfg_st;
typedef struct _vcs_dpi_display_e2e_pipe_params_st display_e2e_pipe_params_st;
typedef struct _vcs_dpi_display_data_rq_misc_params_st display_data_rq_misc_params_st;
typedef struct _vcs_dpi_display_data_rq_sizing_params_st display_data_rq_sizing_params_st;
typedef struct _vcs_dpi_display_data_rq_dlg_params_st display_data_rq_dlg_params_st;
typedef struct _vcs_dpi_display_rq_dlg_params_st display_rq_dlg_params_st;
typedef struct _vcs_dpi_display_rq_sizing_params_st display_rq_sizing_params_st;
typedef struct _vcs_dpi_display_rq_misc_params_st display_rq_misc_params_st;
typedef struct _vcs_dpi_display_rq_params_st display_rq_params_st;
typedef struct _vcs_dpi_display_dlg_regs_st display_dlg_regs_st;
typedef struct _vcs_dpi_display_ttu_regs_st display_ttu_regs_st;
typedef struct _vcs_dpi_display_data_rq_regs_st display_data_rq_regs_st;
typedef struct _vcs_dpi_display_rq_regs_st display_rq_regs_st;
typedef struct _vcs_dpi_display_dlg_sys_params_st display_dlg_sys_params_st;
typedef struct _vcs_dpi_display_arb_params_st display_arb_params_st;

struct _vcs_dpi_voltage_scaling_st {
	int state;
	double dscclk_mhz;
	double dcfclk_mhz;
	double socclk_mhz;
	double dram_speed_mts;
	double fabricclk_mhz;
	double dispclk_mhz;
	double phyclk_mhz;
	double dppclk_mhz;
};

struct _vcs_dpi_soc_bounding_box_st {
	double sr_exit_time_us;
	double sr_enter_plus_exit_time_us;
	double urgent_latency_us;
	double urgent_latency_pixel_data_only_us;
	double urgent_latency_pixel_mixed_with_vm_data_us;
	double urgent_latency_vm_data_only_us;
	double writeback_latency_us;
	double ideal_dram_bw_after_urgent_percent;
	double pct_ideal_dram_sdp_bw_after_urgent_pixel_only; // PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelDataOnly
	double pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm;
	double pct_ideal_dram_sdp_bw_after_urgent_vm_only;
	double max_avg_sdp_bw_use_normal_percent;
	double max_avg_dram_bw_use_normal_percent;
	unsigned int max_request_size_bytes;
	double downspread_percent;
	double dram_page_open_time_ns;
	double dram_rw_turnaround_time_ns;
	double dram_return_buffer_per_channel_bytes;
	double dram_channel_width_bytes;
	double fabric_datapath_to_dcn_data_return_bytes;
	double dcn_downspread_percent;
	double dispclk_dppclk_vco_speed_mhz;
	double dfs_vco_period_ps;
	unsigned int urgent_out_of_order_return_per_channel_pixel_only_bytes;
	unsigned int urgent_out_of_order_return_per_channel_pixel_and_vm_bytes;
	unsigned int urgent_out_of_order_return_per_channel_vm_only_bytes;
	unsigned int round_trip_ping_latency_dcfclk_cycles;
	unsigned int urgent_out_of_order_return_per_channel_bytes;
	unsigned int channel_interleave_bytes;
	unsigned int num_banks;
	unsigned int num_chans;
	unsigned int vmm_page_size_bytes;
	double dram_clock_change_latency_us;
	double writeback_dram_clock_change_latency_us;
	unsigned int return_bus_width_bytes;
	unsigned int voltage_override;
	double xfc_bus_transport_time_us;
	double xfc_xbuf_latency_tolerance_us;
	int use_urgent_burst_bw;
	unsigned int num_states;
	struct _vcs_dpi_voltage_scaling_st clock_limits[8];
};

struct _vcs_dpi_ip_params_st {
	bool gpuvm_enable;
	bool hostvm_enable;
	unsigned int gpuvm_max_page_table_levels;
	unsigned int hostvm_max_page_table_levels;
	unsigned int hostvm_cached_page_table_levels;
	unsigned int pte_group_size_bytes;
	unsigned int max_inter_dcn_tile_repeaters;
	unsigned int num_dsc;
	unsigned int odm_capable;
	unsigned int rob_buffer_size_kbytes;
	unsigned int det_buffer_size_kbytes;
	unsigned int dpte_buffer_size_in_pte_reqs_luma;
	unsigned int dpte_buffer_size_in_pte_reqs_chroma;
	unsigned int pde_proc_buffer_size_64k_reqs;
	unsigned int dpp_output_buffer_pixels;
	unsigned int opp_output_buffer_lines;
	unsigned int pixel_chunk_size_kbytes;
	unsigned char pte_enable;
	unsigned int pte_chunk_size_kbytes;
	unsigned int meta_chunk_size_kbytes;
	unsigned int writeback_chunk_size_kbytes;
	unsigned int line_buffer_size_bits;
	unsigned int max_line_buffer_lines;
	unsigned int writeback_luma_buffer_size_kbytes;
	unsigned int writeback_chroma_buffer_size_kbytes;
	unsigned int writeback_chroma_line_buffer_width_pixels;
	unsigned int max_page_table_levels;
	unsigned int max_num_dpp;
	unsigned int max_num_otg;
	unsigned int cursor_chunk_size;
	unsigned int cursor_buffer_size;
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
	unsigned int xfc_supported;
	unsigned int xfc_fill_constant_bytes;
	double dispclk_ramp_margin_percent;
	double xfc_fill_bw_overhead_percent;
	double underscan_factor;
	unsigned int min_vblank_lines;
	unsigned int dppclk_delay_subtotal;
	unsigned int dispclk_delay_subtotal;
	unsigned int dcfclk_cstate_latency;
	unsigned int dppclk_delay_scl;
	unsigned int dppclk_delay_scl_lb_only;
	unsigned int dppclk_delay_cnvc_formatter;
	unsigned int dppclk_delay_cnvc_cursor;
	unsigned int is_line_buffer_bpp_fixed;
	unsigned int line_buffer_fixed_bpp;
	unsigned int dcc_supported;

	unsigned int IsLineBufferBppFixed;
	unsigned int LineBufferFixedBpp;
	unsigned int can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one;
	unsigned int bug_forcing_LC_req_same_size_fixed;
};

struct _vcs_dpi_display_xfc_params_st {
	double xfc_tslv_vready_offset_us;
	double xfc_tslv_vupdate_width_us;
	double xfc_tslv_vupdate_offset_us;
	int xfc_slv_chunk_size_bytes;
};

struct _vcs_dpi_display_pipe_source_params_st {
	int source_format;
	unsigned char dcc;
	unsigned int dcc_rate;
	unsigned char dcc_use_global;
	unsigned char vm;
	bool gpuvm;    // gpuvm enabled
	bool hostvm;    // hostvm enabled
	bool gpuvm_levels_force_en;
	unsigned int gpuvm_levels_force;
	bool hostvm_levels_force_en;
	unsigned int hostvm_levels_force;
	int source_scan;
	int sw_mode;
	int macro_tile_size;
	unsigned int viewport_width;
	unsigned int viewport_height;
	unsigned int viewport_y_y;
	unsigned int viewport_y_c;
	unsigned int viewport_width_c;
	unsigned int viewport_height_c;
	unsigned int data_pitch;
	unsigned int data_pitch_c;
	unsigned int meta_pitch;
	unsigned int meta_pitch_c;
	unsigned int cur0_src_width;
	int cur0_bpp;
	unsigned int cur1_src_width;
	int cur1_bpp;
	int num_cursors;
	unsigned char is_hsplit;
	unsigned char dynamic_metadata_enable;
	unsigned int dynamic_metadata_lines_before_active;
	unsigned int dynamic_metadata_xmit_bytes;
	unsigned int hsplit_grp;
	unsigned char xfc_enable;
	unsigned char xfc_slave;
	struct _vcs_dpi_display_xfc_params_st xfc_params;
};
struct writeback_st {
	int wb_src_height;
	int wb_dst_width;
	int wb_dst_height;
	int wb_pixel_format;
	int wb_htaps_luma;
	int wb_vtaps_luma;
	int wb_htaps_chroma;
	int wb_vtaps_chroma;
	double wb_hratio;
	double wb_vratio;
};

struct _vcs_dpi_display_output_params_st {
	int dp_lanes;
	int output_bpp;
	int dsc_enable;
	int wb_enable;
	int num_active_wb;
	int output_bpc;
	int output_type;
	int output_format;
	int dsc_slices;
	struct writeback_st wb;
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
	int scl_enable;
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
	unsigned int hactive;
	unsigned int vstartup_start;
	unsigned int vupdate_offset;
	unsigned int vupdate_width;
	unsigned int vready_offset;
	unsigned char interlaced;
	double pixel_rate_mhz;
	unsigned char synchronized_vblank_all_planes;
	unsigned char otg_inst;
	unsigned char odm_combine;
	unsigned char use_maximum_vstartup;
};

struct _vcs_dpi_display_pipe_params_st {
	display_pipe_source_params_st src;
	display_pipe_dest_params_st dest;
	scaler_ratio_depth_st scale_ratio_depth;
	scaler_taps_st scale_taps;
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
	display_pipe_params_st pipe;
	display_output_params_st dout;
	display_clocks_and_cfg_st clks_cfg;
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

struct _vcs_dpi_display_rq_dlg_params_st {
	display_data_rq_dlg_params_st rq_l;
	display_data_rq_dlg_params_st rq_c;
};

struct _vcs_dpi_display_rq_sizing_params_st {
	display_data_rq_sizing_params_st rq_l;
	display_data_rq_sizing_params_st rq_c;
};

struct _vcs_dpi_display_rq_misc_params_st {
	display_data_rq_misc_params_st rq_l;
	display_data_rq_misc_params_st rq_c;
};

struct _vcs_dpi_display_rq_params_st {
	unsigned char yuv420;
	unsigned char yuv420_10bpc;
	display_rq_misc_params_st misc;
	display_rq_sizing_params_st sizing;
	display_rq_dlg_params_st dlg;
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
	unsigned int dst_y_per_vm_flip;
	unsigned int dst_y_per_row_flip;
	unsigned int ref_freq_to_pix_freq;
	unsigned int vratio_prefetch;
	unsigned int vratio_prefetch_c;
	unsigned int refcyc_per_pte_group_vblank_l;
	unsigned int refcyc_per_pte_group_vblank_c;
	unsigned int refcyc_per_meta_chunk_vblank_l;
	unsigned int refcyc_per_meta_chunk_vblank_c;
	unsigned int refcyc_per_pte_group_flip_l;
	unsigned int refcyc_per_pte_group_flip_c;
	unsigned int refcyc_per_meta_chunk_flip_l;
	unsigned int refcyc_per_meta_chunk_flip_c;
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
	unsigned int chunk_hdl_adjust_cur1;
	unsigned int vready_after_vcount0;
	unsigned int dst_y_offset_cur0;
	unsigned int dst_y_offset_cur1;
	unsigned int xfc_reg_transfer_delay;
	unsigned int xfc_reg_precharge_delay;
	unsigned int xfc_reg_remote_surface_flip_latency;
	unsigned int xfc_reg_prefetch_margin;
	unsigned int dst_y_delta_drq_limit;
	unsigned int refcyc_per_vm_group_vblank;
	unsigned int refcyc_per_vm_group_flip;
	unsigned int refcyc_per_vm_req_vblank;
	unsigned int refcyc_per_vm_req_flip;
};

struct _vcs_dpi_display_ttu_regs_st {
	unsigned int qos_level_low_wm;
	unsigned int qos_level_high_wm;
	unsigned int min_ttu_vblank;
	unsigned int qos_level_flip;
	unsigned int refcyc_per_req_delivery_l;
	unsigned int refcyc_per_req_delivery_c;
	unsigned int refcyc_per_req_delivery_cur0;
	unsigned int refcyc_per_req_delivery_cur1;
	unsigned int refcyc_per_req_delivery_pre_l;
	unsigned int refcyc_per_req_delivery_pre_c;
	unsigned int refcyc_per_req_delivery_pre_cur0;
	unsigned int refcyc_per_req_delivery_pre_cur1;
	unsigned int qos_level_fixed_l;
	unsigned int qos_level_fixed_c;
	unsigned int qos_level_fixed_cur0;
	unsigned int qos_level_fixed_cur1;
	unsigned int qos_ramp_disable_l;
	unsigned int qos_ramp_disable_c;
	unsigned int qos_ramp_disable_cur0;
	unsigned int qos_ramp_disable_cur1;
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
	display_data_rq_regs_st rq_regs_l;
	display_data_rq_regs_st rq_regs_c;
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
	double mem_trip_us;
	double t_srx_delay_us;
	double deepsleep_dcfclk_mhz;
	double total_flip_bw;
	unsigned int total_flip_bytes;
};

struct _vcs_dpi_display_arb_params_st {
	int max_req_outstanding;
	int min_req_outstanding;
	int sat_level_us;
};

#endif /*__DISPLAY_MODE_STRUCTS_H__*/
