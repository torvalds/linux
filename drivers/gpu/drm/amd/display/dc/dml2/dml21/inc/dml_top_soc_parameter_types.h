// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML_TOP_SOC_PARAMETER_TYPES_H__
#define __DML_TOP_SOC_PARAMETER_TYPES_H__

#include "dml2_external_lib_deps.h"

#define DML_MAX_CLK_TABLE_SIZE 20

struct dml2_soc_derate_values {
	unsigned int dram_derate_percent_pixel;
	unsigned int dram_derate_percent_vm;
	unsigned int dram_derate_percent_pixel_and_vm;

	unsigned int fclk_derate_percent;
	unsigned int dcfclk_derate_percent;
};

struct dml2_soc_derates {
	struct dml2_soc_derate_values system_active_urgent;
	struct dml2_soc_derate_values system_active_average;
	struct dml2_soc_derate_values dcn_mall_prefetch_urgent;
	struct dml2_soc_derate_values dcn_mall_prefetch_average;
	struct dml2_soc_derate_values system_idle_average;
};

struct dml2_dcn32x_soc_qos_params {
	struct {
		unsigned int base_latency_us;
		unsigned int base_latency_pixel_vm_us;
		unsigned int base_latency_vm_us;
		unsigned int scaling_factor_fclk_us;
		unsigned int scaling_factor_mhz;
	} urgent_latency_us;

	unsigned int loaded_round_trip_latency_fclk_cycles;
	unsigned int urgent_out_of_order_return_per_channel_pixel_only_bytes;
	unsigned int urgent_out_of_order_return_per_channel_pixel_and_vm_bytes;
	unsigned int urgent_out_of_order_return_per_channel_vm_only_bytes;
};

struct dml2_dcn4_uclk_dpm_dependent_qos_params {
	unsigned long minimum_uclk_khz;
	unsigned int urgent_ramp_uclk_cycles;
	unsigned int trip_to_memory_uclk_cycles;
	unsigned int meta_trip_to_memory_uclk_cycles;
	unsigned int maximum_latency_when_urgent_uclk_cycles;
	unsigned int average_latency_when_urgent_uclk_cycles;
	unsigned int maximum_latency_when_non_urgent_uclk_cycles;
	unsigned int average_latency_when_non_urgent_uclk_cycles;
};

struct dml2_dcn4x_soc_qos_params {
	unsigned int df_qos_response_time_fclk_cycles;
	unsigned int max_round_trip_to_furthest_cs_fclk_cycles;
	unsigned int mall_overhead_fclk_cycles;
	unsigned int meta_trip_adder_fclk_cycles;
	unsigned int average_transport_distance_fclk_cycles;
	double umc_urgent_ramp_latency_margin;
	double umc_max_latency_margin;
	double umc_average_latency_margin;
	double fabric_max_transport_latency_margin;
	double fabric_average_transport_latency_margin;
	struct dml2_dcn4_uclk_dpm_dependent_qos_params per_uclk_dpm_params[DML_MAX_CLK_TABLE_SIZE];
};

enum dml2_qos_param_type {
	dml2_qos_param_type_dcn3,
	dml2_qos_param_type_dcn4x
};

struct dml2_soc_qos_parameters {
	struct dml2_soc_derates derate_table;
	struct {
		unsigned int base_latency_us;
		unsigned int scaling_factor_us;
		unsigned int scaling_factor_mhz;
	} writeback;

	union {
		struct dml2_dcn32x_soc_qos_params dcn32x;
		struct dml2_dcn4x_soc_qos_params dcn4x;
	} qos_params;

	enum dml2_qos_param_type qos_type;
};

struct dml2_soc_power_management_parameters {
	double dram_clk_change_blackout_us;
	double dram_clk_change_read_only_us;
	double dram_clk_change_write_only_us;
	double fclk_change_blackout_us;
	double g7_ppt_blackout_us;
	double g7_temperature_read_blackout_us;
	double stutter_enter_plus_exit_latency_us;
	double stutter_exit_latency_us;
	double low_power_stutter_enter_plus_exit_latency_us;
	double low_power_stutter_exit_latency_us;
	double z8_stutter_enter_plus_exit_latency_us;
	double z8_stutter_exit_latency_us;
	double z8_min_idle_time;
	double g6_temp_read_blackout_us[DML_MAX_CLK_TABLE_SIZE];
	double type_b_dram_clk_change_blackout_us;
	double type_b_ppt_blackout_us;
};

struct dml2_clk_table {
	unsigned long clk_values_khz[DML_MAX_CLK_TABLE_SIZE];
	unsigned char num_clk_values;
};

struct dml2_dram_params {
	unsigned int channel_width_bytes;
	unsigned int channel_count;
	unsigned int transactions_per_clock;
};

struct dml2_soc_state_table {
	struct dml2_clk_table uclk;
	struct dml2_clk_table fclk;
	struct dml2_clk_table dcfclk;
	struct dml2_clk_table dispclk;
	struct dml2_clk_table dppclk;
	struct dml2_clk_table dtbclk;
	struct dml2_clk_table phyclk;
	struct dml2_clk_table socclk;
	struct dml2_clk_table dscclk;
	struct dml2_clk_table phyclk_d18;
	struct dml2_clk_table phyclk_d32;

	struct dml2_dram_params dram_config;
};

struct dml2_soc_vmin_clock_limits {
	unsigned long dispclk_khz;
	unsigned long dcfclk_khz;
};

struct dml2_soc_bb {
	struct dml2_soc_state_table clk_table;
	struct dml2_soc_qos_parameters qos_parameters;
	struct dml2_soc_power_management_parameters power_management_parameters;
	struct dml2_soc_vmin_clock_limits vmin_limit;

	double lower_bound_bandwidth_dchub;
	unsigned int dprefclk_mhz;
	unsigned int xtalclk_mhz;
	unsigned int pcie_refclk_mhz;
	unsigned int dchub_refclk_mhz;
	unsigned int mall_allocated_for_dcn_mbytes;
	unsigned int max_outstanding_reqs;
	unsigned long fabric_datapath_to_dcn_data_return_bytes;
	unsigned long return_bus_width_bytes;
	unsigned long hostvm_min_page_size_kbytes;
	unsigned long gpuvm_min_page_size_kbytes;
	double phy_downspread_percent;
	double dcn_downspread_percent;
	double dispclk_dppclk_vco_speed_mhz;
	bool no_dfs;
	bool do_urgent_latency_adjustment;
	unsigned int mem_word_bytes;
	unsigned int num_dcc_mcaches;
	unsigned int mcache_size_bytes;
	unsigned int mcache_line_size_bytes;
	unsigned long max_fclk_for_uclk_dpm_khz;
};

struct dml2_ip_capabilities {
	unsigned int pipe_count;
	unsigned int otg_count;
	unsigned int num_dsc;
	unsigned int max_num_dp2p0_streams;
	unsigned int max_num_hdmi_frl_outputs;
	unsigned int max_num_dp2p0_outputs;
	unsigned int max_num_wb;
	unsigned int rob_buffer_size_kbytes;
	unsigned int config_return_buffer_size_in_kbytes;
	unsigned int config_return_buffer_segment_size_in_kbytes;
	unsigned int meta_fifo_size_in_kentries;
	unsigned int compressed_buffer_segment_size_in_kbytes;
	unsigned int cursor_buffer_size;
	unsigned int max_flip_time_us;
	unsigned int max_flip_time_lines;
	unsigned int hostvm_mode;
	unsigned int subvp_drr_scheduling_margin_us;
	unsigned int subvp_prefetch_end_to_mall_start_us;
	unsigned int subvp_fw_processing_delay;
	unsigned int max_vactive_det_fill_delay_us;

	/* FAMS2 delays */
	struct {
		unsigned int max_allow_delay_us;
		unsigned int scheduling_delay_us;
		unsigned int vertical_interrupt_ack_delay_us; // delay to acknowledge vline int
		unsigned int allow_programming_delay_us; // time requires to program allow
		unsigned int min_allow_width_us;
		unsigned int subvp_df_throttle_delay_us;
		unsigned int subvp_programming_delay_us;
		unsigned int subvp_prefetch_to_mall_delay_us;
		unsigned int drr_programming_delay_us;

		unsigned int lock_timeout_us;
		unsigned int recovery_timeout_us;
		unsigned int flip_programming_delay_us;
	} fams2;
};

#endif
