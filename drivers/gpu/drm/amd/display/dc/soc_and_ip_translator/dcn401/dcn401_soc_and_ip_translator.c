// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dcn401_soc_and_ip_translator.h"
#include "bounding_boxes/dcn4_soc_bb.h"

/* soc_and_ip_translator component used to get up-to-date values for bounding box.
 * Bounding box values are stored in several locations and locations can vary with DCN revision.
 * This component provides an interface to get DCN-specific bounding box values.
 */

static void get_default_soc_bb(struct dml2_soc_bb *soc_bb)
{
	memcpy(soc_bb, &dml2_socbb_dcn401, sizeof(struct dml2_soc_bb));
	memcpy(&soc_bb->qos_parameters, &dml_dcn4_variant_a_soc_qos_params, sizeof(struct dml2_soc_qos_parameters));
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
static void dcn401_convert_dc_clock_table_to_soc_bb_clock_table(
		struct dml2_soc_state_table *dml_clk_table,
		const struct clk_bw_params *dc_bw_params,
		bool use_clock_dc_limits)
{
	int i;
	const struct clk_limit_table *dc_clk_table;

	if (dc_bw_params == NULL)
		/* skip if bw params could not be obtained from smu */
		return;

	dc_clk_table = &dc_bw_params->clk_table;

	/* dcfclk */
	if (dc_clk_table->num_entries_per_clk.num_dcfclk_levels) {
		dml_clk_table->dcfclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_dcfclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dml_clk_table->dcfclk.num_clk_values) {
				if (use_clock_dc_limits && dc_bw_params->dc_mode_limit.dcfclk_mhz &&
						dc_clk_table->entries[i].dcfclk_mhz > dc_bw_params->dc_mode_limit.dcfclk_mhz) {
					if (i == 0 || dc_clk_table->entries[i-1].dcfclk_mhz < dc_bw_params->dc_mode_limit.dcfclk_mhz) {
						dml_clk_table->dcfclk.clk_values_khz[i] = dc_bw_params->dc_mode_limit.dcfclk_mhz * 1000;
						dml_clk_table->dcfclk.num_clk_values = i + 1;
					} else {
						dml_clk_table->dcfclk.clk_values_khz[i] = 0;
						dml_clk_table->dcfclk.num_clk_values = i;
					}
				} else {
					dml_clk_table->dcfclk.clk_values_khz[i] = dc_clk_table->entries[i].dcfclk_mhz * 1000;
				}
			} else {
				dml_clk_table->dcfclk.clk_values_khz[i] = 0;
			}
		}
	}

	/* fclk */
	if (dc_clk_table->num_entries_per_clk.num_fclk_levels) {
		dml_clk_table->fclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_fclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dml_clk_table->fclk.num_clk_values) {
				if (use_clock_dc_limits && dc_bw_params->dc_mode_limit.fclk_mhz &&
						dc_clk_table->entries[i].fclk_mhz > dc_bw_params->dc_mode_limit.fclk_mhz) {
					if (i == 0 || dc_clk_table->entries[i-1].fclk_mhz < dc_bw_params->dc_mode_limit.fclk_mhz) {
						dml_clk_table->fclk.clk_values_khz[i] = dc_bw_params->dc_mode_limit.fclk_mhz * 1000;
						dml_clk_table->fclk.num_clk_values = i + 1;
					} else {
						dml_clk_table->fclk.clk_values_khz[i] = 0;
						dml_clk_table->fclk.num_clk_values = i;
					}
				} else {
					dml_clk_table->fclk.clk_values_khz[i] = dc_clk_table->entries[i].fclk_mhz * 1000;
				}
			} else {
				dml_clk_table->fclk.clk_values_khz[i] = 0;
			}
		}
	}

	/* uclk */
	if (dc_clk_table->num_entries_per_clk.num_memclk_levels) {
		dml_clk_table->uclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_memclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dml_clk_table->uclk.num_clk_values) {
				if (use_clock_dc_limits && dc_bw_params->dc_mode_limit.memclk_mhz &&
						dc_clk_table->entries[i].memclk_mhz > dc_bw_params->dc_mode_limit.memclk_mhz) {
					if (i == 0 || dc_clk_table->entries[i-1].memclk_mhz < dc_bw_params->dc_mode_limit.memclk_mhz) {
						dml_clk_table->uclk.clk_values_khz[i] = dc_bw_params->dc_mode_limit.memclk_mhz * 1000;
						dml_clk_table->uclk.num_clk_values = i + 1;
					} else {
						dml_clk_table->uclk.clk_values_khz[i] = 0;
						dml_clk_table->uclk.num_clk_values = i;
					}
				} else {
					dml_clk_table->uclk.clk_values_khz[i] = dc_clk_table->entries[i].memclk_mhz * 1000;
				}
			} else {
				dml_clk_table->uclk.clk_values_khz[i] = 0;
			}
		}
	}

	/* dispclk */
	if (dc_clk_table->num_entries_per_clk.num_dispclk_levels) {
		dml_clk_table->dispclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_dispclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dml_clk_table->dispclk.num_clk_values) {
				if (use_clock_dc_limits && dc_bw_params->dc_mode_limit.dispclk_mhz &&
						dc_clk_table->entries[i].dispclk_mhz > dc_bw_params->dc_mode_limit.dispclk_mhz) {
					if (i == 0 || dc_clk_table->entries[i-1].dispclk_mhz < dc_bw_params->dc_mode_limit.dispclk_mhz) {
						dml_clk_table->dispclk.clk_values_khz[i] = dc_bw_params->dc_mode_limit.dispclk_mhz * 1000;
						dml_clk_table->dispclk.num_clk_values = i + 1;
					} else {
						dml_clk_table->dispclk.clk_values_khz[i] = 0;
						dml_clk_table->dispclk.num_clk_values = i;
					}
				} else {
					dml_clk_table->dispclk.clk_values_khz[i] = dc_clk_table->entries[i].dispclk_mhz * 1000;
				}
			} else {
				dml_clk_table->dispclk.clk_values_khz[i] = 0;
			}
		}
	}

	/* dppclk */
	if (dc_clk_table->num_entries_per_clk.num_dppclk_levels) {
		dml_clk_table->dppclk.num_clk_values = dc_clk_table->num_entries_per_clk.num_dppclk_levels;
		for (i = 0; i < min(DML_MAX_CLK_TABLE_SIZE, MAX_NUM_DPM_LVL); i++) {
			if (i < dml_clk_table->dppclk.num_clk_values) {
				if (use_clock_dc_limits && dc_bw_params->dc_mode_limit.dppclk_mhz &&
						dc_clk_table->entries[i].dppclk_mhz > dc_bw_params->dc_mode_limit.dppclk_mhz) {
					if (i == 0 || dc_clk_table->entries[i-1].dppclk_mhz < dc_bw_params->dc_mode_limit.dppclk_mhz) {
						dml_clk_table->dppclk.clk_values_khz[i] = dc_bw_params->dc_mode_limit.dppclk_mhz * 1000;
						dml_clk_table->dppclk.num_clk_values = i + 1;
					} else {
						dml_clk_table->dppclk.clk_values_khz[i] = 0;
						dml_clk_table->dppclk.num_clk_values = i;
					}
				} else {
					dml_clk_table->dppclk.clk_values_khz[i] = dc_clk_table->entries[i].dppclk_mhz * 1000;
				}
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
				if (use_clock_dc_limits && dc_bw_params->dc_mode_limit.dtbclk_mhz &&
						dc_clk_table->entries[i].dtbclk_mhz > dc_bw_params->dc_mode_limit.dtbclk_mhz) {
					if (i == 0 || dc_clk_table->entries[i-1].dtbclk_mhz < dc_bw_params->dc_mode_limit.dtbclk_mhz) {
						dml_clk_table->dtbclk.clk_values_khz[i] = dc_bw_params->dc_mode_limit.dtbclk_mhz * 1000;
						dml_clk_table->dtbclk.num_clk_values = i + 1;
					} else {
						dml_clk_table->dtbclk.clk_values_khz[i] = 0;
						dml_clk_table->dtbclk.num_clk_values = i;
					}
				} else {
					dml_clk_table->dtbclk.clk_values_khz[i] = dc_clk_table->entries[i].dtbclk_mhz * 1000;
				}
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
				if (use_clock_dc_limits && dc_bw_params->dc_mode_limit.socclk_mhz &&
						dc_clk_table->entries[i].socclk_mhz > dc_bw_params->dc_mode_limit.socclk_mhz) {
					if (i == 0 || dc_clk_table->entries[i-1].socclk_mhz < dc_bw_params->dc_mode_limit.socclk_mhz) {
						dml_clk_table->socclk.clk_values_khz[i] = dc_bw_params->dc_mode_limit.socclk_mhz * 1000;
						dml_clk_table->socclk.num_clk_values = i + 1;
					} else {
						dml_clk_table->socclk.clk_values_khz[i] = 0;
						dml_clk_table->socclk.num_clk_values = i;
					}
				} else {
					dml_clk_table->socclk.clk_values_khz[i] = dc_clk_table->entries[i].socclk_mhz * 1000;
				}
			} else {
				dml_clk_table->socclk.clk_values_khz[i] = 0;
			}
		}
	}

	/* dram config */
	dml_clk_table->dram_config.channel_count = dc_bw_params->num_channels;
	dml_clk_table->dram_config.channel_width_bytes = dc_bw_params->dram_channel_width_bytes;
}

void dcn401_update_soc_bb_with_values_from_clk_mgr(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config)
{
	soc_bb->dprefclk_mhz = dc->clk_mgr->dprefclk_khz / 1000;
	soc_bb->dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;
	soc_bb->mall_allocated_for_dcn_mbytes = dc->caps.mall_size_total / (1024 * 1024);

	if (dc->clk_mgr->funcs->is_smu_present &&
			dc->clk_mgr->funcs->is_smu_present(dc->clk_mgr)) {
		dcn401_convert_dc_clock_table_to_soc_bb_clock_table(&soc_bb->clk_table,
			dc->clk_mgr->bw_params,
			config->use_clock_dc_limits);
	}
}

void dcn401_update_soc_bb_with_values_from_vbios(struct dml2_soc_bb *soc_bb, const struct dc *dc)
{
	soc_bb->dchub_refclk_mhz = dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000;
	soc_bb->xtalclk_mhz = dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency / 1000;

	/* latencies in vbios are platform specific and should be used if provided */
	if (dc->ctx->dc_bios->bb_info.dram_clock_change_latency_100ns)
		soc_bb->power_management_parameters.dram_clk_change_blackout_us =
				dc->ctx->dc_bios->bb_info.dram_clock_change_latency_100ns / 10.0;

	if (dc->ctx->dc_bios->bb_info.dram_sr_enter_exit_latency_100ns)
		soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us =
				dc->ctx->dc_bios->bb_info.dram_sr_enter_exit_latency_100ns / 10.0;

	if (dc->ctx->dc_bios->bb_info.dram_sr_exit_latency_100ns)
		soc_bb->power_management_parameters.stutter_exit_latency_us =
			dc->ctx->dc_bios->bb_info.dram_sr_exit_latency_100ns / 10.0;
}

void dcn401_update_soc_bb_with_values_from_software_policy(struct dml2_soc_bb *soc_bb, const struct dc *dc)
{
	/* set if the value is provided */
	if (dc->bb_overrides.sr_exit_time_ns)
		soc_bb->power_management_parameters.stutter_exit_latency_us =
				dc->bb_overrides.sr_exit_time_ns / 1000.0;

	if (dc->bb_overrides.sr_enter_plus_exit_time_ns)
		soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us =
				dc->bb_overrides.sr_enter_plus_exit_time_ns / 1000.0;

	if (dc->bb_overrides.dram_clock_change_latency_ns)
		soc_bb->power_management_parameters.dram_clk_change_blackout_us =
				dc->bb_overrides.dram_clock_change_latency_ns / 1000.0;

	if (dc->bb_overrides.fclk_clock_change_latency_ns)
		soc_bb->power_management_parameters.fclk_change_blackout_us =
				dc->bb_overrides.fclk_clock_change_latency_ns / 1000.0;

	//Z8 values not expected nor used on DCN401 but still added for completeness
	if (dc->bb_overrides.sr_exit_z8_time_ns)
		soc_bb->power_management_parameters.z8_stutter_exit_latency_us =
				dc->bb_overrides.sr_exit_z8_time_ns / 1000.0;

	if (dc->bb_overrides.sr_enter_plus_exit_z8_time_ns)
		soc_bb->power_management_parameters.z8_stutter_enter_plus_exit_latency_us =
				dc->bb_overrides.sr_enter_plus_exit_z8_time_ns / 1000.0;
}

static void apply_soc_bb_updates(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config)
{
	/* Individual modification can be overwritten even if it was obtained by a previous function.
	 * Modifications are acquired in order of priority (lowest to highest).
	 */
	dc_assert_fp_enabled();

	dcn401_update_soc_bb_with_values_from_clk_mgr(soc_bb, dc, config);
	dcn401_update_soc_bb_with_values_from_vbios(soc_bb, dc);
	dcn401_update_soc_bb_with_values_from_software_policy(soc_bb, dc);
}

void dcn401_get_soc_bb(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config)
{
	//get default soc_bb with static values
	get_default_soc_bb(soc_bb);
	//update soc_bb values with more accurate values
	apply_soc_bb_updates(soc_bb, dc, config);
}

static void dcn401_get_ip_caps(struct dml2_ip_capabilities *ip_caps)
{
	*ip_caps = dml2_dcn401_max_ip_caps;
}

static struct soc_and_ip_translator_funcs dcn401_translator_funcs = {
	.get_soc_bb = dcn401_get_soc_bb,
	.get_ip_caps = dcn401_get_ip_caps,
};

void dcn401_construct_soc_and_ip_translator(struct soc_and_ip_translator *soc_and_ip_translator)
{
	soc_and_ip_translator->translator_funcs = &dcn401_translator_funcs;
}
