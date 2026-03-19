/* SPDX-License-Identifier: MIT */
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML_DML_DCN42_SOC_BB__
#define __DML_DML_DCN42_SOC_BB__

#include "dml_top_soc_parameter_types.h"

static const struct dml2_soc_qos_parameters dml_dcn42_variant_a_soc_qos_params = {
	.derate_table = {
		.system_active_urgent = {
			.dram_derate_percent_pixel = 65,
			.dram_derate_percent_vm = 30,
			.dram_derate_percent_pixel_and_vm = 60,
			.fclk_derate_percent = 80,
			.dcfclk_derate_percent = 80,
		},
		.system_active_average = {
			.dram_derate_percent_pixel = 30,
			.dram_derate_percent_vm = 30,
			.dram_derate_percent_pixel_and_vm = 30,
			.fclk_derate_percent = 60,
			.dcfclk_derate_percent = 60,
		},
		.dcn_mall_prefetch_urgent = {
			.dram_derate_percent_pixel = 65,
			.dram_derate_percent_vm = 30,
			.dram_derate_percent_pixel_and_vm = 60,
			.fclk_derate_percent = 80,
			.dcfclk_derate_percent = 80,
		},
		.dcn_mall_prefetch_average = {
			.dram_derate_percent_pixel = 30,
			.dram_derate_percent_vm = 30,
			.dram_derate_percent_pixel_and_vm = 30,
			.fclk_derate_percent = 60,
			.dcfclk_derate_percent = 60,
		},
		.system_idle_average = {
			.dram_derate_percent_pixel = 30,
			.dram_derate_percent_vm = 30,
			.dram_derate_percent_pixel_and_vm = 30,
			.fclk_derate_percent = 60,
			.dcfclk_derate_percent = 60,
		},
	},
	.writeback = {
		.base_latency_us = 12,
		.scaling_factor_us = 0,
		.scaling_factor_mhz = 0,
	},
	.qos_params = {
		.dcn32x = {
			.loaded_round_trip_latency_fclk_cycles = 106,
			.urgent_latency_us = {
				.base_latency_us = 4,
				.base_latency_pixel_vm_us = 4,
				.base_latency_vm_us = 4,
				.scaling_factor_fclk_us = 0,
				.scaling_factor_mhz = 0,
			},
			.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
			.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
			.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
		},
	},
	.qos_type = dml2_qos_param_type_dcn3,
};

static const struct dml2_soc_bb dml2_socbb_dcn42 = {
	.clk_table = {
		.wck_ratio = {
				.clk_values_khz = {2},
		},
		.uclk = {
				.clk_values_khz = {400000},
				.num_clk_values = 1,
		},
		.fclk = {
				.clk_values_khz = {400000},
				.num_clk_values = 1,
		},
		.dcfclk = {
				.clk_values_khz = {200000},
				.num_clk_values = 1,
		},
		.dispclk = {
				.clk_values_khz = {1500000},
				.num_clk_values = 1,
		},
		.dppclk = {
				.clk_values_khz = {1500000},
				.num_clk_values = 1,
		},
		.dtbclk = {
				.clk_values_khz = {600000},
				.num_clk_values = 1,
		},
		.phyclk = {
				.clk_values_khz = {810000},
				.num_clk_values = 1,
		},
		.socclk = {
				.clk_values_khz = {600000},
				.num_clk_values = 1,
		},
		.dscclk = {
				.clk_values_khz = {500000},
				.num_clk_values = 1,
		},
		.phyclk_d18 = {
				.clk_values_khz = {667000},
				.num_clk_values = 1,
		},
		.phyclk_d32 = {
				.clk_values_khz = {625000},
				.num_clk_values = 1,
		},
		.dram_config = {
			.channel_width_bytes = 4,
			.channel_count = 4,
			.alt_clock_bw_conversion = true,
		},
	},

	.qos_parameters = {
		.derate_table = {
			.system_active_urgent = {
				.dram_derate_percent_pixel = 65,
				.dram_derate_percent_vm = 30,
				.dram_derate_percent_pixel_and_vm = 60,
				.fclk_derate_percent = 80,
				.dcfclk_derate_percent = 80,
			},
			.system_active_average = {
				.dram_derate_percent_pixel = 30,
				.dram_derate_percent_vm = 30,
				.dram_derate_percent_pixel_and_vm = 30,
				.fclk_derate_percent = 60,
				.dcfclk_derate_percent = 60,
			},
			.dcn_mall_prefetch_urgent = {
				.dram_derate_percent_pixel = 65,
				.dram_derate_percent_vm = 30,
				.dram_derate_percent_pixel_and_vm = 60,
				.fclk_derate_percent = 80,
				.dcfclk_derate_percent = 80,
			},
			.dcn_mall_prefetch_average = {
				.dram_derate_percent_pixel = 30,
				.dram_derate_percent_vm = 30,
				.dram_derate_percent_pixel_and_vm = 30,
				.fclk_derate_percent = 60,
				.dcfclk_derate_percent = 60,
			},
			.system_idle_average = {
				.dram_derate_percent_pixel = 30,
				.dram_derate_percent_vm = 30,
				.dram_derate_percent_pixel_and_vm = 30,
				.fclk_derate_percent = 60,
				.dcfclk_derate_percent = 60,
			},
		},
		.writeback = {
			.base_latency_us = 12,
			.scaling_factor_us = 0,
			.scaling_factor_mhz = 0,
		},
		.qos_params = {
			.dcn32x = {
				.loaded_round_trip_latency_fclk_cycles = 106,
				.urgent_latency_us = {
					.base_latency_us = 4,
					.base_latency_pixel_vm_us = 4,
					.base_latency_vm_us = 4,
					.scaling_factor_fclk_us = 0,
					.scaling_factor_mhz = 0,
				},
				.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
				.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
				.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
			},
		},
		.qos_type = dml2_qos_param_type_dcn3,
	},

	.power_management_parameters = {
		.dram_clk_change_blackout_us = 29,
		.fclk_change_blackout_us = 0,
		.g7_ppt_blackout_us = 0,
		.stutter_enter_plus_exit_latency_us = 11,
		.stutter_exit_latency_us = 9,
		.z8_stutter_enter_plus_exit_latency_us = 300,
		.z8_stutter_exit_latency_us = 200,
	},

	.vmin_limit = {
		.dispclk_khz = 632 * 1000,
	},

	.dprefclk_mhz = 600,
	.xtalclk_mhz = 24,
	.pcie_refclk_mhz = 100,
	.dchub_refclk_mhz = 50,
	.mall_allocated_for_dcn_mbytes = 64,
	.max_outstanding_reqs = 256,
	.fabric_datapath_to_dcn_data_return_bytes = 32,
	.return_bus_width_bytes = 64,
	.hostvm_min_page_size_kbytes = 4,
	.gpuvm_min_page_size_kbytes = 256,
	.gpuvm_max_page_table_levels = 1,
	.hostvm_max_non_cached_page_table_levels = 2,
	.phy_downspread_percent = 0.38,
	.dcn_downspread_percent = 0.38,
	.dispclk_dppclk_vco_speed_mhz = 3000,
	.do_urgent_latency_adjustment = 0,
	.mem_word_bytes = 32,
	.num_dcc_mcaches = 8,
	.mcache_size_bytes = 2048,
	.mcache_line_size_bytes = 32,
	.max_fclk_for_uclk_dpm_khz = 2200 * 1000,
};

static const struct dml2_ip_capabilities dml2_dcn42_max_ip_caps = {
	.pipe_count = 4,
	.otg_count = 4,
	.num_dsc = 4,
	.max_num_dp2p0_streams = 4,
	.max_num_hdmi_frl_outputs = 1,
	.max_num_dp2p0_outputs = 4,
	.rob_buffer_size_kbytes = 64,
	.config_return_buffer_size_in_kbytes = 1792,
	.config_return_buffer_segment_size_in_kbytes = 64,
	.meta_fifo_size_in_kentries = 32,
	.compressed_buffer_segment_size_in_kbytes = 64,
	.cursor_buffer_size = 24,
	.max_flip_time_us = 110,
	.max_flip_time_lines = 50,
	.hostvm_mode = 0,
	.subvp_drr_scheduling_margin_us = 100,
	.subvp_prefetch_end_to_mall_start_us = 15,
	.subvp_fw_processing_delay = 15,
	.max_vactive_det_fill_delay_us = 400,

	.fams2 = {
		.max_allow_delay_us = 100 * 1000,
		.scheduling_delay_us = 550,
		.vertical_interrupt_ack_delay_us = 40,
		.allow_programming_delay_us = 18,
		.min_allow_width_us = 20,
		.subvp_df_throttle_delay_us = 100,
		.subvp_programming_delay_us = 200,
		.subvp_prefetch_to_mall_delay_us = 18,
		.drr_programming_delay_us = 35,

		.lock_timeout_us = 5000,
		.recovery_timeout_us = 5000,
		.flip_programming_delay_us = 300,
	},
};

#endif
