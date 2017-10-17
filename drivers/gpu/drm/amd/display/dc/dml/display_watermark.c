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
#include "display_watermark.h"
#include "display_mode_lib.h"

static void get_bytes_per_pixel(
		enum source_format_class format,
		struct _vcs_dpi_wm_calc_pipe_params_st *plane)
{
	switch (format) {
	case dm_444_64:
		plane->bytes_per_pixel_y = 8.0;
		plane->bytes_per_pixel_c = 0.0;
		break;
	case dm_444_32:
		plane->bytes_per_pixel_y = 4.0;
		plane->bytes_per_pixel_c = 0.0;
		break;
	case dm_444_16:
		plane->bytes_per_pixel_y = 2.0;
		plane->bytes_per_pixel_c = 0.0;
		break;
	case dm_422_10:
		plane->bytes_per_pixel_y = 4.0;
		plane->bytes_per_pixel_c = 0.0;
		break;
	case dm_422_8:
		plane->bytes_per_pixel_y = 2.0;
		plane->bytes_per_pixel_c = 0.0;
		break;
	case dm_420_8:
		plane->bytes_per_pixel_y = 1.0;
		plane->bytes_per_pixel_c = 2.0;
		break;
	case dm_420_10:
		plane->bytes_per_pixel_y = 4.0 / 3;
		plane->bytes_per_pixel_c = 8.0 / 3;
		break;
	default:
		BREAK_TO_DEBUGGER(); /* invalid format in get_bytes_per_pixel */
	}
}

static unsigned int get_swath_width_y(
		struct _vcs_dpi_display_pipe_source_params_st *src_param,
		unsigned int num_dpp)
{
	unsigned int val;

	/* note that we don't divide by num_dpp here because we have an interface which has already split
	 * any viewports
	 */
	if (src_param->source_scan == dm_horz) {
		val = src_param->viewport_width;
	} else {
		val = src_param->viewport_height;
	}

	return val;
}

static void get_swath_height(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_pipe_source_params_st *src_param,
		struct _vcs_dpi_wm_calc_pipe_params_st *plane,
		unsigned int swath_width_y)
{
	double buffer_width;

	if (src_param->source_format == dm_444_64 || src_param->source_format == dm_444_32
			|| src_param->source_format == dm_444_16) {
		if (src_param->sw_mode == dm_sw_linear) {
			plane->swath_height_y = 1;
		} else if (src_param->source_format == dm_444_64) {
			plane->swath_height_y = 4;
		} else {
			plane->swath_height_y = 8;
		}

		if (src_param->source_scan != dm_horz) {
			plane->swath_height_y = 256 / (unsigned int) plane->bytes_per_pixel_y
					/ plane->swath_height_y;
		}

		plane->swath_height_c = 0;

	} else {
		if (src_param->sw_mode == dm_sw_linear) {
			plane->swath_height_y = 1;
			plane->swath_height_c = 1;
		} else if (src_param->source_format == dm_420_8) {
			plane->swath_height_y = 16;
			plane->swath_height_c = 8;
		} else {
			plane->swath_height_y = 8;
			plane->swath_height_c = 8;
		}

		if (src_param->source_scan != dm_horz) {
			double bytes_per_pixel_c_ceil;

			plane->swath_height_y = 256 / dml_ceil(plane->bytes_per_pixel_y)
					/ plane->swath_height_y;

			bytes_per_pixel_c_ceil = dml_ceil_2(plane->bytes_per_pixel_c);

			plane->swath_height_c = 256 / bytes_per_pixel_c_ceil
					/ plane->swath_height_c;
		}
	}

	/* use swath height min if buffer isn't big enough */

	buffer_width = ((double) mode_lib->ip.det_buffer_size_kbytes * 1024.0 / 2.0)
			/ (plane->bytes_per_pixel_y * (double) plane->swath_height_y
					+ (plane->bytes_per_pixel_c / 2.0
							* (double) plane->swath_height_c));

	if ((double) swath_width_y <= buffer_width) {
		/* do nothing, just keep code structure from Gabes vba */
	} else {
		/* substitute swath height with swath height min */
		if (src_param->source_format == dm_444_64 || src_param->source_format == dm_444_32
				|| src_param->source_format == dm_444_16) {
			if ((src_param->sw_mode == dm_sw_linear)
					|| (src_param->source_format == dm_444_64
							&& (src_param->sw_mode == dm_sw_4kb_s
									|| src_param->sw_mode
											== dm_sw_4kb_s_x
									|| src_param->sw_mode
											== dm_sw_64kb_s
									|| src_param->sw_mode
											== dm_sw_64kb_s_t
									|| src_param->sw_mode
											== dm_sw_64kb_s_x
									|| src_param->sw_mode
											== dm_sw_var_s
									|| src_param->sw_mode
											== dm_sw_var_s_x)
							&& src_param->source_scan == dm_horz)) {
				/* do nothing, just keep code structure from Gabes vba */
			} else {
				plane->swath_height_y = plane->swath_height_y / 2;
			}
		} else {
			if (src_param->sw_mode == dm_sw_linear) {
				/* do nothing, just keep code structure from Gabes vba */
			} else if (src_param->source_format == dm_420_8
					&& src_param->source_scan == dm_horz) {
				plane->swath_height_y = plane->swath_height_y / 2;
			} else if (src_param->source_format == dm_420_10
					&& src_param->source_scan == dm_horz) {
				plane->swath_height_c = plane->swath_height_c / 2;
			}
		}
	}

	if (plane->swath_height_c == 0) {
		plane->det_buffer_size_y = mode_lib->ip.det_buffer_size_kbytes * 1024.0;
	} else if (plane->swath_height_c <= plane->swath_height_y) {
		plane->det_buffer_size_y = mode_lib->ip.det_buffer_size_kbytes * 1024.0 / 2.0;
	} else {
		plane->det_buffer_size_y = mode_lib->ip.det_buffer_size_kbytes * 1024.0 * 2.0 / 3.0;
	}
}

static void calc_display_pipe_line_delivery_time(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes)
{
	unsigned int i;

	for (i = 0; i < num_planes; i++) {
		if (planes[i].v_ratio <= 1.0) {
			planes[i].display_pipe_line_delivery_time = planes[i].swath_width_y
					* planes[i].num_dpp / planes[i].h_ratio
					/ planes[i].pixclk_mhz;
		} else {
			double dchub_pscl_bw_per_clk;

			if (planes[i].h_ratio > 1) {
				double num_hscl_kernels;

				num_hscl_kernels = dml_ceil((double) planes[i].h_taps / 6);
				dchub_pscl_bw_per_clk =
						dml_min(
								(double) mode_lib->ip.max_dchub_pscl_bw_pix_per_clk,
								mode_lib->ip.max_pscl_lb_bw_pix_per_clk
										* planes[i].h_ratio
										/ num_hscl_kernels);
			} else {
				dchub_pscl_bw_per_clk =
						dml_min(
								(double) mode_lib->ip.max_dchub_pscl_bw_pix_per_clk,
								(double) mode_lib->ip.max_pscl_lb_bw_pix_per_clk);
			}

			planes[i].display_pipe_line_delivery_time = planes[i].swath_width_y
					/ dchub_pscl_bw_per_clk / planes[i].dppclk_mhz;
		}
	}
}

static double calc_total_data_read_bw(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes)
{
	double val = 0.0;
	unsigned int i;

	for (i = 0; i < num_planes; i++) {
		double swath_width_y_plane = planes[i].swath_width_y * planes[i].num_dpp;

		planes[i].read_bw = swath_width_y_plane
				* (dml_ceil(planes[i].bytes_per_pixel_y)
						+ dml_ceil_2(planes[i].bytes_per_pixel_c) / 2)
				/ (planes[i].h_total / planes[i].pixclk_mhz) * planes[i].v_ratio;

		val += planes[i].read_bw;

		DTRACE("plane[%d] start", i);
		DTRACE("read_bw = %f", planes[i].read_bw);
		DTRACE("plane[%d] end", i);
	}

	return val;
}

double dml_wm_calc_total_data_read_bw(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes)
{
	return calc_total_data_read_bw(mode_lib, planes, num_planes);
}

static double calc_dcfclk_mhz(
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes)
{
	double dcfclk_mhz = -1.0;
	unsigned int i;

	for (i = 0; i < num_planes; i++) {
		/* voltage and dcfclk must be the same for all pipes */
		ASSERT(dcfclk_mhz == -1.0 || dcfclk_mhz == planes[i].dcfclk_mhz);
		dcfclk_mhz = planes[i].dcfclk_mhz;
	}

	return dcfclk_mhz;
}

static enum voltage_state find_voltage(
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes)
{
	int voltage = -1;
	unsigned int i;

	for (i = 0; i < num_planes; i++) {
		ASSERT(voltage == -1 || voltage == planes[i].voltage);
		voltage = planes[i].voltage;
	}

	return (enum voltage_state) voltage;
}

static bool find_dcc_enable(struct _vcs_dpi_wm_calc_pipe_params_st *planes, unsigned int num_planes)
{
	unsigned int i;

	for (i = 0; i < num_planes; i++) {
		if (planes[i].dcc_enable) {
			return true;
		}
	}

	return false;
}

static double calc_return_bw(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes)
{
	struct _vcs_dpi_soc_bounding_box_st *soc;
	double return_bw_mbps;
	double dcfclk_mhz;
	double return_bus_bw;
	enum voltage_state voltage;
	double return_bw_to_dcn;
	bool dcc_enable;
	double rob_chunk_diff;
	double urgent_latency_traffic;
	double critical_compression;
	struct _vcs_dpi_voltage_scaling_st state;

	soc = &mode_lib->soc;

	dcfclk_mhz = calc_dcfclk_mhz(planes, num_planes);
	return_bus_bw = dcfclk_mhz * soc->return_bus_width_bytes;

	DTRACE("INTERMEDIATE dcfclk_mhz        = %f", dcfclk_mhz);
	DTRACE("INTERMEDIATE return_bus_bw        = %f", return_bus_bw);

	voltage = find_voltage(planes, num_planes);
	return_bw_to_dcn = dml_socbb_return_bw_mhz(soc, voltage);

	dcc_enable = find_dcc_enable(planes, num_planes);

	return_bw_mbps = return_bw_to_dcn;
	DTRACE("INTERMEDIATE return_bw_mbps        = %f", return_bw_mbps);

	rob_chunk_diff =
			(mode_lib->ip.rob_buffer_size_kbytes - mode_lib->ip.pixel_chunk_size_kbytes)
					* 1024.0;
	DTRACE("INTERMEDIATE rob_chunk_diff        = %f", rob_chunk_diff);

	if (dcc_enable && return_bw_to_dcn > return_bus_bw / 4) {
		double dcc_return_bw =
				return_bw_to_dcn * 4.0
						* (1.0
								- soc->urgent_latency_us
										/ (rob_chunk_diff
												/ (return_bw_to_dcn
														- return_bus_bw
																/ 4.0)
												+ soc->urgent_latency_us));
		return_bw_mbps = dml_min(return_bw_mbps, dcc_return_bw);
		DTRACE("INTERMEDIATE dcc_return_bw        = %f", dcc_return_bw);
	}

	urgent_latency_traffic = return_bus_bw * soc->urgent_latency_us;
	DTRACE("INTERMEDIATE urgent_latency_traffic        = %f", urgent_latency_traffic);
	critical_compression = 2.0 * urgent_latency_traffic
			/ (return_bw_to_dcn * soc->urgent_latency_us + rob_chunk_diff);
	DTRACE("INTERMEDIATE critical_compression        = %f", critical_compression);

	if (dcc_enable && critical_compression > 1.0 && critical_compression < 4.0) {
		double crit_return_bw = (4 * return_bw_to_dcn * rob_chunk_diff
				* urgent_latency_traffic);
		crit_return_bw = crit_return_bw
				/ dml_pow(
						return_bw_to_dcn * soc->urgent_latency_us
								+ rob_chunk_diff,
						2);
		DTRACE("INTERMEDIATE critical_return_bw        = %f", crit_return_bw);
		return_bw_mbps = dml_min(return_bw_mbps, crit_return_bw);
	}

	/* Gabe does this again for some reason using the value of return_bw_mpbs from the previous calculation
	 * and a lightly different return_bw_to_dcn
	 */

	state = dml_socbb_voltage_scaling(soc, voltage);
	return_bw_to_dcn = dml_min(
			soc->return_bus_width_bytes * dcfclk_mhz,
			state.dram_bw_per_chan_gbps * 1000.0 * (double) soc->num_chans);

	DTRACE("INTERMEDIATE rob_chunk_diff        = %f", rob_chunk_diff);

	if (dcc_enable && return_bw_to_dcn > return_bus_bw / 4) {
		double dcc_return_bw =
				return_bw_to_dcn * 4.0
						* (1.0
								- soc->urgent_latency_us
										/ (rob_chunk_diff
												/ (return_bw_to_dcn
														- return_bus_bw
																/ 4.0)
												+ soc->urgent_latency_us));
		return_bw_mbps = dml_min(return_bw_mbps, dcc_return_bw);
		DTRACE("INTERMEDIATE dcc_return_bw        = %f", dcc_return_bw);
	}

	urgent_latency_traffic = return_bus_bw * soc->urgent_latency_us;
	DTRACE("INTERMEDIATE urgent_latency_traffic        = %f", urgent_latency_traffic);
	critical_compression = 2.0 * urgent_latency_traffic
			/ (return_bw_to_dcn * soc->urgent_latency_us + rob_chunk_diff);
	DTRACE("INTERMEDIATE critical_compression        = %f", critical_compression);

	/* problem here? */
	if (dcc_enable && critical_compression > 1.0 && critical_compression < 4.0) {
		double crit_return_bw = (4 * return_bw_to_dcn * rob_chunk_diff
				* urgent_latency_traffic);
		crit_return_bw = crit_return_bw
				/ dml_pow(
						return_bw_to_dcn * soc->urgent_latency_us
								+ rob_chunk_diff,
						2);
		DTRACE("INTERMEDIATE critical_return_bw       = %f", crit_return_bw);
		DTRACE("INTERMEDIATE return_bw_to_dcn         = %f", return_bw_to_dcn);
		DTRACE("INTERMEDIATE rob_chunk_diff           = %f", rob_chunk_diff);
		DTRACE("INTERMEDIATE urgent_latency_traffic   = %f", urgent_latency_traffic);

		return_bw_mbps = dml_min(return_bw_mbps, crit_return_bw);
	}

	DTRACE("INTERMEDIATE final return_bw_mbps        = %f", return_bw_mbps);
	return return_bw_mbps;
}

double dml_wm_calc_return_bw(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes)
{
	return calc_return_bw(mode_lib, planes, num_planes);
}

static double calc_last_pixel_of_line_extra_wm_us(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes)
{
	double val = 0.0;
	double total_data_read_bw = calc_total_data_read_bw(mode_lib, planes, num_planes);
	int voltage = -1;
	unsigned int i;
	double return_bw_mbps;

	for (i = 0; i < num_planes; i++) {
		/* voltage mode must be the same for all pipes */
		ASSERT(voltage == -1 || voltage == planes[i].voltage);
		voltage = planes[i].voltage;
	}
	return_bw_mbps = calc_return_bw(mode_lib, planes, num_planes);

	for (i = 0; i < num_planes; i++) {
		double bytes_pp_y = dml_ceil(planes[i].bytes_per_pixel_y);
		double bytes_pp_c = dml_ceil_2(planes[i].bytes_per_pixel_c);
		double swath_bytes_y = (double) planes[i].swath_width_y
				* (double) planes[i].swath_height_y * (double) bytes_pp_y;
		double swath_bytes_c = ((double) planes[i].swath_width_y / 2.0)
				* (double) planes[i].swath_height_c * (double) bytes_pp_c;
		double data_fabric_line_delivery_time = (swath_bytes_y + swath_bytes_c)
				/ (return_bw_mbps * planes[i].read_bw / (double) planes[i].num_dpp
						/ total_data_read_bw);

		DTRACE(
				"bytes_pp_y = %f, swath_width_y = %f, swath_height_y = %f, swath_bytes_y = %f",
				bytes_pp_y,
				(double) planes[i].swath_width_y,
				(double) planes[i].swath_height_y,
				swath_bytes_y);
		DTRACE(
				"bytes_pp_c = %f, swath_width_c = %f, swath_height_c = %f, swath_bytes_c = %f",
				bytes_pp_c,
				((double) planes[i].swath_width_y / 2.0),
				(double) planes[i].swath_height_c,
				swath_bytes_c);
		DTRACE(
				"return_bw_mbps = %f, read_bw = %f, num_dpp = %d, total_data_read_bw = %f",
				return_bw_mbps,
				planes[i].read_bw,
				planes[i].num_dpp,
				total_data_read_bw);
		DTRACE("data_fabric_line_delivery_time  = %f", data_fabric_line_delivery_time);
		DTRACE(
				"display_pipe_line_delivery_time = %f",
				planes[i].display_pipe_line_delivery_time);

		val = dml_max(
				val,
				data_fabric_line_delivery_time
						- planes[i].display_pipe_line_delivery_time);
	}

	DTRACE("last_pixel_of_line_extra_wm is %f us", val);
	return val;
}

static bool calc_pte_enable(struct _vcs_dpi_wm_calc_pipe_params_st *planes, unsigned int num_planes)
{
	unsigned int i;

	for (i = 0; i < num_planes; i++) {
		if (planes[i].pte_enable) {
			return true;
		}
	}

	return false;
}

static void calc_lines_in_det_y(struct _vcs_dpi_wm_calc_pipe_params_st *plane)
{
	plane->lines_in_det_y = plane->det_buffer_size_y / plane->bytes_per_pixel_y
			/ plane->swath_width_y;
	plane->lines_in_det_y_rounded_down_to_swath = dml_floor(
			(double) plane->lines_in_det_y / plane->swath_height_y)
			* plane->swath_height_y;
	plane->full_det_buffering_time = plane->lines_in_det_y_rounded_down_to_swath
			* (plane->h_total / plane->pixclk_mhz);
}

/* CHECKME: not obviously 1:1 with calculation described in architectural
 * document or spreadsheet */
static void calc_dcfclk_deepsleep_mhz_per_plane(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *plane)
{
	double bus_width_per_pixel;

	if (plane->swath_height_c == 0) {
		bus_width_per_pixel = dml_ceil(plane->bytes_per_pixel_y) / 64;
	} else {
		double bus_width_per_pixel_c;

		bus_width_per_pixel = dml_ceil(plane->bytes_per_pixel_y) / 32;
		bus_width_per_pixel_c = dml_ceil(plane->bytes_per_pixel_c) / 32;
		if (bus_width_per_pixel < bus_width_per_pixel_c)
			bus_width_per_pixel = bus_width_per_pixel_c;
	}

	if (plane->v_ratio <= 1) {
		plane->dcfclk_deepsleep_mhz_per_plane = 1.1 * plane->pixclk_mhz / plane->num_dpp
				* plane->h_ratio * bus_width_per_pixel;
	} else if (plane->h_ratio > 1) {
		double num_hscl_kernels = dml_ceil((double) plane->h_taps / 6);
		double dchub_pscl_bw_per_clk = dml_min(
				(double) mode_lib->ip.max_dchub_pscl_bw_pix_per_clk,
				mode_lib->ip.max_pscl_lb_bw_pix_per_clk * plane->h_ratio
						/ num_hscl_kernels);

		plane->dcfclk_deepsleep_mhz_per_plane = 1.1 * plane->dppclk_mhz
				* dchub_pscl_bw_per_clk * bus_width_per_pixel;
	} else {
		double dchub_pscl_bw_per_clk = dml_min(
				(double) mode_lib->ip.max_dchub_pscl_bw_pix_per_clk,
				(double) mode_lib->ip.max_pscl_lb_bw_pix_per_clk);

		plane->dcfclk_deepsleep_mhz_per_plane = 1.1 * plane->dppclk_mhz
				* dchub_pscl_bw_per_clk * bus_width_per_pixel;
	}

	plane->dcfclk_deepsleep_mhz_per_plane = dml_max(
			plane->dcfclk_deepsleep_mhz_per_plane,
			plane->pixclk_mhz / 16);
}

/* Implementation of expected stutter efficiency from DCN1_Display_Mode.docx */
double dml_wm_expected_stutter_eff_e2e(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes)
{
	double min_full_det_buffering_time_us;
	double frame_time_for_min_full_det_buffering_time_us = 0.0;
	struct _vcs_dpi_wm_calc_pipe_params_st *planes = mode_lib->wm_param;
	unsigned int num_planes;
	unsigned int i;
	double total_data_read_bw_mbps;
	double average_read_bw_gbps;
	double min_full_det_buffer_size_bytes;
	double rob_fill_size_bytes;
	double part_of_burst_that_fits_in_rob;
	int voltage;
	double dcfclk_mhz;
	unsigned int total_writeback;
	double return_bw_mbps;
	double stutter_burst_time_us;
	double stutter_eff_not_including_vblank;
	double smallest_vblank_us;
	double stutter_eff;

	memset(mode_lib->wm_param, 0, sizeof(mode_lib->wm_param));
	DTRACE("calculating expected stutter efficiency");

	num_planes = dml_wm_e2e_to_wm(mode_lib, e2e, num_pipes, planes);

	for (i = 0; i < num_planes; i++) {
		calc_lines_in_det_y(&planes[i]);

		DTRACE("swath width y plane                   %d = %d", i, planes[i].swath_width_y);
		DTRACE("swath height y plane                  %d = %d", i, planes[i].swath_height_y);
		DTRACE(
				"bytes per pixel det y plane           %d = %f",
				i,
				planes[i].bytes_per_pixel_y);
		DTRACE(
				"bytes per pixel det c plane           %d = %f",
				i,
				planes[i].bytes_per_pixel_c);
		DTRACE(
				"det buffer size plane                 %d = %d",
				i,
				planes[i].det_buffer_size_y);
		DTRACE("lines in det plane                    %d = %d", i, planes[i].lines_in_det_y);
		DTRACE(
				"lines in det rounded to swaths plane  %d = %d",
				i,
				planes[i].lines_in_det_y_rounded_down_to_swath);
	}

	min_full_det_buffering_time_us = 9999.0;
	for (i = 0; i < num_planes; i++) {
		if (planes[i].full_det_buffering_time < min_full_det_buffering_time_us) {
			min_full_det_buffering_time_us = planes[i].full_det_buffering_time;
			frame_time_for_min_full_det_buffering_time_us = (double) planes[i].v_total
					* planes[i].h_total / planes[i].pixclk_mhz;
		}
	}

	DTRACE("INTERMEDIATE: min_full_det_buffering_time_us = %f", min_full_det_buffering_time_us);

	total_data_read_bw_mbps = calc_total_data_read_bw(mode_lib, planes, num_planes);

	average_read_bw_gbps = 0.0;

	for (i = 0; i < num_planes; i++) {
		if (planes[i].dcc_enable) {
			average_read_bw_gbps += planes[i].read_bw / planes[i].dcc_rate / 1000;
		} else {
			average_read_bw_gbps += planes[i].read_bw / 1000;
		}

		if (planes[i].dcc_enable) {
			average_read_bw_gbps += planes[i].read_bw / 1000 / 256;
		}

		if (planes[i].pte_enable) {
			average_read_bw_gbps += planes[i].read_bw / 1000 / 512;
		}
	}

	min_full_det_buffer_size_bytes = min_full_det_buffering_time_us * total_data_read_bw_mbps;
	rob_fill_size_bytes = mode_lib->ip.rob_buffer_size_kbytes * 1024 * total_data_read_bw_mbps
			/ (average_read_bw_gbps * 1000);
	part_of_burst_that_fits_in_rob = dml_min(
			min_full_det_buffer_size_bytes,
			rob_fill_size_bytes);

	voltage = -1;
	dcfclk_mhz = -1.0;
	total_writeback = 0;

	for (i = 0; i < num_pipes; i++) {
		/* voltage and dcfclk must be the same for all pipes */
		ASSERT(voltage == -1 || voltage == e2e[i].clks_cfg.voltage);
		voltage = e2e[i].clks_cfg.voltage;
		ASSERT(dcfclk_mhz == -1.0 || dcfclk_mhz == e2e[i].clks_cfg.dcfclk_mhz);
		dcfclk_mhz = e2e[i].clks_cfg.dcfclk_mhz;

		if (e2e[i].dout.output_type == dm_wb)
			total_writeback++;
	}

	return_bw_mbps = calc_return_bw(mode_lib, planes, num_planes);

	DTRACE("INTERMEDIATE: part_of_burst_that_fits_in_rob = %f", part_of_burst_that_fits_in_rob);
	DTRACE("INTERMEDIATE: average_read_bw_gbps = %f", average_read_bw_gbps);
	DTRACE("INTERMEDIATE: total_data_read_bw_mbps = %f", total_data_read_bw_mbps);
	DTRACE("INTERMEDIATE: return_bw_mbps = %f", return_bw_mbps);

	stutter_burst_time_us = part_of_burst_that_fits_in_rob * (average_read_bw_gbps * 1000)
			/ total_data_read_bw_mbps / return_bw_mbps
			+ (min_full_det_buffering_time_us * total_data_read_bw_mbps
					- part_of_burst_that_fits_in_rob) / (dcfclk_mhz * 64);
	DTRACE("INTERMEDIATE: stutter_burst_time_us = %f", stutter_burst_time_us);

	if (total_writeback == 0) {
		stutter_eff_not_including_vblank = (1.0
				- ((mode_lib->soc.sr_exit_time_us + stutter_burst_time_us)
						/ min_full_det_buffering_time_us)) * 100.0;
	} else {
		stutter_eff_not_including_vblank = 0.0;
	}

	DTRACE("stutter_efficiency_not_including_vblank = %f", stutter_eff_not_including_vblank);

	smallest_vblank_us = 9999.0;

	for (i = 0; i < num_pipes; i++) {
		double vblank_us;
		if (e2e[i].pipe.dest.syncronized_vblank_all_planes != 0 || num_pipes == 1) {
			vblank_us = (double) (e2e[i].pipe.dest.vtotal + 1
					- e2e[i].pipe.dest.vblank_start
					+ e2e[i].pipe.dest.vblank_end * e2e[i].pipe.dest.htotal)
					/ e2e[i].pipe.dest.pixel_rate_mhz;
		} else {
			vblank_us = 0.0;
		}

		smallest_vblank_us = dml_min(smallest_vblank_us, vblank_us);
	}

	DTRACE("smallest vblank = %f us", smallest_vblank_us);

	stutter_eff = 100.0
			* (((stutter_eff_not_including_vblank / 100.0)
					* (frame_time_for_min_full_det_buffering_time_us
							- smallest_vblank_us) + smallest_vblank_us)
					/ frame_time_for_min_full_det_buffering_time_us);

	DTRACE("stutter_efficiency = %f", stutter_eff);

	return stutter_eff_not_including_vblank;
}

double dml_wm_expected_stutter_eff_e2e_with_vblank(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes)
{
	double min_full_det_buffering_time_us;
	double frame_time_for_min_full_det_buffering_time_us = 0.0;
	struct _vcs_dpi_wm_calc_pipe_params_st *planes = mode_lib->wm_param;
	unsigned int num_planes;
	unsigned int i;
	double total_data_read_bw_mbps;
	double average_read_bw_gbps;
	double min_full_det_buffer_size_bytes;
	double rob_fill_size_bytes;
	double part_of_burst_that_fits_in_rob;
	int voltage;
	double dcfclk_mhz;
	unsigned int total_writeback;
	double return_bw_mbps;
	double stutter_burst_time_us;
	double stutter_eff_not_including_vblank;
	double smallest_vblank_us;
	double stutter_eff;

	memset(mode_lib->wm_param, 0, sizeof(mode_lib->wm_param));
	num_planes = dml_wm_e2e_to_wm(mode_lib, e2e, num_pipes, planes);

	for (i = 0; i < num_planes; i++) {
		calc_lines_in_det_y(&planes[i]);
	}

	min_full_det_buffering_time_us = 9999.0;
	for (i = 0; i < num_planes; i++) {
		if (planes[i].full_det_buffering_time < min_full_det_buffering_time_us) {
			min_full_det_buffering_time_us = planes[i].full_det_buffering_time;
			frame_time_for_min_full_det_buffering_time_us = (double) planes[i].v_total
					* planes[i].h_total / planes[i].pixclk_mhz;
		}
	}

	total_data_read_bw_mbps = calc_total_data_read_bw(mode_lib, planes, num_planes);
	average_read_bw_gbps = 0.0;

	for (i = 0; i < num_planes; i++) {
		if (planes[i].dcc_enable) {
			average_read_bw_gbps += planes[i].read_bw / planes[i].dcc_rate / 1000;
		} else {
			average_read_bw_gbps += planes[i].read_bw / 1000;
		}

		if (planes[i].dcc_enable) {
			average_read_bw_gbps += planes[i].read_bw / 1000 / 256;
		}

		if (planes[i].pte_enable) {
			average_read_bw_gbps += planes[i].read_bw / 1000 / 512;
		}
	}

	min_full_det_buffer_size_bytes = min_full_det_buffering_time_us * total_data_read_bw_mbps;
	rob_fill_size_bytes = mode_lib->ip.rob_buffer_size_kbytes * 1024 * total_data_read_bw_mbps
			/ (average_read_bw_gbps * 1000);
	part_of_burst_that_fits_in_rob = dml_min(
			min_full_det_buffer_size_bytes,
			rob_fill_size_bytes);

	voltage = -1;
	dcfclk_mhz = -1.0;
	total_writeback = 0;

	for (i = 0; i < num_pipes; i++) {
		/* voltage and dcfclk must be the same for all pipes */
		ASSERT(voltage == -1 || voltage == e2e[i].clks_cfg.voltage);
		voltage = e2e[i].clks_cfg.voltage;
		ASSERT(dcfclk_mhz == -1.0 || dcfclk_mhz == e2e[i].clks_cfg.dcfclk_mhz);
		dcfclk_mhz = e2e[i].clks_cfg.dcfclk_mhz;

		if (e2e[i].dout.output_type == dm_wb)
			total_writeback++;
	}

	return_bw_mbps = calc_return_bw(mode_lib, planes, num_planes);

	stutter_burst_time_us = part_of_burst_that_fits_in_rob * (average_read_bw_gbps * 1000)
			/ total_data_read_bw_mbps / return_bw_mbps
			+ (min_full_det_buffering_time_us * total_data_read_bw_mbps
					- part_of_burst_that_fits_in_rob) / (dcfclk_mhz * 64);

	if (total_writeback == 0) {
		stutter_eff_not_including_vblank = (1.0
				- ((mode_lib->soc.sr_exit_time_us + stutter_burst_time_us)
						/ min_full_det_buffering_time_us)) * 100.0;
	} else {
		stutter_eff_not_including_vblank = 0.0;
	}

	smallest_vblank_us = 9999.0;

	for (i = 0; i < num_pipes; i++) {
		double vblank_us;
		if (e2e[i].pipe.dest.syncronized_vblank_all_planes != 0 || num_pipes == 1) {
			vblank_us = (double) (e2e[i].pipe.dest.vtotal + 1
					- e2e[i].pipe.dest.vblank_start
					+ e2e[i].pipe.dest.vblank_end * e2e[i].pipe.dest.htotal)
					/ e2e[i].pipe.dest.pixel_rate_mhz;
		} else {
			vblank_us = 0.0;
		}

		smallest_vblank_us = dml_min(smallest_vblank_us, vblank_us);
	}

	stutter_eff = 100.0
			* (((stutter_eff_not_including_vblank / 100.0)
					* (frame_time_for_min_full_det_buffering_time_us
							- smallest_vblank_us) + smallest_vblank_us)
					/ frame_time_for_min_full_det_buffering_time_us);


	return stutter_eff;
}

double urgent_extra_calc(
		struct display_mode_lib *mode_lib,
		double dcfclk_mhz,
		double return_bw_mbps,
		unsigned int total_active_dpp,
		unsigned int total_dcc_active_dpp)
{
	double urgent_extra_latency_us = 0.0;
	double urgent_round_trip_ooo_latency_us;

	urgent_round_trip_ooo_latency_us =
			(((double) mode_lib->soc.round_trip_ping_latency_dcfclk_cycles + 32)
					/ dcfclk_mhz)
					+ (((double) (mode_lib->soc.urgent_out_of_order_return_per_channel_bytes
							* mode_lib->soc.num_chans)) / return_bw_mbps);

	DTRACE(
			"INTERMEDIATE round_trip_ping_latency_dcfclk_cycles        = %d",
			mode_lib->soc.round_trip_ping_latency_dcfclk_cycles);
	DTRACE("INTERMEDIATE dcfclk_mhz                                   = %f", dcfclk_mhz);
	DTRACE(
			"INTERMEDIATE urgent_out_of_order_return_per_channel_bytes = %d",
			mode_lib->soc.urgent_out_of_order_return_per_channel_bytes);

	urgent_extra_latency_us = urgent_round_trip_ooo_latency_us
			+ ((double) total_active_dpp * mode_lib->ip.pixel_chunk_size_kbytes
					+ (double) total_dcc_active_dpp
							* mode_lib->ip.meta_chunk_size_kbytes)
					* 1024.0 / return_bw_mbps; /* to us */

	DTRACE(
			"INTERMEDIATE urgent_round_trip_ooo_latency_us  = %f",
			urgent_round_trip_ooo_latency_us);
	DTRACE("INTERMEDIATE total_active_dpp                  = %d", total_active_dpp);
	DTRACE(
			"INTERMEDIATE pixel_chunk_size_kbytes           = %d",
			mode_lib->ip.pixel_chunk_size_kbytes);
	DTRACE("INTERMEDIATE total_dcc_active_dpp              = %d", total_dcc_active_dpp);
	DTRACE(
			"INTERMEDIATE meta_chunk_size_kbyte             = %d",
			mode_lib->ip.meta_chunk_size_kbytes);
	DTRACE("INTERMEDIATE return_bw_mbps                    = %f", return_bw_mbps);

	return urgent_extra_latency_us;
}

double dml_wm_urgent_extra_max(struct display_mode_lib *mode_lib)
{
	unsigned int total_active_dpp = DC__NUM_DPP;
	unsigned int total_dcc_active_dpp = total_active_dpp;
	double urgent_extra_latency_us = 0.0;
	double dcfclk_mhz = 0.0;
	double return_bw_mbps = 0.0;
	int voltage = dm_vmin;

	/* use minimum voltage */
	return_bw_mbps = dml_socbb_return_bw_mhz(&mode_lib->soc, (enum voltage_state) voltage);
	/* use minimum dcfclk */
	dcfclk_mhz = mode_lib->soc.vmin.dcfclk_mhz;
	/* use max dpps and dpps with dcc */

	urgent_extra_latency_us = urgent_extra_calc(
			mode_lib,
			dcfclk_mhz,
			return_bw_mbps,
			total_active_dpp,
			total_dcc_active_dpp);

	DTRACE("urgent extra max = %f", urgent_extra_latency_us);
	return urgent_extra_latency_us;
}

double dml_wm_urgent_extra(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	unsigned int total_active_dpp = 0;
	unsigned int total_dcc_active_dpp = 0;
	double urgent_extra_latency_us = 0.0;
	double dcfclk_mhz = 0.0;
	double return_bw_mbps = 0.0;
	int voltage = -1;
	bool pte_enable = false;
	unsigned int i;

	for (i = 0; i < num_pipes; i++) {
		/* num_dpp must be greater than 0 */
		ASSERT(pipes[i].num_dpp > 0);

		/* voltage mode must be the same for all pipes */
		ASSERT(voltage == -1 || voltage == pipes[i].voltage);
		voltage = pipes[i].voltage;

		/* dcfclk for all pipes must be the same */
		ASSERT(dcfclk_mhz == 0.0 || dcfclk_mhz == pipes[i].dcfclk_mhz);
		dcfclk_mhz = pipes[i].dcfclk_mhz;

		total_active_dpp += pipes[i].num_dpp;

		if (pipes[i].dcc_enable) {
			total_dcc_active_dpp += pipes[i].num_dpp;
		}
	}

	DTRACE("total active dpps %d", total_active_dpp);
	DTRACE("total active dpps with dcc %d", total_dcc_active_dpp);
	DTRACE("voltage state is %d", voltage);

	return_bw_mbps = calc_return_bw(mode_lib, pipes, num_pipes);

	DTRACE("return_bandwidth is %f MBps", return_bw_mbps);

	pte_enable = calc_pte_enable(pipes, num_pipes);

	/* calculate the maximum extra latency just for comparison purposes */
	/* dml_wm_urgent_extra_max(); */
	urgent_extra_latency_us = urgent_extra_calc(
			mode_lib,
			dcfclk_mhz,
			return_bw_mbps,
			total_active_dpp,
			total_dcc_active_dpp);

	DTRACE("INTERMEDIATE urgent_extra_latency_us_before_pte = %f", urgent_extra_latency_us);

	if (pte_enable) {
		urgent_extra_latency_us += total_active_dpp * mode_lib->ip.pte_chunk_size_kbytes
				* 1024.0 / return_bw_mbps;

		DTRACE("INTERMEDIATE pte_enable = true");
		DTRACE("INTERMEDIATE total_active_dpp      = %d", total_active_dpp);
		DTRACE(
				"INTERMEDIATE pte_chunk_size_kbytes = %d",
				mode_lib->ip.pte_chunk_size_kbytes);
		DTRACE("INTERMEDIATE return_bw_mbps        = %f", return_bw_mbps);
	}

	return urgent_extra_latency_us;
}

double dml_wm_urgent_e2e(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	struct _vcs_dpi_wm_calc_pipe_params_st *wm = mode_lib->wm_param;
	unsigned int combined_pipes;
	double urgent_wm;

	memset(mode_lib->wm_param, 0, sizeof(mode_lib->wm_param));
	combined_pipes = dml_wm_e2e_to_wm(mode_lib, pipes, num_pipes, wm);

	urgent_wm = dml_wm_urgent(mode_lib, wm, combined_pipes);

	return urgent_wm;
}

double dml_wm_urgent(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes)
{
	double urgent_watermark;
	double urgent_extra_latency_us;
	double last_pixel_of_line_extra_wm_us = 0.0;

	DTRACE("calculating urgent watermark");
	calc_display_pipe_line_delivery_time(mode_lib, planes, num_planes);
	urgent_extra_latency_us = dml_wm_urgent_extra(mode_lib, planes, num_planes);

	last_pixel_of_line_extra_wm_us = calc_last_pixel_of_line_extra_wm_us(
			mode_lib,
			planes,
			num_planes);

	urgent_watermark = mode_lib->soc.urgent_latency_us + last_pixel_of_line_extra_wm_us
			+ urgent_extra_latency_us;

	DTRACE("INTERMEDIATE urgent_latency_us              = %f", mode_lib->soc.urgent_latency_us);
	DTRACE("INTERMEDIATE last_pixel_of_line_extra_wm_us = %f", last_pixel_of_line_extra_wm_us);
	DTRACE("INTERMEDIATE urgent_extra_latency_us        = %f", urgent_extra_latency_us);

	DTRACE("urgent_watermark_us = %f", urgent_watermark);
	return urgent_watermark;
}

double dml_wm_pte_meta_urgent(struct display_mode_lib *mode_lib, double urgent_wm_us)
{
	double val;

	val = urgent_wm_us + 2.0 * mode_lib->soc.urgent_latency_us;
	DTRACE("pte_meta_urgent_watermark_us = %f", val);

	return val;
}

double dml_wm_dcfclk_deepsleep_mhz_e2e(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	struct _vcs_dpi_wm_calc_pipe_params_st *planes = mode_lib->wm_param;
	unsigned int num_planes;
	double val;

	memset(mode_lib->wm_param, 0, sizeof(mode_lib->wm_param));
	num_planes = dml_wm_e2e_to_wm(mode_lib, pipes, num_pipes, planes);

	val = dml_wm_dcfclk_deepsleep_mhz(mode_lib, planes, num_planes);

	return val;
}

double dml_wm_dcfclk_deepsleep_mhz(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *planes,
		unsigned int num_planes)
{
	double val = 8.0;
	unsigned int i;

	for (i = 0; i < num_planes; i++) {
		calc_dcfclk_deepsleep_mhz_per_plane(mode_lib, &planes[i]);

		if (val < planes[i].dcfclk_deepsleep_mhz_per_plane) {
			val = planes[i].dcfclk_deepsleep_mhz_per_plane;
		}

		DTRACE("plane[%d] start", i);
		DTRACE("dcfclk_deepsleep_per_plane = %f", planes[i].dcfclk_deepsleep_mhz_per_plane);
		DTRACE("plane[%d] end", i);
	}

	DTRACE("dcfclk_deepsleep_mhz = %f", val);

	return val;
}

struct _vcs_dpi_cstate_pstate_watermarks_st dml_wm_cstate_pstate_e2e(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	struct _vcs_dpi_wm_calc_pipe_params_st *wm = mode_lib->wm_param;
	unsigned int combined_pipes;
	struct _vcs_dpi_cstate_pstate_watermarks_st cstate_pstate_wm;

	memset(mode_lib->wm_param, 0, sizeof(mode_lib->wm_param));
	combined_pipes = dml_wm_e2e_to_wm(mode_lib, pipes, num_pipes, wm);
	cstate_pstate_wm = dml_wm_cstate_pstate(mode_lib, wm, combined_pipes);


	return cstate_pstate_wm;
}

struct _vcs_dpi_cstate_pstate_watermarks_st dml_wm_cstate_pstate(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	struct _vcs_dpi_cstate_pstate_watermarks_st wm;
	double urgent_extra_latency_us;
	double urgent_watermark_us;
	double last_pixel_of_line_extra_wm_us;
	double dcfclk_deepsleep_freq;

	DTRACE("calculating cstate and pstate watermarks");
	urgent_extra_latency_us = dml_wm_urgent_extra(mode_lib, pipes, num_pipes);
	urgent_watermark_us = dml_wm_urgent(mode_lib, pipes, num_pipes);

	last_pixel_of_line_extra_wm_us = calc_last_pixel_of_line_extra_wm_us(
			mode_lib,
			pipes,
			num_pipes);
	dcfclk_deepsleep_freq = dml_wm_dcfclk_deepsleep_mhz(mode_lib, pipes, num_pipes);

	wm.cstate_exit_us = mode_lib->soc.sr_exit_time_us + last_pixel_of_line_extra_wm_us
			+ urgent_extra_latency_us
			+ mode_lib->ip.dcfclk_cstate_latency / dcfclk_deepsleep_freq;
	wm.cstate_enter_plus_exit_us = mode_lib->soc.sr_enter_plus_exit_time_us
			+ last_pixel_of_line_extra_wm_us + urgent_extra_latency_us;
	wm.pstate_change_us = mode_lib->soc.dram_clock_change_latency_us + urgent_watermark_us;

	DTRACE("stutter_exit_watermark_us = %f", wm.cstate_exit_us);
	DTRACE("stutter_enter_plus_exit_watermark_us = %f", wm.cstate_enter_plus_exit_us);
	DTRACE("dram_clock_change_watermark_us = %f", wm.pstate_change_us);

	return wm;
}

double dml_wm_writeback_pstate_e2e(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	struct _vcs_dpi_wm_calc_pipe_params_st *wm = mode_lib->wm_param;
	unsigned int combined_pipes;

	memset(mode_lib->wm_param, 0, sizeof(mode_lib->wm_param));
	combined_pipes = dml_wm_e2e_to_wm(mode_lib, pipes, num_pipes, wm);


	return dml_wm_writeback_pstate(mode_lib, wm, combined_pipes);
}

double dml_wm_writeback_pstate(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_wm_calc_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	unsigned int total_active_wb = 0;
	double wm = 0.0;
	double socclk_mhz = 0.0;
	unsigned int i;

	DTRACE("calculating wb pstate watermark");
	for (i = 0; i < num_pipes; i++) {
		if (pipes[i].output_type == dm_wb)
			total_active_wb++;
		ASSERT(socclk_mhz == 0.0 || socclk_mhz == pipes[i].socclk_mhz);
		socclk_mhz = pipes[i].socclk_mhz;
	}

	DTRACE("total wb outputs %d", total_active_wb);
	DTRACE("socclk frequency %f Mhz", socclk_mhz);

	if (total_active_wb <= 1) {
		wm = mode_lib->soc.writeback_dram_clock_change_latency_us;
	} else {
		wm = mode_lib->soc.writeback_dram_clock_change_latency_us
				+ (mode_lib->ip.writeback_chunk_size_kbytes * 1024.0) / 32.0
						/ socclk_mhz;
	}

	DTRACE("wb pstate watermark %f us", wm);
	return wm;
}

unsigned int dml_wm_e2e_to_wm(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes,
		struct _vcs_dpi_wm_calc_pipe_params_st *wm)
{
	unsigned int num_planes = 0;
	bool visited[DC__NUM_PIPES];
	unsigned int i, j;

	for (i = 0; i < num_pipes; i++) {
		visited[i] = false;
	}

	for (i = 0; i < num_pipes; i++) {
		unsigned int num_dpp = 1;

		if (visited[i]) {
			continue;
		}

		visited[i] = true;

		if (e2e[i].pipe.src.is_hsplit) {
			for (j = i + 1; j < num_pipes; j++) {
				if (e2e[j].pipe.src.is_hsplit && !visited[j]
						&& (e2e[i].pipe.src.hsplit_grp
								== e2e[j].pipe.src.hsplit_grp)) {
					num_dpp++;
					visited[j] = true;
				}
			}
		}

		wm[num_planes].num_dpp = num_dpp;
		wm[num_planes].voltage = e2e[i].clks_cfg.voltage;
		wm[num_planes].output_type = e2e[i].dout.output_type;
		wm[num_planes].dcfclk_mhz = e2e[i].clks_cfg.dcfclk_mhz;
		wm[num_planes].socclk_mhz = e2e[i].clks_cfg.socclk_mhz;
		wm[num_planes].dppclk_mhz = e2e[i].clks_cfg.dppclk_mhz;
		wm[num_planes].pixclk_mhz = e2e[i].pipe.dest.pixel_rate_mhz;

		wm[num_planes].pte_enable = e2e[i].pipe.src.vm;
		wm[num_planes].dcc_enable = e2e[i].pipe.src.dcc;
		wm[num_planes].dcc_rate = e2e[i].pipe.src.dcc_rate;

		get_bytes_per_pixel(
				(enum source_format_class) e2e[i].pipe.src.source_format,
				&wm[num_planes]);
		wm[num_planes].swath_width_y = get_swath_width_y(&e2e[i].pipe.src, num_dpp);
		get_swath_height(
				mode_lib,
				&e2e[i].pipe.src,
				&wm[num_planes],
				wm[num_planes].swath_width_y);

		wm[num_planes].interlace_en = e2e[i].pipe.dest.interlaced;
		wm[num_planes].h_ratio = e2e[i].pipe.scale_ratio_depth.hscl_ratio;
		wm[num_planes].v_ratio = e2e[i].pipe.scale_ratio_depth.vscl_ratio;
		if (wm[num_planes].interlace_en) {
			wm[num_planes].v_ratio = 2 * wm[num_planes].v_ratio;
		}
		wm[num_planes].h_taps = e2e[i].pipe.scale_taps.htaps;
		wm[num_planes].h_total = e2e[i].pipe.dest.htotal;
		wm[num_planes].v_total = e2e[i].pipe.dest.vtotal;
		wm[num_planes].v_active = e2e[i].pipe.dest.vactive;
		wm[num_planes].e2e_index = i;
		num_planes++;
	}

	for (i = 0; i < num_planes; i++) {
		DTRACE("plane[%d] start", i);
		DTRACE("voltage    = %d", wm[i].voltage);
		DTRACE("v_active   = %d", wm[i].v_active);
		DTRACE("h_total    = %d", wm[i].h_total);
		DTRACE("v_total    = %d", wm[i].v_total);
		DTRACE("pixclk_mhz = %f", wm[i].pixclk_mhz);
		DTRACE("dcfclk_mhz = %f", wm[i].dcfclk_mhz);
		DTRACE("dppclk_mhz = %f", wm[i].dppclk_mhz);
		DTRACE("h_ratio    = %f", wm[i].h_ratio);
		DTRACE("v_ratio    = %f", wm[i].v_ratio);
		DTRACE("interlaced = %d", wm[i].interlace_en);
		DTRACE("h_taps     = %d", wm[i].h_taps);
		DTRACE("num_dpp    = %d", wm[i].num_dpp);
		DTRACE("swath_width_y = %d", wm[i].swath_width_y);
		DTRACE("swath_height_y = %d", wm[i].swath_height_y);
		DTRACE("swath_height_c = %d", wm[i].swath_height_c);
		DTRACE("det_buffer_size_y = %d", wm[i].det_buffer_size_y);
		DTRACE("dcc_rate   = %f", wm[i].dcc_rate);
		DTRACE("dcc_enable = %s", wm[i].dcc_enable ? "true" : "false");
		DTRACE("pte_enable = %s", wm[i].pte_enable ? "true" : "false");
		DTRACE("plane[%d] end", i);
	}

	return num_planes;
}
