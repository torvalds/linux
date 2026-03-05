// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dcn42_soc_and_ip_translator.h"
#include "../dcn401/dcn401_soc_and_ip_translator.h"
#include "bounding_boxes/dcn42_soc_bb.h"

/* soc_and_ip_translator component used to get up-to-date values for bounding box.
 * Bounding box values are stored in several locations and locations can vary with DCN revision.
 * This component provides an interface to get DCN-specific bounding box values.
 */

static void get_default_soc_bb(struct dml2_soc_bb *soc_bb, const struct dc *dc)
{
	{
		memcpy(soc_bb, &dml2_socbb_dcn42, sizeof(struct dml2_soc_bb));
		memcpy(&soc_bb->qos_parameters, &dml_dcn42_variant_a_soc_qos_params, sizeof(struct dml2_soc_qos_parameters));
	}
}

/*
 * DC clock table is obtained from SMU during runtime.
 * SMU stands for System Management Unit. It is a power management processor.
 * It owns the initialization of dc's clock table and programming of clock values
 * based on dc's requests.
 * Our clock values in base soc bb is a dummy placeholder. The real clock values
 * are retrieved from SMU firmware to dc clock table at runtime.
 * This function overrides our dummy placeholder values with real values in dc
 * clock table.
 */
static void dcn42_convert_dc_clock_table_to_soc_bb_clock_table(
		struct dml2_soc_state_table *dml_clk_table,
		struct dml2_soc_vmin_clock_limits *vmin_limit,
		const struct clk_bw_params *dc_bw_params)
{
	int i;
	const struct clk_limit_table *dc_clk_table;

	if (dc_bw_params == NULL)
		/* skip if bw params could not be obtained from smu */
		return;

	dc_clk_table = &dc_bw_params->clk_table;

	/* fclk/dcfclk - dcn42 pmfw table can have 0 entries for inactive dpm levels
	 * for use with dml we need to fill in using an active value aiming for >= 2x DCFCLK
	 */
	if (dc_clk_table->num_entries_per_clk.num_fclk_levels && dc_clk_table->num_entries_per_clk.num_dcfclk_levels) {
		dml_clk_table->fclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_dcfclk_levels;
		dml_clk_table->dcfclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_dcfclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dc_clk_table->num_entries_per_clk.num_dcfclk_levels) {
				int j, max_fclk = 0;

				dml_clk_table->dcfclk.clk_values_khz[i] = dc_clk_table->entries[i].dcfclk_mhz * 1000;
				for (j = 0; j < MAX_NUM_DPM_LVL; j++) {
					if (dc_clk_table->entries[j].fclk_mhz * 1000 > max_fclk)
						max_fclk = dc_clk_table->entries[j].fclk_mhz * 1000;
					dml_clk_table->fclk.clk_values_khz[i] = max_fclk;
					if (max_fclk >= 2 * dml_clk_table->dcfclk.clk_values_khz[i])
						break;
				}
			} else {
				dml_clk_table->dcfclk.clk_values_khz[i] = 0;
				dml_clk_table->fclk.clk_values_khz[i] = 0;
			}
		}
	}

	/* uclk */
	if (dc_clk_table->num_entries_per_clk.num_memclk_levels) {
		dml_clk_table->uclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_memclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dml_clk_table->uclk.num_clk_values) {
				dml_clk_table->uclk.clk_values_khz[i] = dc_clk_table->entries[i].memclk_mhz * 1000;
				dml_clk_table->wck_ratio.clk_values_khz[i] = dc_clk_table->entries[i].wck_ratio;
			} else {
				dml_clk_table->uclk.clk_values_khz[i] = 0;
				dml_clk_table->wck_ratio.clk_values_khz[i] = 0;
			}
		}
	}

	/* dispclk */
	if (dc_clk_table->num_entries_per_clk.num_dispclk_levels) {
		dml_clk_table->dispclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_dispclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dml_clk_table->dispclk.num_clk_values) {
				dml_clk_table->dispclk.clk_values_khz[i] = dc_clk_table->entries[i].dispclk_mhz * 1000;
			} else {
				dml_clk_table->dispclk.clk_values_khz[i] = 0;
			}
		}
		vmin_limit->dispclk_khz = min(dc_clk_table->entries[0].dispclk_mhz * 1000, vmin_limit->dispclk_khz);
	}

	/* dppclk */
	if (dc_clk_table->num_entries_per_clk.num_dppclk_levels) {
		dml_clk_table->dppclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_dppclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dml_clk_table->dppclk.num_clk_values) {
				dml_clk_table->dppclk.clk_values_khz[i] = dc_clk_table->entries[i].dppclk_mhz * 1000;
			} else {
				dml_clk_table->dppclk.clk_values_khz[i] = 0;
			}
		}
	}

	/* dtbclk */
	if (dc_clk_table->num_entries_per_clk.num_dtbclk_levels) {
		dml_clk_table->dtbclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_dtbclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dml_clk_table->dtbclk.num_clk_values) {
				dml_clk_table->dtbclk.clk_values_khz[i] = dc_clk_table->entries[i].dtbclk_mhz * 1000;
			} else {
				dml_clk_table->dtbclk.clk_values_khz[i] = 0;
			}
		}
	}

	/* socclk */
	if (dc_clk_table->num_entries_per_clk.num_socclk_levels) {
		dml_clk_table->socclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_socclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dml_clk_table->socclk.num_clk_values) {
				dml_clk_table->socclk.clk_values_khz[i] = dc_clk_table->entries[i].socclk_mhz * 1000;
			} else {
				dml_clk_table->socclk.clk_values_khz[i] = 0;
			}
		}
	}

	/* dram config */
	dml_clk_table->dram_config.channel_count = dc_bw_params->num_channels;
	dml_clk_table->dram_config.channel_width_bytes = dc_bw_params->dram_channel_width_bytes;
}

static void dcn42_update_soc_bb_with_values_from_clk_mgr(struct dml2_soc_bb *soc_bb, const struct dc *dc)
{
	soc_bb->dprefclk_mhz = dc->clk_mgr->dprefclk_khz / 1000;
	soc_bb->dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;
	soc_bb->mall_allocated_for_dcn_mbytes = dc->caps.mall_size_total / (1024 * 1024);

	if (dc->clk_mgr->funcs->is_smu_present &&
			dc->clk_mgr->funcs->is_smu_present(dc->clk_mgr)) {
		dcn42_convert_dc_clock_table_to_soc_bb_clock_table(&soc_bb->clk_table, &soc_bb->vmin_limit,
			dc->clk_mgr->bw_params);
	}
}

static void apply_soc_bb_updates(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config)
{
	/* Individual modification can be overwritten even if it was obtained by a previous function.
	 * Modifications are acquired in order of priority (lowest to highest).
	 */
	dc_assert_fp_enabled();

	dcn42_update_soc_bb_with_values_from_clk_mgr(soc_bb, dc);
	dcn401_update_soc_bb_with_values_from_vbios(soc_bb, dc);
	dcn401_update_soc_bb_with_values_from_software_policy(soc_bb, dc);
}

void dcn42_get_soc_bb(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config)
{
	//get default soc_bb with static values
	get_default_soc_bb(soc_bb, dc);
	//update soc_bb values with more accurate values
	apply_soc_bb_updates(soc_bb, dc, config);
}

static void dcn42_get_ip_caps(struct dml2_ip_capabilities *ip_caps)
{
	*ip_caps = dml2_dcn42_max_ip_caps;
}

static struct soc_and_ip_translator_funcs dcn42_translator_funcs = {
	.get_soc_bb = dcn42_get_soc_bb,
	.get_ip_caps = dcn42_get_ip_caps,
};

void dcn42_construct_soc_and_ip_translator(struct soc_and_ip_translator *soc_and_ip_translator)
{
	soc_and_ip_translator->translator_funcs = &dcn42_translator_funcs;
}

