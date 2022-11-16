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

#include "dm_services.h"
#include "dcn_calc_auto.h"
#include "dcn_calc_math.h"

/*
 * NOTE:
 *   This file is gcc-parseable HW gospel, coming straight from HW engineers.
 *
 * It doesn't adhere to Linux kernel style and sometimes will do things in odd
 * ways. Unless there is something clearly wrong with it the code should
 * remain as-is as it provides us with a guarantee from HW that it is correct.
 */

/*REVISION#250*/
void scaler_settings_calculation(struct dcn_bw_internal_vars *v)
{
	int k;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->allow_different_hratio_vratio == dcn_bw_yes) {
			if (v->source_scan[k] == dcn_bw_hor) {
				v->h_ratio[k] = v->viewport_width[k] / v->scaler_rec_out_width[k];
				v->v_ratio[k] = v->viewport_height[k] / v->scaler_recout_height[k];
			}
			else {
				v->h_ratio[k] = v->viewport_height[k] / v->scaler_rec_out_width[k];
				v->v_ratio[k] = v->viewport_width[k] / v->scaler_recout_height[k];
			}
		}
		else {
			if (v->source_scan[k] == dcn_bw_hor) {
				v->h_ratio[k] =dcn_bw_max2(v->viewport_width[k] / v->scaler_rec_out_width[k], v->viewport_height[k] / v->scaler_recout_height[k]);
			}
			else {
				v->h_ratio[k] =dcn_bw_max2(v->viewport_height[k] / v->scaler_rec_out_width[k], v->viewport_width[k] / v->scaler_recout_height[k]);
			}
			v->v_ratio[k] = v->h_ratio[k];
		}
		if (v->interlace_output[k] == 1.0) {
			v->v_ratio[k] = 2.0 * v->v_ratio[k];
		}
		if (v->underscan_output[k] == 1.0) {
			v->h_ratio[k] = v->h_ratio[k] * v->under_scan_factor;
			v->v_ratio[k] = v->v_ratio[k] * v->under_scan_factor;
		}
	}
	/*scaler taps calculation*/

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->h_ratio[k] > 1.0) {
			v->acceptable_quality_hta_ps =dcn_bw_min2(v->max_hscl_taps, 2.0 *dcn_bw_ceil2(v->h_ratio[k], 1.0));
		}
		else if (v->h_ratio[k] < 1.0) {
			v->acceptable_quality_hta_ps = 4.0;
		}
		else {
			v->acceptable_quality_hta_ps = 1.0;
		}
		if (v->ta_pscalculation == dcn_bw_override) {
			v->htaps[k] = v->override_hta_ps[k];
		}
		else {
			v->htaps[k] = v->acceptable_quality_hta_ps;
		}
		if (v->v_ratio[k] > 1.0) {
			v->acceptable_quality_vta_ps =dcn_bw_min2(v->max_vscl_taps, 2.0 *dcn_bw_ceil2(v->v_ratio[k], 1.0));
		}
		else if (v->v_ratio[k] < 1.0) {
			v->acceptable_quality_vta_ps = 4.0;
		}
		else {
			v->acceptable_quality_vta_ps = 1.0;
		}
		if (v->ta_pscalculation == dcn_bw_override) {
			v->vtaps[k] = v->override_vta_ps[k];
		}
		else {
			v->vtaps[k] = v->acceptable_quality_vta_ps;
		}
		if (v->source_pixel_format[k] == dcn_bw_rgb_sub_64 || v->source_pixel_format[k] == dcn_bw_rgb_sub_32 || v->source_pixel_format[k] == dcn_bw_rgb_sub_16) {
			v->vta_pschroma[k] = 0.0;
			v->hta_pschroma[k] = 0.0;
		}
		else {
			if (v->ta_pscalculation == dcn_bw_override) {
				v->vta_pschroma[k] = v->override_vta_pschroma[k];
				v->hta_pschroma[k] = v->override_hta_pschroma[k];
			}
			else {
				v->vta_pschroma[k] = v->acceptable_quality_vta_ps;
				v->hta_pschroma[k] = v->acceptable_quality_hta_ps;
			}
		}
	}
}

void mode_support_and_system_configuration(struct dcn_bw_internal_vars *v)
{
	int i;
	int j;
	int k;
	/*mode support, voltage state and soc configuration*/

	/*scale ratio support check*/

	v->scale_ratio_support = dcn_bw_yes;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->h_ratio[k] > v->max_hscl_ratio || v->v_ratio[k] > v->max_vscl_ratio || v->h_ratio[k] > v->htaps[k] || v->v_ratio[k] > v->vtaps[k] || (v->source_pixel_format[k] != dcn_bw_rgb_sub_64 && v->source_pixel_format[k] != dcn_bw_rgb_sub_32 && v->source_pixel_format[k] != dcn_bw_rgb_sub_16 && (v->h_ratio[k] / 2.0 > v->hta_pschroma[k] || v->v_ratio[k] / 2.0 > v->vta_pschroma[k]))) {
			v->scale_ratio_support = dcn_bw_no;
		}
	}
	/*source format, pixel format and scan support check*/

	v->source_format_pixel_and_scan_support = dcn_bw_yes;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if ((v->source_surface_mode[k] == dcn_bw_sw_linear && v->source_scan[k] != dcn_bw_hor) || ((v->source_surface_mode[k] == dcn_bw_sw_4_kb_d || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d_x || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_x || v->source_surface_mode[k] == dcn_bw_sw_var_d || v->source_surface_mode[k] == dcn_bw_sw_var_d_x) && v->source_pixel_format[k] != dcn_bw_rgb_sub_64)) {
			v->source_format_pixel_and_scan_support = dcn_bw_no;
		}
	}
	/*bandwidth support check*/

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->source_scan[k] == dcn_bw_hor) {
			v->swath_width_ysingle_dpp[k] = v->viewport_width[k];
		}
		else {
			v->swath_width_ysingle_dpp[k] = v->viewport_height[k];
		}
		if (v->source_pixel_format[k] == dcn_bw_rgb_sub_64) {
			v->byte_per_pixel_in_dety[k] = 8.0;
			v->byte_per_pixel_in_detc[k] = 0.0;
		}
		else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_32) {
			v->byte_per_pixel_in_dety[k] = 4.0;
			v->byte_per_pixel_in_detc[k] = 0.0;
		}
		else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_16) {
			v->byte_per_pixel_in_dety[k] = 2.0;
			v->byte_per_pixel_in_detc[k] = 0.0;
		}
		else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_8) {
			v->byte_per_pixel_in_dety[k] = 1.0;
			v->byte_per_pixel_in_detc[k] = 2.0;
		}
		else {
			v->byte_per_pixel_in_dety[k] = 4.0f / 3.0f;
			v->byte_per_pixel_in_detc[k] = 8.0f / 3.0f;
		}
	}
	v->total_read_bandwidth_consumed_gbyte_per_second = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->read_bandwidth[k] = v->swath_width_ysingle_dpp[k] * (dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) * v->v_ratio[k] +dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / 2.0 * v->v_ratio[k] / 2) / (v->htotal[k] / v->pixel_clock[k]);
		if (v->dcc_enable[k] == dcn_bw_yes) {
			v->read_bandwidth[k] = v->read_bandwidth[k] * (1 + 1 / 256);
		}
		if (v->pte_enable == dcn_bw_yes && v->source_scan[k] != dcn_bw_hor && (v->source_surface_mode[k] == dcn_bw_sw_4_kb_s || v->source_surface_mode[k] == dcn_bw_sw_4_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d_x)) {
			v->read_bandwidth[k] = v->read_bandwidth[k] * (1 + 1 / 64);
		}
		else if (v->pte_enable == dcn_bw_yes && v->source_scan[k] == dcn_bw_hor && (v->source_pixel_format[k] == dcn_bw_rgb_sub_64 || v->source_pixel_format[k] == dcn_bw_rgb_sub_32) && (v->source_surface_mode[k] == dcn_bw_sw_64_kb_s || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_x)) {
			v->read_bandwidth[k] = v->read_bandwidth[k] * (1 + 1 / 256);
		}
		else if (v->pte_enable == dcn_bw_yes) {
			v->read_bandwidth[k] = v->read_bandwidth[k] * (1 + 1 / 512);
		}
		v->total_read_bandwidth_consumed_gbyte_per_second = v->total_read_bandwidth_consumed_gbyte_per_second + v->read_bandwidth[k] / 1000.0;
	}
	v->total_write_bandwidth_consumed_gbyte_per_second = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->output[k] == dcn_bw_writeback && v->output_format[k] == dcn_bw_444) {
			v->write_bandwidth[k] = v->scaler_rec_out_width[k] / (v->htotal[k] / v->pixel_clock[k]) * 4.0;
		}
		else if (v->output[k] == dcn_bw_writeback) {
			v->write_bandwidth[k] = v->scaler_rec_out_width[k] / (v->htotal[k] / v->pixel_clock[k]) * 1.5;
		}
		else {
			v->write_bandwidth[k] = 0.0;
		}
		v->total_write_bandwidth_consumed_gbyte_per_second = v->total_write_bandwidth_consumed_gbyte_per_second + v->write_bandwidth[k] / 1000.0;
	}
	v->total_bandwidth_consumed_gbyte_per_second = v->total_read_bandwidth_consumed_gbyte_per_second + v->total_write_bandwidth_consumed_gbyte_per_second;
	v->dcc_enabled_in_any_plane = dcn_bw_no;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->dcc_enable[k] == dcn_bw_yes) {
			v->dcc_enabled_in_any_plane = dcn_bw_yes;
		}
	}
	for (i = 0; i <= number_of_states_plus_one; i++) {
		v->return_bw_todcn_per_state =dcn_bw_min2(v->return_bus_width * v->dcfclk_per_state[i], v->fabric_and_dram_bandwidth_per_state[i] * 1000.0 * v->percent_of_ideal_drambw_received_after_urg_latency / 100.0);
		v->return_bw_per_state[i] = v->return_bw_todcn_per_state;
		if (v->dcc_enabled_in_any_plane == dcn_bw_yes && v->return_bw_todcn_per_state > v->dcfclk_per_state[i] * v->return_bus_width / 4.0) {
			v->return_bw_per_state[i] =dcn_bw_min2(v->return_bw_per_state[i], v->return_bw_todcn_per_state * 4.0 * (1.0 - v->urgent_latency / ((v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0 / (v->return_bw_todcn_per_state - v->dcfclk_per_state[i] * v->return_bus_width / 4.0) + v->urgent_latency)));
		}
		v->critical_point = 2.0 * v->return_bus_width * v->dcfclk_per_state[i] * v->urgent_latency / (v->return_bw_todcn_per_state * v->urgent_latency + (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0);
		if (v->dcc_enabled_in_any_plane == dcn_bw_yes && v->critical_point > 1.0 && v->critical_point < 4.0) {
			v->return_bw_per_state[i] =dcn_bw_min2(v->return_bw_per_state[i], dcn_bw_pow(4.0 * v->return_bw_todcn_per_state * (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0 * v->return_bus_width * v->dcfclk_per_state[i] * v->urgent_latency / (v->return_bw_todcn_per_state * v->urgent_latency + (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0), 2));
		}
		v->return_bw_todcn_per_state =dcn_bw_min2(v->return_bus_width * v->dcfclk_per_state[i], v->fabric_and_dram_bandwidth_per_state[i] * 1000.0);
		if (v->dcc_enabled_in_any_plane == dcn_bw_yes && v->return_bw_todcn_per_state > v->dcfclk_per_state[i] * v->return_bus_width / 4.0) {
			v->return_bw_per_state[i] =dcn_bw_min2(v->return_bw_per_state[i], v->return_bw_todcn_per_state * 4.0 * (1.0 - v->urgent_latency / ((v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0 / (v->return_bw_todcn_per_state - v->dcfclk_per_state[i] * v->return_bus_width / 4.0) + v->urgent_latency)));
		}
		v->critical_point = 2.0 * v->return_bus_width * v->dcfclk_per_state[i] * v->urgent_latency / (v->return_bw_todcn_per_state * v->urgent_latency + (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0);
		if (v->dcc_enabled_in_any_plane == dcn_bw_yes && v->critical_point > 1.0 && v->critical_point < 4.0) {
			v->return_bw_per_state[i] =dcn_bw_min2(v->return_bw_per_state[i], dcn_bw_pow(4.0 * v->return_bw_todcn_per_state * (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0 * v->return_bus_width * v->dcfclk_per_state[i] * v->urgent_latency / (v->return_bw_todcn_per_state * v->urgent_latency + (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0), 2));
		}
	}
	for (i = 0; i <= number_of_states_plus_one; i++) {
		if ((v->total_read_bandwidth_consumed_gbyte_per_second * 1000.0 <= v->return_bw_per_state[i]) && (v->total_bandwidth_consumed_gbyte_per_second * 1000.0 <= v->fabric_and_dram_bandwidth_per_state[i] * 1000.0 * v->percent_of_ideal_drambw_received_after_urg_latency / 100.0)) {
			v->bandwidth_support[i] = dcn_bw_yes;
		}
		else {
			v->bandwidth_support[i] = dcn_bw_no;
		}
	}
	/*writeback latency support check*/

	v->writeback_latency_support = dcn_bw_yes;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->output[k] == dcn_bw_writeback && v->output_format[k] == dcn_bw_444 && v->scaler_rec_out_width[k] / (v->htotal[k] / v->pixel_clock[k]) * 4.0 > (v->writeback_luma_buffer_size + v->writeback_chroma_buffer_size) * 1024.0 / v->write_back_latency) {
			v->writeback_latency_support = dcn_bw_no;
		}
		else if (v->output[k] == dcn_bw_writeback && v->scaler_rec_out_width[k] / (v->htotal[k] / v->pixel_clock[k]) >dcn_bw_min2(v->writeback_luma_buffer_size, 2.0 * v->writeback_chroma_buffer_size) * 1024.0 / v->write_back_latency) {
			v->writeback_latency_support = dcn_bw_no;
		}
	}
	/*re-ordering buffer support check*/

	for (i = 0; i <= number_of_states_plus_one; i++) {
		v->urgent_round_trip_and_out_of_order_latency_per_state[i] = (v->round_trip_ping_latency_cycles + 32.0) / v->dcfclk_per_state[i] + v->urgent_out_of_order_return_per_channel * v->number_of_channels / v->return_bw_per_state[i];
		if ((v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0 / v->return_bw_per_state[i] > v->urgent_round_trip_and_out_of_order_latency_per_state[i]) {
			v->rob_support[i] = dcn_bw_yes;
		}
		else {
			v->rob_support[i] = dcn_bw_no;
		}
	}
	/*display io support check*/

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->output[k] == dcn_bw_dp && v->dsc_capability == dcn_bw_yes) {
			if (v->output_format[k] == dcn_bw_420) {
				v->required_output_bw = v->pixel_clock[k] / 2.0;
			}
			else {
				v->required_output_bw = v->pixel_clock[k];
			}
		}
		else if (v->output_format[k] == dcn_bw_420) {
			v->required_output_bw = v->pixel_clock[k] * 3.0 / 2.0;
		}
		else {
			v->required_output_bw = v->pixel_clock[k] * 3.0;
		}
		if (v->output[k] == dcn_bw_hdmi) {
			v->required_phyclk[k] = v->required_output_bw;
			switch (v->output_deep_color[k]) {
			case dcn_bw_encoder_10bpc:
				v->required_phyclk[k] =  v->required_phyclk[k] * 5.0 / 4;
			break;
			case dcn_bw_encoder_12bpc:
				v->required_phyclk[k] =  v->required_phyclk[k] * 3.0 / 2;
				break;
			default:
				break;
			}
			v->required_phyclk[k] = v->required_phyclk[k] / 3.0;
		}
		else if (v->output[k] == dcn_bw_dp) {
			v->required_phyclk[k] = v->required_output_bw / 4.0;
		}
		else {
			v->required_phyclk[k] = 0.0;
		}
	}
	for (i = 0; i <= number_of_states_plus_one; i++) {
		v->dio_support[i] = dcn_bw_yes;
		for (k = 0; k <= v->number_of_active_planes - 1; k++) {
			if (v->required_phyclk[k] > v->phyclk_per_state[i] || (v->output[k] == dcn_bw_hdmi && v->required_phyclk[k] > 600.0)) {
				v->dio_support[i] = dcn_bw_no;
			}
		}
	}
	/*total available writeback support check*/

	v->total_number_of_active_writeback = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->output[k] == dcn_bw_writeback) {
			v->total_number_of_active_writeback = v->total_number_of_active_writeback + 1.0;
		}
	}
	if (v->total_number_of_active_writeback <= v->max_num_writeback) {
		v->total_available_writeback_support = dcn_bw_yes;
	}
	else {
		v->total_available_writeback_support = dcn_bw_no;
	}
	/*maximum dispclk/dppclk support check*/

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->h_ratio[k] > 1.0) {
			v->pscl_factor[k] =dcn_bw_min2(v->max_dchub_topscl_throughput, v->max_pscl_tolb_throughput * v->h_ratio[k] /dcn_bw_ceil2(v->htaps[k] / 6.0, 1.0));
		}
		else {
			v->pscl_factor[k] =dcn_bw_min2(v->max_dchub_topscl_throughput, v->max_pscl_tolb_throughput);
		}
		if (v->byte_per_pixel_in_detc[k] == 0.0) {
			v->pscl_factor_chroma[k] = 0.0;
			v->min_dppclk_using_single_dpp[k] = v->pixel_clock[k] *dcn_bw_max3(v->vtaps[k] / 6.0 *dcn_bw_min2(1.0, v->h_ratio[k]), v->h_ratio[k] * v->v_ratio[k] / v->pscl_factor[k], 1.0);
		}
		else {
			if (v->h_ratio[k] / 2.0 > 1.0) {
				v->pscl_factor_chroma[k] =dcn_bw_min2(v->max_dchub_topscl_throughput, v->max_pscl_tolb_throughput * v->h_ratio[k] / 2.0 /dcn_bw_ceil2(v->hta_pschroma[k] / 6.0, 1.0));
			}
			else {
				v->pscl_factor_chroma[k] =dcn_bw_min2(v->max_dchub_topscl_throughput, v->max_pscl_tolb_throughput);
			}
			v->min_dppclk_using_single_dpp[k] = v->pixel_clock[k] *dcn_bw_max5(v->vtaps[k] / 6.0 *dcn_bw_min2(1.0, v->h_ratio[k]), v->h_ratio[k] * v->v_ratio[k] / v->pscl_factor[k], v->vta_pschroma[k] / 6.0 *dcn_bw_min2(1.0, v->h_ratio[k] / 2.0), v->h_ratio[k] * v->v_ratio[k] / 4.0 / v->pscl_factor_chroma[k], 1.0);
		}
	}
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if ((v->source_pixel_format[k] == dcn_bw_rgb_sub_64 || v->source_pixel_format[k] == dcn_bw_rgb_sub_32 || v->source_pixel_format[k] == dcn_bw_rgb_sub_16)) {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
				v->read256_block_height_y[k] = 1.0;
			}
			else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_64) {
				v->read256_block_height_y[k] = 4.0;
			}
			else {
				v->read256_block_height_y[k] = 8.0;
			}
			v->read256_block_width_y[k] = 256.0 /dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / v->read256_block_height_y[k];
			v->read256_block_height_c[k] = 0.0;
			v->read256_block_width_c[k] = 0.0;
		}
		else {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
				v->read256_block_height_y[k] = 1.0;
				v->read256_block_height_c[k] = 1.0;
			}
			else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_8) {
				v->read256_block_height_y[k] = 16.0;
				v->read256_block_height_c[k] = 8.0;
			}
			else {
				v->read256_block_height_y[k] = 8.0;
				v->read256_block_height_c[k] = 8.0;
			}
			v->read256_block_width_y[k] = 256.0 /dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / v->read256_block_height_y[k];
			v->read256_block_width_c[k] = 256.0 /dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / v->read256_block_height_c[k];
		}
		if (v->source_scan[k] == dcn_bw_hor) {
			v->max_swath_height_y[k] = v->read256_block_height_y[k];
			v->max_swath_height_c[k] = v->read256_block_height_c[k];
		}
		else {
			v->max_swath_height_y[k] = v->read256_block_width_y[k];
			v->max_swath_height_c[k] = v->read256_block_width_c[k];
		}
		if ((v->source_pixel_format[k] == dcn_bw_rgb_sub_64 || v->source_pixel_format[k] == dcn_bw_rgb_sub_32 || v->source_pixel_format[k] == dcn_bw_rgb_sub_16)) {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear || (v->source_pixel_format[k] == dcn_bw_rgb_sub_64 && (v->source_surface_mode[k] == dcn_bw_sw_4_kb_s || v->source_surface_mode[k] == dcn_bw_sw_4_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_var_s || v->source_surface_mode[k] == dcn_bw_sw_var_s_x) && v->source_scan[k] == dcn_bw_hor)) {
				v->min_swath_height_y[k] = v->max_swath_height_y[k];
			}
			else {
				v->min_swath_height_y[k] = v->max_swath_height_y[k] / 2.0;
			}
			v->min_swath_height_c[k] = v->max_swath_height_c[k];
		}
		else {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
				v->min_swath_height_y[k] = v->max_swath_height_y[k];
				v->min_swath_height_c[k] = v->max_swath_height_c[k];
			}
			else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_8 && v->source_scan[k] == dcn_bw_hor) {
				v->min_swath_height_y[k] = v->max_swath_height_y[k] / 2.0;
				if (v->bug_forcing_luma_and_chroma_request_to_same_size_fixed == dcn_bw_yes) {
					v->min_swath_height_c[k] = v->max_swath_height_c[k];
				}
				else {
					v->min_swath_height_c[k] = v->max_swath_height_c[k] / 2.0;
				}
			}
			else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_10 && v->source_scan[k] == dcn_bw_hor) {
				v->min_swath_height_c[k] = v->max_swath_height_c[k] / 2.0;
				if (v->bug_forcing_luma_and_chroma_request_to_same_size_fixed == dcn_bw_yes) {
					v->min_swath_height_y[k] = v->max_swath_height_y[k];
				}
				else {
					v->min_swath_height_y[k] = v->max_swath_height_y[k] / 2.0;
				}
			}
			else {
				v->min_swath_height_y[k] = v->max_swath_height_y[k];
				v->min_swath_height_c[k] = v->max_swath_height_c[k];
			}
		}
		if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
			v->maximum_swath_width = 8192.0;
		}
		else {
			v->maximum_swath_width = 5120.0;
		}
		v->number_of_dpp_required_for_det_size =dcn_bw_ceil2(v->swath_width_ysingle_dpp[k] /dcn_bw_min2(v->maximum_swath_width, v->det_buffer_size_in_kbyte * 1024.0 / 2.0 / (v->byte_per_pixel_in_dety[k] * v->min_swath_height_y[k] + v->byte_per_pixel_in_detc[k] / 2.0 * v->min_swath_height_c[k])), 1.0);
		if (v->byte_per_pixel_in_detc[k] == 0.0) {
			v->number_of_dpp_required_for_lb_size =dcn_bw_ceil2((v->vtaps[k] +dcn_bw_max2(dcn_bw_ceil2(v->v_ratio[k], 1.0) - 2, 0.0)) * v->swath_width_ysingle_dpp[k] /dcn_bw_max2(v->h_ratio[k], 1.0) * v->lb_bit_per_pixel[k] / v->line_buffer_size, 1.0);
		}
		else {
			v->number_of_dpp_required_for_lb_size =dcn_bw_max2(dcn_bw_ceil2((v->vtaps[k] +dcn_bw_max2(dcn_bw_ceil2(v->v_ratio[k], 1.0) - 2, 0.0)) * v->swath_width_ysingle_dpp[k] /dcn_bw_max2(v->h_ratio[k], 1.0) * v->lb_bit_per_pixel[k] / v->line_buffer_size, 1.0),dcn_bw_ceil2((v->vta_pschroma[k] +dcn_bw_max2(dcn_bw_ceil2(v->v_ratio[k] / 2.0, 1.0) - 2, 0.0)) * v->swath_width_ysingle_dpp[k] / 2.0 /dcn_bw_max2(v->h_ratio[k] / 2.0, 1.0) * v->lb_bit_per_pixel[k] / v->line_buffer_size, 1.0));
		}
		v->number_of_dpp_required_for_det_and_lb_size[k] =dcn_bw_max2(v->number_of_dpp_required_for_det_size, v->number_of_dpp_required_for_lb_size);
	}
	for (i = 0; i <= number_of_states_plus_one; i++) {
		for (j = 0; j <= 1; j++) {
			v->total_number_of_active_dpp[i][j] = 0.0;
			v->required_dispclk[i][j] = 0.0;
			v->dispclk_dppclk_support[i][j] = dcn_bw_yes;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				v->min_dispclk_using_single_dpp =dcn_bw_max2(v->pixel_clock[k], v->min_dppclk_using_single_dpp[k] * (j + 1)) * (1.0 + v->downspreading / 100.0);
				if (v->odm_capability == dcn_bw_yes) {
					v->min_dispclk_using_dual_dpp =dcn_bw_max2(v->pixel_clock[k] / 2.0, v->min_dppclk_using_single_dpp[k] / 2.0 * (j + 1)) * (1.0 + v->downspreading / 100.0);
				}
				else {
					v->min_dispclk_using_dual_dpp =dcn_bw_max2(v->pixel_clock[k], v->min_dppclk_using_single_dpp[k] / 2.0 * (j + 1)) * (1.0 + v->downspreading / 100.0);
				}
				if (i < number_of_states) {
					v->min_dispclk_using_single_dpp = v->min_dispclk_using_single_dpp * (1.0 + v->dispclk_ramping_margin / 100.0);
					v->min_dispclk_using_dual_dpp = v->min_dispclk_using_dual_dpp * (1.0 + v->dispclk_ramping_margin / 100.0);
				}
				if (v->min_dispclk_using_single_dpp <=dcn_bw_min2(v->max_dispclk[i], (j + 1) * v->max_dppclk[i]) && v->number_of_dpp_required_for_det_and_lb_size[k] <= 1.0) {
					v->no_of_dpp[i][j][k] = 1.0;
					v->required_dispclk[i][j] =dcn_bw_max2(v->required_dispclk[i][j], v->min_dispclk_using_single_dpp);
				}
				else if (v->min_dispclk_using_dual_dpp <=dcn_bw_min2(v->max_dispclk[i], (j + 1) * v->max_dppclk[i])) {
					v->no_of_dpp[i][j][k] = 2.0;
					v->required_dispclk[i][j] =dcn_bw_max2(v->required_dispclk[i][j], v->min_dispclk_using_dual_dpp);
				}
				else {
					v->no_of_dpp[i][j][k] = 2.0;
					v->required_dispclk[i][j] =dcn_bw_max2(v->required_dispclk[i][j], v->min_dispclk_using_dual_dpp);
					v->dispclk_dppclk_support[i][j] = dcn_bw_no;
				}
				v->total_number_of_active_dpp[i][j] = v->total_number_of_active_dpp[i][j] + v->no_of_dpp[i][j][k];
			}
			if (v->total_number_of_active_dpp[i][j] > v->max_num_dpp) {
				v->total_number_of_active_dpp[i][j] = 0.0;
				v->required_dispclk[i][j] = 0.0;
				v->dispclk_dppclk_support[i][j] = dcn_bw_yes;
				for (k = 0; k <= v->number_of_active_planes - 1; k++) {
					v->min_dispclk_using_single_dpp =dcn_bw_max2(v->pixel_clock[k], v->min_dppclk_using_single_dpp[k] * (j + 1)) * (1.0 + v->downspreading / 100.0);
					v->min_dispclk_using_dual_dpp =dcn_bw_max2(v->pixel_clock[k], v->min_dppclk_using_single_dpp[k] / 2.0 * (j + 1)) * (1.0 + v->downspreading / 100.0);
					if (i < number_of_states) {
						v->min_dispclk_using_single_dpp = v->min_dispclk_using_single_dpp * (1.0 + v->dispclk_ramping_margin / 100.0);
						v->min_dispclk_using_dual_dpp = v->min_dispclk_using_dual_dpp * (1.0 + v->dispclk_ramping_margin / 100.0);
					}
					if (v->number_of_dpp_required_for_det_and_lb_size[k] <= 1.0) {
						v->no_of_dpp[i][j][k] = 1.0;
						v->required_dispclk[i][j] =dcn_bw_max2(v->required_dispclk[i][j], v->min_dispclk_using_single_dpp);
						if (v->min_dispclk_using_single_dpp >dcn_bw_min2(v->max_dispclk[i], (j + 1) * v->max_dppclk[i])) {
							v->dispclk_dppclk_support[i][j] = dcn_bw_no;
						}
					}
					else {
						v->no_of_dpp[i][j][k] = 2.0;
						v->required_dispclk[i][j] =dcn_bw_max2(v->required_dispclk[i][j], v->min_dispclk_using_dual_dpp);
						if (v->min_dispclk_using_dual_dpp >dcn_bw_min2(v->max_dispclk[i], (j + 1) * v->max_dppclk[i])) {
							v->dispclk_dppclk_support[i][j] = dcn_bw_no;
						}
					}
					v->total_number_of_active_dpp[i][j] = v->total_number_of_active_dpp[i][j] + v->no_of_dpp[i][j][k];
				}
			}
		}
	}
	/*viewport size check*/

	v->viewport_size_support = dcn_bw_yes;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->number_of_dpp_required_for_det_and_lb_size[k] > 2.0) {
			v->viewport_size_support = dcn_bw_no;
		}
	}
	/*total available pipes support check*/

	for (i = 0; i <= number_of_states_plus_one; i++) {
		for (j = 0; j <= 1; j++) {
			if (v->total_number_of_active_dpp[i][j] <= v->max_num_dpp) {
				v->total_available_pipes_support[i][j] = dcn_bw_yes;
			}
			else {
				v->total_available_pipes_support[i][j] = dcn_bw_no;
			}
		}
	}
	/*urgent latency support check*/

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		for (i = 0; i <= number_of_states_plus_one; i++) {
			for (j = 0; j <= 1; j++) {
				v->swath_width_yper_state[i][j][k] = v->swath_width_ysingle_dpp[k] / v->no_of_dpp[i][j][k];
				v->swath_width_granularity_y = 256.0 /dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / v->max_swath_height_y[k];
				v->rounded_up_max_swath_size_bytes_y = (dcn_bw_ceil2(v->swath_width_yper_state[i][j][k] - 1.0, v->swath_width_granularity_y) + v->swath_width_granularity_y) * v->byte_per_pixel_in_dety[k] * v->max_swath_height_y[k];
				if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_10) {
					v->rounded_up_max_swath_size_bytes_y =dcn_bw_ceil2(v->rounded_up_max_swath_size_bytes_y, 256.0) + 256;
				}
				if (v->max_swath_height_c[k] > 0.0) {
					v->swath_width_granularity_c = 256.0 /dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / v->max_swath_height_c[k];
					v->rounded_up_max_swath_size_bytes_c = (dcn_bw_ceil2(v->swath_width_yper_state[i][j][k] / 2.0 - 1.0, v->swath_width_granularity_c) + v->swath_width_granularity_c) * v->byte_per_pixel_in_detc[k] * v->max_swath_height_c[k];
					if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_10) {
						v->rounded_up_max_swath_size_bytes_c = dcn_bw_ceil2(v->rounded_up_max_swath_size_bytes_c, 256.0) + 256;
					}
				}
				if (v->rounded_up_max_swath_size_bytes_y + v->rounded_up_max_swath_size_bytes_c <= v->det_buffer_size_in_kbyte * 1024.0 / 2.0) {
					v->swath_height_yper_state[i][j][k] = v->max_swath_height_y[k];
					v->swath_height_cper_state[i][j][k] = v->max_swath_height_c[k];
				}
				else {
					v->swath_height_yper_state[i][j][k] = v->min_swath_height_y[k];
					v->swath_height_cper_state[i][j][k] = v->min_swath_height_c[k];
				}
				if (v->byte_per_pixel_in_detc[k] == 0.0) {
					v->lines_in_det_luma = v->det_buffer_size_in_kbyte * 1024.0 / v->byte_per_pixel_in_dety[k] / v->swath_width_yper_state[i][j][k];
					v->lines_in_det_chroma = 0.0;
				}
				else if (v->swath_height_yper_state[i][j][k] <= v->swath_height_cper_state[i][j][k]) {
					v->lines_in_det_luma = v->det_buffer_size_in_kbyte * 1024.0 / 2.0 / v->byte_per_pixel_in_dety[k] / v->swath_width_yper_state[i][j][k];
					v->lines_in_det_chroma = v->det_buffer_size_in_kbyte * 1024.0 / 2.0 / v->byte_per_pixel_in_detc[k] / (v->swath_width_yper_state[i][j][k] / 2.0);
				}
				else {
					v->lines_in_det_luma = v->det_buffer_size_in_kbyte * 1024.0 * 2.0 / 3.0 / v->byte_per_pixel_in_dety[k] / v->swath_width_yper_state[i][j][k];
					v->lines_in_det_chroma = v->det_buffer_size_in_kbyte * 1024.0 / 3.0 / v->byte_per_pixel_in_dety[k] / (v->swath_width_yper_state[i][j][k] / 2.0);
				}
				v->effective_lb_latency_hiding_source_lines_luma =dcn_bw_min2(v->max_line_buffer_lines,dcn_bw_floor2(v->line_buffer_size / v->lb_bit_per_pixel[k] / (v->swath_width_yper_state[i][j][k] /dcn_bw_max2(v->h_ratio[k], 1.0)), 1.0)) - (v->vtaps[k] - 1.0);
				v->effective_detlb_lines_luma =dcn_bw_floor2(v->lines_in_det_luma +dcn_bw_min2(v->lines_in_det_luma * v->required_dispclk[i][j] * v->byte_per_pixel_in_dety[k] * v->pscl_factor[k] / v->return_bw_per_state[i], v->effective_lb_latency_hiding_source_lines_luma), v->swath_height_yper_state[i][j][k]);
				if (v->byte_per_pixel_in_detc[k] == 0.0) {
					v->urgent_latency_support_us_per_state[i][j][k] = v->effective_detlb_lines_luma * (v->htotal[k] / v->pixel_clock[k]) / v->v_ratio[k] - v->effective_detlb_lines_luma * v->swath_width_yper_state[i][j][k] *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / (v->return_bw_per_state[i] / v->no_of_dpp[i][j][k]);
				}
				else {
					v->effective_lb_latency_hiding_source_lines_chroma = dcn_bw_min2(v->max_line_buffer_lines, dcn_bw_floor2(v->line_buffer_size / v->lb_bit_per_pixel[k] / (v->swath_width_yper_state[i][j][k] / 2.0 / dcn_bw_max2(v->h_ratio[k] / 2.0, 1.0)), 1.0)) - (v->vta_pschroma[k] - 1.0);
					v->effective_detlb_lines_chroma = dcn_bw_floor2(v->lines_in_det_chroma + dcn_bw_min2(v->lines_in_det_chroma * v->required_dispclk[i][j] * v->byte_per_pixel_in_detc[k] * v->pscl_factor_chroma[k] / v->return_bw_per_state[i], v->effective_lb_latency_hiding_source_lines_chroma), v->swath_height_cper_state[i][j][k]);
					v->urgent_latency_support_us_per_state[i][j][k] = dcn_bw_min2(v->effective_detlb_lines_luma * (v->htotal[k] / v->pixel_clock[k]) / v->v_ratio[k] - v->effective_detlb_lines_luma * v->swath_width_yper_state[i][j][k] * dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / (v->return_bw_per_state[i] / v->no_of_dpp[i][j][k]), v->effective_detlb_lines_chroma * (v->htotal[k] / v->pixel_clock[k]) / (v->v_ratio[k] / 2.0) - v->effective_detlb_lines_chroma * v->swath_width_yper_state[i][j][k] / 2.0 * dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / (v->return_bw_per_state[i] / v->no_of_dpp[i][j][k]));
				}
			}
		}
	}
	for (i = 0; i <= number_of_states_plus_one; i++) {
		for (j = 0; j <= 1; j++) {
			v->urgent_latency_support[i][j] = dcn_bw_yes;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if (v->urgent_latency_support_us_per_state[i][j][k] < v->urgent_latency / 1.0) {
					v->urgent_latency_support[i][j] = dcn_bw_no;
				}
			}
		}
	}
	/*prefetch check*/

	for (i = 0; i <= number_of_states_plus_one; i++) {
		for (j = 0; j <= 1; j++) {
			v->total_number_of_dcc_active_dpp[i][j] = 0.0;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if (v->dcc_enable[k] == dcn_bw_yes) {
					v->total_number_of_dcc_active_dpp[i][j] = v->total_number_of_dcc_active_dpp[i][j] + v->no_of_dpp[i][j][k];
				}
			}
		}
	}
	for (i = 0; i <= number_of_states_plus_one; i++) {
		for (j = 0; j <= 1; j++) {
			v->projected_dcfclk_deep_sleep = 8.0;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				v->projected_dcfclk_deep_sleep =dcn_bw_max2(v->projected_dcfclk_deep_sleep, v->pixel_clock[k] / 16.0);
				if (v->byte_per_pixel_in_detc[k] == 0.0) {
					if (v->v_ratio[k] <= 1.0) {
						v->projected_dcfclk_deep_sleep =dcn_bw_max2(v->projected_dcfclk_deep_sleep, 1.1 *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / 64.0 * v->h_ratio[k] * v->pixel_clock[k] / v->no_of_dpp[i][j][k]);
					}
					else {
						v->projected_dcfclk_deep_sleep =dcn_bw_max2(v->projected_dcfclk_deep_sleep, 1.1 *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / 64.0 * v->pscl_factor[k] * v->required_dispclk[i][j] / (1 + j));
					}
				}
				else {
					if (v->v_ratio[k] <= 1.0) {
						v->projected_dcfclk_deep_sleep =dcn_bw_max2(v->projected_dcfclk_deep_sleep, 1.1 *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / 32.0 * v->h_ratio[k] * v->pixel_clock[k] / v->no_of_dpp[i][j][k]);
					}
					else {
						v->projected_dcfclk_deep_sleep =dcn_bw_max2(v->projected_dcfclk_deep_sleep, 1.1 *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / 32.0 * v->pscl_factor[k] * v->required_dispclk[i][j] / (1 + j));
					}
					if (v->v_ratio[k] / 2.0 <= 1.0) {
						v->projected_dcfclk_deep_sleep =dcn_bw_max2(v->projected_dcfclk_deep_sleep, 1.1 *dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / 32.0 * v->h_ratio[k] / 2.0 * v->pixel_clock[k] / v->no_of_dpp[i][j][k]);
					}
					else {
						v->projected_dcfclk_deep_sleep =dcn_bw_max2(v->projected_dcfclk_deep_sleep, 1.1 *dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / 32.0 * v->pscl_factor_chroma[k] * v->required_dispclk[i][j] / (1 + j));
					}
				}
			}
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if (v->dcc_enable[k] == dcn_bw_yes) {
					v->meta_req_height_y = 8.0 * v->read256_block_height_y[k];
					v->meta_req_width_y = 64.0 * 256.0 /dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / v->meta_req_height_y;
					v->meta_surface_width_y =dcn_bw_ceil2(v->viewport_width[k] / v->no_of_dpp[i][j][k] - 1.0, v->meta_req_width_y) + v->meta_req_width_y;
					v->meta_surface_height_y =dcn_bw_ceil2(v->viewport_height[k] - 1.0, v->meta_req_height_y) + v->meta_req_height_y;
					if (v->pte_enable == dcn_bw_yes) {
						v->meta_pte_bytes_per_frame_y = (dcn_bw_ceil2((v->meta_surface_width_y * v->meta_surface_height_y *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / 256.0 - 4096.0) / 8.0 / 4096.0, 1.0) + 1) * 64.0;
					}
					else {
						v->meta_pte_bytes_per_frame_y = 0.0;
					}
					if (v->source_scan[k] == dcn_bw_hor) {
						v->meta_row_bytes_y = v->meta_surface_width_y * v->meta_req_height_y *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / 256.0;
					}
					else {
						v->meta_row_bytes_y = v->meta_surface_height_y * v->meta_req_width_y *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / 256.0;
					}
				}
				else {
					v->meta_pte_bytes_per_frame_y = 0.0;
					v->meta_row_bytes_y = 0.0;
				}
				if (v->pte_enable == dcn_bw_yes) {
					if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
						v->macro_tile_block_size_bytes_y = 256.0;
						v->macro_tile_block_height_y = 1.0;
					}
					else if (v->source_surface_mode[k] == dcn_bw_sw_4_kb_s || v->source_surface_mode[k] == dcn_bw_sw_4_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d_x) {
						v->macro_tile_block_size_bytes_y = 4096.0;
						v->macro_tile_block_height_y = 4.0 * v->read256_block_height_y[k];
					}
					else if (v->source_surface_mode[k] == dcn_bw_sw_64_kb_s || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_x) {
						v->macro_tile_block_size_bytes_y = 64.0 * 1024;
						v->macro_tile_block_height_y = 16.0 * v->read256_block_height_y[k];
					}
					else {
						v->macro_tile_block_size_bytes_y = 256.0 * 1024;
						v->macro_tile_block_height_y = 32.0 * v->read256_block_height_y[k];
					}
					if (v->macro_tile_block_size_bytes_y <= 65536.0) {
						v->data_pte_req_height_y = v->macro_tile_block_height_y;
					}
					else {
						v->data_pte_req_height_y = 16.0 * v->read256_block_height_y[k];
					}
					v->data_pte_req_width_y = 4096.0 /dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) / v->data_pte_req_height_y * 8;
					if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
						v->dpte_bytes_per_row_y = 64.0 * (dcn_bw_ceil2((v->viewport_width[k] / v->no_of_dpp[i][j][k] *dcn_bw_min2(128.0, dcn_bw_pow(2.0,dcn_bw_floor2(dcn_bw_log(v->pte_buffer_size_in_requests * v->data_pte_req_width_y / (v->viewport_width[k] / v->no_of_dpp[i][j][k]), 2.0), 1.0))) - 1.0) / v->data_pte_req_width_y, 1.0) + 1);
					}
					else if (v->source_scan[k] == dcn_bw_hor) {
						v->dpte_bytes_per_row_y = 64.0 * (dcn_bw_ceil2((v->viewport_width[k] / v->no_of_dpp[i][j][k] - 1.0) / v->data_pte_req_width_y, 1.0) + 1);
					}
					else {
						v->dpte_bytes_per_row_y = 64.0 * (dcn_bw_ceil2((v->viewport_height[k] - 1.0) / v->data_pte_req_height_y, 1.0) + 1);
					}
				}
				else {
					v->dpte_bytes_per_row_y = 0.0;
				}
				if ((v->source_pixel_format[k] != dcn_bw_rgb_sub_64 && v->source_pixel_format[k] != dcn_bw_rgb_sub_32 && v->source_pixel_format[k] != dcn_bw_rgb_sub_16)) {
					if (v->dcc_enable[k] == dcn_bw_yes) {
						v->meta_req_height_c = 8.0 * v->read256_block_height_c[k];
						v->meta_req_width_c = 64.0 * 256.0 /dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / v->meta_req_height_c;
						v->meta_surface_width_c =dcn_bw_ceil2(v->viewport_width[k] / v->no_of_dpp[i][j][k] / 2.0 - 1.0, v->meta_req_width_c) + v->meta_req_width_c;
						v->meta_surface_height_c =dcn_bw_ceil2(v->viewport_height[k] / 2.0 - 1.0, v->meta_req_height_c) + v->meta_req_height_c;
						if (v->pte_enable == dcn_bw_yes) {
							v->meta_pte_bytes_per_frame_c = (dcn_bw_ceil2((v->meta_surface_width_c * v->meta_surface_height_c *dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / 256.0 - 4096.0) / 8.0 / 4096.0, 1.0) + 1) * 64.0;
						}
						else {
							v->meta_pte_bytes_per_frame_c = 0.0;
						}
						if (v->source_scan[k] == dcn_bw_hor) {
							v->meta_row_bytes_c = v->meta_surface_width_c * v->meta_req_height_c *dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / 256.0;
						}
						else {
							v->meta_row_bytes_c = v->meta_surface_height_c * v->meta_req_width_c *dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / 256.0;
						}
					}
					else {
						v->meta_pte_bytes_per_frame_c = 0.0;
						v->meta_row_bytes_c = 0.0;
					}
					if (v->pte_enable == dcn_bw_yes) {
						if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
							v->macro_tile_block_size_bytes_c = 256.0;
							v->macro_tile_block_height_c = 1.0;
						}
						else if (v->source_surface_mode[k] == dcn_bw_sw_4_kb_s || v->source_surface_mode[k] == dcn_bw_sw_4_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d_x) {
							v->macro_tile_block_size_bytes_c = 4096.0;
							v->macro_tile_block_height_c = 4.0 * v->read256_block_height_c[k];
						}
						else if (v->source_surface_mode[k] == dcn_bw_sw_64_kb_s || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_x) {
							v->macro_tile_block_size_bytes_c = 64.0 * 1024;
							v->macro_tile_block_height_c = 16.0 * v->read256_block_height_c[k];
						}
						else {
							v->macro_tile_block_size_bytes_c = 256.0 * 1024;
							v->macro_tile_block_height_c = 32.0 * v->read256_block_height_c[k];
						}
						v->macro_tile_block_width_c = v->macro_tile_block_size_bytes_c /dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / v->macro_tile_block_height_c;
						if (v->macro_tile_block_size_bytes_c <= 65536.0) {
							v->data_pte_req_height_c = v->macro_tile_block_height_c;
						}
						else {
							v->data_pte_req_height_c = 16.0 * v->read256_block_height_c[k];
						}
						v->data_pte_req_width_c = 4096.0 /dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / v->data_pte_req_height_c * 8;
						if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
							v->dpte_bytes_per_row_c = 64.0 * (dcn_bw_ceil2((v->viewport_width[k] / v->no_of_dpp[i][j][k] / 2.0 * dcn_bw_min2(128.0, dcn_bw_pow(2.0,dcn_bw_floor2(dcn_bw_log(v->pte_buffer_size_in_requests * v->data_pte_req_width_c / (v->viewport_width[k] / v->no_of_dpp[i][j][k] / 2.0), 2.0), 1.0))) - 1.0) / v->data_pte_req_width_c, 1.0) + 1);
						}
						else if (v->source_scan[k] == dcn_bw_hor) {
							v->dpte_bytes_per_row_c = 64.0 * (dcn_bw_ceil2((v->viewport_width[k] / v->no_of_dpp[i][j][k] / 2.0 - 1.0) / v->data_pte_req_width_c, 1.0) + 1);
						}
						else {
							v->dpte_bytes_per_row_c = 64.0 * (dcn_bw_ceil2((v->viewport_height[k] / 2.0 - 1.0) / v->data_pte_req_height_c, 1.0) + 1);
						}
					}
					else {
						v->dpte_bytes_per_row_c = 0.0;
					}
				}
				else {
					v->dpte_bytes_per_row_c = 0.0;
					v->meta_pte_bytes_per_frame_c = 0.0;
					v->meta_row_bytes_c = 0.0;
				}
				v->dpte_bytes_per_row[k] = v->dpte_bytes_per_row_y + v->dpte_bytes_per_row_c;
				v->meta_pte_bytes_per_frame[k] = v->meta_pte_bytes_per_frame_y + v->meta_pte_bytes_per_frame_c;
				v->meta_row_bytes[k] = v->meta_row_bytes_y + v->meta_row_bytes_c;
				v->v_init_y = (v->v_ratio[k] + v->vtaps[k] + 1.0 + v->interlace_output[k] * 0.5 * v->v_ratio[k]) / 2.0;
				v->prefill_y[k] =dcn_bw_floor2(v->v_init_y, 1.0);
				v->max_num_sw_y[k] =dcn_bw_ceil2((v->prefill_y[k] - 1.0) / v->swath_height_yper_state[i][j][k], 1.0) + 1;
				if (v->prefill_y[k] > 1.0) {
					v->max_partial_sw_y =dcn_bw_mod((v->prefill_y[k] - 2.0), v->swath_height_yper_state[i][j][k]);
				}
				else {
					v->max_partial_sw_y =dcn_bw_mod((v->prefill_y[k] + v->swath_height_yper_state[i][j][k] - 2.0), v->swath_height_yper_state[i][j][k]);
				}
				v->max_partial_sw_y =dcn_bw_max2(1.0, v->max_partial_sw_y);
				v->prefetch_lines_y[k] = v->max_num_sw_y[k] * v->swath_height_yper_state[i][j][k] + v->max_partial_sw_y;
				if ((v->source_pixel_format[k] != dcn_bw_rgb_sub_64 && v->source_pixel_format[k] != dcn_bw_rgb_sub_32 && v->source_pixel_format[k] != dcn_bw_rgb_sub_16)) {
					v->v_init_c = (v->v_ratio[k] / 2.0 + v->vtaps[k] + 1.0 + v->interlace_output[k] * 0.5 * v->v_ratio[k] / 2.0) / 2.0;
					v->prefill_c[k] =dcn_bw_floor2(v->v_init_c, 1.0);
					v->max_num_sw_c[k] =dcn_bw_ceil2((v->prefill_c[k] - 1.0) / v->swath_height_cper_state[i][j][k], 1.0) + 1;
					if (v->prefill_c[k] > 1.0) {
						v->max_partial_sw_c =dcn_bw_mod((v->prefill_c[k] - 2.0), v->swath_height_cper_state[i][j][k]);
					}
					else {
						v->max_partial_sw_c =dcn_bw_mod((v->prefill_c[k] + v->swath_height_cper_state[i][j][k] - 2.0), v->swath_height_cper_state[i][j][k]);
					}
					v->max_partial_sw_c =dcn_bw_max2(1.0, v->max_partial_sw_c);
					v->prefetch_lines_c[k] = v->max_num_sw_c[k] * v->swath_height_cper_state[i][j][k] + v->max_partial_sw_c;
				}
				else {
					v->prefetch_lines_c[k] = 0.0;
				}
				v->dst_x_after_scaler = 90.0 * v->pixel_clock[k] / (v->required_dispclk[i][j] / (j + 1)) + 42.0 * v->pixel_clock[k] / v->required_dispclk[i][j];
				if (v->no_of_dpp[i][j][k] > 1.0) {
					v->dst_x_after_scaler = v->dst_x_after_scaler + v->scaler_rec_out_width[k] / 2.0;
				}
				if (v->output_format[k] == dcn_bw_420) {
					v->dst_y_after_scaler = 1.0;
				}
				else {
					v->dst_y_after_scaler = 0.0;
				}
				v->time_calc = 24.0 / v->projected_dcfclk_deep_sleep;
				v->v_update_offset[k][j] = dcn_bw_ceil2(v->htotal[k] / 4.0, 1.0);
				v->total_repeater_delay = v->max_inter_dcn_tile_repeaters * (2.0 / (v->required_dispclk[i][j] / (j + 1)) + 3.0 / v->required_dispclk[i][j]);
				v->v_update_width[k][j] = (14.0 / v->projected_dcfclk_deep_sleep + 12.0 / (v->required_dispclk[i][j] / (j + 1)) + v->total_repeater_delay) * v->pixel_clock[k];
				v->v_ready_offset[k][j] = dcn_bw_max2(150.0 / (v->required_dispclk[i][j] / (j + 1)), v->total_repeater_delay + 20.0 / v->projected_dcfclk_deep_sleep + 10.0 / (v->required_dispclk[i][j] / (j + 1))) * v->pixel_clock[k];
				v->time_setup = (v->v_update_offset[k][j] + v->v_update_width[k][j] + v->v_ready_offset[k][j]) / v->pixel_clock[k];
				v->extra_latency = v->urgent_round_trip_and_out_of_order_latency_per_state[i] + (v->total_number_of_active_dpp[i][j] * v->pixel_chunk_size_in_kbyte + v->total_number_of_dcc_active_dpp[i][j] * v->meta_chunk_size) * 1024.0 / v->return_bw_per_state[i];
				if (v->pte_enable == dcn_bw_yes) {
					v->extra_latency = v->extra_latency + v->total_number_of_active_dpp[i][j] * v->pte_chunk_size * 1024.0 / v->return_bw_per_state[i];
				}
				if (v->can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one == dcn_bw_yes) {
					v->maximum_vstartup = v->vtotal[k] - v->vactive[k] - 1.0;
				}
				else {
					v->maximum_vstartup = v->v_sync_plus_back_porch[k] - 1.0;
				}

				do {
					v->line_times_for_prefetch[k] = v->maximum_vstartup - v->urgent_latency / (v->htotal[k] / v->pixel_clock[k]) - (v->time_calc + v->time_setup) / (v->htotal[k] / v->pixel_clock[k]) - (v->dst_y_after_scaler + v->dst_x_after_scaler / v->htotal[k]);
					v->line_times_for_prefetch[k] =dcn_bw_floor2(4.0 * (v->line_times_for_prefetch[k] + 0.125), 1.0) / 4;
					v->prefetch_bw[k] = (v->meta_pte_bytes_per_frame[k] + 2.0 * v->meta_row_bytes[k] + 2.0 * v->dpte_bytes_per_row[k] + v->prefetch_lines_y[k] * v->swath_width_yper_state[i][j][k] *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) + v->prefetch_lines_c[k] * v->swath_width_yper_state[i][j][k] / 2.0 *dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0)) / (v->line_times_for_prefetch[k] * v->htotal[k] / v->pixel_clock[k]);

					if (v->pte_enable == dcn_bw_yes && v->dcc_enable[k] == dcn_bw_yes) {
						v->time_for_meta_pte_without_immediate_flip = dcn_bw_max3(
								v->meta_pte_bytes_frame[k] / v->prefetch_bw[k],
								v->extra_latency,
								v->htotal[k] / v->pixel_clock[k] / 4.0);
					} else {
						v->time_for_meta_pte_without_immediate_flip = v->htotal[k] / v->pixel_clock[k] / 4.0;
					}

					if (v->pte_enable == dcn_bw_yes || v->dcc_enable[k] == dcn_bw_yes) {
						v->time_for_meta_and_dpte_row_without_immediate_flip = dcn_bw_max3((
								v->meta_row_bytes[k] + v->dpte_bytes_per_row[k]) / v->prefetch_bw[k],
								v->htotal[k] / v->pixel_clock[k] - v->time_for_meta_pte_without_immediate_flip,
								v->extra_latency);
					} else {
						v->time_for_meta_and_dpte_row_without_immediate_flip = dcn_bw_max2(
								v->htotal[k] / v->pixel_clock[k] - v->time_for_meta_pte_without_immediate_flip,
								v->extra_latency - v->time_for_meta_pte_with_immediate_flip);
					}

					v->lines_for_meta_pte_without_immediate_flip[k] =dcn_bw_floor2(4.0 * (v->time_for_meta_pte_without_immediate_flip / (v->htotal[k] / v->pixel_clock[k]) + 0.125), 1.0) / 4;
					v->lines_for_meta_and_dpte_row_without_immediate_flip[k] =dcn_bw_floor2(4.0 * (v->time_for_meta_and_dpte_row_without_immediate_flip / (v->htotal[k] / v->pixel_clock[k]) + 0.125), 1.0) / 4;
					v->maximum_vstartup = v->maximum_vstartup - 1;

					if (v->lines_for_meta_pte_without_immediate_flip[k] < 32.0 && v->lines_for_meta_and_dpte_row_without_immediate_flip[k] < 16.0)
						break;

				} while(1);
			}
			v->bw_available_for_immediate_flip = v->return_bw_per_state[i];
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				v->bw_available_for_immediate_flip = v->bw_available_for_immediate_flip -dcn_bw_max2(v->read_bandwidth[k], v->prefetch_bw[k]);
			}
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				v->total_immediate_flip_bytes[k] = 0.0;
				if ((v->source_pixel_format[k] != dcn_bw_yuv420_sub_8 && v->source_pixel_format[k] != dcn_bw_yuv420_sub_10)) {
					v->total_immediate_flip_bytes[k] = v->total_immediate_flip_bytes[k] + v->meta_pte_bytes_per_frame[k] + v->meta_row_bytes[k] + v->dpte_bytes_per_row[k];
				}
			}
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if (v->pte_enable == dcn_bw_yes && v->dcc_enable[k] == dcn_bw_yes) {
					v->time_for_meta_pte_with_immediate_flip =dcn_bw_max5(v->meta_pte_bytes_per_frame[k] / v->prefetch_bw[k], v->meta_pte_bytes_per_frame[k] * v->total_immediate_flip_bytes[k] / (v->bw_available_for_immediate_flip * (v->meta_pte_bytes_per_frame[k] + v->meta_row_bytes[k] + v->dpte_bytes_per_row[k])), v->extra_latency, v->urgent_latency, v->htotal[k] / v->pixel_clock[k] / 4.0);
				}
				else {
					v->time_for_meta_pte_with_immediate_flip = v->htotal[k] / v->pixel_clock[k] / 4.0;
				}
				if (v->pte_enable == dcn_bw_yes || v->dcc_enable[k] == dcn_bw_yes) {
					v->time_for_meta_and_dpte_row_with_immediate_flip =dcn_bw_max5((v->meta_row_bytes[k] + v->dpte_bytes_per_row[k]) / v->prefetch_bw[k], (v->meta_row_bytes[k] + v->dpte_bytes_per_row[k]) * v->total_immediate_flip_bytes[k] / (v->bw_available_for_immediate_flip * (v->meta_pte_bytes_per_frame[k] + v->meta_row_bytes[k] + v->dpte_bytes_per_row[k])), v->htotal[k] / v->pixel_clock[k] - v->time_for_meta_pte_with_immediate_flip, v->extra_latency, 2.0 * v->urgent_latency);
				}
				else {
					v->time_for_meta_and_dpte_row_with_immediate_flip =dcn_bw_max2(v->htotal[k] / v->pixel_clock[k] - v->time_for_meta_pte_with_immediate_flip, v->extra_latency - v->time_for_meta_pte_with_immediate_flip);
				}
				v->lines_for_meta_pte_with_immediate_flip[k] =dcn_bw_floor2(4.0 * (v->time_for_meta_pte_with_immediate_flip / (v->htotal[k] / v->pixel_clock[k]) + 0.125), 1.0) / 4;
				v->lines_for_meta_and_dpte_row_with_immediate_flip[k] =dcn_bw_floor2(4.0 * (v->time_for_meta_and_dpte_row_with_immediate_flip / (v->htotal[k] / v->pixel_clock[k]) + 0.125), 1.0) / 4;
				v->line_times_to_request_prefetch_pixel_data_with_immediate_flip = v->line_times_for_prefetch[k] - v->lines_for_meta_pte_with_immediate_flip[k] - v->lines_for_meta_and_dpte_row_with_immediate_flip[k];
				v->line_times_to_request_prefetch_pixel_data_without_immediate_flip = v->line_times_for_prefetch[k] - v->lines_for_meta_pte_without_immediate_flip[k] - v->lines_for_meta_and_dpte_row_without_immediate_flip[k];
				if (v->line_times_to_request_prefetch_pixel_data_with_immediate_flip > 0.0) {
					v->v_ratio_pre_ywith_immediate_flip[i][j][k] = v->prefetch_lines_y[k] / v->line_times_to_request_prefetch_pixel_data_with_immediate_flip;
					if ((v->swath_height_yper_state[i][j][k] > 4.0)) {
						if (v->line_times_to_request_prefetch_pixel_data_with_immediate_flip - (v->prefill_y[k] - 3.0) / 2.0 > 0.0) {
							v->v_ratio_pre_ywith_immediate_flip[i][j][k] =dcn_bw_max2(v->v_ratio_pre_ywith_immediate_flip[i][j][k], (v->max_num_sw_y[k] * v->swath_height_yper_state[i][j][k]) / (v->line_times_to_request_prefetch_pixel_data_with_immediate_flip - (v->prefill_y[k] - 3.0) / 2.0));
						}
						else {
							v->v_ratio_pre_ywith_immediate_flip[i][j][k] = 999999.0;
						}
					}
					v->v_ratio_pre_cwith_immediate_flip[i][j][k] = v->prefetch_lines_c[k] / v->line_times_to_request_prefetch_pixel_data_with_immediate_flip;
					if ((v->swath_height_cper_state[i][j][k] > 4.0)) {
						if (v->line_times_to_request_prefetch_pixel_data_with_immediate_flip - (v->prefill_c[k] - 3.0) / 2.0 > 0.0) {
							v->v_ratio_pre_cwith_immediate_flip[i][j][k] =dcn_bw_max2(v->v_ratio_pre_cwith_immediate_flip[i][j][k], (v->max_num_sw_c[k] * v->swath_height_cper_state[i][j][k]) / (v->line_times_to_request_prefetch_pixel_data_with_immediate_flip - (v->prefill_c[k] - 3.0) / 2.0));
						}
						else {
							v->v_ratio_pre_cwith_immediate_flip[i][j][k] = 999999.0;
						}
					}
					v->required_prefetch_pixel_data_bw_with_immediate_flip[i][j][k] = v->no_of_dpp[i][j][k] * (v->prefetch_lines_y[k] / v->line_times_to_request_prefetch_pixel_data_with_immediate_flip *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) + v->prefetch_lines_c[k] / v->line_times_to_request_prefetch_pixel_data_with_immediate_flip *dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / 2.0) * v->swath_width_yper_state[i][j][k] / (v->htotal[k] / v->pixel_clock[k]);
				}
				else {
					v->v_ratio_pre_ywith_immediate_flip[i][j][k] = 999999.0;
					v->v_ratio_pre_cwith_immediate_flip[i][j][k] = 999999.0;
					v->required_prefetch_pixel_data_bw_with_immediate_flip[i][j][k] = 999999.0;
				}
				if (v->line_times_to_request_prefetch_pixel_data_without_immediate_flip > 0.0) {
					v->v_ratio_pre_ywithout_immediate_flip[i][j][k] = v->prefetch_lines_y[k] / v->line_times_to_request_prefetch_pixel_data_without_immediate_flip;
					if ((v->swath_height_yper_state[i][j][k] > 4.0)) {
						if (v->line_times_to_request_prefetch_pixel_data_without_immediate_flip - (v->prefill_y[k] - 3.0) / 2.0 > 0.0) {
							v->v_ratio_pre_ywithout_immediate_flip[i][j][k] =dcn_bw_max2(v->v_ratio_pre_ywithout_immediate_flip[i][j][k], (v->max_num_sw_y[k] * v->swath_height_yper_state[i][j][k]) / (v->line_times_to_request_prefetch_pixel_data_without_immediate_flip - (v->prefill_y[k] - 3.0) / 2.0));
						}
						else {
							v->v_ratio_pre_ywithout_immediate_flip[i][j][k] = 999999.0;
						}
					}
					v->v_ratio_pre_cwithout_immediate_flip[i][j][k] = v->prefetch_lines_c[k] / v->line_times_to_request_prefetch_pixel_data_without_immediate_flip;
					if ((v->swath_height_cper_state[i][j][k] > 4.0)) {
						if (v->line_times_to_request_prefetch_pixel_data_without_immediate_flip - (v->prefill_c[k] - 3.0) / 2.0 > 0.0) {
							v->v_ratio_pre_cwithout_immediate_flip[i][j][k] =dcn_bw_max2(v->v_ratio_pre_cwithout_immediate_flip[i][j][k], (v->max_num_sw_c[k] * v->swath_height_cper_state[i][j][k]) / (v->line_times_to_request_prefetch_pixel_data_without_immediate_flip - (v->prefill_c[k] - 3.0) / 2.0));
						}
						else {
							v->v_ratio_pre_cwithout_immediate_flip[i][j][k] = 999999.0;
						}
					}
					v->required_prefetch_pixel_data_bw_without_immediate_flip[i][j][k] = v->no_of_dpp[i][j][k] * (v->prefetch_lines_y[k] / v->line_times_to_request_prefetch_pixel_data_without_immediate_flip *dcn_bw_ceil2(v->byte_per_pixel_in_dety[k], 1.0) + v->prefetch_lines_c[k] / v->line_times_to_request_prefetch_pixel_data_without_immediate_flip *dcn_bw_ceil2(v->byte_per_pixel_in_detc[k], 2.0) / 2.0) * v->swath_width_yper_state[i][j][k] / (v->htotal[k] / v->pixel_clock[k]);
				}
				else {
					v->v_ratio_pre_ywithout_immediate_flip[i][j][k] = 999999.0;
					v->v_ratio_pre_cwithout_immediate_flip[i][j][k] = 999999.0;
					v->required_prefetch_pixel_data_bw_without_immediate_flip[i][j][k] = 999999.0;
				}
			}
			v->maximum_read_bandwidth_with_prefetch_with_immediate_flip = 0.0;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if ((v->source_pixel_format[k] != dcn_bw_yuv420_sub_8 && v->source_pixel_format[k] != dcn_bw_yuv420_sub_10)) {
					v->maximum_read_bandwidth_with_prefetch_with_immediate_flip = v->maximum_read_bandwidth_with_prefetch_with_immediate_flip +dcn_bw_max2(v->read_bandwidth[k], v->required_prefetch_pixel_data_bw_with_immediate_flip[i][j][k]) +dcn_bw_max2(v->meta_pte_bytes_per_frame[k] / (v->lines_for_meta_pte_with_immediate_flip[k] * v->htotal[k] / v->pixel_clock[k]), (v->meta_row_bytes[k] + v->dpte_bytes_per_row[k]) / (v->lines_for_meta_and_dpte_row_with_immediate_flip[k] * v->htotal[k] / v->pixel_clock[k]));
				}
				else {
					v->maximum_read_bandwidth_with_prefetch_with_immediate_flip = v->maximum_read_bandwidth_with_prefetch_with_immediate_flip +dcn_bw_max2(v->read_bandwidth[k], v->required_prefetch_pixel_data_bw_without_immediate_flip[i][j][k]);
				}
			}
			v->maximum_read_bandwidth_with_prefetch_without_immediate_flip = 0.0;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				v->maximum_read_bandwidth_with_prefetch_without_immediate_flip = v->maximum_read_bandwidth_with_prefetch_without_immediate_flip +dcn_bw_max2(v->read_bandwidth[k], v->required_prefetch_pixel_data_bw_without_immediate_flip[i][j][k]);
			}
			v->prefetch_supported_with_immediate_flip[i][j] = dcn_bw_yes;
			if (v->maximum_read_bandwidth_with_prefetch_with_immediate_flip > v->return_bw_per_state[i]) {
				v->prefetch_supported_with_immediate_flip[i][j] = dcn_bw_no;
			}
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if (v->line_times_for_prefetch[k] < 2.0 || v->lines_for_meta_pte_with_immediate_flip[k] >= 8.0 || v->lines_for_meta_and_dpte_row_with_immediate_flip[k] >= 16.0) {
					v->prefetch_supported_with_immediate_flip[i][j] = dcn_bw_no;
				}
			}
			v->prefetch_supported_without_immediate_flip[i][j] = dcn_bw_yes;
			if (v->maximum_read_bandwidth_with_prefetch_without_immediate_flip > v->return_bw_per_state[i]) {
				v->prefetch_supported_without_immediate_flip[i][j] = dcn_bw_no;
			}
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if (v->line_times_for_prefetch[k] < 2.0 || v->lines_for_meta_pte_without_immediate_flip[k] >= 8.0 || v->lines_for_meta_and_dpte_row_without_immediate_flip[k] >= 16.0) {
					v->prefetch_supported_without_immediate_flip[i][j] = dcn_bw_no;
				}
			}
		}
	}
	for (i = 0; i <= number_of_states_plus_one; i++) {
		for (j = 0; j <= 1; j++) {
			v->v_ratio_in_prefetch_supported_with_immediate_flip[i][j] = dcn_bw_yes;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if ((((v->source_pixel_format[k] != dcn_bw_yuv420_sub_8 && v->source_pixel_format[k] != dcn_bw_yuv420_sub_10) && (v->v_ratio_pre_ywith_immediate_flip[i][j][k] > 4.0 || v->v_ratio_pre_cwith_immediate_flip[i][j][k] > 4.0)) || ((v->source_pixel_format[k] == dcn_bw_yuv420_sub_8 || v->source_pixel_format[k] == dcn_bw_yuv420_sub_10) && (v->v_ratio_pre_ywithout_immediate_flip[i][j][k] > 4.0 || v->v_ratio_pre_cwithout_immediate_flip[i][j][k] > 4.0)))) {
					v->v_ratio_in_prefetch_supported_with_immediate_flip[i][j] = dcn_bw_no;
				}
			}
			v->v_ratio_in_prefetch_supported_without_immediate_flip[i][j] = dcn_bw_yes;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if ((v->v_ratio_pre_ywithout_immediate_flip[i][j][k] > 4.0 || v->v_ratio_pre_cwithout_immediate_flip[i][j][k] > 4.0)) {
					v->v_ratio_in_prefetch_supported_without_immediate_flip[i][j] = dcn_bw_no;
				}
			}
		}
	}
	/*mode support, voltage state and soc configuration*/

	for (i = number_of_states_plus_one; i >= 0; i--) {
		for (j = 0; j <= 1; j++) {
			if (v->scale_ratio_support == dcn_bw_yes && v->source_format_pixel_and_scan_support == dcn_bw_yes && v->viewport_size_support == dcn_bw_yes && v->bandwidth_support[i] == dcn_bw_yes && v->dio_support[i] == dcn_bw_yes && v->urgent_latency_support[i][j] == dcn_bw_yes && v->rob_support[i] == dcn_bw_yes && v->dispclk_dppclk_support[i][j] == dcn_bw_yes && v->total_available_pipes_support[i][j] == dcn_bw_yes && v->total_available_writeback_support == dcn_bw_yes && v->writeback_latency_support == dcn_bw_yes) {
				if (v->prefetch_supported_with_immediate_flip[i][j] == dcn_bw_yes && v->v_ratio_in_prefetch_supported_with_immediate_flip[i][j] == dcn_bw_yes) {
					v->mode_support_with_immediate_flip[i][j] = dcn_bw_yes;
				}
				else {
					v->mode_support_with_immediate_flip[i][j] = dcn_bw_no;
				}
				if (v->prefetch_supported_without_immediate_flip[i][j] == dcn_bw_yes && v->v_ratio_in_prefetch_supported_without_immediate_flip[i][j] == dcn_bw_yes) {
					v->mode_support_without_immediate_flip[i][j] = dcn_bw_yes;
				}
				else {
					v->mode_support_without_immediate_flip[i][j] = dcn_bw_no;
				}
			}
			else {
				v->mode_support_with_immediate_flip[i][j] = dcn_bw_no;
				v->mode_support_without_immediate_flip[i][j] = dcn_bw_no;
			}
		}
	}
	for (i = number_of_states_plus_one; i >= 0; i--) {
		if ((i == number_of_states_plus_one || v->mode_support_with_immediate_flip[i][1] == dcn_bw_yes || v->mode_support_with_immediate_flip[i][0] == dcn_bw_yes) && i >= v->voltage_override_level) {
			v->voltage_level_with_immediate_flip = i;
		}
	}
	for (i = number_of_states_plus_one; i >= 0; i--) {
		if ((i == number_of_states_plus_one || v->mode_support_without_immediate_flip[i][1] == dcn_bw_yes || v->mode_support_without_immediate_flip[i][0] == dcn_bw_yes) && i >= v->voltage_override_level) {
			v->voltage_level_without_immediate_flip = i;
		}
	}
	if (v->voltage_level_with_immediate_flip == number_of_states_plus_one) {
		v->immediate_flip_supported = dcn_bw_no;
		v->voltage_level = v->voltage_level_without_immediate_flip;
	}
	else {
		v->immediate_flip_supported = dcn_bw_yes;
		v->voltage_level = v->voltage_level_with_immediate_flip;
	}
	v->dcfclk = v->dcfclk_per_state[v->voltage_level];
	v->fabric_and_dram_bandwidth = v->fabric_and_dram_bandwidth_per_state[v->voltage_level];
	for (j = 0; j <= 1; j++) {
		v->required_dispclk_per_ratio[j] = v->required_dispclk[v->voltage_level][j];
		for (k = 0; k <= v->number_of_active_planes - 1; k++) {
			v->dpp_per_plane_per_ratio[j][k] = v->no_of_dpp[v->voltage_level][j][k];
		}
		v->dispclk_dppclk_support_per_ratio[j] = v->dispclk_dppclk_support[v->voltage_level][j];
	}
	v->max_phyclk = v->phyclk_per_state[v->voltage_level];
}
void display_pipe_configuration(struct dcn_bw_internal_vars *v)
{
	int j;
	int k;
	/*display pipe configuration*/

	for (j = 0; j <= 1; j++) {
		v->total_number_of_active_dpp_per_ratio[j] = 0.0;
		for (k = 0; k <= v->number_of_active_planes - 1; k++) {
			v->total_number_of_active_dpp_per_ratio[j] = v->total_number_of_active_dpp_per_ratio[j] + v->dpp_per_plane_per_ratio[j][k];
		}
	}
	if ((v->dispclk_dppclk_support_per_ratio[0] == dcn_bw_yes && v->dispclk_dppclk_support_per_ratio[1] == dcn_bw_no) || (v->dispclk_dppclk_support_per_ratio[0] == v->dispclk_dppclk_support_per_ratio[1] && (v->total_number_of_active_dpp_per_ratio[0] < v->total_number_of_active_dpp_per_ratio[1] || (((v->total_number_of_active_dpp_per_ratio[0] == v->total_number_of_active_dpp_per_ratio[1]) && v->required_dispclk_per_ratio[0] <= 0.5 * v->required_dispclk_per_ratio[1]))))) {
		v->dispclk_dppclk_ratio = 1;
		v->final_error_message = v->error_message[0];
	}
	else {
		v->dispclk_dppclk_ratio = 2;
		v->final_error_message = v->error_message[1];
	}
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->dpp_per_plane[k] = v->dpp_per_plane_per_ratio[v->dispclk_dppclk_ratio - 1][k];
	}
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->source_pixel_format[k] == dcn_bw_rgb_sub_64) {
			v->byte_per_pix_dety = 8.0;
			v->byte_per_pix_detc = 0.0;
		}
		else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_32) {
			v->byte_per_pix_dety = 4.0;
			v->byte_per_pix_detc = 0.0;
		}
		else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_16) {
			v->byte_per_pix_dety = 2.0;
			v->byte_per_pix_detc = 0.0;
		}
		else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_8) {
			v->byte_per_pix_dety = 1.0;
			v->byte_per_pix_detc = 2.0;
		}
		else {
			v->byte_per_pix_dety = 4.0f / 3.0f;
			v->byte_per_pix_detc = 8.0f / 3.0f;
		}
		if ((v->source_pixel_format[k] == dcn_bw_rgb_sub_64 || v->source_pixel_format[k] == dcn_bw_rgb_sub_32 || v->source_pixel_format[k] == dcn_bw_rgb_sub_16)) {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
				v->read256_bytes_block_height_y = 1.0;
			}
			else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_64) {
				v->read256_bytes_block_height_y = 4.0;
			}
			else {
				v->read256_bytes_block_height_y = 8.0;
			}
			v->read256_bytes_block_width_y = 256.0 /dcn_bw_ceil2(v->byte_per_pix_dety, 1.0) / v->read256_bytes_block_height_y;
			v->read256_bytes_block_height_c = 0.0;
			v->read256_bytes_block_width_c = 0.0;
		}
		else {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
				v->read256_bytes_block_height_y = 1.0;
				v->read256_bytes_block_height_c = 1.0;
			}
			else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_8) {
				v->read256_bytes_block_height_y = 16.0;
				v->read256_bytes_block_height_c = 8.0;
			}
			else {
				v->read256_bytes_block_height_y = 8.0;
				v->read256_bytes_block_height_c = 8.0;
			}
			v->read256_bytes_block_width_y = 256.0 /dcn_bw_ceil2(v->byte_per_pix_dety, 1.0) / v->read256_bytes_block_height_y;
			v->read256_bytes_block_width_c = 256.0 /dcn_bw_ceil2(v->byte_per_pix_detc, 2.0) / v->read256_bytes_block_height_c;
		}
		if (v->source_scan[k] == dcn_bw_hor) {
			v->maximum_swath_height_y = v->read256_bytes_block_height_y;
			v->maximum_swath_height_c = v->read256_bytes_block_height_c;
		}
		else {
			v->maximum_swath_height_y = v->read256_bytes_block_width_y;
			v->maximum_swath_height_c = v->read256_bytes_block_width_c;
		}
		if ((v->source_pixel_format[k] == dcn_bw_rgb_sub_64 || v->source_pixel_format[k] == dcn_bw_rgb_sub_32 || v->source_pixel_format[k] == dcn_bw_rgb_sub_16)) {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear || (v->source_pixel_format[k] == dcn_bw_rgb_sub_64 && (v->source_surface_mode[k] == dcn_bw_sw_4_kb_s || v->source_surface_mode[k] == dcn_bw_sw_4_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_var_s || v->source_surface_mode[k] == dcn_bw_sw_var_s_x) && v->source_scan[k] == dcn_bw_hor)) {
				v->minimum_swath_height_y = v->maximum_swath_height_y;
			}
			else {
				v->minimum_swath_height_y = v->maximum_swath_height_y / 2.0;
			}
			v->minimum_swath_height_c = v->maximum_swath_height_c;
		}
		else {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
				v->minimum_swath_height_y = v->maximum_swath_height_y;
				v->minimum_swath_height_c = v->maximum_swath_height_c;
			}
			else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_8 && v->source_scan[k] == dcn_bw_hor) {
				v->minimum_swath_height_y = v->maximum_swath_height_y / 2.0;
				if (v->bug_forcing_luma_and_chroma_request_to_same_size_fixed == dcn_bw_yes) {
					v->minimum_swath_height_c = v->maximum_swath_height_c;
				}
				else {
					v->minimum_swath_height_c = v->maximum_swath_height_c / 2.0;
				}
			}
			else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_10 && v->source_scan[k] == dcn_bw_hor) {
				v->minimum_swath_height_c = v->maximum_swath_height_c / 2.0;
				if (v->bug_forcing_luma_and_chroma_request_to_same_size_fixed == dcn_bw_yes) {
					v->minimum_swath_height_y = v->maximum_swath_height_y;
				}
				else {
					v->minimum_swath_height_y = v->maximum_swath_height_y / 2.0;
				}
			}
			else {
				v->minimum_swath_height_y = v->maximum_swath_height_y;
				v->minimum_swath_height_c = v->maximum_swath_height_c;
			}
		}
		if (v->source_scan[k] == dcn_bw_hor) {
			v->swath_width = v->viewport_width[k] / v->dpp_per_plane[k];
		}
		else {
			v->swath_width = v->viewport_height[k] / v->dpp_per_plane[k];
		}
		v->swath_width_granularity_y = 256.0 /dcn_bw_ceil2(v->byte_per_pix_dety, 1.0) / v->maximum_swath_height_y;
		v->rounded_up_max_swath_size_bytes_y = (dcn_bw_ceil2(v->swath_width - 1.0, v->swath_width_granularity_y) + v->swath_width_granularity_y) * v->byte_per_pix_dety * v->maximum_swath_height_y;
		if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_10) {
			v->rounded_up_max_swath_size_bytes_y =dcn_bw_ceil2(v->rounded_up_max_swath_size_bytes_y, 256.0) + 256;
		}
		if (v->maximum_swath_height_c > 0.0) {
			v->swath_width_granularity_c = 256.0 /dcn_bw_ceil2(v->byte_per_pix_detc, 2.0) / v->maximum_swath_height_c;
			v->rounded_up_max_swath_size_bytes_c = (dcn_bw_ceil2(v->swath_width / 2.0 - 1.0, v->swath_width_granularity_c) + v->swath_width_granularity_c) * v->byte_per_pix_detc * v->maximum_swath_height_c;
			if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_10) {
				v->rounded_up_max_swath_size_bytes_c = dcn_bw_ceil2(v->rounded_up_max_swath_size_bytes_c, 256.0) + 256;
			}
		}
		if (v->rounded_up_max_swath_size_bytes_y + v->rounded_up_max_swath_size_bytes_c <= v->det_buffer_size_in_kbyte * 1024.0 / 2.0) {
			v->swath_height_y[k] = v->maximum_swath_height_y;
			v->swath_height_c[k] = v->maximum_swath_height_c;
		}
		else {
			v->swath_height_y[k] = v->minimum_swath_height_y;
			v->swath_height_c[k] = v->minimum_swath_height_c;
		}
		if (v->swath_height_c[k] == 0.0) {
			v->det_buffer_size_y[k] = v->det_buffer_size_in_kbyte * 1024.0;
			v->det_buffer_size_c[k] = 0.0;
		}
		else if (v->swath_height_y[k] <= v->swath_height_c[k]) {
			v->det_buffer_size_y[k] = v->det_buffer_size_in_kbyte * 1024.0 / 2.0;
			v->det_buffer_size_c[k] = v->det_buffer_size_in_kbyte * 1024.0 / 2.0;
		}
		else {
			v->det_buffer_size_y[k] = v->det_buffer_size_in_kbyte * 1024.0 * 2.0 / 3.0;
			v->det_buffer_size_c[k] = v->det_buffer_size_in_kbyte * 1024.0 / 3.0;
		}
	}
}
void dispclkdppclkdcfclk_deep_sleep_prefetch_parameters_watermarks_and_performance_calculation(struct dcn_bw_internal_vars *v)
{
	int k;
	/*dispclk and dppclk calculation*/

	v->dispclk_with_ramping = 0.0;
	v->dispclk_without_ramping = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->h_ratio[k] > 1.0) {
			v->pscl_throughput[k] =dcn_bw_min2(v->max_dchub_topscl_throughput, v->max_pscl_tolb_throughput * v->h_ratio[k] /dcn_bw_ceil2(v->htaps[k] / 6.0, 1.0));
		}
		else {
			v->pscl_throughput[k] =dcn_bw_min2(v->max_dchub_topscl_throughput, v->max_pscl_tolb_throughput);
		}
		v->dppclk_using_single_dpp_luma = v->pixel_clock[k] *dcn_bw_max3(v->vtaps[k] / 6.0 *dcn_bw_min2(1.0, v->h_ratio[k]), v->h_ratio[k] * v->v_ratio[k] / v->pscl_throughput[k], 1.0);
		if ((v->source_pixel_format[k] != dcn_bw_yuv420_sub_8 && v->source_pixel_format[k] != dcn_bw_yuv420_sub_10)) {
			v->pscl_throughput_chroma[k] = 0.0;
			v->dppclk_using_single_dpp = v->dppclk_using_single_dpp_luma;
		}
		else {
			if (v->h_ratio[k] > 1.0) {
				v->pscl_throughput_chroma[k] =dcn_bw_min2(v->max_dchub_topscl_throughput, v->max_pscl_tolb_throughput * v->h_ratio[k] / 2.0 /dcn_bw_ceil2(v->hta_pschroma[k] / 6.0, 1.0));
			}
			else {
				v->pscl_throughput_chroma[k] =dcn_bw_min2(v->max_dchub_topscl_throughput, v->max_pscl_tolb_throughput);
			}
			v->dppclk_using_single_dpp_chroma = v->pixel_clock[k] *dcn_bw_max3(v->vta_pschroma[k] / 6.0 *dcn_bw_min2(1.0, v->h_ratio[k] / 2.0), v->h_ratio[k] * v->v_ratio[k] / 4.0 / v->pscl_throughput_chroma[k], 1.0);
			v->dppclk_using_single_dpp =dcn_bw_max2(v->dppclk_using_single_dpp_luma, v->dppclk_using_single_dpp_chroma);
		}
		if (v->odm_capable == dcn_bw_yes) {
			v->dispclk_with_ramping =dcn_bw_max2(v->dispclk_with_ramping,dcn_bw_max2(v->dppclk_using_single_dpp / v->dpp_per_plane[k] * v->dispclk_dppclk_ratio, v->pixel_clock[k] / v->dpp_per_plane[k]) * (1.0 + v->downspreading / 100.0) * (1.0 + v->dispclk_ramping_margin / 100.0));
			v->dispclk_without_ramping =dcn_bw_max2(v->dispclk_without_ramping,dcn_bw_max2(v->dppclk_using_single_dpp / v->dpp_per_plane[k] * v->dispclk_dppclk_ratio, v->pixel_clock[k] / v->dpp_per_plane[k]) * (1.0 + v->downspreading / 100.0));
		}
		else {
			v->dispclk_with_ramping =dcn_bw_max2(v->dispclk_with_ramping,dcn_bw_max2(v->dppclk_using_single_dpp / v->dpp_per_plane[k] * v->dispclk_dppclk_ratio, v->pixel_clock[k]) * (1.0 + v->downspreading / 100.0) * (1.0 + v->dispclk_ramping_margin / 100.0));
			v->dispclk_without_ramping =dcn_bw_max2(v->dispclk_without_ramping,dcn_bw_max2(v->dppclk_using_single_dpp / v->dpp_per_plane[k] * v->dispclk_dppclk_ratio, v->pixel_clock[k]) * (1.0 + v->downspreading / 100.0));
		}
	}
	if (v->dispclk_without_ramping > v->max_dispclk[number_of_states]) {
		v->dispclk = v->dispclk_without_ramping;
	}
	else if (v->dispclk_with_ramping > v->max_dispclk[number_of_states]) {
		v->dispclk = v->max_dispclk[number_of_states];
	}
	else {
		v->dispclk = v->dispclk_with_ramping;
	}
	v->dppclk = v->dispclk / v->dispclk_dppclk_ratio;
	/*urgent watermark*/

	v->return_bandwidth_to_dcn =dcn_bw_min2(v->return_bus_width * v->dcfclk, v->fabric_and_dram_bandwidth * 1000.0 * v->percent_of_ideal_drambw_received_after_urg_latency / 100.0);
	v->dcc_enabled_any_plane = dcn_bw_no;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->dcc_enable[k] == dcn_bw_yes) {
			v->dcc_enabled_any_plane = dcn_bw_yes;
		}
	}
	v->return_bw = v->return_bandwidth_to_dcn;
	if (v->dcc_enabled_any_plane == dcn_bw_yes && v->return_bandwidth_to_dcn > v->dcfclk * v->return_bus_width / 4.0) {
		v->return_bw =dcn_bw_min2(v->return_bw, v->return_bandwidth_to_dcn * 4.0 * (1.0 - v->urgent_latency / ((v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0 / (v->return_bandwidth_to_dcn - v->dcfclk * v->return_bus_width / 4.0) + v->urgent_latency)));
	}
	v->critical_compression = 2.0 * v->return_bus_width * v->dcfclk * v->urgent_latency / (v->return_bandwidth_to_dcn * v->urgent_latency + (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0);
	if (v->dcc_enabled_any_plane == dcn_bw_yes && v->critical_compression > 1.0 && v->critical_compression < 4.0) {
		v->return_bw =dcn_bw_min2(v->return_bw, dcn_bw_pow(4.0 * v->return_bandwidth_to_dcn * (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0 * v->return_bus_width * v->dcfclk * v->urgent_latency / (v->return_bandwidth_to_dcn * v->urgent_latency + (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0), 2));
	}
	v->return_bandwidth_to_dcn =dcn_bw_min2(v->return_bus_width * v->dcfclk, v->fabric_and_dram_bandwidth * 1000.0);
	if (v->dcc_enabled_any_plane == dcn_bw_yes && v->return_bandwidth_to_dcn > v->dcfclk * v->return_bus_width / 4.0) {
		v->return_bw =dcn_bw_min2(v->return_bw, v->return_bandwidth_to_dcn * 4.0 * (1.0 - v->urgent_latency / ((v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0 / (v->return_bandwidth_to_dcn - v->dcfclk * v->return_bus_width / 4.0) + v->urgent_latency)));
	}
	v->critical_compression = 2.0 * v->return_bus_width * v->dcfclk * v->urgent_latency / (v->return_bandwidth_to_dcn * v->urgent_latency + (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0);
	if (v->dcc_enabled_any_plane == dcn_bw_yes && v->critical_compression > 1.0 && v->critical_compression < 4.0) {
		v->return_bw =dcn_bw_min2(v->return_bw, dcn_bw_pow(4.0 * v->return_bandwidth_to_dcn * (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0 * v->return_bus_width * v->dcfclk * v->urgent_latency / (v->return_bandwidth_to_dcn * v->urgent_latency + (v->rob_buffer_size_in_kbyte - v->pixel_chunk_size_in_kbyte) * 1024.0), 2));
	}
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->source_scan[k] == dcn_bw_hor) {
			v->swath_width_y[k] = v->viewport_width[k] / v->dpp_per_plane[k];
		}
		else {
			v->swath_width_y[k] = v->viewport_height[k] / v->dpp_per_plane[k];
		}
	}
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->source_pixel_format[k] == dcn_bw_rgb_sub_64) {
			v->byte_per_pixel_dety[k] = 8.0;
			v->byte_per_pixel_detc[k] = 0.0;
		}
		else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_32) {
			v->byte_per_pixel_dety[k] = 4.0;
			v->byte_per_pixel_detc[k] = 0.0;
		}
		else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_16) {
			v->byte_per_pixel_dety[k] = 2.0;
			v->byte_per_pixel_detc[k] = 0.0;
		}
		else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_8) {
			v->byte_per_pixel_dety[k] = 1.0;
			v->byte_per_pixel_detc[k] = 2.0;
		}
		else {
			v->byte_per_pixel_dety[k] = 4.0f / 3.0f;
			v->byte_per_pixel_detc[k] = 8.0f / 3.0f;
		}
	}
	v->total_data_read_bandwidth = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->read_bandwidth_plane_luma[k] = v->swath_width_y[k] * v->dpp_per_plane[k] *dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) / (v->htotal[k] / v->pixel_clock[k]) * v->v_ratio[k];
		v->read_bandwidth_plane_chroma[k] = v->swath_width_y[k] / 2.0 * v->dpp_per_plane[k] *dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0) / (v->htotal[k] / v->pixel_clock[k]) * v->v_ratio[k] / 2.0;
		v->total_data_read_bandwidth = v->total_data_read_bandwidth + v->read_bandwidth_plane_luma[k] + v->read_bandwidth_plane_chroma[k];
	}
	v->total_active_dpp = 0.0;
	v->total_dcc_active_dpp = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->total_active_dpp = v->total_active_dpp + v->dpp_per_plane[k];
		if (v->dcc_enable[k] == dcn_bw_yes) {
			v->total_dcc_active_dpp = v->total_dcc_active_dpp + v->dpp_per_plane[k];
		}
	}
	v->urgent_round_trip_and_out_of_order_latency = (v->round_trip_ping_latency_cycles + 32.0) / v->dcfclk + v->urgent_out_of_order_return_per_channel * v->number_of_channels / v->return_bw;
	v->last_pixel_of_line_extra_watermark = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->v_ratio[k] <= 1.0) {
			v->display_pipe_line_delivery_time_luma[k] = v->swath_width_y[k] * v->dpp_per_plane[k] / v->h_ratio[k] / v->pixel_clock[k];
		}
		else {
			v->display_pipe_line_delivery_time_luma[k] = v->swath_width_y[k] / v->pscl_throughput[k] / v->dppclk;
		}
		v->data_fabric_line_delivery_time_luma = v->swath_width_y[k] * v->swath_height_y[k] *dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) / (v->return_bw * v->read_bandwidth_plane_luma[k] / v->dpp_per_plane[k] / v->total_data_read_bandwidth);
		v->last_pixel_of_line_extra_watermark =dcn_bw_max2(v->last_pixel_of_line_extra_watermark, v->data_fabric_line_delivery_time_luma - v->display_pipe_line_delivery_time_luma[k]);
		if (v->byte_per_pixel_detc[k] == 0.0) {
			v->display_pipe_line_delivery_time_chroma[k] = 0.0;
		}
		else {
			if (v->v_ratio[k] / 2.0 <= 1.0) {
				v->display_pipe_line_delivery_time_chroma[k] = v->swath_width_y[k] / 2.0 * v->dpp_per_plane[k] / (v->h_ratio[k] / 2.0) / v->pixel_clock[k];
			}
			else {
				v->display_pipe_line_delivery_time_chroma[k] = v->swath_width_y[k] / 2.0 / v->pscl_throughput_chroma[k] / v->dppclk;
			}
			v->data_fabric_line_delivery_time_chroma = v->swath_width_y[k] / 2.0 * v->swath_height_c[k] *dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0) / (v->return_bw * v->read_bandwidth_plane_chroma[k] / v->dpp_per_plane[k] / v->total_data_read_bandwidth);
			v->last_pixel_of_line_extra_watermark =dcn_bw_max2(v->last_pixel_of_line_extra_watermark, v->data_fabric_line_delivery_time_chroma - v->display_pipe_line_delivery_time_chroma[k]);
		}
	}
	v->urgent_extra_latency = v->urgent_round_trip_and_out_of_order_latency + (v->total_active_dpp * v->pixel_chunk_size_in_kbyte + v->total_dcc_active_dpp * v->meta_chunk_size) * 1024.0 / v->return_bw;
	if (v->pte_enable == dcn_bw_yes) {
		v->urgent_extra_latency = v->urgent_extra_latency + v->total_active_dpp * v->pte_chunk_size * 1024.0 / v->return_bw;
	}
	v->urgent_watermark = v->urgent_latency + v->last_pixel_of_line_extra_watermark + v->urgent_extra_latency;
	v->ptemeta_urgent_watermark = v->urgent_watermark + 2.0 * v->urgent_latency;
	/*nb p-state/dram clock change watermark*/

	v->dram_clock_change_watermark = v->dram_clock_change_latency + v->urgent_watermark;
	v->total_active_writeback = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->output[k] == dcn_bw_writeback) {
			v->total_active_writeback = v->total_active_writeback + 1.0;
		}
	}
	if (v->total_active_writeback <= 1.0) {
		v->writeback_dram_clock_change_watermark = v->dram_clock_change_latency + v->write_back_latency;
	}
	else {
		v->writeback_dram_clock_change_watermark = v->dram_clock_change_latency + v->write_back_latency + v->writeback_chunk_size * 1024.0 / 32.0 / v->socclk;
	}
	/*stutter efficiency*/

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->lines_in_dety[k] = v->det_buffer_size_y[k] / v->byte_per_pixel_dety[k] / v->swath_width_y[k];
		v->lines_in_dety_rounded_down_to_swath[k] =dcn_bw_floor2(v->lines_in_dety[k], v->swath_height_y[k]);
		v->full_det_buffering_time_y[k] = v->lines_in_dety_rounded_down_to_swath[k] * (v->htotal[k] / v->pixel_clock[k]) / v->v_ratio[k];
		if (v->byte_per_pixel_detc[k] > 0.0) {
			v->lines_in_detc[k] = v->det_buffer_size_c[k] / v->byte_per_pixel_detc[k] / (v->swath_width_y[k] / 2.0);
			v->lines_in_detc_rounded_down_to_swath[k] =dcn_bw_floor2(v->lines_in_detc[k], v->swath_height_c[k]);
			v->full_det_buffering_time_c[k] = v->lines_in_detc_rounded_down_to_swath[k] * (v->htotal[k] / v->pixel_clock[k]) / (v->v_ratio[k] / 2.0);
		}
		else {
			v->lines_in_detc[k] = 0.0;
			v->lines_in_detc_rounded_down_to_swath[k] = 0.0;
			v->full_det_buffering_time_c[k] = 999999.0;
		}
	}
	v->min_full_det_buffering_time = 999999.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->full_det_buffering_time_y[k] < v->min_full_det_buffering_time) {
			v->min_full_det_buffering_time = v->full_det_buffering_time_y[k];
			v->frame_time_for_min_full_det_buffering_time = v->vtotal[k] * v->htotal[k] / v->pixel_clock[k];
		}
		if (v->full_det_buffering_time_c[k] < v->min_full_det_buffering_time) {
			v->min_full_det_buffering_time = v->full_det_buffering_time_c[k];
			v->frame_time_for_min_full_det_buffering_time = v->vtotal[k] * v->htotal[k] / v->pixel_clock[k];
		}
	}
	v->average_read_bandwidth_gbyte_per_second = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->dcc_enable[k] == dcn_bw_yes) {
			v->average_read_bandwidth_gbyte_per_second = v->average_read_bandwidth_gbyte_per_second + v->read_bandwidth_plane_luma[k] / v->dcc_rate[k] / 1000.0 + v->read_bandwidth_plane_chroma[k] / v->dcc_rate[k] / 1000.0;
		}
		else {
			v->average_read_bandwidth_gbyte_per_second = v->average_read_bandwidth_gbyte_per_second + v->read_bandwidth_plane_luma[k] / 1000.0 + v->read_bandwidth_plane_chroma[k] / 1000.0;
		}
		if (v->dcc_enable[k] == dcn_bw_yes) {
			v->average_read_bandwidth_gbyte_per_second = v->average_read_bandwidth_gbyte_per_second + v->read_bandwidth_plane_luma[k] / 1000.0 / 256.0 + v->read_bandwidth_plane_chroma[k] / 1000.0 / 256.0;
		}
		if (v->pte_enable == dcn_bw_yes) {
			v->average_read_bandwidth_gbyte_per_second = v->average_read_bandwidth_gbyte_per_second + v->read_bandwidth_plane_luma[k] / 1000.0 / 512.0 + v->read_bandwidth_plane_chroma[k] / 1000.0 / 512.0;
		}
	}
	v->part_of_burst_that_fits_in_rob =dcn_bw_min2(v->min_full_det_buffering_time * v->total_data_read_bandwidth, v->rob_buffer_size_in_kbyte * 1024.0 * v->total_data_read_bandwidth / (v->average_read_bandwidth_gbyte_per_second * 1000.0));
	v->stutter_burst_time = v->part_of_burst_that_fits_in_rob * (v->average_read_bandwidth_gbyte_per_second * 1000.0) / v->total_data_read_bandwidth / v->return_bw + (v->min_full_det_buffering_time * v->total_data_read_bandwidth - v->part_of_burst_that_fits_in_rob) / (v->dcfclk * 64.0);
	if (v->total_active_writeback == 0.0) {
		v->stutter_efficiency_not_including_vblank = (1.0 - (v->sr_exit_time + v->stutter_burst_time) / v->min_full_det_buffering_time) * 100.0;
	}
	else {
		v->stutter_efficiency_not_including_vblank = 0.0;
	}
	v->smallest_vblank = 999999.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->synchronized_vblank == dcn_bw_yes || v->number_of_active_planes == 1) {
			v->v_blank_time = (v->vtotal[k] - v->vactive[k]) * v->htotal[k] / v->pixel_clock[k];
		}
		else {
			v->v_blank_time = 0.0;
		}
		v->smallest_vblank =dcn_bw_min2(v->smallest_vblank, v->v_blank_time);
	}
	v->stutter_efficiency = (v->stutter_efficiency_not_including_vblank / 100.0 * (v->frame_time_for_min_full_det_buffering_time - v->smallest_vblank) + v->smallest_vblank) / v->frame_time_for_min_full_det_buffering_time * 100.0;
	/*dcfclk deep sleep*/

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->byte_per_pixel_detc[k] > 0.0) {
			v->dcfclk_deep_sleep_per_plane[k] =dcn_bw_max2(1.1 * v->swath_width_y[k] *dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) / 32.0 / v->display_pipe_line_delivery_time_luma[k], 1.1 * v->swath_width_y[k] / 2.0 *dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0) / 32.0 / v->display_pipe_line_delivery_time_chroma[k]);
		}
		else {
			v->dcfclk_deep_sleep_per_plane[k] = 1.1 * v->swath_width_y[k] *dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) / 64.0 / v->display_pipe_line_delivery_time_luma[k];
		}
		v->dcfclk_deep_sleep_per_plane[k] =dcn_bw_max2(v->dcfclk_deep_sleep_per_plane[k], v->pixel_clock[k] / 16.0);
	}
	v->dcf_clk_deep_sleep = 8.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->dcf_clk_deep_sleep =dcn_bw_max2(v->dcf_clk_deep_sleep, v->dcfclk_deep_sleep_per_plane[k]);
	}
	/*stutter watermark*/

	v->stutter_exit_watermark = v->sr_exit_time + v->last_pixel_of_line_extra_watermark + v->urgent_extra_latency + 10.0 / v->dcf_clk_deep_sleep;
	v->stutter_enter_plus_exit_watermark = v->sr_enter_plus_exit_time + v->last_pixel_of_line_extra_watermark + v->urgent_extra_latency;
	/*urgent latency supported*/

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->effective_det_plus_lb_lines_luma =dcn_bw_floor2(v->lines_in_dety[k] +dcn_bw_min2(v->lines_in_dety[k] * v->dppclk * v->byte_per_pixel_dety[k] * v->pscl_throughput[k] / (v->return_bw / v->dpp_per_plane[k]), v->effective_lb_latency_hiding_source_lines_luma), v->swath_height_y[k]);
		v->urgent_latency_support_us_luma = v->effective_det_plus_lb_lines_luma * (v->htotal[k] / v->pixel_clock[k]) / v->v_ratio[k] - v->effective_det_plus_lb_lines_luma * v->swath_width_y[k] * v->byte_per_pixel_dety[k] / (v->return_bw / v->dpp_per_plane[k]);
		if (v->byte_per_pixel_detc[k] > 0.0) {
			v->effective_det_plus_lb_lines_chroma =dcn_bw_floor2(v->lines_in_detc[k] +dcn_bw_min2(v->lines_in_detc[k] * v->dppclk * v->byte_per_pixel_detc[k] * v->pscl_throughput_chroma[k] / (v->return_bw / v->dpp_per_plane[k]), v->effective_lb_latency_hiding_source_lines_chroma), v->swath_height_c[k]);
			v->urgent_latency_support_us_chroma = v->effective_det_plus_lb_lines_chroma * (v->htotal[k] / v->pixel_clock[k]) / (v->v_ratio[k] / 2.0) - v->effective_det_plus_lb_lines_chroma * (v->swath_width_y[k] / 2.0) * v->byte_per_pixel_detc[k] / (v->return_bw / v->dpp_per_plane[k]);
			v->urgent_latency_support_us[k] =dcn_bw_min2(v->urgent_latency_support_us_luma, v->urgent_latency_support_us_chroma);
		}
		else {
			v->urgent_latency_support_us[k] = v->urgent_latency_support_us_luma;
		}
	}
	v->min_urgent_latency_support_us = 999999.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->min_urgent_latency_support_us =dcn_bw_min2(v->min_urgent_latency_support_us, v->urgent_latency_support_us[k]);
	}
	/*non-urgent latency tolerance*/

	v->non_urgent_latency_tolerance = v->min_urgent_latency_support_us - v->urgent_watermark;
	/*prefetch*/

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if ((v->source_pixel_format[k] == dcn_bw_rgb_sub_64 || v->source_pixel_format[k] == dcn_bw_rgb_sub_32 || v->source_pixel_format[k] == dcn_bw_rgb_sub_16)) {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
				v->block_height256_bytes_y = 1.0;
			}
			else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_64) {
				v->block_height256_bytes_y = 4.0;
			}
			else {
				v->block_height256_bytes_y = 8.0;
			}
			v->block_height256_bytes_c = 0.0;
		}
		else {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
				v->block_height256_bytes_y = 1.0;
				v->block_height256_bytes_c = 1.0;
			}
			else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_8) {
				v->block_height256_bytes_y = 16.0;
				v->block_height256_bytes_c = 8.0;
			}
			else {
				v->block_height256_bytes_y = 8.0;
				v->block_height256_bytes_c = 8.0;
			}
		}
		if (v->dcc_enable[k] == dcn_bw_yes) {
			v->meta_request_width_y = 64.0 * 256.0 /dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) / (8.0 * v->block_height256_bytes_y);
			v->meta_surf_width_y =dcn_bw_ceil2(v->swath_width_y[k] - 1.0, v->meta_request_width_y) + v->meta_request_width_y;
			v->meta_surf_height_y =dcn_bw_ceil2(v->viewport_height[k] - 1.0, 8.0 * v->block_height256_bytes_y) + 8.0 * v->block_height256_bytes_y;
			if (v->pte_enable == dcn_bw_yes) {
				v->meta_pte_bytes_frame_y = (dcn_bw_ceil2((v->meta_surf_width_y * v->meta_surf_height_y *dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) / 256.0 - 4096.0) / 8.0 / 4096.0, 1.0) + 1) * 64.0;
			}
			else {
				v->meta_pte_bytes_frame_y = 0.0;
			}
			if (v->source_scan[k] == dcn_bw_hor) {
				v->meta_row_byte_y = v->meta_surf_width_y * 8.0 * v->block_height256_bytes_y *dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) / 256.0;
			}
			else {
				v->meta_row_byte_y = v->meta_surf_height_y * v->meta_request_width_y *dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) / 256.0;
			}
		}
		else {
			v->meta_pte_bytes_frame_y = 0.0;
			v->meta_row_byte_y = 0.0;
		}
		if (v->pte_enable == dcn_bw_yes) {
			if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
				v->macro_tile_size_byte_y = 256.0;
				v->macro_tile_height_y = 1.0;
			}
			else if (v->source_surface_mode[k] == dcn_bw_sw_4_kb_s || v->source_surface_mode[k] == dcn_bw_sw_4_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d_x) {
				v->macro_tile_size_byte_y = 4096.0;
				v->macro_tile_height_y = 4.0 * v->block_height256_bytes_y;
			}
			else if (v->source_surface_mode[k] == dcn_bw_sw_64_kb_s || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_x) {
				v->macro_tile_size_byte_y = 64.0 * 1024;
				v->macro_tile_height_y = 16.0 * v->block_height256_bytes_y;
			}
			else {
				v->macro_tile_size_byte_y = 256.0 * 1024;
				v->macro_tile_height_y = 32.0 * v->block_height256_bytes_y;
			}
			if (v->macro_tile_size_byte_y <= 65536.0) {
				v->pixel_pte_req_height_y = v->macro_tile_height_y;
			}
			else {
				v->pixel_pte_req_height_y = 16.0 * v->block_height256_bytes_y;
			}
			v->pixel_pte_req_width_y = 4096.0 /dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) / v->pixel_pte_req_height_y * 8;
			if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
				v->pixel_pte_bytes_per_row_y = 64.0 * (dcn_bw_ceil2((v->swath_width_y[k] *dcn_bw_min2(128.0, dcn_bw_pow(2.0,dcn_bw_floor2(dcn_bw_log(v->pte_buffer_size_in_requests * v->pixel_pte_req_width_y / v->swath_width_y[k], 2.0), 1.0))) - 1.0) / v->pixel_pte_req_width_y, 1.0) + 1);
			}
			else if (v->source_scan[k] == dcn_bw_hor) {
				v->pixel_pte_bytes_per_row_y = 64.0 * (dcn_bw_ceil2((v->swath_width_y[k] - 1.0) / v->pixel_pte_req_width_y, 1.0) + 1);
			}
			else {
				v->pixel_pte_bytes_per_row_y = 64.0 * (dcn_bw_ceil2((v->viewport_height[k] - 1.0) / v->pixel_pte_req_height_y, 1.0) + 1);
			}
		}
		else {
			v->pixel_pte_bytes_per_row_y = 0.0;
		}
		if ((v->source_pixel_format[k] != dcn_bw_rgb_sub_64 && v->source_pixel_format[k] != dcn_bw_rgb_sub_32 && v->source_pixel_format[k] != dcn_bw_rgb_sub_16)) {
			if (v->dcc_enable[k] == dcn_bw_yes) {
				v->meta_request_width_c = 64.0 * 256.0 /dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0) / (8.0 * v->block_height256_bytes_c);
				v->meta_surf_width_c =dcn_bw_ceil2(v->swath_width_y[k] / 2.0 - 1.0, v->meta_request_width_c) + v->meta_request_width_c;
				v->meta_surf_height_c =dcn_bw_ceil2(v->viewport_height[k] / 2.0 - 1.0, 8.0 * v->block_height256_bytes_c) + 8.0 * v->block_height256_bytes_c;
				if (v->pte_enable == dcn_bw_yes) {
					v->meta_pte_bytes_frame_c = (dcn_bw_ceil2((v->meta_surf_width_c * v->meta_surf_height_c *dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0) / 256.0 - 4096.0) / 8.0 / 4096.0, 1.0) + 1) * 64.0;
				}
				else {
					v->meta_pte_bytes_frame_c = 0.0;
				}
				if (v->source_scan[k] == dcn_bw_hor) {
					v->meta_row_byte_c = v->meta_surf_width_c * 8.0 * v->block_height256_bytes_c *dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0) / 256.0;
				}
				else {
					v->meta_row_byte_c = v->meta_surf_height_c * v->meta_request_width_c *dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0) / 256.0;
				}
			}
			else {
				v->meta_pte_bytes_frame_c = 0.0;
				v->meta_row_byte_c = 0.0;
			}
			if (v->pte_enable == dcn_bw_yes) {
				if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
					v->macro_tile_size_bytes_c = 256.0;
					v->macro_tile_height_c = 1.0;
				}
				else if (v->source_surface_mode[k] == dcn_bw_sw_4_kb_s || v->source_surface_mode[k] == dcn_bw_sw_4_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d || v->source_surface_mode[k] == dcn_bw_sw_4_kb_d_x) {
					v->macro_tile_size_bytes_c = 4096.0;
					v->macro_tile_height_c = 4.0 * v->block_height256_bytes_c;
				}
				else if (v->source_surface_mode[k] == dcn_bw_sw_64_kb_s || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_s_x || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_t || v->source_surface_mode[k] == dcn_bw_sw_64_kb_d_x) {
					v->macro_tile_size_bytes_c = 64.0 * 1024;
					v->macro_tile_height_c = 16.0 * v->block_height256_bytes_c;
				}
				else {
					v->macro_tile_size_bytes_c = 256.0 * 1024;
					v->macro_tile_height_c = 32.0 * v->block_height256_bytes_c;
				}
				if (v->macro_tile_size_bytes_c <= 65536.0) {
					v->pixel_pte_req_height_c = v->macro_tile_height_c;
				}
				else {
					v->pixel_pte_req_height_c = 16.0 * v->block_height256_bytes_c;
				}
				v->pixel_pte_req_width_c = 4096.0 /dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0) / v->pixel_pte_req_height_c * 8;
				if (v->source_surface_mode[k] == dcn_bw_sw_linear) {
					v->pixel_pte_bytes_per_row_c = 64.0 * (dcn_bw_ceil2((v->swath_width_y[k] / 2.0 * dcn_bw_min2(128.0, dcn_bw_pow(2.0,dcn_bw_floor2(dcn_bw_log(v->pte_buffer_size_in_requests * v->pixel_pte_req_width_c / (v->swath_width_y[k] / 2.0), 2.0), 1.0))) - 1.0) / v->pixel_pte_req_width_c, 1.0) + 1);
				}
				else if (v->source_scan[k] == dcn_bw_hor) {
					v->pixel_pte_bytes_per_row_c = 64.0 * (dcn_bw_ceil2((v->swath_width_y[k] / 2.0 - 1.0) / v->pixel_pte_req_width_c, 1.0) + 1);
				}
				else {
					v->pixel_pte_bytes_per_row_c = 64.0 * (dcn_bw_ceil2((v->viewport_height[k] / 2.0 - 1.0) / v->pixel_pte_req_height_c, 1.0) + 1);
				}
			}
			else {
				v->pixel_pte_bytes_per_row_c = 0.0;
			}
		}
		else {
			v->pixel_pte_bytes_per_row_c = 0.0;
			v->meta_pte_bytes_frame_c = 0.0;
			v->meta_row_byte_c = 0.0;
		}
		v->pixel_pte_bytes_per_row[k] = v->pixel_pte_bytes_per_row_y + v->pixel_pte_bytes_per_row_c;
		v->meta_pte_bytes_frame[k] = v->meta_pte_bytes_frame_y + v->meta_pte_bytes_frame_c;
		v->meta_row_byte[k] = v->meta_row_byte_y + v->meta_row_byte_c;
		v->v_init_pre_fill_y[k] =dcn_bw_floor2((v->v_ratio[k] + v->vtaps[k] + 1.0 + v->interlace_output[k] * 0.5 * v->v_ratio[k]) / 2.0, 1.0);
		v->max_num_swath_y[k] =dcn_bw_ceil2((v->v_init_pre_fill_y[k] - 1.0) / v->swath_height_y[k], 1.0) + 1;
		if (v->v_init_pre_fill_y[k] > 1.0) {
			v->max_partial_swath_y =dcn_bw_mod((v->v_init_pre_fill_y[k] - 2.0), v->swath_height_y[k]);
		}
		else {
			v->max_partial_swath_y =dcn_bw_mod((v->v_init_pre_fill_y[k] + v->swath_height_y[k] - 2.0), v->swath_height_y[k]);
		}
		v->max_partial_swath_y =dcn_bw_max2(1.0, v->max_partial_swath_y);
		v->prefetch_source_lines_y[k] = v->max_num_swath_y[k] * v->swath_height_y[k] + v->max_partial_swath_y;
		if ((v->source_pixel_format[k] != dcn_bw_rgb_sub_64 && v->source_pixel_format[k] != dcn_bw_rgb_sub_32 && v->source_pixel_format[k] != dcn_bw_rgb_sub_16)) {
			v->v_init_pre_fill_c[k] =dcn_bw_floor2((v->v_ratio[k] / 2.0 + v->vtaps[k] + 1.0 + v->interlace_output[k] * 0.5 * v->v_ratio[k] / 2.0) / 2.0, 1.0);
			v->max_num_swath_c[k] =dcn_bw_ceil2((v->v_init_pre_fill_c[k] - 1.0) / v->swath_height_c[k], 1.0) + 1;
			if (v->v_init_pre_fill_c[k] > 1.0) {
				v->max_partial_swath_c =dcn_bw_mod((v->v_init_pre_fill_c[k] - 2.0), v->swath_height_c[k]);
			}
			else {
				v->max_partial_swath_c =dcn_bw_mod((v->v_init_pre_fill_c[k] + v->swath_height_c[k] - 2.0), v->swath_height_c[k]);
			}
			v->max_partial_swath_c =dcn_bw_max2(1.0, v->max_partial_swath_c);
		}
		else {
			v->max_num_swath_c[k] = 0.0;
			v->max_partial_swath_c = 0.0;
		}
		v->prefetch_source_lines_c[k] = v->max_num_swath_c[k] * v->swath_height_c[k] + v->max_partial_swath_c;
	}
	v->t_calc = 24.0 / v->dcf_clk_deep_sleep;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one == dcn_bw_yes) {
			v->max_vstartup_lines[k] = v->vtotal[k] - v->vactive[k] - 1.0;
		}
		else {
			v->max_vstartup_lines[k] = v->v_sync_plus_back_porch[k] - 1.0;
		}
	}
	v->next_prefetch_mode = 0.0;
	do {
		v->v_startup_lines = 13.0;
		do {
			v->planes_with_room_to_increase_vstartup_prefetch_bw_less_than_active_bw = dcn_bw_yes;
			v->planes_with_room_to_increase_vstartup_vratio_prefetch_more_than4 = dcn_bw_no;
			v->planes_with_room_to_increase_vstartup_destination_line_times_for_prefetch_less_than2 = dcn_bw_no;
			v->v_ratio_prefetch_more_than4 = dcn_bw_no;
			v->destination_line_times_for_prefetch_less_than2 = dcn_bw_no;
			v->prefetch_mode = v->next_prefetch_mode;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				v->dstx_after_scaler = 90.0 * v->pixel_clock[k] / v->dppclk + 42.0 * v->pixel_clock[k] / v->dispclk;
				if (v->dpp_per_plane[k] > 1.0) {
					v->dstx_after_scaler = v->dstx_after_scaler + v->scaler_rec_out_width[k] / 2.0;
				}
				if (v->output_format[k] == dcn_bw_420) {
					v->dsty_after_scaler = 1.0;
				}
				else {
					v->dsty_after_scaler = 0.0;
				}
				v->v_update_offset_pix[k] = dcn_bw_ceil2(v->htotal[k] / 4.0, 1.0);
				v->total_repeater_delay_time = v->max_inter_dcn_tile_repeaters * (2.0 / v->dppclk + 3.0 / v->dispclk);
				v->v_update_width_pix[k] = (14.0 / v->dcf_clk_deep_sleep + 12.0 / v->dppclk + v->total_repeater_delay_time) * v->pixel_clock[k];
				v->v_ready_offset_pix[k] = dcn_bw_max2(150.0 / v->dppclk, v->total_repeater_delay_time + 20.0 / v->dcf_clk_deep_sleep + 10.0 / v->dppclk) * v->pixel_clock[k];
				v->t_setup = (v->v_update_offset_pix[k] + v->v_update_width_pix[k] + v->v_ready_offset_pix[k]) / v->pixel_clock[k];
				v->v_startup[k] =dcn_bw_min2(v->v_startup_lines, v->max_vstartup_lines[k]);
				if (v->prefetch_mode == 0.0) {
					v->t_wait =dcn_bw_max3(v->dram_clock_change_latency + v->urgent_latency, v->sr_enter_plus_exit_time, v->urgent_latency);
				}
				else if (v->prefetch_mode == 1.0) {
					v->t_wait =dcn_bw_max2(v->sr_enter_plus_exit_time, v->urgent_latency);
				}
				else {
					v->t_wait = v->urgent_latency;
				}
				v->destination_lines_for_prefetch[k] =dcn_bw_floor2(4.0 * (v->v_startup[k] - v->t_wait / (v->htotal[k] / v->pixel_clock[k]) - (v->t_calc + v->t_setup) / (v->htotal[k] / v->pixel_clock[k]) - (v->dsty_after_scaler + v->dstx_after_scaler / v->htotal[k]) + 0.125), 1.0) / 4;
				if (v->destination_lines_for_prefetch[k] > 0.0) {
					v->prefetch_bandwidth[k] = (v->meta_pte_bytes_frame[k] + 2.0 * v->meta_row_byte[k] + 2.0 * v->pixel_pte_bytes_per_row[k] + v->prefetch_source_lines_y[k] * v->swath_width_y[k] *dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) + v->prefetch_source_lines_c[k] * v->swath_width_y[k] / 2.0 *dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0)) / (v->destination_lines_for_prefetch[k] * v->htotal[k] / v->pixel_clock[k]);
				}
				else {
					v->prefetch_bandwidth[k] = 999999.0;
				}
			}
			v->bandwidth_available_for_immediate_flip = v->return_bw;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				v->bandwidth_available_for_immediate_flip = v->bandwidth_available_for_immediate_flip -dcn_bw_max2(v->read_bandwidth_plane_luma[k] + v->read_bandwidth_plane_chroma[k], v->prefetch_bandwidth[k]);
			}
			v->tot_immediate_flip_bytes = 0.0;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if (v->immediate_flip_supported == dcn_bw_yes && (v->source_pixel_format[k] != dcn_bw_yuv420_sub_8 && v->source_pixel_format[k] != dcn_bw_yuv420_sub_10)) {
					v->tot_immediate_flip_bytes = v->tot_immediate_flip_bytes + v->meta_pte_bytes_frame[k] + v->meta_row_byte[k] + v->pixel_pte_bytes_per_row[k];
				}
			}
			v->max_rd_bandwidth = 0.0;
			for (k = 0; k <= v->number_of_active_planes - 1; k++) {
				if (v->pte_enable == dcn_bw_yes && v->dcc_enable[k] == dcn_bw_yes) {
					if (v->immediate_flip_supported == dcn_bw_yes && (v->source_pixel_format[k] != dcn_bw_yuv420_sub_8 && v->source_pixel_format[k] != dcn_bw_yuv420_sub_10)) {
						v->time_for_fetching_meta_pte =dcn_bw_max5(v->meta_pte_bytes_frame[k] / v->prefetch_bandwidth[k], v->meta_pte_bytes_frame[k] * v->tot_immediate_flip_bytes / (v->bandwidth_available_for_immediate_flip * (v->meta_pte_bytes_frame[k] + v->meta_row_byte[k] + v->pixel_pte_bytes_per_row[k])), v->urgent_extra_latency, v->urgent_latency, v->htotal[k] / v->pixel_clock[k] / 4.0);
					}
					else {
						v->time_for_fetching_meta_pte =dcn_bw_max3(v->meta_pte_bytes_frame[k] / v->prefetch_bandwidth[k], v->urgent_extra_latency, v->htotal[k] / v->pixel_clock[k] / 4.0);
					}
				}
				else {
					v->time_for_fetching_meta_pte = v->htotal[k] / v->pixel_clock[k] / 4.0;
				}
				v->destination_lines_to_request_vm_inv_blank[k] =dcn_bw_floor2(4.0 * (v->time_for_fetching_meta_pte / (v->htotal[k] / v->pixel_clock[k]) + 0.125), 1.0) / 4;
				if ((v->pte_enable == dcn_bw_yes || v->dcc_enable[k] == dcn_bw_yes)) {
					if (v->immediate_flip_supported == dcn_bw_yes && (v->source_pixel_format[k] != dcn_bw_yuv420_sub_8 && v->source_pixel_format[k] != dcn_bw_yuv420_sub_10)) {
						v->time_for_fetching_row_in_vblank =dcn_bw_max5((v->meta_row_byte[k] + v->pixel_pte_bytes_per_row[k]) / v->prefetch_bandwidth[k], (v->meta_row_byte[k] + v->pixel_pte_bytes_per_row[k]) * v->tot_immediate_flip_bytes / (v->bandwidth_available_for_immediate_flip * (v->meta_pte_bytes_frame[k] + v->meta_row_byte[k] + v->pixel_pte_bytes_per_row[k])), v->urgent_extra_latency, 2.0 * v->urgent_latency, v->htotal[k] / v->pixel_clock[k] - v->time_for_fetching_meta_pte);
					}
					else {
						v->time_for_fetching_row_in_vblank =dcn_bw_max3((v->meta_row_byte[k] + v->pixel_pte_bytes_per_row[k]) / v->prefetch_bandwidth[k], v->urgent_extra_latency, v->htotal[k] / v->pixel_clock[k] - v->time_for_fetching_meta_pte);
					}
				}
				else {
					v->time_for_fetching_row_in_vblank =dcn_bw_max2(v->urgent_extra_latency - v->time_for_fetching_meta_pte, v->htotal[k] / v->pixel_clock[k] - v->time_for_fetching_meta_pte);
				}
				v->destination_lines_to_request_row_in_vblank[k] =dcn_bw_floor2(4.0 * (v->time_for_fetching_row_in_vblank / (v->htotal[k] / v->pixel_clock[k]) + 0.125), 1.0) / 4;
				v->lines_to_request_prefetch_pixel_data = v->destination_lines_for_prefetch[k] - v->destination_lines_to_request_vm_inv_blank[k] - v->destination_lines_to_request_row_in_vblank[k];
				if (v->lines_to_request_prefetch_pixel_data > 0.0) {
					v->v_ratio_prefetch_y[k] = v->prefetch_source_lines_y[k] / v->lines_to_request_prefetch_pixel_data;
					if ((v->swath_height_y[k] > 4.0)) {
						if (v->lines_to_request_prefetch_pixel_data > (v->v_init_pre_fill_y[k] - 3.0) / 2.0) {
							v->v_ratio_prefetch_y[k] =dcn_bw_max2(v->v_ratio_prefetch_y[k], v->max_num_swath_y[k] * v->swath_height_y[k] / (v->lines_to_request_prefetch_pixel_data - (v->v_init_pre_fill_y[k] - 3.0) / 2.0));
						}
						else {
							v->v_ratio_prefetch_y[k] = 999999.0;
						}
					}
				}
				else {
					v->v_ratio_prefetch_y[k] = 999999.0;
				}
				v->v_ratio_prefetch_y[k] =dcn_bw_max2(v->v_ratio_prefetch_y[k], 1.0);
				if (v->lines_to_request_prefetch_pixel_data > 0.0) {
					v->v_ratio_prefetch_c[k] = v->prefetch_source_lines_c[k] / v->lines_to_request_prefetch_pixel_data;
					if ((v->swath_height_c[k] > 4.0)) {
						if (v->lines_to_request_prefetch_pixel_data > (v->v_init_pre_fill_c[k] - 3.0) / 2.0) {
							v->v_ratio_prefetch_c[k] =dcn_bw_max2(v->v_ratio_prefetch_c[k], v->max_num_swath_c[k] * v->swath_height_c[k] / (v->lines_to_request_prefetch_pixel_data - (v->v_init_pre_fill_c[k] - 3.0) / 2.0));
						}
						else {
							v->v_ratio_prefetch_c[k] = 999999.0;
						}
					}
				}
				else {
					v->v_ratio_prefetch_c[k] = 999999.0;
				}
				v->v_ratio_prefetch_c[k] =dcn_bw_max2(v->v_ratio_prefetch_c[k], 1.0);
				if (v->lines_to_request_prefetch_pixel_data > 0.0) {
					v->required_prefetch_pix_data_bw = v->dpp_per_plane[k] * (v->prefetch_source_lines_y[k] / v->lines_to_request_prefetch_pixel_data *dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) + v->prefetch_source_lines_c[k] / v->lines_to_request_prefetch_pixel_data *dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0) / 2.0) * v->swath_width_y[k] / (v->htotal[k] / v->pixel_clock[k]);
				}
				else {
					v->required_prefetch_pix_data_bw = 999999.0;
				}
				v->max_rd_bandwidth = v->max_rd_bandwidth +dcn_bw_max2(v->read_bandwidth_plane_luma[k] + v->read_bandwidth_plane_chroma[k], v->required_prefetch_pix_data_bw);
				if (v->immediate_flip_supported == dcn_bw_yes && (v->source_pixel_format[k] != dcn_bw_yuv420_sub_8 && v->source_pixel_format[k] != dcn_bw_yuv420_sub_10)) {
					v->max_rd_bandwidth = v->max_rd_bandwidth +dcn_bw_max2(v->meta_pte_bytes_frame[k] / (v->destination_lines_to_request_vm_inv_blank[k] * v->htotal[k] / v->pixel_clock[k]), (v->meta_row_byte[k] + v->pixel_pte_bytes_per_row[k]) / (v->destination_lines_to_request_row_in_vblank[k] * v->htotal[k] / v->pixel_clock[k]));
				}
				if (v->v_ratio_prefetch_y[k] > 4.0 || v->v_ratio_prefetch_c[k] > 4.0) {
					v->v_ratio_prefetch_more_than4 = dcn_bw_yes;
				}
				if (v->destination_lines_for_prefetch[k] < 2.0) {
					v->destination_line_times_for_prefetch_less_than2 = dcn_bw_yes;
				}
				if (v->max_vstartup_lines[k] > v->v_startup_lines) {
					if (v->required_prefetch_pix_data_bw > (v->read_bandwidth_plane_luma[k] + v->read_bandwidth_plane_chroma[k])) {
						v->planes_with_room_to_increase_vstartup_prefetch_bw_less_than_active_bw = dcn_bw_no;
					}
					if (v->v_ratio_prefetch_y[k] > 4.0 || v->v_ratio_prefetch_c[k] > 4.0) {
						v->planes_with_room_to_increase_vstartup_vratio_prefetch_more_than4 = dcn_bw_yes;
					}
					if (v->destination_lines_for_prefetch[k] < 2.0) {
						v->planes_with_room_to_increase_vstartup_destination_line_times_for_prefetch_less_than2 = dcn_bw_yes;
					}
				}
			}
			if (v->max_rd_bandwidth <= v->return_bw && v->v_ratio_prefetch_more_than4 == dcn_bw_no && v->destination_line_times_for_prefetch_less_than2 == dcn_bw_no) {
				v->prefetch_mode_supported = dcn_bw_yes;
			}
			else {
				v->prefetch_mode_supported = dcn_bw_no;
			}
			v->v_startup_lines = v->v_startup_lines + 1.0;
		} while (!(v->prefetch_mode_supported == dcn_bw_yes || (v->planes_with_room_to_increase_vstartup_prefetch_bw_less_than_active_bw == dcn_bw_yes && v->planes_with_room_to_increase_vstartup_vratio_prefetch_more_than4 == dcn_bw_no && v->planes_with_room_to_increase_vstartup_destination_line_times_for_prefetch_less_than2 == dcn_bw_no)));
		v->next_prefetch_mode = v->next_prefetch_mode + 1.0;
	} while (!(v->prefetch_mode_supported == dcn_bw_yes || v->prefetch_mode == 2.0));
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->v_ratio_prefetch_y[k] <= 1.0) {
			v->display_pipe_line_delivery_time_luma_prefetch[k] = v->swath_width_y[k] * v->dpp_per_plane[k] / v->h_ratio[k] / v->pixel_clock[k];
		}
		else {
			v->display_pipe_line_delivery_time_luma_prefetch[k] = v->swath_width_y[k] / v->pscl_throughput[k] / v->dppclk;
		}
		if (v->byte_per_pixel_detc[k] == 0.0) {
			v->display_pipe_line_delivery_time_chroma_prefetch[k] = 0.0;
		}
		else {
			if (v->v_ratio_prefetch_c[k] <= 1.0) {
				v->display_pipe_line_delivery_time_chroma_prefetch[k] = v->swath_width_y[k] * v->dpp_per_plane[k] / v->h_ratio[k] / v->pixel_clock[k];
			}
			else {
				v->display_pipe_line_delivery_time_chroma_prefetch[k] = v->swath_width_y[k] / v->pscl_throughput[k] / v->dppclk;
			}
		}
	}
	/*min ttuv_blank*/

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->prefetch_mode == 0.0) {
			v->allow_dram_clock_change_during_vblank[k] = dcn_bw_yes;
			v->allow_dram_self_refresh_during_vblank[k] = dcn_bw_yes;
			v->min_ttuv_blank[k] = v->t_calc +dcn_bw_max3(v->dram_clock_change_watermark, v->stutter_enter_plus_exit_watermark, v->urgent_watermark);
		}
		else if (v->prefetch_mode == 1.0) {
			v->allow_dram_clock_change_during_vblank[k] = dcn_bw_no;
			v->allow_dram_self_refresh_during_vblank[k] = dcn_bw_yes;
			v->min_ttuv_blank[k] = v->t_calc +dcn_bw_max2(v->stutter_enter_plus_exit_watermark, v->urgent_watermark);
		}
		else {
			v->allow_dram_clock_change_during_vblank[k] = dcn_bw_no;
			v->allow_dram_self_refresh_during_vblank[k] = dcn_bw_no;
			v->min_ttuv_blank[k] = v->t_calc + v->urgent_watermark;
		}
	}
	/*nb p-state/dram clock change support*/

	v->active_dp_ps = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->active_dp_ps = v->active_dp_ps + v->dpp_per_plane[k];
	}
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->lb_latency_hiding_source_lines_y =dcn_bw_min2(v->max_line_buffer_lines,dcn_bw_floor2(v->line_buffer_size / v->lb_bit_per_pixel[k] / (v->swath_width_y[k] /dcn_bw_max2(v->h_ratio[k], 1.0)), 1.0)) - (v->vtaps[k] - 1.0);
		v->lb_latency_hiding_source_lines_c =dcn_bw_min2(v->max_line_buffer_lines,dcn_bw_floor2(v->line_buffer_size / v->lb_bit_per_pixel[k] / (v->swath_width_y[k] / 2.0 /dcn_bw_max2(v->h_ratio[k] / 2.0, 1.0)), 1.0)) - (v->vta_pschroma[k] - 1.0);
		v->effective_lb_latency_hiding_y = v->lb_latency_hiding_source_lines_y / v->v_ratio[k] * (v->htotal[k] / v->pixel_clock[k]);
		v->effective_lb_latency_hiding_c = v->lb_latency_hiding_source_lines_c / (v->v_ratio[k] / 2.0) * (v->htotal[k] / v->pixel_clock[k]);
		if (v->swath_width_y[k] > 2.0 * v->dpp_output_buffer_pixels) {
			v->dpp_output_buffer_lines_y = v->dpp_output_buffer_pixels / v->swath_width_y[k];
		}
		else if (v->swath_width_y[k] > v->dpp_output_buffer_pixels) {
			v->dpp_output_buffer_lines_y = 0.5;
		}
		else {
			v->dpp_output_buffer_lines_y = 1.0;
		}
		if (v->swath_width_y[k] / 2.0 > 2.0 * v->dpp_output_buffer_pixels) {
			v->dpp_output_buffer_lines_c = v->dpp_output_buffer_pixels / (v->swath_width_y[k] / 2.0);
		}
		else if (v->swath_width_y[k] / 2.0 > v->dpp_output_buffer_pixels) {
			v->dpp_output_buffer_lines_c = 0.5;
		}
		else {
			v->dpp_output_buffer_lines_c = 1.0;
		}
		v->dppopp_buffering_y = (v->htotal[k] / v->pixel_clock[k]) * (v->dpp_output_buffer_lines_y + v->opp_output_buffer_lines);
		v->max_det_buffering_time_y = v->full_det_buffering_time_y[k] + (v->lines_in_dety[k] - v->lines_in_dety_rounded_down_to_swath[k]) / v->swath_height_y[k] * (v->htotal[k] / v->pixel_clock[k]);
		v->active_dram_clock_change_latency_margin_y = v->dppopp_buffering_y + v->effective_lb_latency_hiding_y + v->max_det_buffering_time_y - v->dram_clock_change_watermark;
		if (v->active_dp_ps > 1.0) {
			v->active_dram_clock_change_latency_margin_y = v->active_dram_clock_change_latency_margin_y - (1.0 - 1.0 / (v->active_dp_ps - 1.0)) * v->swath_height_y[k] * (v->htotal[k] / v->pixel_clock[k]);
		}
		if (v->byte_per_pixel_detc[k] > 0.0) {
			v->dppopp_buffering_c = (v->htotal[k] / v->pixel_clock[k]) * (v->dpp_output_buffer_lines_c + v->opp_output_buffer_lines);
			v->max_det_buffering_time_c = v->full_det_buffering_time_c[k] + (v->lines_in_detc[k] - v->lines_in_detc_rounded_down_to_swath[k]) / v->swath_height_c[k] * (v->htotal[k] / v->pixel_clock[k]);
			v->active_dram_clock_change_latency_margin_c = v->dppopp_buffering_c + v->effective_lb_latency_hiding_c + v->max_det_buffering_time_c - v->dram_clock_change_watermark;
			if (v->active_dp_ps > 1.0) {
				v->active_dram_clock_change_latency_margin_c = v->active_dram_clock_change_latency_margin_c - (1.0 - 1.0 / (v->active_dp_ps - 1.0)) * v->swath_height_c[k] * (v->htotal[k] / v->pixel_clock[k]);
			}
			v->active_dram_clock_change_latency_margin[k] =dcn_bw_min2(v->active_dram_clock_change_latency_margin_y, v->active_dram_clock_change_latency_margin_c);
		}
		else {
			v->active_dram_clock_change_latency_margin[k] = v->active_dram_clock_change_latency_margin_y;
		}
		if (v->output_format[k] == dcn_bw_444) {
			v->writeback_dram_clock_change_latency_margin = (v->writeback_luma_buffer_size + v->writeback_chroma_buffer_size) * 1024.0 / (v->scaler_rec_out_width[k] / (v->htotal[k] / v->pixel_clock[k]) * 4.0) - v->writeback_dram_clock_change_watermark;
		}
		else {
			v->writeback_dram_clock_change_latency_margin =dcn_bw_min2(v->writeback_luma_buffer_size, 2.0 * v->writeback_chroma_buffer_size) * 1024.0 / (v->scaler_rec_out_width[k] / (v->htotal[k] / v->pixel_clock[k])) - v->writeback_dram_clock_change_watermark;
		}
		if (v->output[k] == dcn_bw_writeback) {
			v->active_dram_clock_change_latency_margin[k] =dcn_bw_min2(v->active_dram_clock_change_latency_margin[k], v->writeback_dram_clock_change_latency_margin);
		}
	}
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->allow_dram_clock_change_during_vblank[k] == dcn_bw_yes) {
			v->v_blank_dram_clock_change_latency_margin[k] = (v->vtotal[k] - v->scaler_recout_height[k]) * (v->htotal[k] / v->pixel_clock[k]) -dcn_bw_max2(v->dram_clock_change_watermark, v->writeback_dram_clock_change_watermark);
		}
		else {
			v->v_blank_dram_clock_change_latency_margin[k] = 0.0;
		}
	}
	v->min_active_dram_clock_change_margin = 999999.0;
	v->v_blank_of_min_active_dram_clock_change_margin = 999999.0;
	v->second_min_active_dram_clock_change_margin = 999999.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->active_dram_clock_change_latency_margin[k] < v->min_active_dram_clock_change_margin) {
			v->second_min_active_dram_clock_change_margin = v->min_active_dram_clock_change_margin;
			v->min_active_dram_clock_change_margin = v->active_dram_clock_change_latency_margin[k];
			v->v_blank_of_min_active_dram_clock_change_margin = v->v_blank_dram_clock_change_latency_margin[k];
		}
		else if (v->active_dram_clock_change_latency_margin[k] < v->second_min_active_dram_clock_change_margin) {
			v->second_min_active_dram_clock_change_margin = v->active_dram_clock_change_latency_margin[k];
		}
	}
	v->min_vblank_dram_clock_change_margin = 999999.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->min_vblank_dram_clock_change_margin > v->v_blank_dram_clock_change_latency_margin[k]) {
			v->min_vblank_dram_clock_change_margin = v->v_blank_dram_clock_change_latency_margin[k];
		}
	}
	if (v->synchronized_vblank == dcn_bw_yes || v->number_of_active_planes == 1) {
		v->dram_clock_change_margin =dcn_bw_max2(v->min_active_dram_clock_change_margin, v->min_vblank_dram_clock_change_margin);
	}
	else if (v->v_blank_of_min_active_dram_clock_change_margin > v->min_active_dram_clock_change_margin) {
		v->dram_clock_change_margin =dcn_bw_min2(v->second_min_active_dram_clock_change_margin, v->v_blank_of_min_active_dram_clock_change_margin);
	}
	else {
		v->dram_clock_change_margin = v->min_active_dram_clock_change_margin;
	}
	if (v->min_active_dram_clock_change_margin > 0.0) {
		v->dram_clock_change_support = dcn_bw_supported_in_v_active;
	}
	else if (v->dram_clock_change_margin > 0.0) {
		v->dram_clock_change_support = dcn_bw_supported_in_v_blank;
	}
	else {
		v->dram_clock_change_support = dcn_bw_not_supported;
	}
	/*maximum bandwidth used*/

	v->wr_bandwidth = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->output[k] == dcn_bw_writeback && v->output_format[k] == dcn_bw_444) {
			v->wr_bandwidth = v->wr_bandwidth + v->scaler_rec_out_width[k] / (v->htotal[k] / v->pixel_clock[k]) * 4.0;
		}
		else if (v->output[k] == dcn_bw_writeback) {
			v->wr_bandwidth = v->wr_bandwidth + v->scaler_rec_out_width[k] / (v->htotal[k] / v->pixel_clock[k]) * 1.5;
		}
	}
	v->max_used_bw = v->max_rd_bandwidth + v->wr_bandwidth;
}
