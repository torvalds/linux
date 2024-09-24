// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_mcg_dcn4.h"
#include "dml_top_soc_parameter_types.h"

static bool build_min_clock_table(const struct dml2_soc_bb *soc_bb, struct dml2_mcg_min_clock_table *min_table);

bool mcg_dcn4_build_min_clock_table(struct dml2_mcg_build_min_clock_table_params_in_out *in_out)
{
	return build_min_clock_table(in_out->soc_bb, in_out->min_clk_table);
}

static unsigned long long uclk_to_dram_bw_kbps(unsigned long uclk_khz, const struct dml2_dram_params *dram_config)
{
	unsigned long long bw_kbps = 0;

	bw_kbps = (unsigned long long) uclk_khz * dram_config->channel_count * dram_config->channel_width_bytes * dram_config->transactions_per_clock;

	return bw_kbps;
}

static unsigned long round_up_to_quantized_values(unsigned long value, const unsigned long *quantized_values, int num_quantized_values)
{
	int i;

	if (!quantized_values)
		return 0;

	for (i = 0; i < num_quantized_values; i++) {
		if (quantized_values[i] > value)
			return quantized_values[i];
	}

	return 0;
}

static bool build_min_clk_table_fine_grained(const struct dml2_soc_bb *soc_bb, struct dml2_mcg_min_clock_table *min_table)
{
	bool dcfclk_fine_grained = false, fclk_fine_grained = false;

	int i;
	unsigned int j;

	unsigned long min_dcfclk_khz = 0;
	unsigned long min_fclk_khz = 0;
	unsigned long prev_100, cur_50;

	if (soc_bb->clk_table.dcfclk.num_clk_values == 2) {
		dcfclk_fine_grained = true;
	}

	if (soc_bb->clk_table.fclk.num_clk_values == 2) {
		fclk_fine_grained = true;
	}

	min_dcfclk_khz = soc_bb->clk_table.dcfclk.clk_values_khz[0];
	min_fclk_khz = soc_bb->clk_table.fclk.clk_values_khz[0];

	// First calculate the table for "balanced" bandwidths across UCLK/FCLK
	for (i = 0; i < soc_bb->clk_table.uclk.num_clk_values; i++) {
		min_table->dram_bw_table.entries[i].pre_derate_dram_bw_kbps = uclk_to_dram_bw_kbps(soc_bb->clk_table.uclk.clk_values_khz[i], &soc_bb->clk_table.dram_config);

		min_table->dram_bw_table.entries[i].min_fclk_khz = (unsigned long)((((double)min_table->dram_bw_table.entries[i].pre_derate_dram_bw_kbps * soc_bb->qos_parameters.derate_table.system_active_urgent.dram_derate_percent_pixel / 100) / ((double)soc_bb->qos_parameters.derate_table.system_active_urgent.fclk_derate_percent / 100)) / soc_bb->fabric_datapath_to_dcn_data_return_bytes);
	}
	min_table->dram_bw_table.num_entries = soc_bb->clk_table.uclk.num_clk_values;

	// To create the minium table, effectively shift "up" all the dcfclk/fclk entries by 1, and then replace the lowest entry with min fclk/dcfclk
	for (i = min_table->dram_bw_table.num_entries - 1; i > 0; i--) {
		prev_100 = min_table->dram_bw_table.entries[i - 1].min_fclk_khz;
		cur_50 = min_table->dram_bw_table.entries[i].min_fclk_khz / 2;
		min_table->dram_bw_table.entries[i].min_fclk_khz = prev_100 > cur_50 ? prev_100 : cur_50;

		if (!fclk_fine_grained) {
			min_table->dram_bw_table.entries[i].min_fclk_khz = round_up_to_quantized_values(min_table->dram_bw_table.entries[i].min_fclk_khz, soc_bb->clk_table.fclk.clk_values_khz, soc_bb->clk_table.fclk.num_clk_values);
		}
	}
	min_table->dram_bw_table.entries[0].min_fclk_khz /= 2;

	// Clamp to minimums and maximums
	for (i = 0; i < (int)min_table->dram_bw_table.num_entries; i++) {
		if (min_table->dram_bw_table.entries[i].min_dcfclk_khz < min_dcfclk_khz)
			min_table->dram_bw_table.entries[i].min_dcfclk_khz = min_dcfclk_khz;

		if (min_table->dram_bw_table.entries[i].min_fclk_khz < min_fclk_khz)
			min_table->dram_bw_table.entries[i].min_fclk_khz = min_fclk_khz;

		if (soc_bb->max_fclk_for_uclk_dpm_khz > 0 &&
			min_table->dram_bw_table.entries[i].min_fclk_khz > soc_bb->max_fclk_for_uclk_dpm_khz)
			min_table->dram_bw_table.entries[i].min_fclk_khz = soc_bb->max_fclk_for_uclk_dpm_khz;

		min_table->dram_bw_table.entries[i].min_dcfclk_khz =
			min_table->dram_bw_table.entries[i].min_fclk_khz *
			soc_bb->qos_parameters.derate_table.system_active_urgent.fclk_derate_percent / soc_bb->qos_parameters.derate_table.system_active_urgent.dcfclk_derate_percent;

		min_table->dram_bw_table.entries[i].min_dcfclk_khz =
			min_table->dram_bw_table.entries[i].min_dcfclk_khz * soc_bb->fabric_datapath_to_dcn_data_return_bytes / soc_bb->return_bus_width_bytes;

		if (!dcfclk_fine_grained) {
			min_table->dram_bw_table.entries[i].min_dcfclk_khz = round_up_to_quantized_values(min_table->dram_bw_table.entries[i].min_dcfclk_khz, soc_bb->clk_table.dcfclk.clk_values_khz, soc_bb->clk_table.dcfclk.num_clk_values);
		}
	}

	// Prune states which are invalid (some clocks exceed maximum)
	for (i = 0; i < (int)min_table->dram_bw_table.num_entries; i++) {
		if (min_table->dram_bw_table.entries[i].min_dcfclk_khz > min_table->max_clocks_khz.dcfclk ||
			min_table->dram_bw_table.entries[i].min_fclk_khz > min_table->max_clocks_khz.fclk) {
			min_table->dram_bw_table.num_entries = i;
			break;
		}
	}

	// Prune duplicate states
	for (i = 0; i < (int)min_table->dram_bw_table.num_entries - 1; i++) {
		if (min_table->dram_bw_table.entries[i].min_dcfclk_khz == min_table->dram_bw_table.entries[i + 1].min_dcfclk_khz &&
			min_table->dram_bw_table.entries[i].min_fclk_khz == min_table->dram_bw_table.entries[i + 1].min_fclk_khz &&
			min_table->dram_bw_table.entries[i].pre_derate_dram_bw_kbps == min_table->dram_bw_table.entries[i + 1].pre_derate_dram_bw_kbps) {

			// i + 1 is the same state as i, so shift everything
			for (j = i + 1; j < min_table->dram_bw_table.num_entries; j++) {
				min_table->dram_bw_table.entries[j].min_dcfclk_khz = min_table->dram_bw_table.entries[j + 1].min_dcfclk_khz;
				min_table->dram_bw_table.entries[j].min_fclk_khz = min_table->dram_bw_table.entries[j + 1].min_fclk_khz;
				min_table->dram_bw_table.entries[j].pre_derate_dram_bw_kbps = min_table->dram_bw_table.entries[j + 1].pre_derate_dram_bw_kbps;
			}
			min_table->dram_bw_table.num_entries--;
		}
	}

	return true;
}

static bool build_min_clk_table_coarse_grained(const struct dml2_soc_bb *soc_bb, struct dml2_mcg_min_clock_table *min_table)
{
	int i;

	for (i = 0; i < soc_bb->clk_table.uclk.num_clk_values; i++) {
		min_table->dram_bw_table.entries[i].pre_derate_dram_bw_kbps = uclk_to_dram_bw_kbps(soc_bb->clk_table.uclk.clk_values_khz[i], &soc_bb->clk_table.dram_config);
		min_table->dram_bw_table.entries[i].min_dcfclk_khz = soc_bb->clk_table.dcfclk.clk_values_khz[i];
		min_table->dram_bw_table.entries[i].min_fclk_khz = soc_bb->clk_table.fclk.clk_values_khz[i];
	}
	min_table->dram_bw_table.num_entries = soc_bb->clk_table.uclk.num_clk_values;

	return true;
}

static bool build_min_clock_table(const struct dml2_soc_bb *soc_bb, struct dml2_mcg_min_clock_table *min_table)
{
	bool result;
	bool dcfclk_fine_grained = false, fclk_fine_grained = false, clock_state_count_equal = false;

	if (!soc_bb || !min_table)
		return false;

	if (soc_bb->clk_table.dcfclk.num_clk_values < 2 || soc_bb->clk_table.fclk.num_clk_values < 2)
		return false;

	if (soc_bb->clk_table.uclk.num_clk_values > DML_MCG_MAX_CLK_TABLE_SIZE)
		return false;

	if (soc_bb->clk_table.dcfclk.num_clk_values == 2) {
		dcfclk_fine_grained = true;
	}

	if (soc_bb->clk_table.fclk.num_clk_values == 2) {
		fclk_fine_grained = true;
	}

	if (soc_bb->clk_table.fclk.num_clk_values == soc_bb->clk_table.dcfclk.num_clk_values &&
		soc_bb->clk_table.fclk.num_clk_values == soc_bb->clk_table.uclk.num_clk_values)
		clock_state_count_equal = true;

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

	min_table->max_clocks_khz.dcfclk = soc_bb->clk_table.dcfclk.clk_values_khz[soc_bb->clk_table.dcfclk.num_clk_values - 1];
	min_table->max_clocks_khz.fclk = soc_bb->clk_table.fclk.clk_values_khz[soc_bb->clk_table.fclk.num_clk_values - 1];

	if (dcfclk_fine_grained || fclk_fine_grained || !clock_state_count_equal)
		result = build_min_clk_table_fine_grained(soc_bb, min_table);
	else
		result = build_min_clk_table_coarse_grained(soc_bb, min_table);

	return result;
}
