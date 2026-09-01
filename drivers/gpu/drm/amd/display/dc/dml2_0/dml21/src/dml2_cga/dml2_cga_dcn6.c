// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.
#include "dml2_cga_dcn6.h"
#include "lib_float_math.h"
#include "dml2_debug.h"

#define DFS_DIVIDER_RANGE_SCALE_FACTOR 4.0
#define CLOCK_UNIT_GRANULARITY 0.001
#define DPPREFCLK_DIVIDER 255

static double cga_dcn6_add_overhead_percent(double clk, double overhead_percent)
{
	return clk * (1 + overhead_percent / 100.0);
}

static double cga_dcn6_calculate_refclk_mhz(const struct dml2_clock_granularity_adjuster *adjuster,
		const double *clks_mhz, unsigned int count)
{
	unsigned int i;
	double max_clk_mhz = 0;
	double refclk_mhz;

	for (i = 0; i < count; i++)
		max_clk_mhz = math_max2(clks_mhz[i], max_clk_mhz);

	refclk_mhz = math_floor2(max_clk_mhz, CLOCK_UNIT_GRANULARITY);
	refclk_mhz = cga_dcn6_add_overhead_percent(refclk_mhz, adjuster->dcn_downspread_percent);
	refclk_mhz = math_floor2(refclk_mhz, CLOCK_UNIT_GRANULARITY);

	return refclk_mhz;
}

static double cga_dcn6_calculate_actual_dispclk_mhz(const struct dml2_clock_granularity_adjuster *adjuster, double dispclk_mhz)
{
	double dispclk_with_downspread_mhz;
	double dispclk_with_ramp_margin_mhz;

	dispclk_mhz = math_floor2(dispclk_mhz, CLOCK_UNIT_GRANULARITY);
	dispclk_with_downspread_mhz = cga_dcn6_add_overhead_percent(dispclk_mhz, adjuster->dcn_downspread_percent);
	dispclk_with_ramp_margin_mhz = cga_dcn6_add_overhead_percent(
			dispclk_with_downspread_mhz, adjuster->dispclk_ramp_margin_percent);
	if (dispclk_with_downspread_mhz <= adjuster->max_dispclk_mhz &&
			dispclk_with_ramp_margin_mhz > adjuster->max_dispclk_mhz)
		/* when dispclk with ramp margin is slightly over max, clamp the ramp margin to the max dispclk */
		return math_floor2(adjuster->max_dispclk_mhz, CLOCK_UNIT_GRANULARITY);
	else if (dispclk_with_downspread_mhz > adjuster->max_dispclk_mhz)
		return math_floor2(dispclk_with_downspread_mhz, CLOCK_UNIT_GRANULARITY);
	else
		return math_floor2(dispclk_with_ramp_margin_mhz, CLOCK_UNIT_GRANULARITY);
}

static double cga_dcn6_adjust_to_dfs_clock_value_mhz(const struct dml2_clock_granularity_adjuster *adjuster, double clk_mhz)
{
	double vco_speed_scaled_mhz;
	double vco_divider;
	double adjusted_clock_mhz;

	DML_ASSERT_MSG(adjuster->dispclk_dppclk_vco_speed_mhz > 1, "invalid dispclk_dppclk_vco_speed_mhz value!\n");
	if (clk_mhz == 0)
		/* There are cases when a clock is not needed */
		return 0;

	vco_speed_scaled_mhz = math_floor2(adjuster->dispclk_dppclk_vco_speed_mhz, 0.001);
	vco_speed_scaled_mhz *= DFS_DIVIDER_RANGE_SCALE_FACTOR;
	vco_divider = vco_speed_scaled_mhz / clk_mhz;
	vco_divider = math_floor(vco_divider);
	adjusted_clock_mhz = vco_speed_scaled_mhz / vco_divider;
	adjusted_clock_mhz = math_floor2(adjusted_clock_mhz, CLOCK_UNIT_GRANULARITY);
	return adjusted_clock_mhz;
}

static double dga_dcn6_calculate_adjusted_dppclk_mhz(const struct dml2_clock_granularity_adjuster *adjuster,
		double dpprefclk_mhz, double dppclk_mhz)
{
	double granularity_mhz = dpprefclk_mhz / DPPREFCLK_DIVIDER;

	dppclk_mhz = math_floor2(dppclk_mhz, CLOCK_UNIT_GRANULARITY);
	dppclk_mhz = cga_dcn6_add_overhead_percent(dppclk_mhz, adjuster->dcn_downspread_percent);
	dppclk_mhz = math_ceil2(dppclk_mhz, granularity_mhz);
	dppclk_mhz = math_floor2(dppclk_mhz, CLOCK_UNIT_GRANULARITY);

	return dppclk_mhz;
}

static double dga_dcn6_calculate_adjusted_dtbclk_mhz(
		const struct dml2_clock_granularity_adjuster *adjuster, double dppclk_mhz)
{
	(void)adjuster;
	return math_floor2(dppclk_mhz, CLOCK_UNIT_GRANULARITY);
}

static double cga_dcn6_adjust_dispclk_mhz(const struct dml2_clock_granularity_adjuster *adjuster, double dispclk_mhz)
{
	double adjusted_dispclk_mhz;

	adjusted_dispclk_mhz = cga_dcn6_calculate_actual_dispclk_mhz(adjuster, dispclk_mhz);
	adjusted_dispclk_mhz = cga_dcn6_adjust_to_dfs_clock_value_mhz(adjuster, adjusted_dispclk_mhz);

	return adjusted_dispclk_mhz;
}

static void cga_dcn6_adjust_dppclks_mhz(const struct dml2_clock_granularity_adjuster *adjuster, unsigned int count,
		const double *dppclks_mhz, double *adjusted_dppclks_mhz, double *adjusted_dpprefclk_mhz)
{
	unsigned int i;
	double dpprefclk_mhz;

	dpprefclk_mhz = cga_dcn6_calculate_refclk_mhz(adjuster, dppclks_mhz, count);
	*adjusted_dpprefclk_mhz = cga_dcn6_adjust_to_dfs_clock_value_mhz(adjuster, dpprefclk_mhz);
	for (i = 0; i < count; i++)
		adjusted_dppclks_mhz[i] = dga_dcn6_calculate_adjusted_dppclk_mhz(
					adjuster, *adjusted_dpprefclk_mhz, dppclks_mhz[i]);
}

static void cga_dcn6_adjust_dtbclks_mhz(const struct dml2_clock_granularity_adjuster *adjuster, unsigned int count,
		const double *dtbclks_mhz, double *adjusted_dtbclks_mhz, double *adjusted_dtbrefclk_mhz)
{
	unsigned int i;
	double dtbrefclk_mhz;

	dtbrefclk_mhz = cga_dcn6_calculate_refclk_mhz(adjuster, dtbclks_mhz, count);
	*adjusted_dtbrefclk_mhz = cga_dcn6_adjust_to_dfs_clock_value_mhz(adjuster, dtbrefclk_mhz);
	for (i = 0; i < count; i++)
		adjusted_dtbclks_mhz[i] = dga_dcn6_calculate_adjusted_dtbclk_mhz(
					adjuster, dtbclks_mhz[i]);
}

static double cga_dcn6_adjust_dcfclk_deepsleep_mhz(const struct dml2_clock_granularity_adjuster *adjuster,
		double dcfclk_deepsleep_mhz)
{
	(void)adjuster;
	return math_ceil2(dcfclk_deepsleep_mhz, CLOCK_UNIT_GRANULARITY);
}

static void cga_dcn6_initialize(const struct dml2_cga_initialize_in_out *in_out)
{
	in_out->adjuster->dcn_downspread_percent = in_out->soc_bb->dcn_downspread_percent;
	in_out->adjuster->dispclk_dppclk_vco_speed_mhz = in_out->soc_bb->dispclk_dppclk_vco_speed_mhz;
	in_out->adjuster->dispclk_ramp_margin_percent = in_out->ip->dispclk_ramp_margin_percent;
	in_out->adjuster->max_dispclk_mhz =
		in_out->soc_bb->clk_table.dispclk.clk_values_khz[in_out->soc_bb->clk_table.dispclk.num_clk_values - 1] / 1000.0;
}

void cga_dcn6_create(struct dml2_clock_granularity_adjuster *adjuster)
{
	adjuster->initialize = cga_dcn6_initialize;
	adjuster->adjust_dispclk_mhz = cga_dcn6_adjust_dispclk_mhz;
	adjuster->adjust_dppclks_mhz = cga_dcn6_adjust_dppclks_mhz;
	adjuster->adjust_dtbclks_mhz = cga_dcn6_adjust_dtbclks_mhz;
	adjuster->adjust_dcfclk_deepsleep_mhz = cga_dcn6_adjust_dcfclk_deepsleep_mhz;
}
