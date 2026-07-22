// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dml2_pmo_dcn5_stage_optimizers.h"
#include "dml2_pmo_dcn6_stage_optimizers.h"
#include "dml2_debug.h"
#include "lib_float_math.h"

static enum dml2_pstate_method uclk_pstate_strategy_override_to_pstate_method(const enum dml2_uclk_pstate_change_strategy override_strategy)
{
	enum dml2_pstate_method method = dml2_pstate_method_na;

	switch (override_strategy) {
	case dml2_uclk_pstate_change_strategy_force_vactive:
		method = dml2_pstate_method_vactive;
		break;
	case dml2_uclk_pstate_change_strategy_force_vblank:
		method = dml2_pstate_method_vblank;
		break;
	case dml2_uclk_pstate_change_strategy_force_drr:
		method = dml2_pstate_method_fw_drr;
		break;
	case dml2_uclk_pstate_change_strategy_force_alternate:
		method = dml2_pstate_method_alternate;
		break;
	case dml2_uclk_pstate_change_strategy_force_mall_svp:
	case dml2_uclk_pstate_change_strategy_force_mall_full_frame:
	case dml2_uclk_pstate_change_strategy_auto:
	default:
		method = dml2_pstate_method_na;
	}

	return method;
}

static enum dml2_uclk_pstate_change_strategy pstate_method_to_uclk_pstate_strategy_override(const enum dml2_pstate_method method)
{
	enum dml2_uclk_pstate_change_strategy override_strategy = dml2_uclk_pstate_change_strategy_auto;

	switch (method) {
	case dml2_pstate_method_vactive:
	case dml2_pstate_method_fw_vactive_drr:
		override_strategy = dml2_uclk_pstate_change_strategy_force_vactive;
		break;
	case dml2_pstate_method_vblank:
	case dml2_pstate_method_fw_vblank_drr:
		override_strategy = dml2_uclk_pstate_change_strategy_force_vblank;
		break;
	case dml2_pstate_method_fw_drr:
		override_strategy = dml2_uclk_pstate_change_strategy_force_drr;
		break;
	case dml2_pstate_method_alternate:
		override_strategy = dml2_uclk_pstate_change_strategy_force_alternate;
		break;
	case dml2_pstate_method_fw_svp:
	case dml2_pstate_method_fw_svp_drr:
	case dml2_pstate_method_reserved_hw:
	case dml2_pstate_method_reserved_fw:
	case dml2_pstate_method_reserved_fw_drr_clamped:
	case dml2_pstate_method_reserved_fw_drr_var:
	case dml2_pstate_method_count:
	case dml2_pstate_method_na:
	default:
		override_strategy = dml2_uclk_pstate_change_strategy_auto;
	}

	return override_strategy;
}

static bool all_planes_match_method(const struct dml2_display_cfg *display_cfg, int plane_mask, enum dml2_pstate_method method)
{
	unsigned char i;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (display_cfg->plane_descriptors[i].overrides.uclk_pstate_change_strategy != dml2_uclk_pstate_change_strategy_auto &&
				display_cfg->plane_descriptors[i].overrides.uclk_pstate_change_strategy != pstate_method_to_uclk_pstate_strategy_override(method))
				return false;
		}
	}

	return true;
}

static const struct dml2_pstate_per_method_common_meta *get_per_method_common_meta(
	const struct dml2_pstate_meta *stream_pstate_meta,
	enum dml2_pstate_method stream_pstate_method,
	int stream_idx)
{
	const struct dml2_pstate_per_method_common_meta *stream_method_pstate_meta = NULL;

	switch (stream_pstate_method) {
	case dml2_pstate_method_vactive:
	case dml2_pstate_method_fw_vactive_drr:
		stream_method_pstate_meta = &stream_pstate_meta[stream_idx].method_vactive.common;
		break;
	case dml2_pstate_method_vblank:
	case dml2_pstate_method_fw_vblank_drr:
		stream_method_pstate_meta = &stream_pstate_meta[stream_idx].method_vblank.common;
		break;
	case dml2_pstate_method_fw_drr:
		stream_method_pstate_meta = &stream_pstate_meta[stream_idx].method_drr.common;
		break;
	case dml2_pstate_method_alternate:
		stream_method_pstate_meta = &stream_pstate_meta[stream_idx].method_alternate.common;
		break;
	case dml2_pstate_method_fw_svp:
	case dml2_pstate_method_fw_svp_drr:
	case dml2_pstate_method_reserved_hw:
	case dml2_pstate_method_reserved_fw:
	case dml2_pstate_method_reserved_fw_drr_clamped:
	case dml2_pstate_method_reserved_fw_drr_var:
	case dml2_pstate_method_count:
	case dml2_pstate_method_na:
	default:
		stream_method_pstate_meta = NULL;
	}

	return stream_method_pstate_meta;
}

static void dcn6_get_params_for_pstate_type(const struct dml2_pmo_instance *pmo,
		const struct dml2_optimization_worksheet *worksheet,
		enum dml2_pstate_type pstate_type,
		double *allow_delay_us,
		double *blackout_us,
		double *watermark_us)
{
	switch (pstate_type) {
	case dml2_pstate_type_uclk:
		*allow_delay_us = (double)pmo->ip_caps->fams2.max_allow_delay_us;
		*blackout_us = pmo->utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us;
		*watermark_us = worksheet->validation_result.mode_support.global.watermarks.DRAMClockChangeWatermark;
		break;
	case dml2_pstate_type_fclk:
		*allow_delay_us = (double)pmo->ip_caps->fams2.max_allow_delay_us; /* TODO placeholder */
		*blackout_us = pmo->utm_soc_bb->power_management_parameters.fclk_change_blackout_us;
		*watermark_us = worksheet->validation_result.mode_support.global.watermarks.FCLKChangeWatermark;
		break;
	case dml2_pstate_type_ppt:
		*allow_delay_us = (double)pmo->ip_caps->ppt_max_allow_delay_us;
		*blackout_us = math_max2(
				pmo->utm_soc_bb->power_management_parameters.g7_ppt_blackout_us,
				pmo->utm_soc_bb->power_management_parameters.g7_temperature_read_blackout_us);
		*watermark_us = worksheet->validation_result.mode_support.global.watermarks.temp_read_or_ppt_watermark_us;
		break;
	case dml2_pstate_type_temp_read:
	case dml2_pstate_type_dummy_pstate:
		*allow_delay_us = (double)pmo->ip_caps->temp_read_max_allow_delay_us;
		*blackout_us = math_max2(
				pmo->utm_soc_bb->power_management_parameters.g7_ppt_blackout_us,
				pmo->utm_soc_bb->power_management_parameters.g7_temperature_read_blackout_us);
		*watermark_us = worksheet->validation_result.mode_support.global.watermarks.temp_read_or_ppt_watermark_us;
		break;
	case dml2_pstate_type_count:
	default:
		*allow_delay_us = 0.0;
		*blackout_us = 0.0;
		*watermark_us = 0.0;
		break;
	}
}

static bool dcn6_is_timing_group_schedulable(
	const struct dml2_pstate_meta *stream_pstate_meta,
	const struct dml2_display_cfg *display_cfg,
	const enum dml2_pstate_method *per_stream_pstate_method,
	const unsigned int timing_group_idx,
	const double max_allow_delay_us,
	struct dml2_pstate_per_method_common_meta *group_pstate_meta,
	struct dml2_pmo_synchronized_timing_groups *s)
{
	unsigned int i;
	const struct dml2_pstate_per_method_common_meta *stream_method_pstate_meta;
	unsigned int base_stream_idx = 0;

	/* find base stream idx */
	for (base_stream_idx = 0; base_stream_idx < display_cfg->num_streams; base_stream_idx++) {
		if (is_bit_set_in_bitfield(s->synchronized_timing_group_masks[timing_group_idx], base_stream_idx)) {
			/* master stream found */
			break;
		}
	}

	/* init allow start and end lines for timing group */
	stream_method_pstate_meta = get_per_method_common_meta(stream_pstate_meta, per_stream_pstate_method[base_stream_idx], base_stream_idx);
	if (!stream_method_pstate_meta)
		return false;

	group_pstate_meta->allow_start_otg_vline = stream_method_pstate_meta->allow_start_otg_vline;
	group_pstate_meta->allow_end_otg_vline = stream_method_pstate_meta->allow_end_otg_vline;
	group_pstate_meta->period_us = stream_method_pstate_meta->period_us;
	for (i = base_stream_idx + 1; i < display_cfg->num_streams; i++) {
		if (is_bit_set_in_bitfield(s->synchronized_timing_group_masks[timing_group_idx], i)) {
			stream_method_pstate_meta = get_per_method_common_meta(stream_pstate_meta, per_stream_pstate_method[i], i);
			if (!stream_method_pstate_meta)
				continue;

			if (group_pstate_meta->allow_start_otg_vline < stream_method_pstate_meta->allow_start_otg_vline) {
				/* set group allow start to larger otg vline */
				group_pstate_meta->allow_start_otg_vline = stream_method_pstate_meta->allow_start_otg_vline;
			}

			if (group_pstate_meta->allow_end_otg_vline > stream_method_pstate_meta->allow_end_otg_vline) {
				/* set group allow end to smaller otg vline */
				group_pstate_meta->allow_end_otg_vline = stream_method_pstate_meta->allow_end_otg_vline;
			}

			/* check waveform still has positive width */
			if (group_pstate_meta->allow_start_otg_vline >= group_pstate_meta->allow_end_otg_vline) {
				/* timing group is not schedulable */
				return false;
			}
		}
	}

	/* calculate the rest of the meta */
	dcn5_build_method_scheduling_params(group_pstate_meta, &stream_pstate_meta[base_stream_idx]);

	return group_pstate_meta->allow_time_us > 0.0 &&
		group_pstate_meta->disallow_time_us <= max_allow_delay_us;
}

static bool dcn6_is_pstate_schedulable(
	struct dml2_pstate_meta *stream_pstate_meta,
	const struct dml2_display_cfg *display_cfg,
	const enum dml2_pstate_method *per_stream_pstate_method,
	const double max_allow_delay_us,
	struct dml2_pmo_synchronized_timing_groups *synchronized_timing_groups,
	struct dml2_scheduling_check_locals *s)
{
	double max_disallow_time_us = 0.0;
	unsigned int i, j;
	bool schedulable;

	memset(s->group_common_pstate_meta, 0, sizeof(s->group_common_pstate_meta));
	memset(s->sorted_group_gtl_disallow_index, 0, sizeof(unsigned int) * DML2_MAX_PLANES);

	/* search for a general solution to the schedule */

	/* STAGE 0: Early return for special cases */
	if (display_cfg->num_streams == 0) {
		return true;
	}

	/* STAGE 1: confirm allow waves overlap for synchronizable streams */
	schedulable = true;
	for (i = 0; i < synchronized_timing_groups->num_timing_groups; i++) {
		s->sorted_group_gtl_disallow_index[i] = i;
		s->sorted_group_gtl_period_index[i] = i;
		if (!dcn6_is_timing_group_schedulable(stream_pstate_meta,
					display_cfg,
					per_stream_pstate_method,
					i,
					max_allow_delay_us,
					&s->group_common_pstate_meta[i],
					synchronized_timing_groups)) {
			/* synchronized timing group was not schedulable */
			schedulable = false;
			break;
		}
		max_disallow_time_us += s->group_common_pstate_meta[i].disallow_time_us;
	}

	if ((schedulable && synchronized_timing_groups->num_timing_groups <= 1) || !schedulable) {
		/* 1. the only timing group was schedulable, so early pass
		 * 2. one of the timing groups was not schedulable, so early fail */
		return schedulable;
	}

	/* STAGE 2: Check allow can't be masked entirely by other disallows */
	schedulable = true;

	/* sort disallow times from greatest to least */
	for (i = 0; i < synchronized_timing_groups->num_timing_groups; i++) {
		bool swapped = false;

		for (j = 0; j < synchronized_timing_groups->num_timing_groups - 1; j++) {
			double j_disallow_us              = s->group_common_pstate_meta[s->sorted_group_gtl_disallow_index[j]].disallow_time_us;
			double jp1_disallow_us            = s->group_common_pstate_meta[s->sorted_group_gtl_disallow_index[j + 1]].disallow_time_us;
			if (j_disallow_us < jp1_disallow_us) {
				/* swap as A < B */
				swap(s->sorted_group_gtl_disallow_index[j],
					 s->sorted_group_gtl_disallow_index[j+1]);
				swapped = true;
			}
		}

		/* sorted, exit early */
		if (!swapped)
			break;
	}

	/* Check worst case disallow region occurs in the middle of allow for the
	* other display, or when >2 streams continue to halve the remaining allow time.
	*/
	for (i = 0; i < synchronized_timing_groups->num_timing_groups; i++) {
		if (s->group_common_pstate_meta[i].disallow_time_us <= 0.0) {
			/* this timing group always allows */
			continue;
		}

		double max_allow_time_us = s->group_common_pstate_meta[i].allow_time_us;
		for (j = 0; j < synchronized_timing_groups->num_timing_groups; j++) {
			unsigned int sorted_j = s->sorted_group_gtl_disallow_index[j];
			/* stream can't overlap itself */
			if (i != sorted_j && s->group_common_pstate_meta[sorted_j].disallow_time_us > 0.0) {
				double unmasked_allow_time_us = (max_allow_time_us - s->group_common_pstate_meta[sorted_j].disallow_time_us) / 2;

				max_allow_time_us = math_min2(
					s->group_common_pstate_meta[sorted_j].allow_time_us,
					unmasked_allow_time_us);

				if (max_allow_time_us < 0.0) {
					/* failed exit early */
					break;
				}
			}
		}

		if (max_allow_time_us <= 0.0) {
			/* not enough time for microschedule in the worst case */
			schedulable = false;
			break;
		}
	}

	if (schedulable && max_disallow_time_us < max_allow_delay_us) {
		return true;
	}

	/* STAGE 3: check larger allow can fit period of all other streams */
	schedulable = true;

	/* sort periods from greatest to least */
	for (i = 0; i < synchronized_timing_groups->num_timing_groups; i++) {
		bool swapped = false;

		for (j = 0; j < synchronized_timing_groups->num_timing_groups - 1; j++) {
			double j_period_us                 = s->group_common_pstate_meta[s->sorted_group_gtl_period_index[j]].period_us;
			double jp1_period_us               = s->group_common_pstate_meta[s->sorted_group_gtl_period_index[j + 1]].period_us;
			if (j_period_us < jp1_period_us) {
				/* swap as A < B */
				swap(s->sorted_group_gtl_period_index[j],
					 s->sorted_group_gtl_period_index[j + 1]);
				swapped = true;
			}
		}

		/* sorted, exit early */
		if (!swapped)
			break;
	}

	/* check larger allow can fit period of all other streams */
	for (i = 0; i < synchronized_timing_groups->num_timing_groups - 1; i++) {
		unsigned int sorted_i = s->sorted_group_gtl_period_index[i];
		unsigned int sorted_ip1 = s->sorted_group_gtl_period_index[i + 1];

		if (s->group_common_pstate_meta[sorted_i].allow_time_us < s->group_common_pstate_meta[sorted_ip1].period_us ||
			(synchronized_timing_groups->group_is_drr_enabled[sorted_ip1] && synchronized_timing_groups->group_is_drr_active[sorted_ip1])) {
			schedulable = false;
			break;
		}
	}

	if (schedulable && max_disallow_time_us < max_allow_delay_us) {
		return true;
	}

	/* STAGE 4: When using HW exclusive modes, check disallow alignments are within allowed threshold */
	if (synchronized_timing_groups->num_timing_groups == 2 &&
		!is_bit_set_in_bitfield(PMO_FW_STRATEGY_MASK, per_stream_pstate_method[0]) &&
		!is_bit_set_in_bitfield(PMO_FW_STRATEGY_MASK, per_stream_pstate_method[1])) {
		double sum_allow_time_us;
		double shift_per_period;
		double period_ratio;
		double max_shift_us;

		/* default period_0 > period_1 */
		unsigned int lrg_idx = 0;
		unsigned int sml_idx = 1;
		if (s->group_common_pstate_meta[0].period_us < s->group_common_pstate_meta[1].period_us) {
			/* period_0 < period_1 */
			lrg_idx = 1;
			sml_idx = 0;
		}
		period_ratio = s->group_common_pstate_meta[lrg_idx].period_us / s->group_common_pstate_meta[sml_idx].period_us;
		shift_per_period = s->group_common_pstate_meta[sml_idx].period_us * (period_ratio - math_floor(period_ratio));
		max_shift_us = s->group_common_pstate_meta[lrg_idx].disallow_time_us - s->group_common_pstate_meta[sml_idx].allow_time_us;
		max_disallow_time_us = max_shift_us / shift_per_period * s->group_common_pstate_meta[lrg_idx].period_us;
		sum_allow_time_us = s->group_common_pstate_meta[lrg_idx].allow_time_us + s->group_common_pstate_meta[sml_idx].allow_time_us;

		if (shift_per_period > 0.0 &&
			shift_per_period < sum_allow_time_us &&
			max_disallow_time_us < max_allow_delay_us) {
			schedulable = true;
		}
	}

	return schedulable;
}

static bool dcn6_update_worksheet_for_pstate_admissibility(struct dml2_optimization_worksheet *worksheet,
		struct dml2_pstate_meta *per_stream_pstate_meta,
		enum dml2_pstate_type pstate_type)
{
	const double vblank_ratio = 0.8;
	unsigned int plane_index, stream_index;
	double ideal_relative_disallow, ideal_disallow_time_us, disallow_time_us;
	double extra_time_required_us;
	double vblank_time_us;
	double min_unreserved_vblank_time_us;
	double min_det_fill_delay_us;
	double delta_max_det_fill_delay_us;
	double delta_reserved_vblank_time_us;

	int max_vactive_det_fill_delay_us;
	long reserved_vblank_time_ns;
	bool unvalidated_changes = false;

	const struct dml2_display_cfg *display_config = worksheet->orig_dispcfg;
	/* Two possible locations to obtain the extra time required to pass admissibility
	 *   1. Allocate some time from the reserved vblank
	 *   2. Use some of the time reserved for the vactive det fill delay
	 * For now statically favour getting most of the required time from the reserved vblank since
	 * configs failing pstate admissibility tend to have a large vblank to vactive ratio.
	 */

	ideal_relative_disallow = 1.0;
	if (display_config->num_streams > 1) {
		/* The disallow region should be less than (0.5) ^ (number of displays - 1) * total frame time
		* Calculation assumes identical displays / timings when performing the scheduling check
		* Make the relative disallow 5% smaller than the ideal case to add extra margin
		*/
		ideal_relative_disallow = (double)math_pow(0.5, (float)(display_config->num_streams - 1)) * 0.95;
	}

	for (stream_index = 0; stream_index < display_config->num_streams; stream_index++) {
		disallow_time_us = per_stream_pstate_meta[stream_index].method_vactive.common.disallow_time_us;
		ideal_disallow_time_us = per_stream_pstate_meta[stream_index].method_vactive.common.period_us * ideal_relative_disallow;

		extra_time_required_us = 0.0;
		delta_max_det_fill_delay_us = 0.0;
		delta_reserved_vblank_time_us = 0.0;
		if (disallow_time_us > ideal_disallow_time_us) {
			/* minimum unreserved 15% of blank, or 50us left over for prefetch */
			vblank_time_us = display_config->stream_descriptors[stream_index].timing.vblank_nom *
					per_stream_pstate_meta[stream_index].otg_vline_time_us;
			min_unreserved_vblank_time_us = math_min2(vblank_time_us * 0.15, 50);

			/* minmum of blackout time (~2x VActive bandwidth) */
			min_det_fill_delay_us = per_stream_pstate_meta[stream_index].blackout_otg_vlines *
					per_stream_pstate_meta[stream_index].otg_vline_time_us;

			extra_time_required_us = disallow_time_us - ideal_disallow_time_us;

			/* try compressing VActive fill first */
			delta_max_det_fill_delay_us = math_min2(extra_time_required_us * (1.0 - vblank_ratio),
					math_max2(0.0,
					per_stream_pstate_meta[stream_index].method_vactive.max_vactive_det_fill_delay_us - min_det_fill_delay_us));
			delta_max_det_fill_delay_us = math_floor2(delta_max_det_fill_delay_us, per_stream_pstate_meta[stream_index].otg_vline_time_us);
			extra_time_required_us -= delta_max_det_fill_delay_us;

			/* try reserving VBlank */
			delta_reserved_vblank_time_us = math_min2(extra_time_required_us,
					math_max2(0.0,
					vblank_time_us - per_stream_pstate_meta[stream_index].method_vactive.reserved_vblank_required_us - min_unreserved_vblank_time_us));
			delta_reserved_vblank_time_us = math_floor2(delta_reserved_vblank_time_us, per_stream_pstate_meta[stream_index].otg_vline_time_us);
			extra_time_required_us -= delta_reserved_vblank_time_us;

			if (extra_time_required_us > 0.0 &&
					per_stream_pstate_meta[stream_index].method_vactive.max_vactive_det_fill_delay_us > math_ceil2(delta_max_det_fill_delay_us + extra_time_required_us, per_stream_pstate_meta[stream_index].otg_vline_time_us)) {
				/* final attempt to compress fill time */
				delta_max_det_fill_delay_us = delta_max_det_fill_delay_us + extra_time_required_us;
				delta_max_det_fill_delay_us = math_ceil2(delta_max_det_fill_delay_us, per_stream_pstate_meta[stream_index].otg_vline_time_us);
				extra_time_required_us = 0.0;
			}
		}

		/* Update the worksheet */
		for (plane_index = 0; plane_index < display_config->num_planes; plane_index++) {
			if (display_config->plane_descriptors[plane_index].stream_index != stream_index) {
				continue;
			}

			max_vactive_det_fill_delay_us = 0;
			if (worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][pstate_type] > 0.0) {
				max_vactive_det_fill_delay_us = (int)math_floor(math_min2(
						(double)worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][pstate_type],
						per_stream_pstate_meta[stream_index].method_vactive.max_vactive_det_fill_delay_us - delta_max_det_fill_delay_us));
			} else {
				max_vactive_det_fill_delay_us = (int)math_floor(
						per_stream_pstate_meta[stream_index].method_vactive.max_vactive_det_fill_delay_us - delta_max_det_fill_delay_us);
			}

			reserved_vblank_time_ns = (long)math_max2(
					(double)worksheet->cur.config.reserved_vblank_time_ns[plane_index],
					(per_stream_pstate_meta[stream_index].method_vactive.reserved_vblank_required_us + delta_reserved_vblank_time_us) * 1000.0);

			if ((max_vactive_det_fill_delay_us > 0 && worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][pstate_type] == 0) ||
					max_vactive_det_fill_delay_us < worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][pstate_type] ||
					(reserved_vblank_time_ns > 0 && worksheet->cur.config.reserved_vblank_time_ns[plane_index] == 0) ||
					reserved_vblank_time_ns > worksheet->cur.config.reserved_vblank_time_ns[plane_index]) {
				/* only modify the worksheet if required */
				worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][pstate_type] = max_vactive_det_fill_delay_us;
				worksheet->cur.config.reserved_vblank_time_ns[plane_index] = reserved_vblank_time_ns;

				worksheet->cur.unvalidated_change.bits.reserved_vblank_time = true;
				unvalidated_changes = true;
			}
			DML_LOG_DEBUG("worksheet->cur.config.reserved_vblank_time_ns[%d] = %lu\n", plane_index, worksheet->cur.config.reserved_vblank_time_ns[plane_index]);
			DML_LOG_DEBUG("worksheet->cur.config.max_vactive_det_fill_delay_us[%d] = %u\n", plane_index, worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][pstate_type]);
		}
	}

	if (!worksheet->cur.config.fclk_pstate_support
			|| !worksheet->cur.config.ppt_temp_read_support) {
		worksheet->cur.config.fclk_pstate_support = true;
		worksheet->cur.config.ppt_temp_read_support = true;
		worksheet->cur.unvalidated_change.bits.uclk_pstate_method = true;
		unvalidated_changes = true;
	}

	return unvalidated_changes;
}

static int dcn6_get_vactive_latency_hiding(const struct dml2_validation_result *validation_res, int plane_mask)
{
	unsigned char i;
	int min_vactive_latency_hiding_us = 0xFFFFFFF;

	if (!validation_res->is_mode_support_valid)
		return min_vactive_latency_hiding_us;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (validation_res->mode_support.cfg_support_info.plane_support_info[i].active_latency_hiding_us < min_vactive_latency_hiding_us)
				min_vactive_latency_hiding_us = validation_res->mode_support.cfg_support_info.plane_support_info[i].active_latency_hiding_us;
		}
	}

	return min_vactive_latency_hiding_us;
}

static int dcn6_get_vactive_det_fill_delay_us(
		const struct dml2_validation_result *validation_res,
		enum dml2_pstate_type pstate_type,
		int plane_mask)
{
	unsigned int i;
	int max_vactive_det_fill_delay_us = 0;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (validation_res->mode_support.cfg_support_info.plane_support_info[i].vactive_det_fill_delay_us[pstate_type] > max_vactive_det_fill_delay_us)
				max_vactive_det_fill_delay_us = validation_res->mode_support.cfg_support_info.plane_support_info[i].vactive_det_fill_delay_us[pstate_type];
		}
	}

	return max_vactive_det_fill_delay_us;
}

static int dcn6_get_required_vactive_det_fill_delay_us(
	const struct dml2_optimization_worksheet *worksheet,
	enum dml2_pstate_type pstate_type,
	int plane_mask)
{
	unsigned int i;
	int max_vactive_det_fill_delay_us = 0;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (worksheet->cur.config.max_vactive_det_fill_delay_us[i][pstate_type] > max_vactive_det_fill_delay_us)
				max_vactive_det_fill_delay_us = worksheet->cur.config.max_vactive_det_fill_delay_us[i][pstate_type];
		}
	}

	return max_vactive_det_fill_delay_us;
}

static bool dcn6_all_timings_support_vactive(struct dml2_pmo_stage_optimizer *stage,
		const struct dml2_display_cfg *display_config,
		unsigned int mask)
{
	struct dml2_stage_optimizer_uclk_pstate_init_locals *s = &stage->func_locals->uclk_pstate_init;
	unsigned int i;
	bool valid = true;

	// Create a remap array to enable simple iteration through only masked stream indicies
	for (i = 0; i < display_config->num_streams; i++) {
		if (is_bit_set_in_bitfield(mask, i)) {
			/* check if stream has enough vactive margin, or single display in case blank can also be used */
			valid &= is_bit_set_in_bitfield(s->stream_vactive_capability_mask, i) ||
					display_config->num_streams == 1;
		}
	}

	return valid;
}

static bool validate_pstate_support_strategy_cofunctionality(struct dml2_pmo_stage_optimizer *stage,
		struct dml2_optimization_worksheet *worksheet,
		const struct dml2_display_cfg *display_cfg,
		const struct dml2_pmo_pstate_strategy *pstate_strategy)
{
	const struct dml2_pmo_instance *pmo = stage->pmo;

	unsigned int stream_index = 0;

	unsigned int drr_count = 0;
	unsigned int drr_stream_mask = 0;
	unsigned int vactive_count = 0;
	unsigned int vactive_stream_mask = 0;
	unsigned int vblank_count = 0;
	unsigned int vblank_stream_mask = 0;
	unsigned int alternate_count = 0;
	unsigned int alternate_stream_mask = 0;

	bool strategy_matches_forced_requirements = true;
	bool strategy_matches_drr_requirements = true;

	// Tabulate everything
	for (stream_index = 0; stream_index < display_cfg->num_streams; stream_index++) {

		if (!all_planes_match_method(display_cfg, worksheet->uclk_pstate.stream_plane_mask[stream_index],
			pstate_strategy->per_stream_pstate_method[stream_index])) {
			strategy_matches_forced_requirements = false;
			break;
		}

		strategy_matches_drr_requirements &=
			dcn5_stream_matches_drr_policy(stage, display_cfg, pstate_strategy->per_stream_pstate_method[stream_index], stream_index);

		if (pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pstate_method_fw_drr) {
			drr_count++;
			set_bit_in_bitfield(&drr_stream_mask, stream_index);
		} else if (pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pstate_method_vactive ||
			pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pstate_method_fw_vactive_drr) {
			vactive_count++;
			set_bit_in_bitfield(&vactive_stream_mask, stream_index);
		} else if (pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pstate_method_vblank ||
			pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pstate_method_fw_vblank_drr) {
			vblank_count++;
			set_bit_in_bitfield(&vblank_stream_mask, stream_index);
		} else if (pstate_strategy->per_stream_pstate_method[stream_index] == dml2_pstate_method_alternate) {
			alternate_count++;
			set_bit_in_bitfield(&alternate_stream_mask, stream_index);
		}
	}

	if (!strategy_matches_forced_requirements || !strategy_matches_drr_requirements)
		return false;

	if (vactive_count > 0 && !dcn6_all_timings_support_vactive(stage, display_cfg, vactive_stream_mask))
		return false;

	if (vblank_count > 0 && (pmo->options->disable_vblank || !dcn5_all_timings_support_vblank(stage, display_cfg, vblank_stream_mask)))
		return false;

	if (drr_count > 0 && (pmo->options->disable_drr_var || !dcn5_all_timings_support_drr(stage, worksheet, display_cfg, drr_stream_mask)))
		return false;

	if (alternate_count > 0 && pmo->options->disable_alternate_memory_training)
		return false;

	return dcn6_is_pstate_schedulable(
		worksheet->uclk_pstate.stream_pstate_meta,
		display_cfg,
		pstate_strategy->per_stream_pstate_method,
		stage->func_locals->uclk_pstate_init.allow_delay_us,
		&stage->func_locals->uclk_pstate_init.synchronized_timing_groups,
		&stage->func_locals->uclk_pstate_init.scheduling_check_locals);
}

static void dcn6_build_pstate_meta_per_stream(const struct dml2_display_cfg *display_cfg,
	const struct dml2_ip_capabilities *ip_caps,
	const struct dml2_optimization_worksheet *worksheet,
	enum dml2_pstate_type pstate_type,
	double watermark_us,
	double blackout_us,
	double max_allow_delay_us,
	int stream_index,
	unsigned int stream_plane_mask,
	/* output */
	struct dml2_pstate_meta *stream_pstate_meta)
{
	const struct dml2_stream_parameters *stream_descriptor = &display_cfg->stream_descriptors[stream_index];
	const struct dml2_timing_cfg *timing                   = &stream_descriptor->timing;

	int max_det_fill_delay_otg_vlines;
	int min_reserved_blank_otg_vlines;

	/* worst case all other streams require some programming at the same time, 0 if only 1 stream */
	double contention_delay_us = ((double)ip_caps->fams2.vertical_interrupt_ack_delay_us +
		math_max2(ip_caps->fams2.drr_programming_delay_us, ip_caps->fams2.allow_programming_delay_us)) *
		(display_cfg->num_streams - 1);

	/* common */
	stream_pstate_meta->valid               = true;
	stream_pstate_meta->nom_vtotal          = stream_descriptor->timing.vblank_nom + stream_descriptor->timing.v_active;
	stream_pstate_meta->otg_vline_time_us   = (double)timing->h_total / timing->pixel_clock_khz * 1000.0;
	stream_pstate_meta->vblank_start        = timing->v_blank_end + timing->v_active;
	stream_pstate_meta->nom_refresh_rate_hz = timing->pixel_clock_khz * 1000.0 /
		(stream_pstate_meta->nom_vtotal * timing->h_total);
	stream_pstate_meta->nom_frame_time_us   =
		(double)stream_pstate_meta->nom_vtotal * stream_pstate_meta->otg_vline_time_us;

	if (stream_descriptor->timing.drr_config.enabled == true) {
		if (stream_descriptor->timing.drr_config.min_refresh_uhz != 0.0) {
			stream_pstate_meta->max_vtotal = (int)math_floor((double)stream_descriptor->timing.pixel_clock_khz /
				((double)stream_descriptor->timing.drr_config.min_refresh_uhz * stream_descriptor->timing.h_total) * 1e9);
		} else {
			/* assume min of 48Hz */
			stream_pstate_meta->max_vtotal = (int)math_floor((double)stream_descriptor->timing.pixel_clock_khz /
				(48000000.0 * stream_descriptor->timing.h_total) * 1e9);
		}
	} else {
		stream_pstate_meta->max_vtotal = stream_pstate_meta->nom_vtotal;
	}
	stream_pstate_meta->min_refresh_rate_hz = timing->pixel_clock_khz * 1000.0 /
		(stream_pstate_meta->max_vtotal * timing->h_total);
	stream_pstate_meta->max_frame_time_us   =
		(double)stream_pstate_meta->max_vtotal * stream_pstate_meta->otg_vline_time_us;

	stream_pstate_meta->scheduling_delay_otg_vlines =
		(int)math_ceil(ip_caps->fams2.scheduling_delay_us / stream_pstate_meta->otg_vline_time_us);
	stream_pstate_meta->vertical_interrupt_ack_delay_otg_vlines =
		(int)math_ceil(ip_caps->fams2.vertical_interrupt_ack_delay_us / stream_pstate_meta->otg_vline_time_us);
	stream_pstate_meta->contention_delay_otg_vlines =
		(int)math_ceil(contention_delay_us / stream_pstate_meta->otg_vline_time_us);
	/* worst case allow to target needs to account for all streams' allow events overlapping, and 1 line for error */
	stream_pstate_meta->allow_to_target_delay_otg_vlines =
		(int)(math_ceil((ip_caps->fams2.vertical_interrupt_ack_delay_us + contention_delay_us + ip_caps->fams2.allow_programming_delay_us) / stream_pstate_meta->otg_vline_time_us)) + 1;
	stream_pstate_meta->min_allow_width_otg_vlines =
		(int)math_ceil(ip_caps->fams2.min_allow_width_us / stream_pstate_meta->otg_vline_time_us);
	stream_pstate_meta->blackout_otg_vlines = (int)math_ceil(blackout_us /	stream_pstate_meta->otg_vline_time_us);
	stream_pstate_meta->max_allow_delay_otg_vlines = (int)math_floor((double)max_allow_delay_us / stream_pstate_meta->otg_vline_time_us);
	if (stream_pstate_meta->max_allow_delay_otg_vlines < 0)
		stream_pstate_meta->max_allow_delay_otg_vlines = 0;
	max_det_fill_delay_otg_vlines = (int)math_floor(
			(double)dcn6_get_required_vactive_det_fill_delay_us(worksheet, pstate_type, stream_plane_mask) /
			stream_pstate_meta->otg_vline_time_us);
	min_reserved_blank_otg_vlines = (int)math_ceil(
			(double)dcn5_get_minimum_reserved_time_us_for_planes(worksheet, stream_plane_mask) /
			stream_pstate_meta->otg_vline_time_us);
	stream_pstate_meta->nom_vblank_time_us = stream_descriptor->timing.vblank_nom * stream_pstate_meta->otg_vline_time_us;

	/* scheduling params should be built based on the worst case for allow_time:disallow_time */

	/* vactive */
	stream_pstate_meta->method_vactive.vactive_latency_hiding_us =
			(double)dcn6_get_vactive_latency_hiding(&worksheet->validation_result, stream_plane_mask);
	if (stream_pstate_meta->method_vactive.vactive_latency_hiding_us < watermark_us) {
		/* achieve single pulse of allow by utilizing blank */
		stream_pstate_meta->method_vactive.reserved_vblank_required_us =
				blackout_us -
				stream_pstate_meta->method_vactive.vactive_latency_hiding_us;
		stream_pstate_meta->method_vactive.reserved_blank_required_vlines = (int)math_max3(
				0.0,
				math_ceil(stream_pstate_meta->method_vactive.reserved_vblank_required_us /
				stream_pstate_meta->otg_vline_time_us),
				(double)min_reserved_blank_otg_vlines);
	} else {
		/* account for already reserved vblank */
		stream_pstate_meta->method_vactive.reserved_vblank_required_us =
				min_reserved_blank_otg_vlines *
				stream_pstate_meta->otg_vline_time_us;
		stream_pstate_meta->method_vactive.reserved_blank_required_vlines = min_reserved_blank_otg_vlines;
	}

	if (display_cfg->num_streams == 1) {
		/* for single stream, guarantee at least an instant of allow */
		stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines = (int)math_floor(
				math_max2(0.0,
				timing->v_active - math_max2(1.0, stream_pstate_meta->min_allow_width_otg_vlines) -
				(stream_pstate_meta->blackout_otg_vlines -
				stream_pstate_meta->method_vactive.reserved_blank_required_vlines)));
	} else {
		/* for multi stream, bound to a max fill time defined by the parameter */
		stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines =
				(int)math_floor((double)ip_caps->max_vactive_det_fill_delay_us / stream_pstate_meta->otg_vline_time_us);
	}

	if (max_det_fill_delay_otg_vlines > 0) {
		/* consider existing DET fill time enforcement */
		if (stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines > 0) {
			stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines = (int)math_min2(
					(double)stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines,
					(double)max_det_fill_delay_otg_vlines);
		} else {
			stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines = max_det_fill_delay_otg_vlines;
		}
	}

	/* consider max allow delay enforcement */
	if (stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines > 0) {
		stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines = (int)math_min2(
				(double)stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines,
				math_max2(0.0,
				stream_pstate_meta->max_allow_delay_otg_vlines +
				stream_pstate_meta->method_vactive.reserved_blank_required_vlines -
				(int)stream_descriptor->timing.vblank_nom -
				stream_pstate_meta->blackout_otg_vlines));
	} else {
		stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines = (int)math_max2(0.0,
				stream_pstate_meta->max_allow_delay_otg_vlines +
				stream_pstate_meta->method_vactive.reserved_blank_required_vlines -
				(int)stream_descriptor->timing.vblank_nom -
				stream_pstate_meta->blackout_otg_vlines);
	}
	stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_us =
			stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines *
			stream_pstate_meta->otg_vline_time_us;

	if (stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_us > 0.0) {
		stream_pstate_meta->method_vactive.common.allow_start_otg_vline =
				timing->v_blank_end + stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines;
		stream_pstate_meta->method_vactive.common.allow_end_otg_vline =
				stream_pstate_meta->vblank_start -
				stream_pstate_meta->blackout_otg_vlines +
				stream_pstate_meta->method_vactive.reserved_blank_required_vlines;
	} else {
		stream_pstate_meta->method_vactive.common.allow_start_otg_vline = 0;
		stream_pstate_meta->method_vactive.common.allow_end_otg_vline = 0;
	}
	stream_pstate_meta->method_vactive.common.period_us = stream_pstate_meta->nom_frame_time_us;

	/* vblank */
	stream_pstate_meta->method_vblank.common.allow_start_otg_vline = stream_pstate_meta->vblank_start;
	stream_pstate_meta->method_vblank.common.period_us             = stream_pstate_meta->nom_frame_time_us;
	stream_pstate_meta->method_vblank.common.allow_end_otg_vline   =
		stream_pstate_meta->method_vblank.common.allow_start_otg_vline + 1;

	if (pstate_type == dml2_pstate_type_uclk) {
		/* alternate */
		stream_pstate_meta->method_alternate.programming_delay_otg_vlines =
			(int)math_ceil(ip_caps->fams2.subvp_programming_delay_us / stream_pstate_meta->otg_vline_time_us);
		stream_pstate_meta->method_alternate.pmfw_throttle_delay_otg_vlines =
			(int)math_ceil(ip_caps->fams2.subvp_df_throttle_delay_us / stream_pstate_meta->otg_vline_time_us);
		stream_pstate_meta->method_alternate.common.period_us = stream_pstate_meta->nom_frame_time_us;
		stream_pstate_meta->method_alternate.common.allow_start_otg_vline = 0;
		stream_pstate_meta->method_alternate.common.allow_end_otg_vline = stream_pstate_meta->nom_vtotal;

		/* drr */
		stream_pstate_meta->method_drr.common.period_us = stream_pstate_meta->nom_frame_time_us;
		stream_pstate_meta->method_drr.programming_delay_otg_vlines =
			(int)math_ceil(ip_caps->fams2.drr_programming_delay_us / stream_pstate_meta->otg_vline_time_us);
		stream_pstate_meta->method_drr.common.allow_start_otg_vline =
			stream_pstate_meta->vblank_start +
			stream_pstate_meta->allow_to_target_delay_otg_vlines;

		if (display_cfg->num_streams <= 1) {
			/* only need to stretch vblank for blackout time */
			stream_pstate_meta->method_drr.stretched_vtotal =
				stream_pstate_meta->nom_vtotal +
				stream_pstate_meta->allow_to_target_delay_otg_vlines +
				stream_pstate_meta->min_allow_width_otg_vlines +
				stream_pstate_meta->blackout_otg_vlines;
		} else {
			/* multi display needs to always be schedulable */
			stream_pstate_meta->method_drr.stretched_vtotal =
				stream_pstate_meta->nom_vtotal * 2 +
				stream_pstate_meta->allow_to_target_delay_otg_vlines +
				stream_pstate_meta->min_allow_width_otg_vlines +
				stream_pstate_meta->blackout_otg_vlines;
		}
		stream_pstate_meta->method_drr.common.allow_end_otg_vline =
			stream_pstate_meta->method_drr.stretched_vtotal -
			stream_pstate_meta->blackout_otg_vlines;

		dcn5_build_method_scheduling_params(&stream_pstate_meta->method_drr.common, stream_pstate_meta);
		dcn5_build_method_scheduling_params(&stream_pstate_meta->method_alternate.common, stream_pstate_meta);
	}

	dcn5_build_method_scheduling_params(&stream_pstate_meta->method_vactive.common, stream_pstate_meta);
	dcn5_build_method_scheduling_params(&stream_pstate_meta->method_vblank.common, stream_pstate_meta);
}


static void dml2_pmo_dcn6_stage_optimizer_uclk_pstate_init(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	const struct dml2_pmo_instance *pmo = stage->pmo;
	struct dml2_stage_optimizer_uclk_pstate_init_locals *s = &stage->func_locals->uclk_pstate_init;

	const struct dml2_display_cfg *display_config = worksheet->orig_dispcfg;
	const struct dml2_plane_parameters *plane_descriptor;
	const struct dml2_pmo_pstate_strategy *strategy_list = NULL;
	struct dml2_pmo_pstate_strategy override_base_strategy = { 0 };
	unsigned int strategy_list_size = 0;
	unsigned int plane_index, i;
	unsigned int stream_index;
	bool build_override_strategy = true;

	DML_LOG_COMP_IF_ENTER();
	memset(s, 0, sizeof(struct dml2_stage_optimizer_uclk_pstate_init_locals));

	if (display_config->overrides.all_streams_blanked) {
		goto exit;
	}

	// First build the stream plane mask (array of bitfields indexed by stream, indicating plane mapping)
	for (plane_index = 0; plane_index < display_config->num_planes; plane_index++) {
		plane_descriptor = &display_config->plane_descriptors[plane_index];

		set_bit_in_bitfield(&worksheet->uclk_pstate.stream_plane_mask[plane_descriptor->stream_index], plane_index);

		build_override_strategy &= plane_descriptor->overrides.uclk_pstate_change_strategy != dml2_uclk_pstate_change_strategy_auto;
		override_base_strategy.per_stream_pstate_method[plane_descriptor->stream_index] =
				uclk_pstate_strategy_override_to_pstate_method(plane_descriptor->overrides.uclk_pstate_change_strategy);

		/* Save initial reserved vblank time as pstate optimize may overwrite this value. But
		 * if validation or permissibility fails then we must restore to the original value.
		 */
		worksheet->uclk_pstate.init_reserved_vblank_time_ns[plane_index] = worksheet->cur.config.reserved_vblank_time_ns[plane_index];
	}

	dcn6_get_params_for_pstate_type(pmo, worksheet, dml2_pstate_type_uclk, &s->allow_delay_us, &s->blackout_us, &s->watermark_us);

	// Figure out which streams can do vactive, and also build up implicit FAMS2 meta
	for (stream_index = 0; stream_index < display_config->num_streams; stream_index++) {
		unsigned int stream_plane_mask = worksheet->uclk_pstate.stream_plane_mask[stream_index];
		struct dml2_validation_result *validation_result = &worksheet->validation_result;

		if (dcn5_get_vactive_pstate_margin(validation_result, stream_plane_mask) > 0)
			set_bit_in_bitfield(&s->stream_vactive_capability_mask, stream_index);

		/* pstate meta */
		dcn6_build_pstate_meta_per_stream(worksheet->orig_dispcfg,
			pmo->ip_caps,
			worksheet,
			dml2_pstate_type_uclk,
			s->watermark_us,
			s->blackout_us,
			s->allow_delay_us,
			stream_index,
			stream_plane_mask,
			&worksheet->uclk_pstate.stream_pstate_meta[stream_index]);
	}

	/* get synchronized timing groups */
	dcn5_build_synchronized_timing_groups(&stage->func_locals->uclk_pstate_init.synchronized_timing_groups, display_config);

	if (build_override_strategy) {
		/* build expanded override strategy list (no permutations) */
		override_base_strategy.allow_state_increase = true;
		s->num_expanded_override_strategies = 0;
		dcn5_insert_strategy_into_expanded_list(&override_base_strategy,
				display_config->num_streams,
				s->expanded_override_strategy_list,
				&s->num_expanded_override_strategies);
		dcn5_expand_variant_strategy(&override_base_strategy,
				display_config->num_streams,
				false,
				s->expanded_override_strategy_list,
				&s->num_expanded_override_strategies);

		/* use override strategy list */
		strategy_list = s->expanded_override_strategy_list;
		strategy_list_size = s->num_expanded_override_strategies;
	} else {
		/* use predefined strategy list */
		strategy_list = dcn5_get_expanded_strategy_list(stage, display_config->num_streams);
		strategy_list_size = dcn5_get_num_expanded_strategies(stage, display_config->num_streams);
	}

	worksheet->uclk_pstate.num_pstate_candidates = 0;

	if (!strategy_list || strategy_list_size == 0)
		goto exit;

	for (i = 0; i < strategy_list_size && worksheet->uclk_pstate.num_pstate_candidates < DML2_PMO_PSTATE_CANDIDATE_LIST_SIZE; i++) {
		if (validate_pstate_support_strategy_cofunctionality(stage, worksheet, display_config, &strategy_list[i])) {
			dcn5_insert_into_candidate_list(&strategy_list[i], display_config->num_streams, worksheet);
		}
	}

	if (worksheet->uclk_pstate.num_pstate_candidates > 0) {
		worksheet->uclk_pstate.pstate_strategy_candidates[worksheet->uclk_pstate.num_pstate_candidates-1].allow_state_increase = true;
		worksheet->uclk_pstate.cur_pstate_candidate = -1;
		goto exit;
	} else {
		goto exit;
	}
exit:
	DML_LOG_COMP_IF_EXIT();
}

static void setup_planes_for_alternate_by_mask(struct dml2_pmo_stage_optimizer *stage,
		struct dml2_optimization_worksheet *worksheet,
		int plane_mask)
{
	(void)stage;
	unsigned int plane_index;
	const struct dml2_display_cfg *display_config = worksheet->orig_dispcfg;

	for (plane_index = 0; plane_index < display_config->num_planes; plane_index++)
		if (is_bit_set_in_bitfield(plane_mask, plane_index))
			worksheet->cur.config.uclk_pstate_switch_modes[plane_index] = dml2_pstate_method_alternate;
}

static void dcn6_setup_planes_for_vactive_by_mask(struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet, int plane_mask)
{
	unsigned int plane_index;
	unsigned int stream_index;
	const struct dml2_display_cfg *display_config = worksheet->orig_dispcfg;
	const struct dml2_pmo_instance *pmo = stage->pmo;

	for (plane_index = 0; plane_index < display_config->num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			stream_index = display_config->plane_descriptors[plane_index].stream_index;

			worksheet->cur.config.uclk_pstate_switch_modes[plane_index] = dml2_pstate_method_vactive;

			if (!pmo->options->disable_vactive_det_fill_bw_pad) {
				if (worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][dml2_pstate_type_uclk] > 0) {
					worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][dml2_pstate_type_uclk] = (int)math_min2(
							math_floor(worksheet->uclk_pstate.stream_pstate_meta[stream_index].method_vactive.max_vactive_det_fill_delay_us),
							worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][dml2_pstate_type_uclk]);
				} else {
					worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][dml2_pstate_type_uclk] = (int)math_floor(
							worksheet->uclk_pstate.stream_pstate_meta[stream_index].method_vactive.max_vactive_det_fill_delay_us);
				}
			}

			worksheet->cur.config.reserved_vblank_time_ns[plane_index] = (long)math_max2(
					worksheet->uclk_pstate.stream_pstate_meta[stream_index].method_vactive.reserved_vblank_required_us * 1000,
					worksheet->cur.config.reserved_vblank_time_ns[plane_index]);
		}
	}
}

static bool setup_optimized_worksheet_for_uclk_pstate(struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	bool fams2_required = false;
	bool legacy_pstate_info_for_dmu = false;
	bool success = true;
	unsigned int stream_index, plane_index;
	int strategy_index = worksheet->uclk_pstate.cur_pstate_candidate;
	const struct dml2_plane_parameters *plane_descriptor;

	for (plane_index = 0; plane_index < worksheet->orig_dispcfg->num_planes; plane_index++) {
		plane_descriptor = &worksheet->orig_dispcfg->plane_descriptors[plane_index];
		set_bit_in_bitfield(&worksheet->uclk_pstate.stream_plane_mask[plane_descriptor->stream_index], plane_index);
	}

	for (stream_index = 0; stream_index < worksheet->orig_dispcfg->num_streams; stream_index++) {

		if (worksheet->uclk_pstate.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pstate_method_na) {
			success = false;
			break;
		} else if (worksheet->uclk_pstate.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pstate_method_vactive) {
			legacy_pstate_info_for_dmu = true;
			dcn6_setup_planes_for_vactive_by_mask(stage, worksheet, worksheet->uclk_pstate.stream_plane_mask[stream_index]);
		} else if (worksheet->uclk_pstate.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pstate_method_vblank) {
			legacy_pstate_info_for_dmu = true;
			dcn5_setup_planes_for_vblank_by_mask(stage, worksheet, worksheet->uclk_pstate.stream_plane_mask[stream_index]);
		} else if (worksheet->uclk_pstate.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pstate_method_fw_vactive_drr) {
			fams2_required = true;
			dcn5_setup_planes_for_vactive_drr_by_mask(stage, worksheet, worksheet->uclk_pstate.stream_plane_mask[stream_index]);
		} else if (worksheet->uclk_pstate.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pstate_method_fw_vblank_drr) {
			fams2_required = true;
			dcn5_setup_planes_for_vblank_drr_by_mask(stage, worksheet, worksheet->uclk_pstate.stream_plane_mask[stream_index]);
		} else if (worksheet->uclk_pstate.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pstate_method_fw_drr) {
			fams2_required = true;
			dcn5_setup_planes_for_drr_by_mask(stage, worksheet, worksheet->uclk_pstate.stream_plane_mask[stream_index]);
		} else if (worksheet->uclk_pstate.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index] == dml2_pstate_method_alternate) {
			fams2_required = true;
			setup_planes_for_alternate_by_mask(stage, worksheet, worksheet->uclk_pstate.stream_plane_mask[stream_index]);
		}
	}

	/* Indicate if FAMS2 required */
	if (success) {
		worksheet->cur.config.fams2_required = fams2_required;
		worksheet->cur.config.legacy_pstate_info_for_dmu = legacy_pstate_info_for_dmu;
		// Copy FAMS2 meta unconditionally - we need for vactive as well
		memcpy(&worksheet->cur.config.stream_pstate_meta,
				&worksheet->uclk_pstate.stream_pstate_meta,
				sizeof(struct dml2_pstate_meta) * DML2_MAX_PLANES);
		worksheet->cur.config.uclk_pstate_support = true;
	}

	return success;
}

static bool dml2_pmo_dcn6_stage_optimizer_uclk_pstate_optimize_next(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	bool should_continue = true;

	DML_LOG_COMP_IF_ENTER();
	/* Nothing to optimize if there are no candidates, so return false */
	if (worksheet->uclk_pstate.num_pstate_candidates == 0) {
		should_continue = false;
		goto exit;
	}

	/* Optimization is completed if we find a candidate that passed validation and also passes permissibility.
	 * There are scenarios where permissibility can pass even if validation fails, so we need to check the
	 * validation result here as well.
	 */
	if (worksheet->validation_result.is_mode_support_valid
			&& stage->test_permissibility(stage, worksheet) == DML2_STATUS_OK) {
		should_continue = false;
		goto exit;
	}

	/* If we've reached the end of the p-state candidate list, return false since
	 * there's no more potential optimization options */
	if (worksheet->uclk_pstate.cur_pstate_candidate == worksheet->uclk_pstate.num_pstate_candidates - 1) {
		should_continue = false;
		goto exit;
	}

	/* Reset current settings since the previous optimization attempt did not pass */
	dcn5_reset_worksheet_for_uclk_pstate(worksheet);
	worksheet->uclk_pstate.cur_pstate_candidate++;
	worksheet->cur.unvalidated_change.bits.uclk_pstate_method =
			setup_optimized_worksheet_for_uclk_pstate(stage, worksheet);
	DML_ASSERT_MSG(worksheet->cur.unvalidated_change.bits.uclk_pstate_method, "optimize_next must apply changes"
		" when returning true!\n");

exit:
	DML_LOG_DEBUG("%s exit with should_continue = %s\n", __func__, should_continue ? "true" : "false");
	DML_LOG_COMP_IF_EXIT();
	return should_continue;
}

static bool dcn6_alternate_permissible(
		const struct dml2_validation_result *validation_res,
		const struct dml2_pmo_instance *pmo,
		const struct dml2_display_cfg *display_cfg,
		int stream_idx)
{
	(void)pmo;
	unsigned int svp0_dst_lines = validation_res->mode_support.cfg_support_info.stream_support_info[stream_idx].alternate_svp0_dst_lines;
	unsigned int svp1_dst_lines = validation_res->mode_support.cfg_support_info.stream_support_info[stream_idx].alternate_svp1_dst_lines;
	unsigned int max_dst_y_pre = validation_res->mode_support.cfg_support_info.stream_support_info[stream_idx].max_dst_y_prefetch;
	unsigned int max_dst_y_after_scaler = validation_res->mode_support.cfg_support_info.stream_support_info[stream_idx].max_dst_y_after_scaler;
	const unsigned int max_hw_cursor_size = 135; // Actual is 128, but set to 135 for margin

	// svp0 + svp1 < vtotal - vstartup is required to support alt-chan
	// TBD if we need to increase constraint to vtotal - vstartup - cursor_height -> required if last cursor deadline is beyond vblank end
	if (svp0_dst_lines + svp1_dst_lines >= display_cfg->stream_descriptors[stream_idx].timing.v_total - max_dst_y_pre - max_dst_y_after_scaler - max_hw_cursor_size)
		return false;

	return true;
}

static enum dml2_status dml2_pmo_dcn6_stage_optimizer_uclk_pstate_test_permissibility(
	struct dml2_pmo_stage_optimizer *stage, const struct dml2_optimization_worksheet *worksheet)
{
	enum dml2_status status = DML2_STATUS_OK;
	unsigned int stream_index;
	const struct dml2_pmo_instance *pmo = stage->pmo;

	int REQUIRED_RESERVED_TIME = 0;

	DML_LOG_COMP_IF_ENTER();

	/* Permissibility passes if all streams are blanked - p-state support is guaranteed for this case*/
	if (worksheet->orig_dispcfg->overrides.all_streams_blanked) {
		status = DML2_STATUS_OK;
		goto exit;
	}

	/* If there are no pstate candidates then pstate support is false and permissibility fails */
	if (worksheet->uclk_pstate.num_pstate_candidates == 0) {
		status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
		goto exit;
	}

	if (worksheet->uclk_pstate.cur_pstate_candidate < 0) {
		status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
		goto exit;
	}

	if (!worksheet->validation_result.mode_support.global.uclk_pstate_supported) {
		status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
		goto exit;
	}

	REQUIRED_RESERVED_TIME = (int)pmo->utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us;

	for (stream_index = 0; stream_index < worksheet->orig_dispcfg->num_streams; stream_index++) {
		const struct dml2_pstate_meta *stream_pstate_meta = &worksheet->cur.config.stream_pstate_meta[stream_index];

		if (worksheet->uclk_pstate.pstate_strategy_candidates[worksheet->uclk_pstate.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pstate_method_vactive ||
				worksheet->uclk_pstate.pstate_strategy_candidates[worksheet->uclk_pstate.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pstate_method_fw_vactive_drr) {
			if (worksheet->orig_dispcfg->num_streams == 1) {
				/* Peak VActive + VBlank (single stream only) */
				if (dcn6_get_vactive_latency_hiding(&worksheet->validation_result, worksheet->uclk_pstate.stream_plane_mask[stream_index]) +
						dcn5_get_minimum_reserved_time_us_for_planes(worksheet, worksheet->uclk_pstate.stream_plane_mask[stream_index]) < REQUIRED_RESERVED_TIME) {
					status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
					break;
				}
			} else if (dcn5_get_vactive_pstate_margin(&worksheet->validation_result, worksheet->uclk_pstate.stream_plane_mask[stream_index]) < 0.0 ||
				dcn6_get_vactive_det_fill_delay_us(&worksheet->validation_result, dml2_pstate_type_uclk, worksheet->uclk_pstate.stream_plane_mask[stream_index]) > math_ceil(stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_us))  {
				status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
				break;
			}
		} else if (worksheet->uclk_pstate.pstate_strategy_candidates[worksheet->uclk_pstate.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pstate_method_vblank ||
				worksheet->uclk_pstate.pstate_strategy_candidates[worksheet->uclk_pstate.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pstate_method_fw_vblank_drr) {
			if (dcn5_get_minimum_reserved_time_us_for_planes(worksheet, worksheet->uclk_pstate.stream_plane_mask[stream_index]) < REQUIRED_RESERVED_TIME) {
				status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
				break;
			}
		} else if (worksheet->uclk_pstate.pstate_strategy_candidates[worksheet->uclk_pstate.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pstate_method_fw_drr) {
			if (!all_planes_match_method(worksheet->orig_dispcfg, worksheet->uclk_pstate.stream_plane_mask[stream_index], dml2_pstate_method_fw_drr)) {
				status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
				break;
			}
		} else if (worksheet->uclk_pstate.pstate_strategy_candidates[worksheet->uclk_pstate.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pstate_method_alternate) {
			if (!all_planes_match_method(worksheet->orig_dispcfg, worksheet->uclk_pstate.stream_plane_mask[stream_index], dml2_pstate_method_alternate) ||
					!dcn6_alternate_permissible(&worksheet->validation_result, pmo, worksheet->orig_dispcfg, stream_index)) {
				status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
				break;
			}
		} else if (worksheet->uclk_pstate.pstate_strategy_candidates[worksheet->uclk_pstate.cur_pstate_candidate].per_stream_pstate_method[stream_index] == dml2_pstate_method_na) {
			status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
			break;
		}
	}
exit:
	DML_LOG_DEBUG("%s exit with status = %s\n", __func__, dml2_status_str(status));
	DML_LOG_COMP_IF_EXIT();
	return status;
}

void dml2_pmo_dcn6_stage_optimizer_uclk_pstate_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage)
{
	stage->pmo = pmo;
	stage->func_locals = &pmo->scratch.pmo_dcn5.func_locals;
	stage->init = dml2_pmo_dcn6_stage_optimizer_uclk_pstate_init;
	stage->optimize_next = dml2_pmo_dcn6_stage_optimizer_uclk_pstate_optimize_next;
	stage->test_permissibility =
			dml2_pmo_dcn6_stage_optimizer_uclk_pstate_test_permissibility;
}

/*
* Counts the number of elements inside input array within the given span length.
* Formally, what is the size of the largest subset of the array where the largest and smallest element
* differ no more than the span.
*/
static unsigned int count_elements_in_span(const int *array, unsigned int array_size, unsigned int span)
{
	unsigned int i;
	unsigned int span_start_value;
	unsigned int span_start_index;
	unsigned int greatest_element_count;

	if (array_size == 0)
		return 1;

	if (span == 0)
		return array_size > 0 ? 1 : 0;

	span_start_value = 0;
	span_start_index = 0;
	greatest_element_count = 0;

	while (span_start_index < array_size) {
		for (i = span_start_index; i < array_size; i++) {
			if (array[i] - span_start_value <= span) {
				if (i - span_start_index + 1 > greatest_element_count) {
					greatest_element_count = i - span_start_index + 1;
				}
			} else
				break;
		}

		span_start_index++;

		if (span_start_index < array_size) {
			span_start_value = array[span_start_index - 1] + 1;
		}
	}

	return greatest_element_count;
}

static bool calculate_h_split_for_scaling_transform(int full_vp_width, int h_active, int num_pipes,
	enum dml2_scaling_transform scaling_transform, int *pipe_vp_x_start, int *pipe_vp_x_end)
{
	(void)h_active;
	int i, slice_width;
	const char MAX_SCL_VP_OVERLAP = 3;
	bool success = false;

	switch (scaling_transform) {
	case dml2_scaling_transform_centered:
	case dml2_scaling_transform_aspect_ratio:
	case dml2_scaling_transform_fullscreen:
		slice_width = full_vp_width / num_pipes;
		for (i = 0; i < num_pipes; i++) {
			pipe_vp_x_start[i] = i * slice_width;
			pipe_vp_x_end[i] = (i + 1) * slice_width - 1;

			if (pipe_vp_x_start[i] < MAX_SCL_VP_OVERLAP)
				pipe_vp_x_start[i] = 0;
			else
				pipe_vp_x_start[i] -= MAX_SCL_VP_OVERLAP;

			if (pipe_vp_x_end[i] > full_vp_width - MAX_SCL_VP_OVERLAP - 1)
				pipe_vp_x_end[i] = full_vp_width - 1;
			else
				pipe_vp_x_end[i] += MAX_SCL_VP_OVERLAP;
		}
		break;
	case dml2_scaling_transform_explicit:
	default:
		success = false;
		break;
	}

	return success;
}

/*
* Takes an input set of mcache boundaries and finds the appropriate setting of cache programming.
* Returns true if a valid set of programming can be made, and false otherwise. "Valid" means
* that the horizontal viewport does not span more than 2 cache slices.
*
* It optionally also can apply a constant shift to all the cache boundaries.
*/
static bool calculate_first_second_splitting(const int *mcache_boundaries, int num_boundaries, int shift,
	int pipe_h_vp_start, int pipe_h_vp_end, int *first_offset, int *second_offset)
{
	const int MAX_VP = 0xFFFFFF;
	int left_cache_id;
	int right_cache_id;
	int range_start;
	int range_end;
	bool success = false;

	if (num_boundaries <= 1) {
		if (first_offset && second_offset) {
			*first_offset = 0;
			*second_offset = -1;
		}
		success = true;
		return success;
	} else {
		range_start = 0;
		for (left_cache_id = 0; left_cache_id < num_boundaries; left_cache_id++) {
			range_end = mcache_boundaries[left_cache_id] - shift - 1;

			if (range_start <= pipe_h_vp_start && pipe_h_vp_start <= range_end)
				break;

			range_start = range_end + 1;
		}

		range_end = MAX_VP;
		for (right_cache_id = num_boundaries - 1; right_cache_id >= -1; right_cache_id--) {
			if (right_cache_id >= 0)
				range_start = mcache_boundaries[right_cache_id] - shift;
			else
				range_start = 0;

			if (range_start <= pipe_h_vp_end && pipe_h_vp_end <= range_end) {
				break;
			}
			range_end = range_start - 1;
		}
		right_cache_id = (right_cache_id + 1) % num_boundaries;

		if (right_cache_id == left_cache_id) {
			if (first_offset && second_offset) {
				*first_offset = left_cache_id;
				*second_offset = -1;
			}
			success = true;
		} else if (right_cache_id == (left_cache_id + 1) % num_boundaries) {
			if (first_offset && second_offset) {
				*first_offset = left_cache_id;
				*second_offset = right_cache_id;
			}
			success = true;
		}
	}

	return success;
}

/*
* For a given set of pipe start/end x positions, checks to see it can support the input mcache splitting.
* It also attempts to "optimize" by finding a shift if the default 0 shift does not work.
*/
static bool find_shift_for_valid_cache_id_assignment(const int *mcache_boundaries, unsigned int num_boundaries,
	int *pipe_vp_startx, int *pipe_vp_endx, unsigned int pipe_count, int shift_granularity, int *shift)
{
	int max_shift = 0xFFFF;
	unsigned int pipe_index;
	unsigned int i, slice_width;
	bool success = false;

	for (i = 0; i < num_boundaries; i++) {
		if (i == 0)
			slice_width = mcache_boundaries[i];
		else
			slice_width = mcache_boundaries[i] - mcache_boundaries[i - 1];

		if (max_shift > (int)slice_width) {
			max_shift = slice_width;
		}
	}

	for (*shift = 0; *shift <= max_shift; *shift += shift_granularity) {
		success = true;
		for (pipe_index = 0; pipe_index < pipe_count; pipe_index++) {
			if (!calculate_first_second_splitting(mcache_boundaries, num_boundaries, *shift,
				pipe_vp_startx[pipe_index], pipe_vp_endx[pipe_index], 0, 0)) {
				success = false;
				break;
			}
		}
		if (success)
			break;
	}

	return success;
}



static void dml2_pmo_dcn6_stage_optimizer_mcache_decide_shifts(struct dml2_pmo_stage_optimizer *stage,
	struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	const int MAX_PIXEL_OVERLAP = 6;
	int max_per_pipe_vp_p0 = 0;
	int max_per_pipe_vp_p1 = 0;
	int temp, p0shift, p1shift;
	unsigned int plane_index = 0;
	unsigned int i;
	unsigned int odm_combine_factor;
	unsigned int mpc_combine_factor;
	unsigned int num_dpps;
	unsigned int num_boundaries;
	enum dml2_scaling_transform scaling_transform;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;
	const struct dml2_mcache_surface_allocation *base_allocations = worksheet->validation_result.mcache_allocations;
	struct dml2_mcache_surface_allocation *new_allocations = worksheet->cur.config.mcache_allocations;
	bool p0pass = false;
	bool p1pass = false;

	for (plane_index = 0; plane_index < worksheet->orig_dispcfg->num_planes; plane_index++) {
		if (!worksheet->orig_dispcfg->plane_descriptors[plane_index].surface.dcc.enable)
			continue;

		plane = &worksheet->orig_dispcfg->plane_descriptors[plane_index];
		stream = &worksheet->orig_dispcfg->stream_descriptors[plane->stream_index];

		odm_combine_factor = worksheet->cur.config.odm_combine_overrides[plane->stream_index] > 0 ?
			worksheet->cur.config.odm_combine_overrides[plane->stream_index] :
			worksheet->validation_result.mode_support.cfg_support_info.stream_support_info[plane->stream_index].odms_used;
		if (odm_combine_factor == 1) {
			mpc_combine_factor = worksheet->cur.config.mpc_combine_overrides[plane_index] > 0 ?
				worksheet->cur.config.mpc_combine_overrides[plane_index] :
				(unsigned int)worksheet->validation_result.mode_support.cfg_support_info.plane_support_info[plane_index].dpps_used;
			num_dpps = mpc_combine_factor;
		} else {
			mpc_combine_factor = 1;
			num_dpps = odm_combine_factor;
		}

		if (odm_combine_factor > 1) {
			max_per_pipe_vp_p0 = plane->surface.plane0.width;
			temp = (unsigned int)math_ceil(
				plane->composition.scaler_info.plane0.h_ratio * stream->timing.h_active
				/ odm_combine_factor);
			if (temp < max_per_pipe_vp_p0)
				max_per_pipe_vp_p0 = temp;

			max_per_pipe_vp_p1 = plane->surface.plane1.width;
			temp = (unsigned int)math_ceil(
				plane->composition.scaler_info.plane1.h_ratio * stream->timing.h_active
				/ odm_combine_factor);
			if (temp < max_per_pipe_vp_p1)
				max_per_pipe_vp_p1 = temp;
		} else {
			max_per_pipe_vp_p0 = plane->surface.plane0.width / mpc_combine_factor;
			max_per_pipe_vp_p1 = plane->surface.plane1.width / mpc_combine_factor;
		}
		max_per_pipe_vp_p0 += 2 * MAX_PIXEL_OVERLAP;
		max_per_pipe_vp_p1 += MAX_PIXEL_OVERLAP;
		p0shift = 0;
		p1shift = 0;
		// The last element in the unshifted boundary array will always be the first pixel outside the
		// plane, which means theres no mcache associated with it, so -1
		num_boundaries =
			base_allocations[plane_index].num_mcaches_plane0 == 0 ?
			0 : base_allocations[plane_index].num_mcaches_plane0 - 1;
		if ((count_elements_in_span(base_allocations[plane_index].mcache_x_offsets_plane0, num_boundaries,
			max_per_pipe_vp_p0) <= 1) && (num_boundaries <= num_dpps)) {
			p0pass = true;
		}
		num_boundaries =
			base_allocations[plane_index].num_mcaches_plane1 == 0 ?
			0 : base_allocations[plane_index].num_mcaches_plane1 - 1;
		if ((count_elements_in_span(base_allocations[plane_index].mcache_x_offsets_plane1, num_boundaries,
			max_per_pipe_vp_p1) <= 1) && (num_boundaries <= num_dpps)) {
			p1pass = true;
		}
		if (!p0pass || !p1pass) {
			if (odm_combine_factor > 1) {
				num_dpps = odm_combine_factor;
				scaling_transform = plane->composition.scaling_transform;
			} else {
				num_dpps = mpc_combine_factor;
				scaling_transform = dml2_scaling_transform_fullscreen;
			}
			if (!p0pass) {
				if (plane->composition.viewport.stationary) {
					calculate_h_split_for_scaling_transform(plane->surface.plane0.width,
						stream->timing.h_active, num_dpps, scaling_transform,
						&worksheet->mcache.plane0.pipe_vp_startx[plane_index],
						&worksheet->mcache.plane0.pipe_vp_endx[plane_index]);
					p0pass = find_shift_for_valid_cache_id_assignment(
						base_allocations[plane_index].mcache_x_offsets_plane0,
						base_allocations[plane_index].num_mcaches_plane0,
						&worksheet->mcache.plane0.pipe_vp_startx[plane_index],
						&worksheet->mcache.plane0.pipe_vp_endx[plane_index], num_dpps,
						base_allocations[plane_index].shift_granularity.p0, &p0shift);
				}
			}
			if (!p1pass) {
				if (plane->composition.viewport.stationary) {
					calculate_h_split_for_scaling_transform(plane->surface.plane1.width,
						stream->timing.h_active, num_dpps, scaling_transform,
						&worksheet->mcache.plane0.pipe_vp_startx[plane_index],
						&worksheet->mcache.plane0.pipe_vp_endx[plane_index]);
					p1pass = find_shift_for_valid_cache_id_assignment(
						base_allocations[plane_index].mcache_x_offsets_plane1,
						base_allocations[plane_index].num_mcaches_plane1,
						&worksheet->mcache.plane1.pipe_vp_startx[plane_index],
						&worksheet->mcache.plane1.pipe_vp_endx[plane_index], num_dpps,
						base_allocations[plane_index].shift_granularity.p1, &p1shift);
				}
			}
		}
		if (p0pass && p1pass) {
			for (i = 0; i < base_allocations[plane_index].num_mcaches_plane0; i++)
				new_allocations[plane_index].mcache_x_offsets_plane0[i] =
				base_allocations[plane_index].mcache_x_offsets_plane0[i] - p0shift;
			for (i = 0; i < base_allocations[plane_index].num_mcaches_plane1; i++)
				new_allocations[plane_index].mcache_x_offsets_plane1[i] =
				base_allocations[plane_index].mcache_x_offsets_plane1[i] - p1shift;
		}
		worksheet->mcache.per_plane_status[plane_index] = p0pass && p1pass;
	}
}

static enum dml2_status dml2_pmo_dcn6_stage_optimizer_mcache_test_permissibility(
	struct dml2_pmo_stage_optimizer *stage, const struct dml2_optimization_worksheet *worksheet)
{
	if (!dml2_pmo_dcn5_stage_optimizer_mcache_test_total_mcache_limit(stage, worksheet))
		return DML2_STATUS_OPTIMIZE_FAIL_MCACHE;

	if (!dml2_pmo_dcn5_stage_optimizer_mcache_test_mcache_status(stage, worksheet))
		return DML2_STATUS_OPTIMIZE_FAIL_MCACHE;

	return DML2_STATUS_OK;
}

static bool dml2_pmo_dcn6_stage_optimizer_mcache_optimize_next(
	struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	if (!worksheet->validation_result.is_mode_support_valid
		|| !worksheet->validation_result.is_mcache_allocation_valid)
		/* validation has failed, stop optimizing further */
		return false;

	if (stage->test_permissibility(stage, worksheet) == DML2_STATUS_OK)
		/* optimization is permissible, no need to optimize further */
		return false;

	if (worksheet->mcache.is_default_pipe_usage_attempted) {
		if (!dml2_pmo_dcn5_stage_optimizer_mcache_increment_pipe_usage(stage, worksheet))
			return false;
	} else {
		dml2_pmo_dcn5_stage_optimizer_mcache_apply_default_pipe_usage(stage, worksheet);
		worksheet->mcache.is_default_pipe_usage_attempted = true;
	}

	dml2_pmo_dcn6_stage_optimizer_mcache_decide_shifts(stage, worksheet);
	worksheet->cur.unvalidated_change.bits.mcache_allocation = true;

	return true;
}

void dml2_pmo_dcn6_stage_optimizer_mcache_create(struct dml2_pmo_instance *pmo,
	struct dml2_pmo_stage_optimizer *stage)
{
	stage->pmo = pmo;
	stage->func_locals = &pmo->scratch.pmo_dcn5.func_locals;
	stage->init = dml2_pmo_dcn5_stage_optimizer_mcache_init;
	stage->optimize_next = dml2_pmo_dcn6_stage_optimizer_mcache_optimize_next;
	stage->test_permissibility =
		dml2_pmo_dcn6_stage_optimizer_mcache_test_permissibility;
}

static void dml2_pmo_dcn6_stage_optimizer_vmin_dcfclk_init(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	const struct dml2_utm_soc_bb *utm_soc_bb = stage->pmo->utm_soc_bb;

	DML_LOG_COMP_IF_ENTER();
	worksheet->dcfclk_vmin.max_available_bandwidth_kbps =
			utm_soc_bb->vmin_limit.dcfclk_khz * utm_soc_bb->return_bus_width_bytes;
	DML_LOG_COMP_IF_EXIT();
}

static bool dml2_pmo_dcn6_stage_optimizer_vmin_dcfclk_optimize_next(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	bool should_continue = true;
	const struct dml2_utm_soc_bb *utm_soc_bb = stage->pmo->utm_soc_bb;
	const struct dml2_sop_table *sop_table = &utm_soc_bb->sop_table;

	DML_LOG_COMP_IF_ENTER();
	if (utm_soc_bb->vmin_limit.dcfclk_khz == 0) {
		/* vmin limit for dcfclk is not configured in soc bb */
		should_continue = false;
		goto exit;
	}

	if (worksheet->cur.config.enable_vmin_dcfclk) {
		/* vmin dcfclk is already enabled, nothing else to try */
		should_continue = false;
		goto exit;
	}

	if (sop_table->sop_optimal_dcfclks_khz[worksheet->cur.config.min_sop_index] <= utm_soc_bb->vmin_limit.dcfclk_khz) {
		/* current sop optimal dcfclk is already less than vmin dcfclk */
		should_continue = false;
		goto exit;
	}
	if (worksheet->validation_result.mode_support.bandwidth_upper_bound.dcn5.urgent_bandwidth_kbps
	> worksheet->dcfclk_vmin.max_available_bandwidth_kbps) {
		/*
		 * required urgent bandwidth exceeds max bandwidth available, reducing dcfclk will only increase
		 * bandwidth requirements even more. It is guaranteed to fail bandwidth validation. No need to attempt.
		 */
		should_continue = false;
		goto exit;
	}

	worksheet->cur.unvalidated_change.bits.dcfclk_override = true;
	worksheet->cur.config.enable_vmin_dcfclk = true;
exit:
	DML_LOG_DEBUG("%s exit with should_continue = %s\n", __func__, should_continue ? "true" : "false");
	DML_LOG_COMP_IF_EXIT();
	return should_continue;
}

static enum dml2_status dml2_pmo_dcn6_stage_optimizer_vmin_dcfclk_test_permissibility(
		struct dml2_pmo_stage_optimizer *stage, const struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	enum dml2_status status = worksheet->cur.config.enable_vmin_dcfclk ?
			DML2_STATUS_OK : DML2_STATUS_OPTIMIZE_FAIL_VMIN_DCFCLK;

	DML_LOG_COMP_IF_ENTER();
	DML_LOG_DEBUG("%s exit with status = %s\n", __func__, dml2_status_str(status));
	DML_LOG_COMP_IF_EXIT();
	return status;
}

void dml2_pmo_dcn6_stage_optimizer_vmin_dcfclk_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage)
{
	stage->pmo = pmo;
	stage->func_locals = &pmo->scratch.pmo_dcn5.func_locals;
	stage->init = dml2_pmo_dcn6_stage_optimizer_vmin_dcfclk_init;
	stage->optimize_next = dml2_pmo_dcn6_stage_optimizer_vmin_dcfclk_optimize_next;
	stage->test_permissibility =
			dml2_pmo_dcn6_stage_optimizer_vmin_dcfclk_test_permissibility;
}

static void dml2_pmo_dcn6_stage_optimizer_fclk_ppt_temp_read_pstate_init(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	DML_LOG_COMP_IF_ENTER();
	worksheet->fclk_ppt_temp_read_pstate.is_attempted = false;
	memset(&stage->func_locals->fclk_ppt_temp_read_pstate_optimize, 0,
			sizeof(struct dml2_stage_optimizer_fclk_ppt_temp_read_pstate_optimize_locals));
	DML_LOG_COMP_IF_EXIT();
}

static bool dml2_pmo_dcn6_stage_optimizer_fclk_ppt_temp_read_pstate_optimize_next(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	const struct dml2_pmo_instance *pmo = stage->pmo;
	const enum dml2_pstate_type pstate_type_list[3] = {
		dml2_pstate_type_fclk,
		dml2_pstate_type_ppt,
		dml2_pstate_type_temp_read,
	};
	struct dml2_stage_optimizer_fclk_ppt_temp_read_pstate_optimize_locals *l
			= &stage->func_locals->fclk_ppt_temp_read_pstate_optimize;
	const struct dml2_display_cfg *display_config = worksheet->orig_dispcfg;
	unsigned int stream_index, plane_index, plane_mask;
	unsigned int i;
	bool modified = false;

	DML_LOG_COMP_IF_ENTER();

	if (worksheet->fclk_ppt_temp_read_pstate.is_attempted) {
		DML_LOG_COMP_IF_EXIT();
		return false;
	}

	worksheet->fclk_ppt_temp_read_pstate.is_attempted = true;

	if (display_config->overrides.all_streams_blanked)
		goto exit;

	for (i = 0; i < 3; i++) {
		dcn6_get_params_for_pstate_type(pmo, worksheet,
				pstate_type_list[i],
				&l->pstate_allow_delay_us,
				&l->pstate_blackout_us,
				&l->pstate_watermark_us);

		if (l->pstate_blackout_us <= 0.0)
			continue;

		for (stream_index = 0; stream_index < display_config->num_streams; stream_index++) {
			plane_mask = 0;
			for (plane_index = 0; plane_index < display_config->num_planes; plane_index++) {
				if (display_config->plane_descriptors[plane_index].stream_index == stream_index)
					set_bit_in_bitfield(&plane_mask, plane_index);
			}

			l->per_stream_pstate_method[stream_index] = dml2_pstate_method_vactive;

			dcn6_build_pstate_meta_per_stream(display_config,
					pmo->ip_caps,
					worksheet,
					pstate_type_list[i],
					l->pstate_watermark_us,
					l->pstate_blackout_us,
					l->pstate_allow_delay_us,
					stream_index,
					plane_mask,
					&l->per_stream_pstate_meta[stream_index]);
		}

		/* always update the worksheet with latest requirements */
		if (dcn6_update_worksheet_for_pstate_admissibility(worksheet,
				l->per_stream_pstate_meta, pstate_type_list[i])) {
			modified = true;
			DML_LOG_VERBOSE("fclk_ppt_temp_read pstate not admissible with current worksheet, adjusting the reserved vblank time\n");
		}
	}

exit:
	DML_LOG_DEBUG("%s exit with should_continue = %s\n", __func__, modified ? "true" : "false");
	DML_LOG_COMP_IF_EXIT();
	return modified;
}

static enum dml2_status dml2_pmo_dcn6_stage_optimizer_fclk_ppt_temp_read_pstate_test_permissibility(
		struct dml2_pmo_stage_optimizer *stage, const struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	(void)worksheet;
	DML_LOG_COMP_IF_ENTER();
	DML_LOG_COMP_IF_EXIT();
	return DML2_STATUS_OK;
}

void dml2_pmo_dcn6_stage_optimizer_fclk_ppt_temp_read_pstate_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage)
{
	stage->pmo = pmo;
	stage->func_locals = &pmo->scratch.pmo_dcn5.func_locals;
	stage->init = dml2_pmo_dcn6_stage_optimizer_fclk_ppt_temp_read_pstate_init;
	stage->optimize_next = dml2_pmo_dcn6_stage_optimizer_fclk_ppt_temp_read_pstate_optimize_next;
	stage->test_permissibility =
			dml2_pmo_dcn6_stage_optimizer_fclk_ppt_temp_read_pstate_test_permissibility;
}
