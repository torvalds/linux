// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dml2_mcg_dcn42.h"
#include "dml_top_soc_parameter_types.h"

static unsigned long long uclk_to_dram_bw_kbps(unsigned long uclk_khz, const struct dml2_dram_params *dram_config, unsigned long wck_ratio)
{
	unsigned long long bw_kbps = 0;

	bw_kbps = (unsigned long long) uclk_khz * dram_config->channel_count * dram_config->channel_width_bytes * wck_ratio * 2;
	return bw_kbps;
}

static bool build_min_clk_table_coarse_grained(const struct dml2_soc_bb *soc_bb, struct dml2_mcg_min_clock_table *min_table)
{
	int i;

	for (i = 0; i < soc_bb->clk_table.fclk.num_clk_values; i++) {
		if (i < soc_bb->clk_table.uclk.num_clk_values) {
			min_table->dram_bw_table.entries[i].pre_derate_dram_bw_kbps =
					uclk_to_dram_bw_kbps(soc_bb->clk_table.uclk.clk_values_khz[i], &soc_bb->clk_table.dram_config, soc_bb->clk_table.wck_ratio.clk_values_khz[i]);
			min_table->dram_bw_table.entries[i].min_uclk_khz = soc_bb->clk_table.uclk.clk_values_khz[i];
		} else {
			min_table->dram_bw_table.entries[i].pre_derate_dram_bw_kbps = min_table->dram_bw_table.entries[soc_bb->clk_table.uclk.num_clk_values - 1].pre_derate_dram_bw_kbps;
			min_table->dram_bw_table.entries[i].min_uclk_khz = soc_bb->clk_table.uclk.clk_values_khz[soc_bb->clk_table.uclk.num_clk_values - 1];
		}

		min_table->dram_bw_table.entries[i].min_dcfclk_khz = soc_bb->clk_table.dcfclk.clk_values_khz[i];
		min_table->dram_bw_table.entries[i].min_fclk_khz = soc_bb->clk_table.fclk.clk_values_khz[i];
	}
	min_table->dram_bw_table.num_entries = soc_bb->clk_table.fclk.num_clk_values;

	return true;
}

static bool build_min_clock_table(const struct dml2_soc_bb *soc_bb, struct dml2_mcg_min_clock_table *min_table)
{
	bool result;

	if (!soc_bb || !min_table)
		return false;


	if (soc_bb->clk_table.uclk.num_clk_values > DML_MCG_MAX_CLK_TABLE_SIZE)
		return false;

	min_table->fixed_clocks_khz.amclk = 0;
	min_table->fixed_clocks_khz.dprefclk = soc_bb->dprefclk_mhz * 1000;
	min_table->fixed_clocks_khz.pcierefclk = soc_bb->pcie_refclk_mhz * 1000;
	min_table->fixed_clocks_khz.dchubrefclk = soc_bb->dchub_refclk_mhz * 1000;
	min_table->fixed_clocks_khz.xtalclk = soc_bb->xtalclk_mhz * 1000;

	min_table->max_clocks_khz.dispclk = soc_bb->clk_table.dispclk.clk_values_khz[soc_bb->clk_table.dispclk.num_clk_values - 1];
	min_table->max_clocks_khz.dppclk = soc_bb->clk_table.dppclk.clk_values_khz[soc_bb->clk_table.dppclk.num_clk_values - 1];
	min_table->max_clocks_khz.dscclk = soc_bb->clk_table.dscclk.clk_values_khz[soc_bb->clk_table.dscclk.num_clk_values - 1];
	min_table->max_clocks_khz.dtbclk = soc_bb->clk_table.dtbclk.clk_values_khz[soc_bb->clk_table.dtbclk.num_clk_values - 1];
	min_table->max_clocks_khz.phyclk = soc_bb->clk_table.phyclk.clk_values_khz[soc_bb->clk_table.phyclk.num_clk_values - 1];

	min_table->max_ss_clocks_khz.dispclk = (unsigned int)((double)min_table->max_clocks_khz.dispclk / (1.0 + soc_bb->dcn_downspread_percent / 100.0));
	min_table->max_ss_clocks_khz.dppclk = (unsigned int)((double)min_table->max_clocks_khz.dppclk / (1.0 + soc_bb->dcn_downspread_percent / 100.0));
	min_table->max_ss_clocks_khz.dtbclk = (unsigned int)((double)min_table->max_clocks_khz.dtbclk / (1.0 + soc_bb->dcn_downspread_percent / 100.0));

	min_table->max_clocks_khz.dcfclk = soc_bb->clk_table.dcfclk.clk_values_khz[soc_bb->clk_table.dcfclk.num_clk_values - 1];
	min_table->max_clocks_khz.fclk = soc_bb->clk_table.fclk.clk_values_khz[soc_bb->clk_table.fclk.num_clk_values - 1];

	result = build_min_clk_table_coarse_grained(soc_bb, min_table);

	return result;
}

bool mcg_dcn42_build_min_clock_table(struct dml2_mcg_build_min_clock_table_params_in_out *in_out)
{
	return build_min_clock_table(in_out->soc_bb, in_out->min_clk_table);
}
