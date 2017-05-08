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

#include "display_pipe_clocks.h"
#include "display_mode_lib.h"
#include "soc_bounding_box.h"

static enum voltage_state power_state(
		struct display_mode_lib *mode_lib,
		double dispclk,
		double dppclk)
{
	enum voltage_state state1;
	enum voltage_state state2;

	if (dispclk <= mode_lib->soc.vmin.dispclk_mhz)
		state1 = dm_vmin;
	else if (dispclk <= mode_lib->soc.vnom.dispclk_mhz)
		state1 = dm_vnom;
	else if (dispclk <= mode_lib->soc.vmax.dispclk_mhz)
		state1 = dm_vmax;
	else
		state1 = dm_vmax_exceeded;

	if (dppclk <= mode_lib->soc.vmin.dppclk_mhz)
		state2 = dm_vmin;
	else if (dppclk <= mode_lib->soc.vnom.dppclk_mhz)
		state2 = dm_vnom;
	else if (dppclk <= mode_lib->soc.vmax.dppclk_mhz)
		state2 = dm_vmax;
	else
		state2 = dm_vmax_exceeded;

	if (state1 > state2)
		return state1;
	else
		return state2;
}

static unsigned int dpp_in_grp(
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes,
		unsigned int hsplit_grp)
{
	unsigned int num_dpp = 0;
	unsigned int i;

	for (i = 0; i < num_pipes; i++) {
		if (e2e[i].pipe.src.is_hsplit) {
			if (e2e[i].pipe.src.hsplit_grp == hsplit_grp) {
				num_dpp++;
			}
		}
	}

	if (0 == num_dpp)
		num_dpp = 1;

	return num_dpp;
}

static void calculate_pipe_clk_requirement(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_dpp_in_grp,
		double *dppclk,
		double *dispclk,
		bool *dppdiv)
{
	double pscl_throughput = 0.0;
	double max_hratio = e2e->pipe.scale_ratio_depth.hscl_ratio;
	double max_vratio = e2e->pipe.scale_ratio_depth.vscl_ratio;
	double max_htaps = e2e->pipe.scale_taps.htaps;
	double max_vtaps = e2e->pipe.scale_taps.vtaps;
	double dpp_clock_divider = (double) num_dpp_in_grp;
	double dispclk_dppclk_ratio;
	double dispclk_ramp_margin_percent;

	if (max_hratio > 1.0) {
		double pscl_to_lb = ((double) mode_lib->ip.max_pscl_lb_bw_pix_per_clk * max_hratio)
				/ dml_ceil(max_htaps / 6.0);
		pscl_throughput = dml_min(
				pscl_to_lb,
				(double) mode_lib->ip.max_dchub_pscl_bw_pix_per_clk);
	} else {
		pscl_throughput = dml_min(
				(double) mode_lib->ip.max_pscl_lb_bw_pix_per_clk,
				(double) mode_lib->ip.max_dchub_pscl_bw_pix_per_clk);
	}

	DTRACE("pscl_throughput: %f pix per clk", pscl_throughput);
	DTRACE("vtaps: %f hratio: %f vratio: %f", max_vtaps, max_hratio, max_vratio);
	*dppclk = dml_max(
			max_vtaps / 6.0 * dml_min(1.0, max_hratio),
			max_hratio * max_vratio / pscl_throughput);
	DTRACE("pixel rate multiplier: %f", *dppclk);
	*dppclk = dml_max(*dppclk, 1.0);
	DTRACE("pixel rate multiplier clamped: %f", *dppclk);
	*dppclk = *dppclk * e2e->pipe.dest.pixel_rate_mhz;

	*dppclk = *dppclk / dpp_clock_divider;
	DTRACE("dppclk after split: %f", *dppclk);

	if (dpp_clock_divider > 1.0 && (*dppclk < e2e->pipe.dest.pixel_rate_mhz)) {
		dispclk_dppclk_ratio = 2.0;
		*dppdiv = true;
	} else {
		dispclk_dppclk_ratio = 1.0;
		*dppdiv = false;
	}

	dispclk_ramp_margin_percent = mode_lib->ip.dispclk_ramp_margin_percent;

	/* Comment this out because of Gabes possible bug in spreadsheet,
	 * just to make other cases evident during debug
	 *
	 *if(e2e->clks_cfg.voltage == dm_vmax)
	 *    dispclk_ramp_margin_percent = 0.0;
	 */

	/* account for ramping margin and downspread */
	*dispclk = dml_max(*dppclk * dispclk_dppclk_ratio, e2e->pipe.dest.pixel_rate_mhz)
			* (1.0 + (double) mode_lib->soc.downspread_percent / 100.0)
			* (1.0 + (double) dispclk_ramp_margin_percent / 100.0);

	return;
}

bool dml_clks_pipe_clock_requirement_fit_power_constraint(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_dpp_in_grp)
{
	double dppclk = 0;
	double dispclk = 0;
	bool dppdiv = 0;

	calculate_pipe_clk_requirement(mode_lib, e2e, num_dpp_in_grp, &dppclk, &dispclk, &dppdiv);

	if (power_state(mode_lib, dispclk, dppclk) > e2e->clks_cfg.voltage) {
		return false;
	}

	return true;
}

static void get_plane_clks(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes,
		double *dppclks,
		double *dispclks,
		bool *dppdiv)
{
	/* it is assumed that the scale ratios passed into the e2e pipe params have already been calculated
	 * for any split pipe configurations, where extra pixels inthe overlap region do not contribute to
	 * the scale ratio. This means that we can simply calculate the dppclk for each dpp independently
	 * and we would expect the same result on any split pipes, which would be handled
	 */
	unsigned int i;

	for (i = 0; i < num_pipes; i++) {
		double num_dpp_in_grp;
		double dispclk_ramp_margin_percent;
		double dispclk_margined;

		if (e2e[i].pipe.src.is_hsplit)
			num_dpp_in_grp = (double) dpp_in_grp(
					e2e,
					num_pipes,
					e2e[i].pipe.src.hsplit_grp);
		else
			num_dpp_in_grp = 1;

		calculate_pipe_clk_requirement(
				mode_lib,
				&e2e[i],
				num_dpp_in_grp,
				&dppclks[i],
				&dispclks[i],
				&dppdiv[i]);

		dispclk_ramp_margin_percent = mode_lib->ip.dispclk_ramp_margin_percent;

		dispclk_margined = e2e[i].pipe.dest.pixel_rate_mhz
				* (1.0 + (double) mode_lib->soc.downspread_percent / 100.0)
				* (1.0 + (double) dispclk_ramp_margin_percent / 100.0);

		DTRACE("p%d: requested power state: %d", i, (int) e2e[0].clks_cfg.voltage);

		if (power_state(mode_lib, dispclks[i], dppclks[i])
				> power_state(mode_lib, dispclk_margined, dispclk_margined)
				&& dispclk_margined > dppclks[i]) {
			if (power_state(mode_lib, dispclks[i], dppclks[i])
					> e2e[0].clks_cfg.voltage) {
				dispclks[i] = dispclk_margined;
				dppclks[i] = dispclk_margined;
				dppdiv[i] = false;
			}
		}

		DTRACE("p%d: dispclk: %f", i, dispclks[i]);
	}
}

static void get_dcfclk(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes,
		double *dcfclk_mhz)
{
	double bytes_per_pixel_det_y[DC__NUM_PIPES__MAX];
	double bytes_per_pixel_det_c[DC__NUM_PIPES__MAX];
	double swath_width_y[DC__NUM_PIPES__MAX];
	unsigned int i;
	double total_read_bandwidth_gbps = 0.0;

	for (i = 0; i < num_pipes; i++) {
		if (e2e[i].pipe.src.source_scan == dm_horz) {
			swath_width_y[i] = e2e[i].pipe.src.viewport_width * 1.0;
		} else {
			swath_width_y[i] = e2e[i].pipe.src.viewport_height * 1.0;
		}

		switch (e2e[i].pipe.src.source_format) {
		case dm_444_64:
			bytes_per_pixel_det_y[i] = 8.0;
			bytes_per_pixel_det_c[i] = 0.0;
			break;
		case dm_444_32:
			bytes_per_pixel_det_y[i] = 4.0;
			bytes_per_pixel_det_c[i] = 0.0;
			break;
		case dm_444_16:
			bytes_per_pixel_det_y[i] = 2.0;
			bytes_per_pixel_det_c[i] = 0.0;
			break;
		case dm_422_8:
			bytes_per_pixel_det_y[i] = 2.0;
			bytes_per_pixel_det_c[i] = 0.0;
			break;
		case dm_422_10:
			bytes_per_pixel_det_y[i] = 4.0;
			bytes_per_pixel_det_c[i] = 0.0;
			break;
		case dm_420_8:
			bytes_per_pixel_det_y[i] = 1.0;
			bytes_per_pixel_det_c[i] = 2.0;
			break;
		case dm_420_10:
			bytes_per_pixel_det_y[i] = 4.0 / 3.0;
			bytes_per_pixel_det_c[i] = 8.0 / 3.0;
			break;
		default:
			BREAK_TO_DEBUGGER(); /* invalid src_format in get_dcfclk */
		}
	}

	for (i = 0; i < num_pipes; i++) {
		double read_bandwidth_plane_mbps = 0.0;
		read_bandwidth_plane_mbps = (double) swath_width_y[i]
				* ((double) bytes_per_pixel_det_y[i]
						+ (double) bytes_per_pixel_det_c[i] / 2.0)
				/ ((double) e2e[i].pipe.dest.htotal
						/ (double) e2e[i].pipe.dest.pixel_rate_mhz)
				* e2e[i].pipe.scale_ratio_depth.vscl_ratio;

		if (e2e[i].pipe.src.dcc) {
			read_bandwidth_plane_mbps += (read_bandwidth_plane_mbps / 1000.0 / 256.0);
		}

		if (e2e[i].pipe.src.vm) {
			read_bandwidth_plane_mbps += (read_bandwidth_plane_mbps / 1000.0 / 512.0);
		}

		total_read_bandwidth_gbps = total_read_bandwidth_gbps
				+ read_bandwidth_plane_mbps / 1000.0;
	}

	DTRACE("total bandwidth = %f gbps", total_read_bandwidth_gbps);

	(*dcfclk_mhz) = (total_read_bandwidth_gbps * 1000.0) / mode_lib->soc.return_bus_width_bytes;

	DTRACE(
			"minimum theoretical dcfclk without stutter and full utilization = %f MHz",
			(*dcfclk_mhz));

}

struct _vcs_dpi_display_pipe_clock_st dml_clks_get_pipe_clocks(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes)
{
	struct _vcs_dpi_display_pipe_clock_st clocks;
	double max_dispclk = 0.0;
	double dcfclk;
	double dispclks[DC__NUM_PIPES__MAX];
	double dppclks[DC__NUM_PIPES__MAX];
	bool dppdiv[DC__NUM_PIPES__MAX];
	unsigned int i;

	DTRACE("Calculating pipe clocks...");

	/* this is the theoretical minimum, have to adjust based on valid values for soc */
	get_dcfclk(mode_lib, e2e, num_pipes, &dcfclk);

	/*    if(dcfclk > soc.vnom.dcfclk_mhz)
	 *        dcfclk = soc.vmax.dcfclk_mhz;
	 *    else if(dcfclk > soc.vmin.dcfclk_mhz)
	 *        dcfclk = soc.vnom.dcfclk_mhz;
	 *    else
	 *        dcfclk = soc.vmin.dcfclk_mhz;
	 */

	dcfclk = dml_socbb_voltage_scaling(
			&mode_lib->soc,
			(enum voltage_state) e2e[0].clks_cfg.voltage).dcfclk_mhz;
	clocks.dcfclk_mhz = dcfclk;

	get_plane_clks(mode_lib, e2e, num_pipes, dppclks, dispclks, dppdiv);

	for (i = 0; i < num_pipes; i++) {
		max_dispclk = dml_max(max_dispclk, dispclks[i]);
	}

	clocks.dispclk_mhz = max_dispclk;
	DTRACE("dispclk: %f Mhz", clocks.dispclk_mhz);
	DTRACE("dcfclk: %f Mhz", clocks.dcfclk_mhz);

	for (i = 0; i < num_pipes; i++) {
		if (dppclks[i] * 2 < max_dispclk)
			dppdiv[i] = 1;

		if (dppdiv[i])
			clocks.dppclk_div[i] = 1;
		else
			clocks.dppclk_div[i] = 0;

		clocks.dppclk_mhz[i] = max_dispclk / ((dppdiv[i]) ? 2.0 : 1.0);
		DTRACE("dppclk%d: %f Mhz", i, clocks.dppclk_mhz[i]);
	}

	return clocks;
}
