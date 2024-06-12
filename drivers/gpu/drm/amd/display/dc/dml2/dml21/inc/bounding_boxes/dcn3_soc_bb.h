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

#ifndef __DML_DML_DCN3_SOC_BB__
#define __DML_DML_DCN3_SOC_BB__

#include "dml_top_soc_parameter_types.h"

static const struct dml2_soc_qos_parameters dml_dcn31_soc_qos_params = {
	.derate_table = {
		.system_active_urgent = {
			.dram_derate_percent_pixel = 22,
			.dram_derate_percent_vm = 0,
			.dram_derate_percent_pixel_and_vm = 0,
			.fclk_derate_percent = 76,
			.dcfclk_derate_percent = 100,
		},
		.system_active_average = {
			.dram_derate_percent_pixel = 17,
			.dram_derate_percent_vm = 0,
			.dram_derate_percent_pixel_and_vm = 0,
			.fclk_derate_percent = 57,
			.dcfclk_derate_percent = 75,
		},
		.dcn_mall_prefetch_urgent = {
			.dram_derate_percent_pixel = 22,
			.dram_derate_percent_vm = 0,
			.dram_derate_percent_pixel_and_vm = 0,
			.fclk_derate_percent = 76,
			.dcfclk_derate_percent = 100,
		},
		.dcn_mall_prefetch_average = {
			.dram_derate_percent_pixel = 17,
			.dram_derate_percent_vm = 0,
			.dram_derate_percent_pixel_and_vm = 0,
			.fclk_derate_percent = 57,
			.dcfclk_derate_percent = 75,
		},
		.system_idle_average = {
			.dram_derate_percent_pixel = 17,
			.dram_derate_percent_vm = 0,
			.dram_derate_percent_pixel_and_vm = 0,
			.fclk_derate_percent = 57,
			.dcfclk_derate_percent = 100,
		},
	},
	.writeback = {
		.base_latency_us = 12,
		.scaling_factor_us = 0,
		.scaling_factor_mhz = 0,
	},
	.qos_params = {
		.dcn4 = {
			.df_qos_response_time_fclk_cycles = 300,
			.max_round_trip_to_furthest_cs_fclk_cycles = 350,
			.mall_overhead_fclk_cycles = 50,
			.meta_trip_adder_fclk_cycles = 36,
			.average_transport_distance_fclk_cycles = 257,
			.umc_urgent_ramp_latency_margin = 50,
			.umc_max_latency_margin = 30,
			.umc_average_latency_margin = 20,
			.fabric_max_transport_latency_margin = 20,
			.fabric_average_transport_latency_margin = 10,

			.per_uclk_dpm_params = {
				{
					.minimum_uclk_khz = 97,
					.urgent_ramp_uclk_cycles = 472,
					.trip_to_memory_uclk_cycles = 827,
					.meta_trip_to_memory_uclk_cycles = 827,
					.maximum_latency_when_urgent_uclk_cycles = 72,
					.average_latency_when_urgent_uclk_cycles = 61,
					.maximum_latency_when_non_urgent_uclk_cycles = 827,
					.average_latency_when_non_urgent_uclk_cycles = 118,
				},
				{
					.minimum_uclk_khz = 435,
					.urgent_ramp_uclk_cycles = 546,
					.trip_to_memory_uclk_cycles = 848,
					.meta_trip_to_memory_uclk_cycles = 848,
					.maximum_latency_when_urgent_uclk_cycles = 146,
					.average_latency_when_urgent_uclk_cycles = 90,
					.maximum_latency_when_non_urgent_uclk_cycles = 848,
					.average_latency_when_non_urgent_uclk_cycles = 135,
				},
				{
					.minimum_uclk_khz = 731,
					.urgent_ramp_uclk_cycles = 632,
					.trip_to_memory_uclk_cycles = 874,
					.meta_trip_to_memory_uclk_cycles = 874,
					.maximum_latency_when_urgent_uclk_cycles = 232,
					.average_latency_when_urgent_uclk_cycles = 124,
					.maximum_latency_when_non_urgent_uclk_cycles = 874,
					.average_latency_when_non_urgent_uclk_cycles = 155,
				},
				{
					.minimum_uclk_khz = 1187,
					.urgent_ramp_uclk_cycles = 716,
					.trip_to_memory_uclk_cycles = 902,
					.meta_trip_to_memory_uclk_cycles = 902,
					.maximum_latency_when_urgent_uclk_cycles = 316,
					.average_latency_when_urgent_uclk_cycles = 160,
					.maximum_latency_when_non_urgent_uclk_cycles = 902,
					.average_latency_when_non_urgent_uclk_cycles = 177,
				},
			},
		},
	},
	.qos_type = dml2_qos_param_type_dcn4,
};

static const struct dml2_soc_bb dml2_socbb_dcn31 = {
	.clk_table = {
		.uclk = {
				.clk_values_khz = {97000, 435000, 731000, 1187000},
				.num_clk_values = 4,
		},
		.fclk = {
				.clk_values_khz = {300000, 2500000},
				.num_clk_values = 2,
		},
		.dcfclk = {
				.clk_values_khz = {200000, 1800000},
				.num_clk_values = 2,
		},
		.dispclk = {
				.clk_values_khz = {100000, 2000000},
				.num_clk_values = 2,
		},
		.dppclk = {
				.clk_values_khz = {100000, 2000000},
				.num_clk_values = 2,
		},
		.dtbclk = {
				.clk_values_khz = {100000, 2000000},
				.num_clk_values = 2,
		},
		.phyclk = {
				.clk_values_khz = {810000, 810000},
				.num_clk_values = 2,
		},
		.socclk = {
				.clk_values_khz = {300000, 1600000},
				.num_clk_values = 2,
		},
		.dscclk = {
				.clk_values_khz = {666667, 666667},
				.num_clk_values = 2,
		},
		.phyclk_d18 = {
				.clk_values_khz = {625000, 625000},
				.num_clk_values = 2,
		},
		.phyclk_d32 = {
				.clk_values_khz = {2000000, 2000000},
				.num_clk_values = 2,
		},
		.dram_config = {
			.channel_width_bytes = 2,
			.channel_count = 16,
			.transactions_per_clock = 16,
		},
	},

	.qos_parameters = {
		.derate_table = {
			.system_active_urgent = {
				.dram_derate_percent_pixel = 22,
				.dram_derate_percent_vm = 0,
				.dram_derate_percent_pixel_and_vm = 0,
				.fclk_derate_percent = 76,
				.dcfclk_derate_percent = 100,
			},
			.system_active_average = {
				.dram_derate_percent_pixel = 17,
				.dram_derate_percent_vm = 0,
				.dram_derate_percent_pixel_and_vm = 0,
				.fclk_derate_percent = 57,
				.dcfclk_derate_percent = 75,
			},
			.dcn_mall_prefetch_urgent = {
				.dram_derate_percent_pixel = 22,
				.dram_derate_percent_vm = 0,
				.dram_derate_percent_pixel_and_vm = 0,
				.fclk_derate_percent = 76,
				.dcfclk_derate_percent = 100,
			},
			.dcn_mall_prefetch_average = {
				.dram_derate_percent_pixel = 17,
				.dram_derate_percent_vm = 0,
				.dram_derate_percent_pixel_and_vm = 0,
				.fclk_derate_percent = 57,
				.dcfclk_derate_percent = 75,
			},
			.system_idle_average = {
				.dram_derate_percent_pixel = 17,
				.dram_derate_percent_vm = 0,
				.dram_derate_percent_pixel_and_vm = 0,
				.fclk_derate_percent = 57,
				.dcfclk_derate_percent = 100,
			},
		},
		.writeback = {
			.base_latency_us = 0,
			.scaling_factor_us = 0,
			.scaling_factor_mhz = 0,
		},
		.qos_params = {
			.dcn4 = {
				.df_qos_response_time_fclk_cycles = 300,
				.max_round_trip_to_furthest_cs_fclk_cycles = 350,
				.mall_overhead_fclk_cycles = 50,
				.meta_trip_adder_fclk_cycles = 36,
				.average_transport_distance_fclk_cycles = 260,
				.umc_urgent_ramp_latency_margin = 50,
				.umc_max_latency_margin = 30,
				.umc_average_latency_margin = 20,
				.fabric_max_transport_latency_margin = 20,
				.fabric_average_transport_latency_margin = 10,

				.per_uclk_dpm_params = {
					{
						// State 1
						.minimum_uclk_khz = 0,
						.urgent_ramp_uclk_cycles = 472,
						.trip_to_memory_uclk_cycles = 827,
						.meta_trip_to_memory_uclk_cycles = 827,
						.maximum_latency_when_urgent_uclk_cycles = 72,
						.average_latency_when_urgent_uclk_cycles = 72,
						.maximum_latency_when_non_urgent_uclk_cycles = 827,
						.average_latency_when_non_urgent_uclk_cycles = 117,
					},
					{
						// State 2
						.minimum_uclk_khz = 0,
						.urgent_ramp_uclk_cycles = 546,
						.trip_to_memory_uclk_cycles = 848,
						.meta_trip_to_memory_uclk_cycles = 848,
						.maximum_latency_when_urgent_uclk_cycles = 146,
						.average_latency_when_urgent_uclk_cycles = 146,
						.maximum_latency_when_non_urgent_uclk_cycles = 848,
						.average_latency_when_non_urgent_uclk_cycles = 133,
					},
					{
						// State 3
						.minimum_uclk_khz = 0,
						.urgent_ramp_uclk_cycles = 564,
						.trip_to_memory_uclk_cycles = 853,
						.meta_trip_to_memory_uclk_cycles = 853,
						.maximum_latency_when_urgent_uclk_cycles = 164,
						.average_latency_when_urgent_uclk_cycles = 164,
						.maximum_latency_when_non_urgent_uclk_cycles = 853,
						.average_latency_when_non_urgent_uclk_cycles = 136,
					},
					{
						// State 4
						.minimum_uclk_khz = 0,
						.urgent_ramp_uclk_cycles = 613,
						.trip_to_memory_uclk_cycles = 869,
						.meta_trip_to_memory_uclk_cycles = 869,
						.maximum_latency_when_urgent_uclk_cycles = 213,
						.average_latency_when_urgent_uclk_cycles = 213,
						.maximum_latency_when_non_urgent_uclk_cycles = 869,
						.average_latency_when_non_urgent_uclk_cycles = 149,
					},
					{
						// State 5
						.minimum_uclk_khz = 0,
						.urgent_ramp_uclk_cycles = 632,
						.trip_to_memory_uclk_cycles = 874,
						.meta_trip_to_memory_uclk_cycles = 874,
						.maximum_latency_when_urgent_uclk_cycles = 232,
						.average_latency_when_urgent_uclk_cycles = 232,
						.maximum_latency_when_non_urgent_uclk_cycles = 874,
						.average_latency_when_non_urgent_uclk_cycles = 153,
					},
					{
						// State 6
						.minimum_uclk_khz = 0,
						.urgent_ramp_uclk_cycles = 665,
						.trip_to_memory_uclk_cycles = 885,
						.meta_trip_to_memory_uclk_cycles = 885,
						.maximum_latency_when_urgent_uclk_cycles = 265,
						.average_latency_when_urgent_uclk_cycles = 265,
						.maximum_latency_when_non_urgent_uclk_cycles = 885,
						.average_latency_when_non_urgent_uclk_cycles = 161,
					},
					{
						// State 7
						.minimum_uclk_khz = 0,
						.urgent_ramp_uclk_cycles = 689,
						.trip_to_memory_uclk_cycles = 895,
						.meta_trip_to_memory_uclk_cycles = 895,
						.maximum_latency_when_urgent_uclk_cycles = 289,
						.average_latency_when_urgent_uclk_cycles = 289,
						.maximum_latency_when_non_urgent_uclk_cycles = 895,
						.average_latency_when_non_urgent_uclk_cycles = 167,
					},
					{
						// State 8
						.minimum_uclk_khz = 0,
						.urgent_ramp_uclk_cycles = 716,
						.trip_to_memory_uclk_cycles = 902,
						.meta_trip_to_memory_uclk_cycles = 902,
						.maximum_latency_when_urgent_uclk_cycles = 316,
						.average_latency_when_urgent_uclk_cycles = 316,
						.maximum_latency_when_non_urgent_uclk_cycles = 902,
						.average_latency_when_non_urgent_uclk_cycles = 174,
					},
				},
			},
		},
		.qos_type = dml2_qos_param_type_dcn4,
	},

	.power_management_parameters = {
		.dram_clk_change_blackout_us = 400,
		.fclk_change_blackout_us = 0,
		.g7_ppt_blackout_us = 0,
		.stutter_enter_plus_exit_latency_us = 50,
		.stutter_exit_latency_us = 43,
		.z8_stutter_enter_plus_exit_latency_us = 0,
		.z8_stutter_exit_latency_us = 0,
	},

	 .vmin_limit = {
		.dispclk_khz = 600 * 1000,
	 },

	.dprefclk_mhz = 700,
	.xtalclk_mhz = 100,
	.pcie_refclk_mhz = 100,
	.dchub_refclk_mhz = 50,
	.mall_allocated_for_dcn_mbytes = 64,
	.max_outstanding_reqs = 512,
	.fabric_datapath_to_dcn_data_return_bytes = 64,
	.return_bus_width_bytes = 64,
	.hostvm_min_page_size_kbytes = 0,
	.gpuvm_min_page_size_kbytes = 256,
	.phy_downspread_percent = 0,
	.dcn_downspread_percent = 0,
	.dispclk_dppclk_vco_speed_mhz = 4500,
	.do_urgent_latency_adjustment = 0,
	.mem_word_bytes = 32,
	.num_dcc_mcaches = 8,
	.mcache_size_bytes = 2048,
	.mcache_line_size_bytes = 32,
	.max_fclk_for_uclk_dpm_khz = 1250 * 1000,
};

static const struct dml2_ip_capabilities dml2_dcn31_max_ip_caps = {
	.pipe_count = 4,
	.otg_count = 4,
	.num_dsc = 4,
	.max_num_dp2p0_streams = 4,
	.max_num_hdmi_frl_outputs = 1,
	.max_num_dp2p0_outputs = 4,
	.rob_buffer_size_kbytes = 192,
	.config_return_buffer_size_in_kbytes = 1152,
	.meta_fifo_size_in_kentries = 22,
	.compressed_buffer_segment_size_in_kbytes = 64,
	.subvp_drr_scheduling_margin_us = 100,
	.subvp_prefetch_end_to_mall_start_us = 15,
	.subvp_fw_processing_delay = 15,

	.fams2 = {
		.max_allow_delay_us = 100 * 1000,
		.scheduling_delay_us = 50,
		.vertical_interrupt_ack_delay_us = 18,
		.allow_programming_delay_us = 18,
		.min_allow_width_us = 20,
		.subvp_df_throttle_delay_us = 100,
		.subvp_programming_delay_us = 18,
		.subvp_prefetch_to_mall_delay_us = 18,
		.drr_programming_delay_us = 18,
	},
};

#endif /* __DML_DML_DCN3_SOC_BB__ */
