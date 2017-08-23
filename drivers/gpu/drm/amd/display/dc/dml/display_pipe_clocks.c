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

display_pipe_clock_st dml_clks_get_pipe_clocks(
		struct display_mode_lib *mode_lib,
		display_e2e_pipe_params_st *e2e,
		unsigned int num_pipes)
{
	display_pipe_clock_st clocks;
	bool visited[DC__NUM_PIPES__MAX];
	double max_dispclk = 25.0; //the min dispclk is 25MHz, so keep the min dispclk caculated larger thant 25MHz
	double dcfclk, socclk;
	unsigned int i, j, k;
	unsigned int dsc_inst = 0;

	DTRACE("Calculating pipe clocks...");

	dcfclk = dml_socbb_voltage_scaling(
			&mode_lib->soc,
			(enum voltage_state) e2e[0].clks_cfg.voltage).dcfclk_mhz;
	socclk = dml_socbb_voltage_scaling(
			&mode_lib->soc,
			(enum voltage_state) e2e[0].clks_cfg.voltage).socclk_mhz;
	clocks.dcfclk_mhz = dcfclk;
	clocks.socclk_mhz = socclk;

	max_dispclk = dml_max(max_dispclk, get_dispclk_calculated(mode_lib, e2e, num_pipes));
	clocks.dispclk_mhz = max_dispclk;
	DTRACE("  dispclk: %f Mhz", clocks.dispclk_mhz);
	DTRACE("  dcfclk: %f Mhz", clocks.dcfclk_mhz);
	DTRACE("  socclk: %f Mhz", clocks.socclk_mhz);

	for (k = 0; k < num_pipes; ++k)
		visited[k] = false;

	for (i = 0; i < num_pipes; i++) {
		clocks.dppclk_mhz[i] = get_dppclk_calculated(mode_lib, e2e, num_pipes, i);
		DTRACE("  dppclk%d: %f Mhz", i, clocks.dppclk_mhz[i]);

		if (e2e[i].pipe.src.is_hsplit && !visited[i]) {
			unsigned int grp = e2e[i].pipe.src.hsplit_grp;

			for (j = i; j < num_pipes; j++) {
				if (e2e[j].pipe.src.hsplit_grp == grp && e2e[j].pipe.src.is_hsplit
						&& !visited[j]) {
					clocks.dscclk_mhz[j] = get_dscclk_calculated(
							mode_lib,
							e2e,
							num_pipes,
							dsc_inst);
					DTRACE("  dscclk%d: %f Mhz", j, clocks.dscclk_mhz[j]);
					visited[j] = true;
				}
			}
			dsc_inst++;
		}

		if (!visited[i]) {
			unsigned int otg_inst = e2e[i].pipe.dest.otg_inst;

			for (j = i; j < num_pipes; j++) {
				// assign dscclk to all planes with this otg, except if they're doing odm combine, or mpc combine
				// which is handled by the conditions above, the odm_combine is not required, but it helps make sense of this code
				if (e2e[j].pipe.dest.otg_inst == otg_inst
						&& !e2e[j].pipe.dest.odm_combine && !visited[j]) {
					clocks.dscclk_mhz[j] = get_dscclk_calculated(
							mode_lib,
							e2e,
							num_pipes,
							dsc_inst);
					DTRACE("  dscclk%d: %f Mhz", j, clocks.dscclk_mhz[j]);
					visited[j] = true;
				}
			}
			dsc_inst++;
		}
	}

	return clocks;
}
