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
#include "dc_features.h"

#include "dml_inline_defs.h"

/*
 * NOTE:
 *   This file is gcc-parseable HW gospel, coming straight from HW engineers.
 *
 * It doesn't adhere to Linux kernel style and sometimes will do things in odd
 * ways. Unless there is something clearly wrong with it the code should
 * remain as-is as it provides us with a guarantee from HW that it is correct.
 */

void dml_socbb_set_latencies(soc_bounding_box_st *to_box, soc_bounding_box_st *from_box)
{
	to_box->dram_clock_change_latency_us = from_box->dram_clock_change_latency_us;
	to_box->sr_exit_time_us = from_box->sr_exit_time_us;
	to_box->sr_enter_plus_exit_time_us = from_box->sr_enter_plus_exit_time_us;
	to_box->urgent_latency_us = from_box->urgent_latency_us;
	to_box->writeback_latency_us = from_box->writeback_latency_us;
}

voltage_scaling_st dml_socbb_voltage_scaling(
		const soc_bounding_box_st *soc,
		enum voltage_state voltage)
{
	const voltage_scaling_st *voltage_state;
	const voltage_scaling_st * const voltage_end = soc->clock_limits + DC__VOLTAGE_STATES;

	for (voltage_state = soc->clock_limits;
			voltage_state < voltage_end && voltage_state->state != voltage;
			voltage_state++) {
	}

	if (voltage_state < voltage_end)
		return *voltage_state;
	return soc->clock_limits[DC__VOLTAGE_STATES - 1];
}

double dml_socbb_return_bw_mhz(soc_bounding_box_st *box, enum voltage_state voltage)
{
	double return_bw;

	voltage_scaling_st state = dml_socbb_voltage_scaling(box, voltage);

	return_bw = dml_min((double) box->return_bus_width_bytes * state.dcfclk_mhz,
			state.dram_bw_per_chan_gbps * 1000.0 * (double) box->num_chans
					* box->ideal_dram_bw_after_urgent_percent / 100.0);

	return_bw = dml_min((double) box->return_bus_width_bytes * state.fabricclk_mhz, return_bw);

	return return_bw;
}
