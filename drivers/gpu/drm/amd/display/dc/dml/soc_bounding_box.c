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
#include "soc_bounding_box.h"
#include "display_mode_lib.h"

void dml_socbb_set_latencies(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_soc_bounding_box_st *from_box)
{
	struct _vcs_dpi_soc_bounding_box_st *to_box = &mode_lib->soc;

	to_box->dram_clock_change_latency_us = from_box->dram_clock_change_latency_us;
	to_box->sr_exit_time_us = from_box->sr_exit_time_us;
	to_box->sr_enter_plus_exit_time_us = from_box->sr_enter_plus_exit_time_us;
	to_box->urgent_latency_us = from_box->urgent_latency_us;
	to_box->writeback_latency_us = from_box->writeback_latency_us;
	DTRACE("box.dram_clock_change_latency_us: %f", from_box->dram_clock_change_latency_us);
	DTRACE("box.sr_exit_time_us: %f", from_box->sr_exit_time_us);
	DTRACE("box.sr_enter_plus_exit_time_us: %f", from_box->sr_enter_plus_exit_time_us);
	DTRACE("box.urgent_latency_us: %f", from_box->urgent_latency_us);
	DTRACE("box.writeback_latency_us: %f", from_box->writeback_latency_us);

}

struct _vcs_dpi_voltage_scaling_st dml_socbb_voltage_scaling(
		struct _vcs_dpi_soc_bounding_box_st *box,
		enum voltage_state voltage)
{
	switch (voltage) {
	case dm_vmin:
		return box->vmin;
	case dm_vnom:
		return box->vnom;
	case dm_vmax:
	default:
		return box->vmax;
	}
}

double dml_socbb_return_bw_mhz(struct _vcs_dpi_soc_bounding_box_st *box, enum voltage_state voltage)
{
	double return_bw;

	struct _vcs_dpi_voltage_scaling_st state = dml_socbb_voltage_scaling(box, voltage);

	return_bw = dml_min(
			((double) box->return_bus_width_bytes) * state.dcfclk_mhz,
			state.dram_bw_per_chan_gbps * 1000.0 * box->ideal_dram_bw_after_urgent_percent / 100.0);
	return return_bw;
}
