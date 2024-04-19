// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#include "dml2_pmo_factory.h"
#include "dml2_pmo_dcn4.h"

static const int MIN_VACTIVE_MARGIN_US = 100; // We need more than non-zero margin because DET buffer granularity can alter vactive latency hiding
static const int SUBVP_DRR_MARGIN_US = 100;

static const enum dml2_pmo_pstate_strategy full_strategy_list_1_display[][4] = {
	// VActive Preferred
	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },

	// Then SVP
	{ dml2_pmo_pstate_strategy_fw_svp, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },

	// Then VBlank
	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },

	// Finally DRR
	{ dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
};

static const int full_strategy_list_1_display_size = sizeof(full_strategy_list_1_display) / (sizeof(enum dml2_pmo_pstate_strategy) * 4);

static const enum dml2_pmo_pstate_strategy full_strategy_list_2_display[][4] = {
	// VActive only is preferred
	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },

	// Then VActive + VBlank
	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },

	// Then VBlank only
	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },

	// Then SVP + VBlank
	{ dml2_pmo_pstate_strategy_fw_svp, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_fw_svp, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },

	// Then SVP + SVP
	{ dml2_pmo_pstate_strategy_fw_svp, dml2_pmo_pstate_strategy_fw_svp, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },

	// Finally DRR + DRR
	{ dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_na, dml2_pmo_pstate_strategy_na },
};

static const int full_strategy_list_2_display_size = sizeof(full_strategy_list_2_display) / (sizeof(enum dml2_pmo_pstate_strategy) * 4);

static const enum dml2_pmo_pstate_strategy full_strategy_list_3_display[][4] = {
	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na }, // All VActive

	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na },  // VActive + 1 VBlank
	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na },
	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na },

//	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na },	// VActive + 2 VBlank
//	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na },
//	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na },

//	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na }, // VActive + 3 VBlank
//	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na },
//	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_na },

	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_na }, // All VBlank

	{ dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_na }, // All DRR
};

static const int full_strategy_list_3_display_size = sizeof(full_strategy_list_3_display) / (sizeof(enum dml2_pmo_pstate_strategy) * 4);

static const enum dml2_pmo_pstate_strategy full_strategy_list_4_display[][4] = {
	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive }, // All VActive

	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive },  // VActive + 1 VBlank
	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive },
	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive },
	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank },

//	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive },	// VActive + 2 VBlank
//	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive },
//	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank },
//	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive },
//	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank },
//	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank },

//	{ dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank }, // VActive + 3 VBlank
//	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank },
//	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive, dml2_pmo_pstate_strategy_vblank },
//	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vactive },

	{ dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank, dml2_pmo_pstate_strategy_vblank }, // All Vblank

	{ dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr, dml2_pmo_pstate_strategy_fw_drr }, // All DRR
};

static const int full_strategy_list_4_display_size = sizeof(full_strategy_list_4_display) / (sizeof(enum dml2_pmo_pstate_strategy) * 4);

static bool increase_odm_combine_factor(enum dml2_odm_mode *odm_mode, int odms_calculated)
{
	bool result = true;

	if (*odm_mode == dml2_odm_mode_auto) {
		switch (odms_calculated) {
		case 1:
			*odm_mode = dml2_odm_mode_bypass;
			break;
		case 2:
			*odm_mode = dml2_odm_mode_combine_2to1;
			break;
		case 3:
			*odm_mode = dml2_odm_mode_combine_3to1;
			break;
		case 4:
			*odm_mode = dml2_odm_mode_combine_4to1;
			break;
		default:
			result = false;
			break;
		}
	}

	if (result) {
		if (*odm_mode == dml2_odm_mode_bypass) {
			*odm_mode = dml2_odm_mode_combine_2to1;
		} else if (*odm_mode == dml2_odm_mode_combine_2to1) {
			*odm_mode = dml2_odm_mode_combine_3to1;
		} else if (*odm_mode == dml2_odm_mode_combine_3to1) {
			*odm_mode = dml2_odm_mode_combine_4to1;
		} else {
			result = false;
		}
	}

	return result;
}

static bool increase_mpc_combine_factor(unsigned int *mpc_combine_factor, unsigned int limit)
{
	if (*mpc_combine_factor < limit) {
		(*mpc_combine_factor)++;
		return true;
	}

	return false;
}

static int count_planes_with_stream_index(const struct dml2_display_cfg *display_cfg, unsigned int stream_index)
{
	unsigned int i;
	int count;

	count = 0;
	for (i = 0; i < display_cfg->num_planes; i++) {
		if (display_cfg->plane_descriptors[i].stream_index == stream_index)
			count++;
	}

	return count;
}

static bool optimize_dcc_mcache_no_odm(struct dml2_pmo_optimize_dcc_mcache_in_out *in_out,
	int free_pipes)
{
	struct dml2_pmo_instance *pmo = in_out->instance;

	unsigned int i;
	bool result = true;

	for (i = 0; i < in_out->optimized_display_cfg->num_planes; i++) {
		// For pipes that failed dcc mcache check, we want to increase the pipe count.
		// The logic for doing this depends on how many pipes is already being used,
		// and whether it's mpcc or odm combine.
		if (!in_out->dcc_mcache_supported[i]) {
			// For the general case of "n displays", we can only optimize streams with an ODM combine factor of 1
			if (in_out->cfg_support_info->stream_support_info[in_out->optimized_display_cfg->plane_descriptors[i].stream_index].odms_used == 1) {
				in_out->optimized_display_cfg->plane_descriptors[i].overrides.mpcc_combine_factor =
					in_out->cfg_support_info->plane_support_info[i].dpps_used;
				// For each plane that is not passing mcache validation, just add another pipe to it, up to the limit.
				if (free_pipes > 0) {
					if (!increase_mpc_combine_factor(&in_out->optimized_display_cfg->plane_descriptors[i].overrides.mpcc_combine_factor,
						pmo->mpc_combine_limit)) {
						// We've reached max pipes allocatable to a single plane, so we fail.
						result = false;
						break;
					} else {
						// Successfully added another pipe to this failing plane.
						free_pipes--;
					}
				} else {
					// No free pipes to add.
					result = false;
					break;
				}
			} else {
				// If the stream of this plane needs ODM combine, no further optimization can be done.
				result = false;
				break;
			}
		}
	}

	return result;
}

bool pmo_dcn4_optimize_dcc_mcache(struct dml2_pmo_optimize_dcc_mcache_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;

	unsigned int i, used_pipes, free_pipes, planes_on_stream;
	bool result;

	if (in_out->display_config != in_out->optimized_display_cfg) {
		memcpy(in_out->optimized_display_cfg, in_out->display_config, sizeof(struct dml2_display_cfg));
	}

	//Count number of free pipes, and check if any odm combine is in use.
	used_pipes = 0;
	for (i = 0; i < in_out->optimized_display_cfg->num_planes; i++) {
		used_pipes += in_out->cfg_support_info->plane_support_info[i].dpps_used;
	}
	free_pipes = pmo->ip_caps->pipe_count - used_pipes;

	// Optimization loop
	// The goal here is to add more pipes to any planes
	// which are failing mcache admissibility
	result = true;

	// The optimization logic depends on whether ODM combine is enabled, and the stream count.
	if (in_out->optimized_display_cfg->num_streams > 1) {
		// If there are multiple streams, we are limited to only be able to optimize mcache failures on planes
		// which are not ODM combined.

		result = optimize_dcc_mcache_no_odm(in_out, free_pipes);
	} else if (in_out->optimized_display_cfg->num_streams == 1) {
		// In single stream cases, we still optimize mcache failures when there's ODM combine with some
		// additional logic.

		if (in_out->cfg_support_info->stream_support_info[0].odms_used > 1) {
			// If ODM combine is enabled, then the logic is to increase ODM combine factor.

			// Optimization for streams with > 1 ODM combine factor is only supported for single display.
			planes_on_stream = count_planes_with_stream_index(in_out->optimized_display_cfg, 0);

			for (i = 0; i < in_out->optimized_display_cfg->num_planes; i++) {
				// For pipes that failed dcc mcache check, we want to increase the pipe count.
				// The logic for doing this depends on how many pipes is already being used,
				// and whether it's mpcc or odm combine.
				if (!in_out->dcc_mcache_supported[i]) {
					// Increasing ODM combine factor on a stream requires a free pipe for each plane on the stream.
					if (free_pipes >= planes_on_stream) {
						if (!increase_odm_combine_factor(&in_out->optimized_display_cfg->stream_descriptors[i].overrides.odm_mode,
							in_out->cfg_support_info->plane_support_info[i].dpps_used)) {
							result = false;
						} else {
							free_pipes -= planes_on_stream;
							break;
						}
					} else {
						result = false;
						break;
					}
				}
			}
		} else {
			// If ODM combine is not enabled, then we can actually use the same logic as before.

			result = optimize_dcc_mcache_no_odm(in_out, free_pipes);
		}
	} else {
		result = true;
	}

	return result;
}

bool pmo_dcn4_initialize(struct dml2_pmo_initialize_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;

	pmo->soc_bb = in_out->soc_bb;
	pmo->ip_caps = in_out->ip_caps;
	pmo->mpc_combine_limit = 2;
	pmo->odm_combine_limit = 4;
	pmo->min_clock_table_size = in_out->min_clock_table_size;

	pmo->fams_params.v1.subvp.fw_processing_delay_us = 10;
	pmo->fams_params.v1.subvp.prefetch_end_to_mall_start_us = 50;
	pmo->fams_params.v1.subvp.refresh_rate_limit_max = 175;
	pmo->fams_params.v1.subvp.refresh_rate_limit_min = 0;

	pmo->options = in_out->options;

	return true;
}

static bool is_h_timing_divisible_by(const struct dml2_timing_cfg *timing, unsigned char denominator)
{
	/*
	 * Htotal, Hblank start/end, and Hsync start/end all must be divisible
	 * in order for the horizontal timing params to be considered divisible
	 * by 2. Hsync start is always 0.
	 */
	unsigned long h_blank_start = timing->h_total - timing->h_front_porch;

	return (timing->h_total % denominator == 0) &&
			(h_blank_start % denominator == 0) &&
			(timing->h_blank_end % denominator == 0) &&
			(timing->h_sync_width % denominator == 0);
}

static bool is_dp_encoder(enum dml2_output_encoder_class encoder_type)
{
	switch (encoder_type) {
	case dml2_dp:
	case dml2_edp:
	case dml2_dp2p0:
	case dml2_none:
		return true;
	case dml2_hdmi:
	case dml2_hdmifrl:
	default:
		return false;
	}
}

bool pmo_dcn4_init_for_vmin(struct dml2_pmo_init_for_vmin_in_out *in_out)
{
	unsigned int i;
	const struct dml2_display_cfg *display_config =
			&in_out->base_display_config->display_config;
	const struct dml2_core_mode_support_result *mode_support_result =
			&in_out->base_display_config->mode_support_result;

	if (in_out->instance->options->disable_dyn_odm ||
			(in_out->instance->options->disable_dyn_odm_for_multi_stream && display_config->num_streams > 1))
		return false;

	for (i = 0; i < display_config->num_planes; i++)
		/*
		 * vmin optimization is required to be seamlessly switched off
		 * at any time when the new configuration is no longer
		 * supported. However switching from ODM combine to MPC combine
		 * is not always seamless. When there not enough free pipes, we
		 * will have to use the same secondary OPP heads as secondary
		 * DPP pipes in MPC combine in new state. This transition is
		 * expected to cause glitches. To avoid the transition, we only
		 * allow vmin optimization if the stream's base configuration
		 * doesn't require MPC combine. This condition checks if MPC
		 * combine is enabled. If so do not optimize the stream.
		 */
		if (mode_support_result->cfg_support_info.plane_support_info[i].dpps_used > 1 &&
				mode_support_result->cfg_support_info.stream_support_info[display_config->plane_descriptors[i].stream_index].odms_used == 1)
			in_out->base_display_config->stage4.unoptimizable_streams[display_config->plane_descriptors[i].stream_index] = true;

	for (i = 0; i < display_config->num_streams; i++) {
		if (display_config->stream_descriptors[i].overrides.disable_dynamic_odm)
			in_out->base_display_config->stage4.unoptimizable_streams[i] = true;
		else if (in_out->base_display_config->stage3.stream_svp_meta[i].valid &&
				in_out->instance->options->disable_dyn_odm_for_stream_with_svp)
			in_out->base_display_config->stage4.unoptimizable_streams[i] = true;
		/*
		 * ODM Combine requires horizontal timing divisible by 2 so each
		 * ODM segment has the same size.
		 */
		else if (!is_h_timing_divisible_by(&display_config->stream_descriptors[i].timing, 2))
			in_out->base_display_config->stage4.unoptimizable_streams[i] = true;
		/*
		 * Our hardware support seamless ODM transitions for DP encoders
		 * only.
		 */
		else if (!is_dp_encoder(display_config->stream_descriptors[i].output.output_encoder))
			in_out->base_display_config->stage4.unoptimizable_streams[i] = true;
	}

	return true;
}

bool pmo_dcn4_test_for_vmin(struct dml2_pmo_test_for_vmin_in_out *in_out)
{
	bool is_vmin = true;

	if (in_out->vmin_limits->dispclk_khz > 0 &&
		in_out->display_config->mode_support_result.global.dispclk_khz > in_out->vmin_limits->dispclk_khz)
		is_vmin = false;

	return is_vmin;
}

static int find_highest_odm_load_stream_index(
		const struct dml2_display_cfg *display_config,
		const struct dml2_core_mode_support_result *mode_support_result)
{
	unsigned int i;
	int odm_load, highest_odm_load = -1, highest_odm_load_index = -1;

	for (i = 0; i < display_config->num_streams; i++) {
		odm_load = display_config->stream_descriptors[i].timing.pixel_clock_khz
				/ mode_support_result->cfg_support_info.stream_support_info[i].odms_used;
		if (odm_load > highest_odm_load) {
			highest_odm_load_index = i;
			highest_odm_load = odm_load;
		}
	}

	return highest_odm_load_index;
}

bool pmo_dcn4_optimize_for_vmin(struct dml2_pmo_optimize_for_vmin_in_out *in_out)
{
	int stream_index;
	const struct dml2_display_cfg *display_config =
			&in_out->base_display_config->display_config;
	const struct dml2_core_mode_support_result *mode_support_result =
			&in_out->base_display_config->mode_support_result;
	unsigned int odms_used;
	struct dml2_stream_parameters *stream_descriptor;
	bool optimizable = false;

	/*
	 * highest odm load stream must be optimizable to continue as dispclk is
	 * bounded by it.
	 */
	stream_index = find_highest_odm_load_stream_index(display_config,
			mode_support_result);

	if (stream_index < 0 ||
			in_out->base_display_config->stage4.unoptimizable_streams[stream_index])
		return false;

	odms_used = mode_support_result->cfg_support_info.stream_support_info[stream_index].odms_used;
	if ((int)odms_used >= in_out->instance->odm_combine_limit)
		return false;

	memcpy(in_out->optimized_display_config,
			in_out->base_display_config,
			sizeof(struct display_configuation_with_meta));

	stream_descriptor = &in_out->optimized_display_config->display_config.stream_descriptors[stream_index];
	while (!optimizable && increase_odm_combine_factor(
			&stream_descriptor->overrides.odm_mode,
			odms_used)) {
		switch (stream_descriptor->overrides.odm_mode) {
		case dml2_odm_mode_combine_2to1:
			optimizable = true;
			break;
		case dml2_odm_mode_combine_3to1:
			/*
			 * In ODM Combine 3:1 OTG_valid_pixel rate is 1/4 of
			 * actual pixel rate. Therefore horizontal timing must
			 * be divisible by 4.
			 */
			if (is_h_timing_divisible_by(&display_config->stream_descriptors[stream_index].timing, 4)) {
				if (mode_support_result->cfg_support_info.stream_support_info[stream_index].dsc_enable) {
					/*
					 * DSC h slice count must be divisible
					 * by 3.
					 */
					if (mode_support_result->cfg_support_info.stream_support_info[stream_index].num_dsc_slices % 3 == 0)
						optimizable = true;
				} else {
					optimizable = true;
				}
			}
			break;
		case dml2_odm_mode_combine_4to1:
			/*
			 * In ODM Combine 4:1 OTG_valid_pixel rate is 1/4 of
			 * actual pixel rate. Therefore horizontal timing must
			 * be divisible by 4.
			 */
			if (is_h_timing_divisible_by(&display_config->stream_descriptors[stream_index].timing, 4)) {
				if (mode_support_result->cfg_support_info.stream_support_info[stream_index].dsc_enable) {
					/*
					 * DSC h slice count must be divisible
					 * by 4.
					 */
					if (mode_support_result->cfg_support_info.stream_support_info[stream_index].num_dsc_slices % 4 == 0)
						optimizable = true;
				} else {
					optimizable = true;
				}
			}
			break;
		case dml2_odm_mode_auto:
		case dml2_odm_mode_bypass:
		case dml2_odm_mode_split_1to2:
		case dml2_odm_mode_mso_1to2:
		case dml2_odm_mode_mso_1to4:
		default:
			break;
		}
	}

	return optimizable;
}

static bool are_timings_trivially_synchronizable(const struct display_configuation_with_meta *display_config, int mask)
{
	unsigned int i;
	bool identical = true;
	bool contains_drr = false;
	unsigned int remap_array[DML2_MAX_PLANES];
	unsigned int remap_array_size = 0;

	// Create a remap array to enable simple iteration through only masked stream indicies
	for (i = 0; i < display_config->display_config.num_streams; i++) {
		if (mask & (0x1 << i)) {
			remap_array[remap_array_size++] = i;
		}
	}

	// 0 or 1 display is always trivially synchronizable
	if (remap_array_size <= 1)
		return true;

	for (i = 1; i < remap_array_size; i++) {
		if (memcmp(&display_config->display_config.stream_descriptors[remap_array[i - 1]].timing,
			&display_config->display_config.stream_descriptors[remap_array[i]].timing,
			sizeof(struct dml2_timing_cfg))) {
			identical = false;
			break;
		}
	}

	for (i = 0; i < remap_array_size; i++) {
		if (display_config->display_config.stream_descriptors[remap_array[i]].timing.drr_config.enabled) {
			contains_drr = true;
			break;
		}
	}

	return !contains_drr && identical;
}

static void set_bit_in_bitfield(unsigned int *bit_field, unsigned int bit_offset)
{
	*bit_field = *bit_field | (0x1 << bit_offset);
}

static bool is_bit_set_in_bitfield(unsigned int bit_field, unsigned int bit_offset)
{
	if (bit_field & (0x1 << bit_offset))
		return true;

	return false;
}

static bool are_all_timings_drr_enabled(const struct display_configuation_with_meta *display_config, int mask)
{
	unsigned char i;
	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(mask, i)) {
			if (!display_config->display_config.stream_descriptors[i].timing.drr_config.enabled)
				return false;
		}
	}

	return true;
}

static void insert_into_candidate_list(const enum dml2_pmo_pstate_strategy *per_stream_pstate_strategy, int stream_count, struct dml2_pmo_scratch *scratch)
{
	int stream_index;

	scratch->pmo_dcn4.allow_state_increase_for_strategy[scratch->pmo_dcn4.num_pstate_candidates] = true;

	for (stream_index = 0; stream_index < stream_count; stream_index++) {
		scratch->pmo_dcn4.per_stream_pstate_strategy[scratch->pmo_dcn4.num_pstate_candidates][stream_index] = per_stream_pstate_strategy[stream_index];

		if (per_stream_pstate_strategy[stream_index] == dml2_pmo_pstate_strategy_vblank)
			scratch->pmo_dcn4.allow_state_increase_for_strategy[scratch->pmo_dcn4.num_pstate_candidates] = false;
	}

	scratch->pmo_dcn4.num_pstate_candidates++;
}

static bool all_planes_match_strategy(const struct display_configuation_with_meta *display_cfg, int plane_mask, enum dml2_pmo_pstate_strategy strategy)
{
	unsigned char i;
	enum dml2_uclk_pstate_change_strategy matching_strategy = (enum dml2_uclk_pstate_change_strategy) dml2_pmo_pstate_strategy_na;

	if (strategy == dml2_pmo_pstate_strategy_vactive)
		matching_strategy = dml2_uclk_pstate_change_strategy_force_vactive;
	else if (strategy == dml2_pmo_pstate_strategy_vblank)
		matching_strategy = dml2_uclk_pstate_change_strategy_force_vblank;
	else if (strategy == dml2_pmo_pstate_strategy_fw_svp)
		matching_strategy = dml2_uclk_pstate_change_strategy_force_mall_svp;
	else if (strategy == dml2_pmo_pstate_strategy_fw_drr)
		matching_strategy = dml2_uclk_pstate_change_strategy_force_drr;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (display_cfg->display_config.plane_descriptors[i].overrides.uclk_pstate_change_strategy != dml2_uclk_pstate_change_strategy_auto &&
				display_cfg->display_config.plane_descriptors[i].overrides.uclk_pstate_change_strategy != matching_strategy)
				return false;
		}
	}

	return true;
}

static bool subvp_subvp_schedulable(struct dml2_pmo_instance *pmo, const struct display_configuation_with_meta *display_cfg,
	unsigned int *svp_stream_indicies, int svp_stream_count)
{
	struct dml2_pmo_scratch *s = &pmo->scratch;
	int i;
	int microschedule_lines, time_us, refresh_hz;
	int max_microschedule_us = 0;
	int vactive1_us, vactive2_us, vblank1_us, vblank2_us;

	const struct dml2_timing_cfg *svp_timing1 = 0;
	const struct dml2_implicit_svp_meta *svp_meta1 = 0;

	const struct dml2_timing_cfg *svp_timing2 = 0;

	if (svp_stream_count <= 1)
		return true;
	else if (svp_stream_count > 2)
		return false;

	/* Loop to calculate the maximum microschedule time between the two SubVP pipes,
	 * and also to store the two main SubVP pipe pointers in subvp_pipes[2].
	 */
	for (i = 0; i < svp_stream_count; i++) {
		svp_timing1 = &display_cfg->display_config.stream_descriptors[svp_stream_indicies[i]].timing;
		svp_meta1 = &s->pmo_dcn4.stream_svp_meta[svp_stream_indicies[i]];

		microschedule_lines = svp_meta1->v_active;

		// Round up when calculating microschedule time (+ 1 at the end)
		time_us = (int)((microschedule_lines * svp_timing1->h_total) / (double)(svp_timing1->pixel_clock_khz * 1000) * 1000000 +
			pmo->fams_params.v1.subvp.prefetch_end_to_mall_start_us +	pmo->fams_params.v1.subvp.fw_processing_delay_us + 1);

		if (time_us > max_microschedule_us)
			max_microschedule_us = time_us;

		refresh_hz = (int)((double)(svp_timing1->pixel_clock_khz * 1000) / (svp_timing1->v_total * svp_timing1->h_total));

		if (refresh_hz < pmo->fams_params.v1.subvp.refresh_rate_limit_min ||
			refresh_hz > pmo->fams_params.v1.subvp.refresh_rate_limit_max) {
			return false;
		}
	}

	svp_timing1 = &display_cfg->display_config.stream_descriptors[svp_stream_indicies[0]].timing;
	svp_meta1 = &s->pmo_dcn4.stream_svp_meta[svp_stream_indicies[0]];

	vactive1_us = (int)((svp_timing1->v_active * svp_timing1->h_total) / (double)(svp_timing1->pixel_clock_khz * 1000) * 1000000);

	vblank1_us = (int)(((svp_timing1->v_total - svp_timing1->v_active) * svp_timing1->h_total) / (double)(svp_timing1->pixel_clock_khz * 1000) * 1000000);

	svp_timing2 = &display_cfg->display_config.stream_descriptors[svp_stream_indicies[1]].timing;

	vactive2_us = (int)((svp_timing2->v_active * svp_timing2->h_total) / (double)(svp_timing2->pixel_clock_khz * 1000) * 1000000);

	vblank2_us = (int)(((svp_timing2->v_total - svp_timing2->v_active) * svp_timing2->h_total) / (double)(svp_timing2->pixel_clock_khz * 1000) * 1000000);

	if ((vactive1_us - vblank2_us) / 2 > max_microschedule_us &&
		(vactive2_us - vblank1_us) / 2 > max_microschedule_us)
		return true;

	return false;
}

static bool validate_svp_cofunctionality(struct dml2_pmo_instance *pmo,
	const struct display_configuation_with_meta *display_cfg, int svp_stream_mask)
{
	bool result = false;
	unsigned int stream_index;

	unsigned int svp_stream_indicies[2] = { 0 };
	unsigned int svp_stream_count = 0;

	// Find the SVP streams, store only the first 2, but count all of them
	for (stream_index = 0; stream_index < display_cfg->display_config.num_streams; stream_index++) {
		if (is_bit_set_in_bitfield(svp_stream_mask, stream_index)) {
			if (svp_stream_count < 2)
				svp_stream_indicies[svp_stream_count] = stream_index;

			svp_stream_count++;
		}
	}

	if (svp_stream_count == 1) {
		result = true; // 1 SVP is always co_functional
	} else if (svp_stream_count == 2) {
		result = subvp_subvp_schedulable(pmo, display_cfg, svp_stream_indicies, svp_stream_count);
	}

	return result;
}

static bool validate_drr_cofunctionality(struct dml2_pmo_instance *pmo,
	const struct display_configuation_with_meta *display_cfg, int drr_stream_mask)
{
	unsigned int stream_index;
	int drr_stream_count = 0;

	// Find the SVP streams and count all of them
	for (stream_index = 0; stream_index < display_cfg->display_config.num_streams; stream_index++) {
		if (is_bit_set_in_bitfield(drr_stream_mask, stream_index)) {
			drr_stream_count++;
		}
	}

	return drr_stream_count <= 4;
}

static bool validate_svp_drr_cofunctionality(struct dml2_pmo_instance *pmo,
	const struct display_configuation_with_meta *display_cfg, int svp_stream_mask, int drr_stream_mask)
{
	unsigned int stream_index;
	int drr_stream_count = 0;
	int svp_stream_count = 0;

	int prefetch_us = 0;
	int mall_region_us = 0;
	int drr_frame_us = 0;	// nominal frame time
	int subvp_active_us = 0;
	int stretched_drr_us = 0;
	int drr_stretched_vblank_us = 0;
	int max_vblank_mallregion = 0;

	const struct dml2_timing_cfg *svp_timing = 0;
	const struct dml2_timing_cfg *drr_timing = 0;
	const struct dml2_implicit_svp_meta *svp_meta = 0;

	bool schedulable = false;

	// Find the SVP streams and count all of them
	for (stream_index = 0; stream_index < display_cfg->display_config.num_streams; stream_index++) {
		if (is_bit_set_in_bitfield(svp_stream_mask, stream_index)) {
			svp_timing = &display_cfg->display_config.stream_descriptors[stream_index].timing;
			svp_meta = &pmo->scratch.pmo_dcn4.stream_svp_meta[stream_index];
			svp_stream_count++;
		}
		if (is_bit_set_in_bitfield(drr_stream_mask, stream_index)) {
			drr_timing = &display_cfg->display_config.stream_descriptors[stream_index].timing;
			drr_stream_count++;
		}
	}

	if (svp_stream_count == 1 && drr_stream_count == 1 && svp_timing != drr_timing) {
		prefetch_us = (int)((svp_meta->v_total - svp_meta->v_front_porch)
			* svp_timing->h_total /	(double)(svp_timing->pixel_clock_khz * 1000) * 1000000 +
			pmo->fams_params.v1.subvp.prefetch_end_to_mall_start_us);

		subvp_active_us = (int)(svp_timing->v_active * svp_timing->h_total /
			(double)(svp_timing->pixel_clock_khz * 1000) * 1000000);

		drr_frame_us = (int)(drr_timing->v_total * drr_timing->h_total /
			(double)(drr_timing->pixel_clock_khz * 1000) * 1000000);

		// P-State allow width and FW delays already included phantom_timing->v_addressable
		mall_region_us = (int)(svp_meta->v_active * svp_timing->h_total /
			(double)(svp_timing->pixel_clock_khz * 1000) * 1000000);

		stretched_drr_us = drr_frame_us + mall_region_us + SUBVP_DRR_MARGIN_US;

		drr_stretched_vblank_us = (int)((drr_timing->v_total - drr_timing->v_active) * drr_timing->h_total /
			(double)(drr_timing->pixel_clock_khz * 1000) * 1000000 + (stretched_drr_us - drr_frame_us));

		max_vblank_mallregion = drr_stretched_vblank_us > mall_region_us ? drr_stretched_vblank_us : mall_region_us;

		/* We consider SubVP + DRR schedulable if the stretched frame duration of the DRR display (i.e. the
		 * highest refresh rate + margin that can support UCLK P-State switch) passes the static analysis
		 * for VBLANK: (VACTIVE region of the SubVP pipe can fit the MALL prefetch, VBLANK frame time,
		 * and the max of (VBLANK blanking time, MALL region)).
		 */
		if (stretched_drr_us < (1 / (double)drr_timing->drr_config.min_refresh_uhz) * 1000000 * 1000000 &&
			subvp_active_us - prefetch_us - stretched_drr_us - max_vblank_mallregion > 0)
			schedulable = true;
	}

	return schedulable;
}

static bool validate_svp_vblank_cofunctionality(struct dml2_pmo_instance *pmo,
	const struct display_configuation_with_meta *display_cfg, int svp_stream_mask, int vblank_stream_mask)
{
	unsigned int stream_index;
	int vblank_stream_count = 0;
	int svp_stream_count = 0;

	const struct dml2_timing_cfg *svp_timing = 0;
	const struct dml2_timing_cfg *vblank_timing = 0;
	const struct dml2_implicit_svp_meta *svp_meta = 0;

	int prefetch_us = 0;
	int mall_region_us = 0;
	int vblank_frame_us = 0;
	int subvp_active_us = 0;
	int vblank_blank_us = 0;
	int max_vblank_mallregion = 0;

	bool schedulable = false;

	// Find the SVP streams and count all of them
	for (stream_index = 0; stream_index < display_cfg->display_config.num_streams; stream_index++) {
		if (is_bit_set_in_bitfield(svp_stream_mask, stream_index)) {
			svp_timing = &display_cfg->display_config.stream_descriptors[stream_index].timing;
			svp_meta = &pmo->scratch.pmo_dcn4.stream_svp_meta[stream_index];
			svp_stream_count++;
		}
		if (is_bit_set_in_bitfield(vblank_stream_mask, stream_index)) {
			vblank_timing = &display_cfg->display_config.stream_descriptors[stream_index].timing;
			vblank_stream_count++;
		}
	}

	if (svp_stream_count == 1 && vblank_stream_count > 0) {
		// Prefetch time is equal to VACTIVE + BP + VSYNC of the phantom pipe
		// Also include the prefetch end to mallstart delay time
		prefetch_us = (int)((svp_meta->v_total - svp_meta->v_front_porch) * svp_timing->h_total
			/ (double)(svp_timing->pixel_clock_khz * 1000) * 1000000 +
			pmo->fams_params.v1.subvp.prefetch_end_to_mall_start_us);

		// P-State allow width and FW delays already included phantom_timing->v_addressable
		mall_region_us = (int)(svp_meta->v_active * svp_timing->h_total /
			(double)(svp_timing->pixel_clock_khz * 1000) * 1000000);

		vblank_frame_us = (int)(vblank_timing->v_total * vblank_timing->h_total /
			(double)(vblank_timing->pixel_clock_khz * 1000) * 1000000);

		vblank_blank_us = (int)((vblank_timing->v_total - vblank_timing->v_active) * vblank_timing->h_total /
			(double)(vblank_timing->pixel_clock_khz * 1000) * 1000000);

		subvp_active_us = (int)(svp_timing->v_active * svp_timing->h_total /
			(double)(svp_timing->pixel_clock_khz * 1000) * 1000000);

		max_vblank_mallregion = vblank_blank_us > mall_region_us ? vblank_blank_us : mall_region_us;

		// Schedulable if VACTIVE region of the SubVP pipe can fit the MALL prefetch, VBLANK frame time,
		// and the max of (VBLANK blanking time, MALL region)
		// TODO: Possibly add some margin (i.e. the below conditions should be [...] > X instead of [...] > 0)
		if (subvp_active_us - prefetch_us - vblank_frame_us - max_vblank_mallregion > 0)
			schedulable = true;
	}
	return schedulable;
}

static bool validate_drr_vblank_cofunctionality(struct dml2_pmo_instance *pmo,
	const struct display_configuation_with_meta *display_cfg, int drr_stream_mask, int vblank_stream_mask)
{
	return false;
}

static bool validate_pstate_support_strategy_cofunctionality(struct dml2_pmo_instance *pmo,
	const struct display_configuation_with_meta *display_cfg, const enum dml2_pmo_pstate_strategy per_stream_pstate_strategy[4])
{
	struct dml2_pmo_scratch *s = &pmo->scratch;

	unsigned int stream_index = 0;

	unsigned int svp_count = 0;
	unsigned int svp_stream_mask = 0;
	unsigned int drr_count = 0;
	unsigned int drr_stream_mask = 0;
	unsigned int vactive_count = 0;
	unsigned int vactive_stream_mask = 0;
	unsigned int vblank_count = 0;
	unsigned int vblank_stream_mask = 0;

	bool strategy_matches_forced_requirements = true;

	bool admissible = false;

	// Tabulate everything
	for (stream_index = 0; stream_index < display_cfg->display_config.num_streams; stream_index++) {

		if (!all_planes_match_strategy(display_cfg, s->pmo_dcn4.stream_plane_mask[stream_index],
			per_stream_pstate_strategy[stream_index])) {
			strategy_matches_forced_requirements = false;
			break;
		}

		if (per_stream_pstate_strategy[stream_index] == dml2_pmo_pstate_strategy_fw_svp) {
			svp_count++;
			set_bit_in_bitfield(&svp_stream_mask, stream_index);
		} else if (per_stream_pstate_strategy[stream_index] == dml2_pmo_pstate_strategy_fw_drr) {
			drr_count++;
			set_bit_in_bitfield(&drr_stream_mask, stream_index);
		} else if (per_stream_pstate_strategy[stream_index] == dml2_pmo_pstate_strategy_vactive) {
			vactive_count++;
			set_bit_in_bitfield(&vactive_stream_mask, stream_index);
		} else if (per_stream_pstate_strategy[stream_index] == dml2_pmo_pstate_strategy_vblank) {
			vblank_count++;
			set_bit_in_bitfield(&vblank_stream_mask, stream_index);
		}
	}

	if (!strategy_matches_forced_requirements)
		return false;

	// Check for trivial synchronization for vblank
	if (vblank_count > 0 && (pmo->options->disable_vblank || !are_timings_trivially_synchronizable(display_cfg, vblank_stream_mask)))
		return false;

	if (svp_count > 0 && pmo->options->disable_svp)
		return false;

	if (drr_count > 0 && (pmo->options->disable_drr_var || !are_all_timings_drr_enabled(display_cfg, drr_stream_mask)))
		return false;

	// Validate for FAMS admissibiliy
	if (svp_count == 0 && drr_count == 0) {
		// No FAMS
		admissible = true;
	} else {
		admissible = false;
		if (svp_count > 0 && drr_count == 0 && vactive_count == 0 && vblank_count == 0) {
			// All SVP
			admissible = validate_svp_cofunctionality(pmo, display_cfg, svp_stream_mask);
		} else if (svp_count == 0 && drr_count > 0 && vactive_count == 0 && vblank_count == 0) {
			// All DRR
			admissible = validate_drr_cofunctionality(pmo, display_cfg, drr_stream_mask);
		} else if (svp_count > 0 && drr_count > 0 && vactive_count == 0 && vblank_count == 0) {
			// SVP + DRR
			admissible = validate_svp_drr_cofunctionality(pmo, display_cfg, svp_stream_mask, drr_stream_mask);
		} else if (svp_count > 0 && drr_count == 0 && vactive_count == 0 && vblank_count > 0) {
			// SVP + VBlank
			admissible = validate_svp_vblank_cofunctionality(pmo, display_cfg, svp_stream_mask, vblank_stream_mask);
		} else if (svp_count == 0 && drr_count > 0 && vactive_count == 0 && vblank_count > 0) {
			// DRR + VBlank
			admissible = validate_drr_vblank_cofunctionality(pmo, display_cfg, drr_stream_mask, vblank_stream_mask);
		}
	}

	return admissible;
}

static int get_vactive_pstate_margin(const struct display_configuation_with_meta *display_cfg, int plane_mask)
{
	unsigned char i;
	int min_vactive_margin_us = 0xFFFFFFF;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (display_cfg->mode_support_result.cfg_support_info.plane_support_info[i].dram_change_latency_hiding_margin_in_active < min_vactive_margin_us)
				min_vactive_margin_us = display_cfg->mode_support_result.cfg_support_info.plane_support_info[i].dram_change_latency_hiding_margin_in_active;
		}
	}

	return min_vactive_margin_us;
}

bool pmo_dcn4_init_for_pstate_support(struct dml2_pmo_init_for_pstate_support_in_out *in_out)
{
	struct dml2_pmo_instance *pmo = in_out->instance;
	struct dml2_optimization_stage3_state *state = &in_out->base_display_config->stage3;
	struct dml2_pmo_scratch *s = &pmo->scratch;

	struct display_configuation_with_meta *display_config;
	const struct dml2_plane_parameters *plane_descriptor;
	const enum dml2_pmo_pstate_strategy (*strategy_list)[4] = 0;
	unsigned int strategy_list_size = 0;
	unsigned int plane_index, stream_index, i;

	state->performed = true;

	display_config = in_out->base_display_config;
	display_config->display_config.overrides.enable_subvp_implicit_pmo = true;

	memset(s, 0, sizeof(struct dml2_pmo_scratch));

	pmo->scratch.pmo_dcn4.min_latency_index = in_out->base_display_config->stage1.min_clk_index_for_latency;
	pmo->scratch.pmo_dcn4.max_latency_index = pmo->min_clock_table_size;
	pmo->scratch.pmo_dcn4.cur_latency_index = in_out->base_display_config->stage1.min_clk_index_for_latency;

	// First build the stream plane mask (array of bitfields indexed by stream, indicating plane mapping)
	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		plane_descriptor = &display_config->display_config.plane_descriptors[plane_index];

		set_bit_in_bitfield(&s->pmo_dcn4.stream_plane_mask[plane_descriptor->stream_index], plane_index);

		state->pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_vactive;
	}

	// Figure out which streams can do vactive, and also build up implicit SVP meta
	for (stream_index = 0; stream_index < display_config->display_config.num_streams; stream_index++) {
		if (get_vactive_pstate_margin(display_config, s->pmo_dcn4.stream_plane_mask[stream_index]) >=
			MIN_VACTIVE_MARGIN_US)
			set_bit_in_bitfield(&s->pmo_dcn4.stream_vactive_capability_mask, stream_index);

		s->pmo_dcn4.stream_svp_meta[stream_index].valid = true;
		s->pmo_dcn4.stream_svp_meta[stream_index].v_active =
			display_config->mode_support_result.cfg_support_info.stream_support_info[stream_index].phantom_v_active;
		s->pmo_dcn4.stream_svp_meta[stream_index].v_total =
			display_config->mode_support_result.cfg_support_info.stream_support_info[stream_index].phantom_v_total;
		s->pmo_dcn4.stream_svp_meta[stream_index].v_front_porch = 1;
	}

	switch (display_config->display_config.num_streams) {
	case 1:
		strategy_list = full_strategy_list_1_display;
		strategy_list_size = full_strategy_list_1_display_size;
		break;
	case 2:
		strategy_list = full_strategy_list_2_display;
		strategy_list_size = full_strategy_list_2_display_size;
		break;
	case 3:
		strategy_list = full_strategy_list_3_display;
		strategy_list_size = full_strategy_list_3_display_size;
		break;
	case 4:
		strategy_list = full_strategy_list_4_display;
		strategy_list_size = full_strategy_list_4_display_size;
		break;
	default:
		strategy_list_size = 0;
		break;
	}

	if (strategy_list_size == 0)
		return false;

	s->pmo_dcn4.num_pstate_candidates = 0;

	for (i = 0; i < strategy_list_size && i < DML2_PMO_PSTATE_CANDIDATE_LIST_SIZE; i++) {
		if (validate_pstate_support_strategy_cofunctionality(pmo, display_config, strategy_list[i])) {
			insert_into_candidate_list(strategy_list[i], display_config->display_config.num_streams, s);
		}
	}

	if (s->pmo_dcn4.num_pstate_candidates > 0) {
		// There's this funny case...
		// If the first entry in the candidate list is all vactive, then we can consider it "tested", so the current index is 0
		// Otherwise the current index should be -1 because we run the optimization at least once
		s->pmo_dcn4.cur_pstate_candidate = 0;
		for (i = 0; i < display_config->display_config.num_streams; i++) {
			if (s->pmo_dcn4.per_stream_pstate_strategy[0][i] != dml2_pmo_pstate_strategy_vactive) {
				s->pmo_dcn4.cur_pstate_candidate = -1;
				break;
			}
		}
		return true;
	} else {
		return false;
	}
}

static void reset_display_configuration(struct display_configuation_with_meta *display_config)
{
	unsigned int plane_index;
	unsigned int stream_index;
	struct dml2_plane_parameters *plane;

	for (stream_index = 0; stream_index < display_config->display_config.num_streams; stream_index++) {
		display_config->stage3.stream_svp_meta[stream_index].valid = false;
	}

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		plane = &display_config->display_config.plane_descriptors[plane_index];

		// Unset SubVP
		plane->overrides.legacy_svp_config = dml2_svp_mode_override_auto;

		// Remove reserve time
		plane->overrides.reserved_vblank_time_ns = 0;

		// Reset strategy to auto
		plane->overrides.uclk_pstate_change_strategy = dml2_uclk_pstate_change_strategy_auto;

		display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_not_supported;
	}
}

static void setup_planes_for_drr_by_mask(struct display_configuation_with_meta *display_config, int plane_mask)
{
	unsigned int plane_index;
	struct dml2_plane_parameters *plane;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			plane = &display_config->display_config.plane_descriptors[plane_index];

			// Setup DRR
			plane->overrides.uclk_pstate_change_strategy = dml2_uclk_pstate_change_strategy_force_drr;

			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_fw_drr;
		}
	}
}

static void setup_planes_for_svp_by_mask(struct display_configuation_with_meta *display_config, int plane_mask)
{
	unsigned int plane_index;
	int stream_index = -1;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			stream_index = (char)display_config->display_config.plane_descriptors[plane_index].stream_index;
			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_fw_subvp_phantom;
		}
	}

	if (stream_index >= 0) {
		display_config->stage3.stream_svp_meta[stream_index].valid = true;
		display_config->stage3.stream_svp_meta[stream_index].v_active =
			display_config->mode_support_result.cfg_support_info.stream_support_info[stream_index].phantom_v_active;
		display_config->stage3.stream_svp_meta[stream_index].v_total =
			display_config->mode_support_result.cfg_support_info.stream_support_info[stream_index].phantom_v_total;
		display_config->stage3.stream_svp_meta[stream_index].v_front_porch = 1;
	}
}

static void setup_planes_for_vblank_by_mask(struct display_configuation_with_meta *display_config, int plane_mask)
{
	unsigned int plane_index;
	struct dml2_plane_parameters *plane;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			plane = &display_config->display_config.plane_descriptors[plane_index];

			// Setup reserve time
			plane->overrides.reserved_vblank_time_ns = 400 * 1000;

			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_vblank;
		}
	}
}

static void setup_planes_for_vactive_by_mask(struct display_configuation_with_meta *display_config, int plane_mask)
{
	unsigned int plane_index;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			display_config->stage3.pstate_switch_modes[plane_index] = dml2_uclk_pstate_support_method_vactive;
		}
	}
}

static bool setup_display_config(struct display_configuation_with_meta *display_config, struct dml2_pmo_scratch *scratch, int strategy_index)
{
	bool success = true;
	unsigned int stream_index;

	reset_display_configuration(display_config);

	for (stream_index = 0; stream_index < display_config->display_config.num_streams; stream_index++) {
		if (scratch->pmo_dcn4.per_stream_pstate_strategy[strategy_index][stream_index] == dml2_pmo_pstate_strategy_na) {
			success = false;
			break;
		} else if (scratch->pmo_dcn4.per_stream_pstate_strategy[strategy_index][stream_index] == dml2_pmo_pstate_strategy_vblank) {
			setup_planes_for_vblank_by_mask(display_config, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		} else if (scratch->pmo_dcn4.per_stream_pstate_strategy[strategy_index][stream_index] == dml2_pmo_pstate_strategy_fw_svp) {
			setup_planes_for_svp_by_mask(display_config, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		} else if (scratch->pmo_dcn4.per_stream_pstate_strategy[strategy_index][stream_index] == dml2_pmo_pstate_strategy_fw_drr) {
			setup_planes_for_drr_by_mask(display_config, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		} else if (scratch->pmo_dcn4.per_stream_pstate_strategy[strategy_index][stream_index] == dml2_pmo_pstate_strategy_vactive) {
			setup_planes_for_vactive_by_mask(display_config, scratch->pmo_dcn4.stream_plane_mask[stream_index]);
		}
	}

	return success;
}

static int get_minimum_reserved_time_us_for_planes(struct display_configuation_with_meta *display_config, int plane_mask)
{
	int min_time_us = 0xFFFFFF;
	unsigned int plane_index = 0;

	for (plane_index = 0; plane_index < display_config->display_config.num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			if (min_time_us > (display_config->display_config.plane_descriptors[plane_index].overrides.reserved_vblank_time_ns / 1000))
				min_time_us = display_config->display_config.plane_descriptors[plane_index].overrides.reserved_vblank_time_ns / 1000;
		}
	}
	return min_time_us;
}

bool pmo_dcn4_test_for_pstate_support(struct dml2_pmo_test_for_pstate_support_in_out *in_out)
{
	bool p_state_supported = true;
	unsigned int stream_index;
	struct dml2_pmo_scratch *s = &in_out->instance->scratch;

	if (s->pmo_dcn4.cur_pstate_candidate < 0)
		return false;

	for (stream_index = 0; stream_index < in_out->base_display_config->display_config.num_streams; stream_index++) {

		if (s->pmo_dcn4.per_stream_pstate_strategy[s->pmo_dcn4.cur_pstate_candidate][stream_index] == dml2_pmo_pstate_strategy_vactive) {
			if (get_vactive_pstate_margin(in_out->base_display_config, s->pmo_dcn4.stream_plane_mask[stream_index]) < MIN_VACTIVE_MARGIN_US) {
				p_state_supported = false;
				break;
			}
		} else if (s->pmo_dcn4.per_stream_pstate_strategy[s->pmo_dcn4.cur_pstate_candidate][stream_index] == dml2_pmo_pstate_strategy_vblank) {
			if (get_minimum_reserved_time_us_for_planes(in_out->base_display_config, s->pmo_dcn4.stream_plane_mask[stream_index]) <
				in_out->instance->soc_bb->power_management_parameters.dram_clk_change_blackout_us) {
				p_state_supported = false;
				break;
			}
		} else if (s->pmo_dcn4.per_stream_pstate_strategy[s->pmo_dcn4.cur_pstate_candidate][stream_index] == dml2_pmo_pstate_strategy_fw_svp) {
			if (in_out->base_display_config->stage3.stream_svp_meta[stream_index].valid == false) {
				p_state_supported = false;
				break;
			}
		} else if (s->pmo_dcn4.per_stream_pstate_strategy[s->pmo_dcn4.cur_pstate_candidate][stream_index] == dml2_pmo_pstate_strategy_fw_drr) {
			if (!all_planes_match_strategy(in_out->base_display_config, s->pmo_dcn4.stream_plane_mask[stream_index], dml2_pmo_pstate_strategy_fw_drr)) {
				p_state_supported = false;
				break;
			}
		} else if (s->pmo_dcn4.per_stream_pstate_strategy[s->pmo_dcn4.cur_pstate_candidate][stream_index] == dml2_pmo_pstate_strategy_na) {
			p_state_supported = false;
			break;
		}
	}

	return p_state_supported;
}

bool pmo_dcn4_optimize_for_pstate_support(struct dml2_pmo_optimize_for_pstate_support_in_out *in_out)
{
	bool success = false;
	struct dml2_pmo_scratch *s = &in_out->instance->scratch;

	memcpy(in_out->optimized_display_config, in_out->base_display_config, sizeof(struct display_configuation_with_meta));

	if (in_out->last_candidate_failed) {
		if (s->pmo_dcn4.allow_state_increase_for_strategy[s->pmo_dcn4.cur_pstate_candidate] &&
			s->pmo_dcn4.cur_latency_index < s->pmo_dcn4.max_latency_index) {
			s->pmo_dcn4.cur_latency_index++;

			success = true;
		}
	}

	if (!success) {
		s->pmo_dcn4.cur_latency_index = s->pmo_dcn4.min_latency_index;
		s->pmo_dcn4.cur_pstate_candidate++;

		if (s->pmo_dcn4.cur_pstate_candidate < s->pmo_dcn4.num_pstate_candidates) {
			success = true;
		}
	}

	if (success) {
		in_out->optimized_display_config->stage3.min_clk_index_for_latency = s->pmo_dcn4.cur_latency_index;
		setup_display_config(in_out->optimized_display_config, &in_out->instance->scratch, in_out->instance->scratch.pmo_dcn4.cur_pstate_candidate);
	}

	return success;
}
