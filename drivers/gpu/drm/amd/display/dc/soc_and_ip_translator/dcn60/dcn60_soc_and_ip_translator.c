// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dcn60_soc_and_ip_translator.h"
#include "soc_and_ip_translator/dcn401/dcn401_soc_and_ip_translator.h"
#include "bounding_boxes/dcn6_soc_bb.h"

/* soc_and_ip_translator component used to get up-to-date values for bounding box.
 * Bounding box values are stored in several locations and locations can vary with DCN revision.
 * This component provides an interface to get DCN-specific bounding box values.
 */
static void dcn60_update_soc_bb_with_values_from_dmub(struct dml2_soc_bb *soc_bb, const struct dml2_configuration_options *config)
{
	const struct dmub_soc_bb_params *dmub_bb_params =
			(const struct dmub_soc_bb_params *)config->bb_from_dmub;
	int i;
	unsigned int min_alt_ch_carveout_size_mb = 0;

	if (dmub_bb_params == NULL)
		return;

	//only update if value is provided
	if (dmub_bb_params->dram_clk_change_blackout_ns > 0)
		soc_bb->power_management_parameters.dram_clk_change_blackout_us =
			(double) dmub_bb_params->dram_clk_change_blackout_ns / 1000.0;
	if (dmub_bb_params->dram_clk_change_read_only_ns > 0)
		soc_bb->power_management_parameters.dram_clk_change_read_only_us =
			(double) dmub_bb_params->dram_clk_change_read_only_ns / 1000.0;
	if (dmub_bb_params->dram_clk_change_write_only_ns > 0)
		soc_bb->power_management_parameters.dram_clk_change_write_only_us =
			(double) dmub_bb_params->dram_clk_change_write_only_ns / 1000.0;
	if (dmub_bb_params->fclk_change_blackout_ns > 0)
		soc_bb->power_management_parameters.fclk_change_blackout_us =
			(double) dmub_bb_params->fclk_change_blackout_ns / 1000.0;
	if (dmub_bb_params->g7_ppt_blackout_ns > 0)
		soc_bb->power_management_parameters.g7_ppt_blackout_us =
			(double) dmub_bb_params->g7_ppt_blackout_ns / 1000.0;
	if (dmub_bb_params->stutter_enter_plus_exit_latency_ns > 0)
		soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us =
			(double) dmub_bb_params->stutter_enter_plus_exit_latency_ns / 1000.0;
	if (dmub_bb_params->stutter_exit_latency_ns > 0)
		soc_bb->power_management_parameters.stutter_exit_latency_us =
		(double) dmub_bb_params->stutter_exit_latency_ns / 1000.0;
	if (dmub_bb_params->z8_stutter_enter_plus_exit_latency_ns > 0)
		soc_bb->power_management_parameters.z8_stutter_enter_plus_exit_latency_us =
			(double) dmub_bb_params->z8_stutter_enter_plus_exit_latency_ns / 1000.0;
	if (dmub_bb_params->z8_stutter_exit_latency_ns > 0)
		soc_bb->power_management_parameters.z8_stutter_exit_latency_us =
			(double) dmub_bb_params->z8_stutter_exit_latency_ns / 1000.0;
	if (dmub_bb_params->z8_min_idle_time_ns > 0)
		soc_bb->power_management_parameters.z8_min_idle_time =
			(double) dmub_bb_params->z8_min_idle_time_ns / 1000.0;
	if (dmub_bb_params->type_b_dram_clk_change_blackout_ns > 0)
		soc_bb->power_management_parameters.type_b_dram_clk_change_blackout_us =
			(double) dmub_bb_params->type_b_dram_clk_change_blackout_ns / 1000.0;
	if (dmub_bb_params->type_b_ppt_blackout_ns > 0)
		soc_bb->power_management_parameters.type_b_ppt_blackout_us =
			(double) dmub_bb_params->type_b_ppt_blackout_ns / 1000.0;
	if (dmub_bb_params->vmin_limit_dispclk_khz > 0)
		soc_bb->vmin_limit.dispclk_khz = dmub_bb_params->vmin_limit_dispclk_khz;
	if (dmub_bb_params->vmin_limit_dcfclk_khz > 0)
		soc_bb->vmin_limit.dcfclk_khz = dmub_bb_params->vmin_limit_dcfclk_khz;
	if (dmub_bb_params->g7_temperature_read_blackout_ns > 0)
		soc_bb->power_management_parameters.g7_temperature_read_blackout_us =
				(double) dmub_bb_params->g7_temperature_read_blackout_ns / 1000.0;

	/* populate alt-channel info */
	for (i = 0; i < 2; i++) {
		/* find the minimum carveout size (expected to be the same for all) */
		if (min_alt_ch_carveout_size_mb > config->alt_ch_cfg.region_size_bytes[i] >> 20) {
			min_alt_ch_carveout_size_mb = (config->alt_ch_cfg.region_size_bytes[i] >> 20);
		}
	}

	if (min_alt_ch_carveout_size_mb > 0)
		soc_bb->power_management_parameters.alternate_dram_carveout_size_mb =
			min_alt_ch_carveout_size_mb;
}

static void apply_soc_bb_updates(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config)
{
	/* Individual modification can be overwritten even if it was obtained by a previous function.
	 * Modifications are acquired in order of priority (lowest to highest).
	 */
	dc_assert_fp_enabled();

	dcn60_update_soc_bb_with_values_from_dmub(soc_bb, config);
	dcn401_update_soc_bb_with_values_from_clk_mgr(soc_bb, dc, config);
	dcn401_update_soc_bb_with_values_from_vbios(soc_bb, dc);
	dcn401_update_soc_bb_with_values_from_software_policy(soc_bb, dc);
}

static void dcn60_get_soc_bb(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config)
{
	//get default soc_bb with static values
	dcn6_test_initialize_soc_bb(soc_bb);
	//get default soc_bb with static values
	apply_soc_bb_updates(soc_bb, dc, config);

}

static void dcn60_get_ip_caps(struct dml2_ip_capabilities *ip_caps)
{
	dcn6_test_initialize_ip_caps(ip_caps);
}

static struct soc_and_ip_translator_funcs dcn60_translator_funcs = {
	.get_soc_bb  = dcn60_get_soc_bb,
	.get_ip_caps = dcn60_get_ip_caps,
};

void dcn60_construct_soc_and_ip_translator(struct soc_and_ip_translator *soc_and_ip_translator)
{
	soc_and_ip_translator->translator_funcs = &dcn60_translator_funcs;
}

