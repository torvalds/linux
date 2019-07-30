/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 */
#ifndef __AMDGPU_SOCBB_H__
#define __AMDGPU_SOCBB_H__

struct gpu_info_voltage_scaling_v1_0 {
	uint32_t state;
	uint32_t dscclk_mhz;
	uint32_t dcfclk_mhz;
	uint32_t socclk_mhz;
	uint32_t dram_speed_mts;
	uint32_t fabricclk_mhz;
	uint32_t dispclk_mhz;
	uint32_t phyclk_mhz;
	uint32_t dppclk_mhz;
};

struct gpu_info_soc_bounding_box_v1_0 {
	uint32_t sr_exit_time_us;
	uint32_t sr_enter_plus_exit_time_us;
	uint32_t urgent_latency_us;
	uint32_t urgent_latency_pixel_data_only_us;
	uint32_t urgent_latency_pixel_mixed_with_vm_data_us;
	uint32_t urgent_latency_vm_data_only_us;
	uint32_t writeback_latency_us;
	uint32_t ideal_dram_bw_after_urgent_percent;
	uint32_t pct_ideal_dram_sdp_bw_after_urgent_pixel_only; // PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelDataOnly
	uint32_t pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm;
	uint32_t pct_ideal_dram_sdp_bw_after_urgent_vm_only;
	uint32_t max_avg_sdp_bw_use_normal_percent;
	uint32_t max_avg_dram_bw_use_normal_percent;
	uint32_t max_request_size_bytes;
	uint32_t downspread_percent;
	uint32_t dram_page_open_time_ns;
	uint32_t dram_rw_turnaround_time_ns;
	uint32_t dram_return_buffer_per_channel_bytes;
	uint32_t dram_channel_width_bytes;
	uint32_t fabric_datapath_to_dcn_data_return_bytes;
	uint32_t dcn_downspread_percent;
	uint32_t dispclk_dppclk_vco_speed_mhz;
	uint32_t dfs_vco_period_ps;
	uint32_t urgent_out_of_order_return_per_channel_pixel_only_bytes;
	uint32_t urgent_out_of_order_return_per_channel_pixel_and_vm_bytes;
	uint32_t urgent_out_of_order_return_per_channel_vm_only_bytes;
	uint32_t round_trip_ping_latency_dcfclk_cycles;
	uint32_t urgent_out_of_order_return_per_channel_bytes;
	uint32_t channel_interleave_bytes;
	uint32_t num_banks;
	uint32_t num_chans;
	uint32_t vmm_page_size_bytes;
	uint32_t dram_clock_change_latency_us;
	uint32_t writeback_dram_clock_change_latency_us;
	uint32_t return_bus_width_bytes;
	uint32_t voltage_override;
	uint32_t xfc_bus_transport_time_us;
	uint32_t xfc_xbuf_latency_tolerance_us;
	uint32_t use_urgent_burst_bw;
	uint32_t num_states;
	struct gpu_info_voltage_scaling_v1_0 clock_limits[8];
};

#endif
