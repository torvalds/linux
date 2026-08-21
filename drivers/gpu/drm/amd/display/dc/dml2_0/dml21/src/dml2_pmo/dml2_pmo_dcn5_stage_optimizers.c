// SPDX-License-Identifier: MIT
//
// Copyright 2024-2025 Advanced Micro Devices, Inc.

#include "dml2_pmo_dcn5_stage_optimizers.h"
#include "dml2_debug.h"
#include "lib_float_math.h"

static bool dml2_pmo_dcn5_stage_optimizer_mcache_increment_pipe_usage_with_odm(struct dml2_pmo_stage_optimizer *stage,
		struct dml2_optimization_worksheet *worksheet)
{
	unsigned int i;
	const struct dml2_display_cfg *orig_dispcfg = worksheet->orig_dispcfg;
	const struct core_stream_support_info *stream_support_info;
	unsigned int stream_idx;
	bool is_incremented = false;

	for (i = 0; i < orig_dispcfg->num_planes; i++) {
		stream_idx = orig_dispcfg->plane_descriptors[i].stream_index;
		stream_support_info = &worksheet->validation_result.mode_support.cfg_support_info.stream_support_info[stream_idx];
		if (!worksheet->mcache.per_plane_status[i]) {
			if (worksheet->cur.unvalidated_change.bits.odm_combine_overrides) {
				if (worksheet->cur.config.odm_combine_overrides[stream_idx] >= (unsigned int)stage->pmo->odm_combine_limit)
					return false;
			} else if (stream_support_info->odms_used >= (unsigned int)stage->pmo->odm_combine_limit) {
				return false;
			}
		}
	}

	for (i = 0; i < orig_dispcfg->num_planes; i++) {
		if (worksheet->mcache.per_plane_status[i])
			continue;

		stream_idx = orig_dispcfg->plane_descriptors[i].stream_index;
		if (worksheet->cur.unvalidated_change.bits.odm_combine_overrides) {
			worksheet->cur.config.odm_combine_overrides[stream_idx]++;
		} else {
			worksheet->cur.config.odm_combine_overrides[stream_idx] =
				worksheet->validation_result.mode_support.cfg_support_info.stream_support_info[stream_idx].odms_used + 1;
		}
		is_incremented = true;
	}

	return is_incremented;
}

static bool dml2_pmo_dcn5_stage_optimizer_mcache_increment_pipe_usage_with_mpc(struct dml2_pmo_stage_optimizer *stage,
		struct dml2_optimization_worksheet *worksheet)
{
	unsigned int i;
	const struct dml2_display_cfg *orig_dispcfg = worksheet->orig_dispcfg;
	bool is_incremented = false;

	for (i = 0; i < orig_dispcfg->num_planes; i++)
		if (worksheet->mcache.per_plane_status[i] == false
				&& worksheet->validation_result.mode_support.cfg_support_info.plane_support_info[i].dpps_used >= stage->pmo->mpc_combine_limit)
			return false;

	for (i = 0; i < orig_dispcfg->num_planes; i++) {
		if (worksheet->mcache.per_plane_status[i])
			continue;

		worksheet->cur.config.mpc_combine_overrides[i] =
				worksheet->validation_result.mode_support.cfg_support_info.plane_support_info[i].dpps_used + 1;
		is_incremented = true;
	}

	return is_incremented;
}

bool dml2_pmo_dcn5_stage_optimizer_mcache_increment_pipe_usage(struct dml2_pmo_stage_optimizer *stage,
		struct dml2_optimization_worksheet *worksheet)
{
	bool result;

	if (worksheet->mcache.is_single_stream_odm_case) {
		result = dml2_pmo_dcn5_stage_optimizer_mcache_increment_pipe_usage_with_odm(stage, worksheet);
		if (result)
			worksheet->cur.unvalidated_change.bits.odm_combine_overrides = true;
	} else {
		result = dml2_pmo_dcn5_stage_optimizer_mcache_increment_pipe_usage_with_mpc(stage, worksheet);
		if (result)
			worksheet->cur.unvalidated_change.bits.mpc_combine_overrides = true;
	}

	return result;
}

bool dml2_pmo_dcn5_stage_optimizer_mcache_test_total_mcache_limit(struct dml2_pmo_stage_optimizer *stage,
		const struct dml2_optimization_worksheet *worksheet)
{
	unsigned int total_mcaches_required = 0;
	unsigned int i;
	const struct dml2_mcache_surface_allocation *allocation = worksheet->validation_result.mcache_allocations;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (allocation[i].valid) {
			total_mcaches_required += allocation[i].num_mcaches_plane0
					+ allocation[i].num_mcaches_plane1;
			if (allocation[i].last_slice_sharing.plane0_plane1)
				total_mcaches_required--;
		}
	}
	return total_mcaches_required <= stage->pmo->utm_soc_bb->num_dcc_mcaches;
}

bool dml2_pmo_dcn5_stage_optimizer_mcache_test_mcache_status(struct dml2_pmo_stage_optimizer *stage,
		const struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	unsigned int i;

	for (i = 0; i < DML2_MAX_PLANES; i++)
		if (worksheet->orig_dispcfg->plane_descriptors[i].surface.dcc.enable &&
				!worksheet->mcache.per_plane_status[i])
			return false;
	return true;
}

static bool dml2_pmo_dcn5_stage_optimizer_mcache_test_mcache_pipe_limit(struct dml2_pmo_stage_optimizer *stage,
	const struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;

	unsigned int total_mcaches_required = 0;
	unsigned int total_pipe_usage = 0;
	unsigned int i;
	const struct dml2_mcache_surface_allocation *allocation = worksheet->validation_result.mcache_allocations;
	const struct dml2_display_cfg *orig_dispcfg = worksheet->orig_dispcfg;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (allocation[i].valid) {
			total_mcaches_required += allocation[i].num_mcaches_plane0
				+ allocation[i].num_mcaches_plane1;
			if (allocation[i].last_slice_sharing.plane0_plane1)
				total_mcaches_required--;
		}
	}

	for (i = 0; i < orig_dispcfg->num_planes; i++)
		if (worksheet->cur.config.mpc_combine_overrides[i])
			total_pipe_usage += worksheet->cur.config.mpc_combine_overrides[i];
		else if (worksheet->cur.config.odm_combine_overrides[orig_dispcfg->plane_descriptors[i].stream_index])
			total_pipe_usage += worksheet->cur.config.odm_combine_overrides[orig_dispcfg->plane_descriptors[i].stream_index];
		else
			total_pipe_usage += worksheet->validation_result.mode_support.cfg_support_info.plane_support_info[i].dpps_used;

	return total_pipe_usage >= total_mcaches_required;

}

void dml2_pmo_dcn5_stage_optimizer_mcache_apply_default_pipe_usage(struct dml2_pmo_stage_optimizer *stage,
	struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	unsigned int i;

	if (!worksheet->validation_result.is_mcache_allocation_valid)
		return;

	for (i = 0; i < worksheet->orig_dispcfg->num_planes; i++)
		if (worksheet->validation_result.mcache_allocations[i].valid)
			memcpy(&worksheet->cur.config.mcache_allocations[i],
					&worksheet->validation_result.mcache_allocations[i],
					sizeof(struct dml2_mcache_surface_allocation));
}

void dml2_pmo_dcn5_stage_optimizer_mcache_init(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	DML_LOG_COMP_IF_ENTER();
	if (worksheet->orig_dispcfg->num_streams == 1
			&& worksheet->validation_result.mode_support.cfg_support_info.stream_support_info[0].odms_used > 1)
		worksheet->mcache.is_single_stream_odm_case = true;
	DML_LOG_COMP_IF_EXIT();
}

static void dml2_pmo_dcn5_stage_optimizer_mcache_decide_plane_status(
	struct dml2_pmo_stage_optimizer *stage,
	struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	const struct dml2_mcache_surface_allocation *allocation = worksheet->validation_result.mcache_allocations;
	const struct dml2_display_cfg *orig_dispcfg = worksheet->orig_dispcfg;

	for (unsigned int plane_index = 0; plane_index < orig_dispcfg->num_planes; plane_index++) {
		if (!orig_dispcfg->plane_descriptors[plane_index].surface.dcc.enable)
			continue;

		unsigned int pipe_usage = 0;
		unsigned int mcaches_required = 0;
		int stream_index = orig_dispcfg->plane_descriptors[plane_index].stream_index;

		if (worksheet->cur.config.mpc_combine_overrides[plane_index]) {
			pipe_usage = worksheet->cur.config.mpc_combine_overrides[plane_index];
		} else if (worksheet->cur.config.odm_combine_overrides[stream_index]) {
			pipe_usage = worksheet->cur.config.odm_combine_overrides[stream_index];
		} else {
			pipe_usage = worksheet->validation_result.mode_support.cfg_support_info
				.plane_support_info[plane_index].dpps_used;
		}

		if (allocation[plane_index].valid) {
			mcaches_required = allocation[plane_index].num_mcaches_plane0 +
				allocation[plane_index].num_mcaches_plane1;

			if (allocation[plane_index].last_slice_sharing.plane0_plane1)
				mcaches_required--;
		}

		worksheet->mcache.per_plane_status[plane_index] = (pipe_usage >= mcaches_required);
	}
}

static bool dml2_pmo_dcn5_stage_optimizer_mcache_optimize_next(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	bool should_continue = true;

	DML_LOG_COMP_IF_ENTER();
	if (!worksheet->validation_result.is_mcache_allocation_valid) {
		/* validation has failed, stop optimizing further */
		should_continue = false;
		goto exit;
	}

	if (worksheet->validation_result.is_mode_support_valid && stage->test_permissibility(stage, worksheet) == DML2_STATUS_OK) {
		/* optimization is permissible, no need to optimize further */
		should_continue = false;
		goto exit;
	}

	if (worksheet->mcache.is_default_pipe_usage_attempted) {
		if (!dml2_pmo_dcn5_stage_optimizer_mcache_increment_pipe_usage(stage, worksheet)) {
			should_continue = false;
			goto exit;
		}
	} else {
		dml2_pmo_dcn5_stage_optimizer_mcache_apply_default_pipe_usage(stage, worksheet);
		worksheet->mcache.is_default_pipe_usage_attempted = true;
	}

	dml2_pmo_dcn5_stage_optimizer_mcache_decide_plane_status(stage, worksheet);
	worksheet->cur.unvalidated_change.bits.mcache_allocation = true;
exit:
	DML_LOG_DEBUG("%s exit with should_continue = %s\n", __func__, should_continue ? "true" : "false");
	DML_LOG_COMP_IF_EXIT();
	return should_continue;
}

static enum dml2_status dml2_pmo_dcn5_stage_optimizer_mcache_test_permissibility(
		struct dml2_pmo_stage_optimizer *stage, const struct dml2_optimization_worksheet *worksheet)
{
	enum dml2_status status = DML2_STATUS_OK;

	DML_LOG_COMP_IF_ENTER();
	if (!dml2_pmo_dcn5_stage_optimizer_mcache_test_total_mcache_limit(stage, worksheet)) {
		status = DML2_STATUS_OPTIMIZE_FAIL_MCACHE;
		goto exit;
	}

	if (!dml2_pmo_dcn5_stage_optimizer_mcache_test_mcache_status(stage, worksheet)) {
		status = DML2_STATUS_OPTIMIZE_FAIL_MCACHE;
		goto exit;
	}

	if (!dml2_pmo_dcn5_stage_optimizer_mcache_test_mcache_pipe_limit(stage, worksheet)) {
		status = DML2_STATUS_OPTIMIZE_FAIL_MCACHE;
		goto exit;
	}
exit:
	DML_LOG_DEBUG("%s exit with status = %s\n", __func__, dml2_status_str(status));
	DML_LOG_COMP_IF_EXIT();
	return status;
}

void dml2_pmo_dcn5_stage_optimizer_mcache_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage)
{
	stage->pmo = pmo;
	stage->func_locals = &pmo->scratch.pmo_dcn5.func_locals;
	stage->init = dml2_pmo_dcn5_stage_optimizer_mcache_init;
	stage->optimize_next = dml2_pmo_dcn5_stage_optimizer_mcache_optimize_next;
	stage->test_permissibility =
			dml2_pmo_dcn5_stage_optimizer_mcache_test_permissibility;
}

void set_bit_in_bitfield(unsigned int *bit_field, unsigned int bit_offset)
{
	*bit_field = *bit_field | (0x1 << bit_offset);
}

bool is_bit_set_in_bitfield(unsigned int bit_field, unsigned int bit_offset)
{
	if (bit_field & (0x1 << bit_offset))
		return true;

	return false;
}

int dcn5_get_vactive_pstate_margin(const struct dml2_validation_result *validation_res, int plane_mask)
{
	unsigned char i;
	int min_vactive_margin_us = 0xFFFFFFF;

	if (!validation_res->is_mode_support_valid)
		return min_vactive_margin_us;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (validation_res->mode_support.cfg_support_info.plane_support_info[i].dram_change_latency_hiding_margin_in_active < min_vactive_margin_us)
				min_vactive_margin_us = validation_res->mode_support.cfg_support_info.plane_support_info[i].dram_change_latency_hiding_margin_in_active;
		}
	}

	return min_vactive_margin_us;
}

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
	case dml2_uclk_pstate_change_strategy_force_mall_svp:
	case dml2_uclk_pstate_change_strategy_force_alternate:
	case dml2_uclk_pstate_change_strategy_force_mall_full_frame:
	case dml2_uclk_pstate_change_strategy_auto:
	default:
		method = dml2_pstate_method_na;
	}

	return method;
}

void dcn5_build_method_scheduling_params(
	struct dml2_pstate_per_method_common_meta *stream_method_pstate_meta,
	const struct dml2_pstate_meta *stream_pstate_meta)
{
	if (stream_method_pstate_meta->allow_start_otg_vline < 0.0 ||
			stream_method_pstate_meta->allow_end_otg_vline < 0.0 ||
			stream_method_pstate_meta->allow_start_otg_vline > stream_method_pstate_meta->allow_end_otg_vline) {
		/* method is not schedulable */
		stream_method_pstate_meta->allow_time_us = 0.0;
		stream_method_pstate_meta->disallow_time_us = stream_method_pstate_meta->period_us;
		return;
	}

	stream_method_pstate_meta->allow_time_us =
			(double)(stream_method_pstate_meta->allow_end_otg_vline - stream_method_pstate_meta->allow_start_otg_vline) *
			stream_pstate_meta->otg_vline_time_us;
	if (stream_method_pstate_meta->allow_time_us >= stream_method_pstate_meta->period_us) {
		/* when allow wave overlaps an entire frame, it is always schedulable (DRR can do this)*/
		stream_method_pstate_meta->disallow_time_us = 0.0;
	} else {
		stream_method_pstate_meta->disallow_time_us =
				stream_method_pstate_meta->period_us - stream_method_pstate_meta->allow_time_us;
	}
}

static void dcn5_build_pstate_meta_per_stream(const struct dml2_display_cfg *display_cfg,
	const struct dml2_ip_capabilities *ip_caps,
	double blackout_us,
	double det_fill_delay_us,
	double extra_vactive_allow_time_us,
	int stream_index,
	/* output */
	struct dml2_pstate_meta *stream_pstate_meta)
{
	const struct dml2_stream_parameters *stream_descriptor = &display_cfg->stream_descriptors[stream_index];
	const struct dml2_timing_cfg *timing                   = &stream_descriptor->timing;

	/* worst case all other streams require some programming at the same time, 0 if only 1 stream */
	unsigned int contention_delay_us = (ip_caps->fams2.vertical_interrupt_ack_delay_us +
		(unsigned int)math_max2(ip_caps->fams2.drr_programming_delay_us, ip_caps->fams2.allow_programming_delay_us)) *
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
			stream_pstate_meta->max_vtotal = (unsigned int)math_floor((double)stream_descriptor->timing.pixel_clock_khz /
				((double)stream_descriptor->timing.drr_config.min_refresh_uhz * stream_descriptor->timing.h_total) * 1e9);
		} else {
			/* assume min of 48Hz */
			stream_pstate_meta->max_vtotal = (unsigned int)math_floor((double)stream_descriptor->timing.pixel_clock_khz /
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
		(unsigned int)math_ceil(ip_caps->fams2.scheduling_delay_us / stream_pstate_meta->otg_vline_time_us);
	stream_pstate_meta->vertical_interrupt_ack_delay_otg_vlines =
		(unsigned int)math_ceil(ip_caps->fams2.vertical_interrupt_ack_delay_us / stream_pstate_meta->otg_vline_time_us);
	stream_pstate_meta->contention_delay_otg_vlines =
		(unsigned int)math_ceil(contention_delay_us / stream_pstate_meta->otg_vline_time_us);
	/* worst case allow to target needs to account for all streams' allow events overlapping, and 1 line for error */
	stream_pstate_meta->allow_to_target_delay_otg_vlines =
		(unsigned int)(math_ceil((ip_caps->fams2.vertical_interrupt_ack_delay_us + contention_delay_us + ip_caps->fams2.allow_programming_delay_us) / stream_pstate_meta->otg_vline_time_us)) + 1;
	stream_pstate_meta->min_allow_width_otg_vlines =
		(unsigned int)math_ceil(ip_caps->fams2.min_allow_width_us / stream_pstate_meta->otg_vline_time_us);
	/* this value should account for urgent latency */
	stream_pstate_meta->blackout_otg_vlines = (unsigned int)math_ceil(blackout_us /	stream_pstate_meta->otg_vline_time_us);

	/* scheduling params should be built based on the worst case for allow_time:disallow_time */

	/* vactive */
	if (display_cfg->num_streams == 1) {
		/* for single stream, guarantee at least an instant of allow */
		stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines = (unsigned int)math_floor(
			math_max2(0.0,
				timing->v_active - math_max2(1.0, stream_pstate_meta->min_allow_width_otg_vlines) - stream_pstate_meta->blackout_otg_vlines));
	} else {
		/* for multi stream, bound to a max fill time defined by IP caps */
		stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines =
			(unsigned int)math_max2(1.0, math_floor(det_fill_delay_us / stream_pstate_meta->otg_vline_time_us));
	}
	stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_us = stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines * stream_pstate_meta->otg_vline_time_us;

	if (stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_us > 0.0) {
		stream_pstate_meta->method_vactive.common.allow_start_otg_vline =
			timing->v_blank_end + stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines;
		stream_pstate_meta->method_vactive.common.allow_end_otg_vline   =
			stream_pstate_meta->vblank_start -
			stream_pstate_meta->blackout_otg_vlines +
			(unsigned int)(extra_vactive_allow_time_us / stream_pstate_meta->otg_vline_time_us);
	} else {
		stream_pstate_meta->method_vactive.common.allow_start_otg_vline = 0;
		stream_pstate_meta->method_vactive.common.allow_end_otg_vline   = 0;
	}
	stream_pstate_meta->method_vactive.common.period_us = stream_pstate_meta->nom_frame_time_us;

	/* vblank */
	stream_pstate_meta->method_vblank.common.allow_start_otg_vline = stream_pstate_meta->vblank_start;
	stream_pstate_meta->method_vblank.common.period_us             = stream_pstate_meta->nom_frame_time_us;
	stream_pstate_meta->method_vblank.common.allow_end_otg_vline   =
		stream_pstate_meta->method_vblank.common.allow_start_otg_vline + 1;

	/* drr */
	stream_pstate_meta->method_drr.common.period_us = stream_pstate_meta->nom_frame_time_us;
	stream_pstate_meta->method_drr.programming_delay_otg_vlines =
		(unsigned int)math_ceil(ip_caps->fams2.drr_programming_delay_us / stream_pstate_meta->otg_vline_time_us);
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

	dcn5_build_method_scheduling_params(&stream_pstate_meta->method_vactive.common, stream_pstate_meta);
	dcn5_build_method_scheduling_params(&stream_pstate_meta->method_vblank.common, stream_pstate_meta);
	dcn5_build_method_scheduling_params(&stream_pstate_meta->method_drr.common, stream_pstate_meta);
}

void dcn5_build_synchronized_timing_groups(
	// Output
	struct dml2_pmo_synchronized_timing_groups *s,
	// Input
	const struct dml2_display_cfg *display_config)
{
	unsigned int i, j;
	const struct dml2_timing_cfg *master_timing;

	unsigned int stream_mapped_mask                        = 0;
	unsigned int num_timing_groups                         = 0;
	unsigned int timing_group_idx                          = 0;

	/* clear all group masks */
	memset(s->synchronized_timing_group_masks, 0, sizeof(s->synchronized_timing_group_masks));
	memset(s->group_is_drr_enabled, 0, sizeof(s->group_is_drr_enabled));
	memset(s->group_is_drr_active, 0, sizeof(s->group_is_drr_active));
	memset(s->group_line_time_us, 0, sizeof(s->group_line_time_us));
	s->num_timing_groups = 0;

	for (i = 0; i < display_config->num_streams; i++) {
		master_timing = &display_config->stream_descriptors[i].timing;

		/* only need to build group of this stream is not in a group already */
		if (is_bit_set_in_bitfield(stream_mapped_mask, i)) {
			continue;
		}

		set_bit_in_bitfield(&stream_mapped_mask, i);
		timing_group_idx = num_timing_groups;
		num_timing_groups++;

		/* trivially set default timing group to itself */
		set_bit_in_bitfield(&s->synchronized_timing_group_masks[timing_group_idx], i);
		s->group_line_time_us[timing_group_idx] = (double)master_timing->h_total / master_timing->pixel_clock_khz * 1000.0;

		/* if drr is in use, timing is not sychnronizable */
		if (master_timing->drr_config.enabled) {
			s->group_is_drr_enabled[timing_group_idx] = true;
			s->group_is_drr_active[timing_group_idx]  = !master_timing->drr_config.disallowed &&
				(master_timing->drr_config.drr_active_fixed || master_timing->drr_config.drr_active_variable);
			continue;
		}

		/* find synchronizable timing groups */
		for (j = i + 1; j < display_config->num_streams; j++) {
			if (memcmp(master_timing,
				&display_config->stream_descriptors[j].timing,
				sizeof(struct dml2_timing_cfg)) == 0) {
				set_bit_in_bitfield(&s->synchronized_timing_group_masks[timing_group_idx], j);
				set_bit_in_bitfield(&stream_mapped_mask, j);
			}
		}
	}

	s->num_timing_groups = num_timing_groups;
}

void dcn5_insert_strategy_into_expanded_list(
	const struct dml2_pmo_pstate_strategy *per_stream_pstate_strategy,
	const int stream_count,
	struct dml2_pmo_pstate_strategy *expanded_strategy_list,
	unsigned int *num_expanded_strategies)
{
	(void)stream_count;
	if (expanded_strategy_list && num_expanded_strategies) {
		memcpy(&expanded_strategy_list[*num_expanded_strategies], per_stream_pstate_strategy, sizeof(struct dml2_pmo_pstate_strategy));

		(*num_expanded_strategies)++;
	}
}

static enum dml2_pstate_method convert_strategy_to_drr_variant(const enum dml2_pstate_method base_strategy)
{
	enum dml2_pstate_method variant_strategy = 0;

	switch (base_strategy) {
	case dml2_pstate_method_vactive:
		variant_strategy = dml2_pstate_method_fw_vactive_drr;
		break;
	case dml2_pstate_method_vblank:
		variant_strategy = dml2_pstate_method_fw_vblank_drr;
		break;
	case dml2_pstate_method_fw_svp:
	case dml2_pstate_method_alternate:
	case dml2_pstate_method_fw_vactive_drr:
	case dml2_pstate_method_fw_vblank_drr:
	case dml2_pstate_method_fw_svp_drr:
	case dml2_pstate_method_fw_drr:
	case dml2_pstate_method_reserved_hw:
	case dml2_pstate_method_reserved_fw:
	case dml2_pstate_method_reserved_fw_drr_clamped:
	case dml2_pstate_method_reserved_fw_drr_var:
	case dml2_pstate_method_count:
	case dml2_pstate_method_na:
	default:
		/* no variant for this mode */
		variant_strategy = base_strategy;
	}

	return variant_strategy;
}

bool dcn5_is_variant_method_valid(const struct dml2_pmo_pstate_strategy *base_strategy,
		const struct dml2_pmo_pstate_strategy *variant_strategy,
		const unsigned int num_streams_per_base_method[PMO_DCN4_MAX_DISPLAYS],
		const unsigned int num_streams_per_variant_method[PMO_DCN4_MAX_DISPLAYS],
		const unsigned int stream_count)
{
	(void)variant_strategy;
	bool valid = true;
	unsigned int i;

	/* check all restrictions are met */
	for (i = 0; i < stream_count; i++) {
		/* vblank + vblank_drr variants are invalid */
		if (base_strategy->per_stream_pstate_method[i] == dml2_pstate_method_vblank &&
				((num_streams_per_base_method[i] > 0 && num_streams_per_variant_method[i] > 0) ||
				num_streams_per_variant_method[i] > 1)) {
			valid = false;
			break;
		}
	}

	return valid;
}

void dcn5_expand_base_strategy(
	const struct dml2_pmo_pstate_strategy *base_strategy,
	const unsigned int stream_count,
	struct dml2_pmo_pstate_strategy *expanded_strategy_list,
	unsigned int *num_expanded_strategies)
{
	bool skip_to_next_stream;
	bool expanded_strategy_added;
	bool skip_iteration;
	unsigned int i, j;
	unsigned int num_streams_per_method[PMO_DCN4_MAX_DISPLAYS] = { 0 };
	unsigned int stream_iteration_indices[PMO_DCN4_MAX_DISPLAYS] = { 0 };
	struct dml2_pmo_pstate_strategy cur_strategy_list = { 0 };

	/* determine number of displays per method */
	for (i = 0; i < stream_count; i++) {
		/* increment the count of the earliest index with the same method */
		for (j = 0; j < stream_count; j++) {
			if (base_strategy->per_stream_pstate_method[i] == base_strategy->per_stream_pstate_method[j]) {
				num_streams_per_method[j] = num_streams_per_method[j] + 1;
				break;
			}
		}
	}

	cur_strategy_list.allow_state_increase = base_strategy->allow_state_increase;

	i = 0;
	/* uses a while loop instead of recursion to build permutations of base strategy */
	while (stream_iteration_indices[0] < stream_count) {
		skip_to_next_stream = false;
		expanded_strategy_added = false;
		skip_iteration = false;

		/* determine what to do for this iteration */
		if (stream_iteration_indices[i] < stream_count && num_streams_per_method[stream_iteration_indices[i]] != 0) {
			/* decrement count and assign method */
			cur_strategy_list.per_stream_pstate_method[i] = base_strategy->per_stream_pstate_method[stream_iteration_indices[i]];
			num_streams_per_method[stream_iteration_indices[i]] -= 1;

			if (i >= stream_count - 1) {
				/* insert into strategy list */
				dcn5_insert_strategy_into_expanded_list(&cur_strategy_list, stream_count, expanded_strategy_list, num_expanded_strategies);
				expanded_strategy_added = true;
			} else {
				/* skip to next stream */
				skip_to_next_stream = true;
			}
		} else {
			skip_iteration = true;
		}

		/* prepare for next iteration */
		if (skip_to_next_stream) {
			i++;
		} else {
			/* restore count */
			if (!skip_iteration) {
				num_streams_per_method[stream_iteration_indices[i]] += 1;
			}

			/* increment iteration count */
			stream_iteration_indices[i]++;

			/* if iterations are complete, or last stream was reached */
			if ((stream_iteration_indices[i] >= stream_count || expanded_strategy_added) && i > 0) {
				/* reset per stream index, decrement i */
				stream_iteration_indices[i] = 0;
				i--;

				/* restore previous stream's count and increment index */
				num_streams_per_method[stream_iteration_indices[i]] += 1;
				stream_iteration_indices[i]++;
			}
		}
	}
}

void dcn5_expand_variant_strategy(
		const struct dml2_pmo_pstate_strategy *base_strategy,
		const unsigned int stream_count,
		const bool should_permute,
		struct dml2_pmo_pstate_strategy *expanded_strategy_list,
		unsigned int *num_expanded_strategies)
{
	bool variant_found;
	unsigned int i, j;
	unsigned int method_index;
	unsigned int stream_index;
	unsigned int num_streams_per_method[PMO_DCN4_MAX_DISPLAYS] = { 0 };
	unsigned int num_streams_per_base_method[PMO_DCN4_MAX_DISPLAYS] = { 0 };
	unsigned int num_streams_per_variant_method[PMO_DCN4_MAX_DISPLAYS] = { 0 };
	enum dml2_pstate_method per_stream_variant_method[DML2_MAX_PLANES];
	struct dml2_pmo_pstate_strategy variant_strategy = { 0 };

	/* determine number of displays per method */
	for (i = 0; i < stream_count; i++) {
		/* increment the count of the earliest index with the same method */
		for (j = 0; j < stream_count; j++) {
			if (base_strategy->per_stream_pstate_method[i] == base_strategy->per_stream_pstate_method[j]) {
				num_streams_per_method[j] = num_streams_per_method[j] + 1;
				break;
			}
		}

		per_stream_variant_method[i] = convert_strategy_to_drr_variant(base_strategy->per_stream_pstate_method[i]);
	}
	memcpy(num_streams_per_base_method, num_streams_per_method, sizeof(unsigned int) * PMO_DCN4_MAX_DISPLAYS);

	memcpy(&variant_strategy, base_strategy, sizeof(struct dml2_pmo_pstate_strategy));

	method_index = 0;
	/* uses a while loop instead of recursion to build permutations of base strategy */
	while (num_streams_per_base_method[0] > 0 || method_index != 0) {
		if (method_index == stream_count) {
			/* construct variant strategy */
			variant_found = false;
			stream_index = 0;

			for (i = 0; i < stream_count; i++) {
				for (j = 0; j < num_streams_per_base_method[i]; j++) {
					variant_strategy.per_stream_pstate_method[stream_index++] = base_strategy->per_stream_pstate_method[i];
				}

				for (j = 0; j < num_streams_per_variant_method[i]; j++) {
					variant_strategy.per_stream_pstate_method[stream_index++] = per_stream_variant_method[i];
					if (base_strategy->per_stream_pstate_method[i] != per_stream_variant_method[i]) {
						variant_found = true;
					}
				}
			}

			if (variant_found && dcn5_is_variant_method_valid(base_strategy, &variant_strategy, num_streams_per_base_method, num_streams_per_variant_method, stream_count)) {
				if (should_permute) {
					/* permutations are permitted, proceed to expand */
					dcn5_expand_base_strategy(&variant_strategy, stream_count, expanded_strategy_list, num_expanded_strategies);
				} else {
					/* no permutations allowed, so add to list now */
					dcn5_insert_strategy_into_expanded_list(&variant_strategy, stream_count, expanded_strategy_list, num_expanded_strategies);
				}
			}

			/* rollback to earliest method with bases remaining */
			for (method_index = stream_count - 1; method_index > 0; method_index--) {
				if (num_streams_per_base_method[method_index]) {
					/* bases remaining */
					break;
				} else {
					/* reset counters */
					num_streams_per_base_method[method_index] = num_streams_per_method[method_index];
					num_streams_per_variant_method[method_index] = 0;
				}
			}
		}

		if (num_streams_per_base_method[method_index]) {
			num_streams_per_base_method[method_index]--;
			num_streams_per_variant_method[method_index]++;

			method_index++;
		} else if (method_index != 0) {
			method_index++;
		}
	}
}

const struct dml2_pmo_pstate_strategy *dcn5_get_expanded_strategy_list(struct dml2_pmo_stage_optimizer *stage, int stream_count)
{
	const struct dml2_pmo_pstate_strategy *expanded_strategy_list = NULL;

	switch (stream_count) {
	case 1:
		expanded_strategy_list = stage->pmo->init_data.pmo_dcn4.expanded_strategy_list_1_display;
		break;
	case 2:
		expanded_strategy_list = stage->pmo->init_data.pmo_dcn4.expanded_strategy_list_2_display;
		break;
	case 3:
		expanded_strategy_list = stage->pmo->init_data.pmo_dcn4.expanded_strategy_list_3_display;
		break;
	case 4:
		expanded_strategy_list = stage->pmo->init_data.pmo_dcn4.expanded_strategy_list_4_display;
		break;
	default:
		break;
	}

	return expanded_strategy_list;
}

unsigned int dcn5_get_num_expanded_strategies(
		struct dml2_pmo_stage_optimizer *stage,
		int stream_count)
{
	return stage->pmo->init_data.pmo_dcn4.num_expanded_strategies_per_list[stream_count - 1];
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
	case dml2_pstate_method_fw_svp:
	case dml2_pstate_method_fw_svp_drr:
	case dml2_pstate_method_alternate:
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

bool dcn5_stream_matches_drr_policy(struct dml2_pmo_stage_optimizer *stage,
	const struct dml2_display_cfg *display_cfg,
	const enum dml2_pstate_method stream_pstate_method,
	unsigned int stream_index)
{
	const struct dml2_pmo_instance *pmo = stage->pmo;
	const struct dml2_stream_parameters *stream_descriptor = &display_cfg->stream_descriptors[stream_index];
	bool strategy_matches_drr_requirements = true;

	/* check if strategy is compatible with stream drr capability and strategy */
	if (is_bit_set_in_bitfield(PMO_NO_DRR_STRATEGY_MASK, stream_pstate_method) &&
			display_cfg->num_streams > 1 &&
			stream_descriptor->timing.drr_config.enabled &&
			(stream_descriptor->timing.drr_config.drr_active_fixed || stream_descriptor->timing.drr_config.drr_active_variable)) {
		/* DRR is active, so config may become unschedulable */
		strategy_matches_drr_requirements = false;
	} else if (is_bit_set_in_bitfield(PMO_NO_DRR_STRATEGY_MASK, stream_pstate_method) &&
			is_bit_set_in_bitfield(PMO_FW_STRATEGY_MASK, stream_pstate_method) &&
			stream_descriptor->timing.drr_config.enabled &&
			stream_descriptor->timing.drr_config.drr_active_variable) {
		/* DRR is variable, fw exclusive methods require DRR to be clamped */
		strategy_matches_drr_requirements = false;
	} else if (is_bit_set_in_bitfield(PMO_DRR_VAR_STRATEGY_MASK, stream_pstate_method) &&
			pmo->options->disable_drr_var_when_var_active &&
			stream_descriptor->timing.drr_config.enabled &&
			stream_descriptor->timing.drr_config.drr_active_variable) {
		/* DRR variable is active, but policy blocks DRR for p-state when this happens */
		strategy_matches_drr_requirements = false;
	} else if (is_bit_set_in_bitfield(PMO_DRR_VAR_STRATEGY_MASK, stream_pstate_method) &&
			(pmo->options->disable_drr_var ||
			!stream_descriptor->timing.drr_config.enabled ||
			stream_descriptor->timing.drr_config.disallowed)) {
		/* DRR variable strategies are disallowed due to settings or policy */
		strategy_matches_drr_requirements = false;
	} else if (is_bit_set_in_bitfield(PMO_DRR_CLAMPED_STRATEGY_MASK, stream_pstate_method) &&
		(pmo->options->disable_drr_clamped ||
			(!stream_descriptor->timing.drr_config.enabled ||
			(!stream_descriptor->timing.drr_config.drr_active_fixed && !stream_descriptor->timing.drr_config.drr_active_variable)) ||
			(pmo->options->disable_drr_clamped_when_var_active &&
			stream_descriptor->timing.drr_config.enabled &&
			stream_descriptor->timing.drr_config.drr_active_variable))) {
		/* DRR fixed strategies are disallowed due to settings or policy */
		strategy_matches_drr_requirements = false;
	} else if (is_bit_set_in_bitfield(PMO_FW_STRATEGY_MASK, stream_pstate_method) &&
			pmo->options->disable_fams2) {
		/* FW modes require FAMS2 */
		strategy_matches_drr_requirements = false;
	}

	return strategy_matches_drr_requirements;
}

bool dcn5_all_timings_support_vactive(struct dml2_pmo_stage_optimizer *stage,
		const struct dml2_display_cfg *display_config,
		unsigned int mask)
{
	struct dml2_stage_optimizer_uclk_pstate_init_locals *s = &stage->func_locals->uclk_pstate_init;
	unsigned int i;
	bool valid = true;

	// Create a remap array to enable simple iteration through only masked stream indicies
	for (i = 0; i < display_config->num_streams; i++) {
		if (is_bit_set_in_bitfield(mask, i)) {
			/* check if stream has enough vactive margin */
			valid &= is_bit_set_in_bitfield(s->stream_vactive_capability_mask, i);
		}
	}

	return valid;
}

bool dcn5_all_timings_support_vblank(struct dml2_pmo_stage_optimizer *stage,
		const struct dml2_display_cfg *display_config,
		unsigned int mask)
{
	struct dml2_pmo_synchronized_timing_groups *s = &stage->func_locals->uclk_pstate_init.synchronized_timing_groups;
	unsigned int i;
	bool synchronizable = true;

	/* find first vblank stream index and compare the timing group mask */
	for (i = 0; i < display_config->num_streams; i++) {
		if (is_bit_set_in_bitfield(mask, i)) {
			if (mask != s->synchronized_timing_group_masks[i]) {
				/* vblank streams are not synchronizable */
				synchronizable = false;
			}
			break;
		}
	}

	return synchronizable;
}

bool dcn5_all_timings_support_drr(struct dml2_pmo_stage_optimizer *stage,
		const struct dml2_optimization_worksheet *worksheet,
		const struct dml2_display_cfg *display_config,
		unsigned int mask)
{
	const struct dml2_pmo_instance *pmo = stage->pmo;
	unsigned char i;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		const struct dml2_stream_parameters *stream_descriptor;
		const struct dml2_pstate_meta *stream_pstate_meta;

		if (is_bit_set_in_bitfield(mask, i)) {
			stream_descriptor = &display_config->stream_descriptors[i];
			stream_pstate_meta = &worksheet->uclk_pstate.stream_pstate_meta[i];

			if (!stream_descriptor->timing.drr_config.enabled)
				return false;

			/* cannot support required vtotal */
			if (stream_pstate_meta->method_drr.stretched_vtotal > stream_pstate_meta->max_vtotal) {
				return false;
			}

			/* check rr is within bounds */
			if (stream_pstate_meta->nom_refresh_rate_hz < pmo->fams_params.v2.drr.refresh_rate_limit_min ||
				stream_pstate_meta->nom_refresh_rate_hz > pmo->fams_params.v2.drr.refresh_rate_limit_max) {
				return false;
			}

			/* check required stretch is allowed */
			if (stream_descriptor->timing.drr_config.max_instant_vtotal_delta > 0 &&
					stream_pstate_meta->method_drr.stretched_vtotal - stream_pstate_meta->nom_vtotal > (int)stream_descriptor->timing.drr_config.max_instant_vtotal_delta) {
				return false;
			}
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

static bool dcn5_is_timing_group_schedulable(
	const struct dml2_ip_capabilities *ip_caps,
	const struct dml2_pstate_meta *stream_pstate_meta,
	const struct dml2_display_cfg *display_cfg,
	const enum dml2_pstate_method *per_stream_pstate_method,
	const unsigned int timing_group_idx,
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
		group_pstate_meta->disallow_time_us < ip_caps->fams2.max_allow_delay_us;
}

static bool dcn5_is_pstate_schedulable(
	const struct dml2_ip_capabilities *ip_caps,
	struct dml2_pstate_meta *stream_pstate_meta,
	const struct dml2_display_cfg *display_cfg,
	const enum dml2_pstate_method *per_stream_pstate_method,
	struct dml2_pmo_synchronized_timing_groups *synchronized_timing_groups,
	struct dml2_scheduling_check_locals *s)
{
	double max_allow_delay_us = 0.0;
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
		if (!dcn5_is_timing_group_schedulable(ip_caps, stream_pstate_meta, display_cfg, per_stream_pstate_method, i, &s->group_common_pstate_meta[i], synchronized_timing_groups)) {
			/* synchronized timing group was not schedulable */
			schedulable = false;
			break;
		}
		max_allow_delay_us += s->group_common_pstate_meta[i].disallow_time_us;
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

	if (schedulable && max_allow_delay_us < ip_caps->fams2.max_allow_delay_us) {
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

	if (schedulable && max_allow_delay_us < ip_caps->fams2.max_allow_delay_us) {
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
		max_allow_delay_us = max_shift_us / shift_per_period * s->group_common_pstate_meta[lrg_idx].period_us;
		sum_allow_time_us = s->group_common_pstate_meta[lrg_idx].allow_time_us + s->group_common_pstate_meta[sml_idx].allow_time_us;

		if (shift_per_period > 0.0 &&
			shift_per_period < sum_allow_time_us &&
			max_allow_delay_us < ip_caps->fams2.max_allow_delay_us) {
			schedulable = true;
		}
	}

	return schedulable;
}

static bool validate_pstate_support_strategy_cofunctionality(struct dml2_pmo_stage_optimizer *stage,
		struct dml2_optimization_worksheet *worksheet,
		const struct dml2_display_cfg *display_cfg,
		const struct dml2_pmo_pstate_strategy *pstate_strategy)
{
	const struct dml2_pmo_instance *pmo       = stage->pmo;
	unsigned int vactive_stream_mask          = 0;
	unsigned int vblank_stream_mask           = 0;
	unsigned int drr_stream_mask              = 0;
	unsigned int vactive_count                = 0;
	unsigned int vblank_count                 = 0;
	unsigned int drr_count                    = 0;
	unsigned int stream_index                 = 0;
	bool strategy_matches_forced_requirements = true;
	bool strategy_matches_drr_requirements    = true;

	// Tabulate everything
	for (stream_index = 0; stream_index < display_cfg->num_streams; stream_index++) {

		enum dml2_pstate_method per_stream_pstate_method = pstate_strategy->per_stream_pstate_method[stream_index];
		unsigned int stream_plane_mask                   = worksheet->uclk_pstate.stream_plane_mask[stream_index];

		if (!all_planes_match_method(display_cfg, stream_plane_mask, per_stream_pstate_method)) {
			strategy_matches_forced_requirements = false;
			break;
		}

		strategy_matches_drr_requirements &=
			dcn5_stream_matches_drr_policy(stage, display_cfg, per_stream_pstate_method, stream_index);

		bool is_fw_drr      = per_stream_pstate_method == dml2_pstate_method_fw_drr;
		bool is_vactive     = per_stream_pstate_method == dml2_pstate_method_vactive;
		bool is_vactive_drr = per_stream_pstate_method == dml2_pstate_method_fw_vactive_drr;
		bool is_vblank      = per_stream_pstate_method == dml2_pstate_method_vblank;
		bool is_vblank_drr   = per_stream_pstate_method == dml2_pstate_method_fw_vblank_drr;

		// Checks method and increases count
		if (is_fw_drr) {
			drr_count++;
			set_bit_in_bitfield(&drr_stream_mask, stream_index);
		} else if (is_vactive || is_vactive_drr) {
			vactive_count++;
			set_bit_in_bitfield(&vactive_stream_mask, stream_index);
		} else if (is_vblank || is_vblank_drr) {
			vblank_count++;
			set_bit_in_bitfield(&vblank_stream_mask, stream_index);
		}
	}

	if (!strategy_matches_forced_requirements || !strategy_matches_drr_requirements)
		return false;

	if (vactive_count > 0 && !dcn5_all_timings_support_vactive(stage, display_cfg, vactive_stream_mask))
		return false;

	if (vblank_count > 0 && (pmo->options->disable_vblank || !dcn5_all_timings_support_vblank(stage, display_cfg, vblank_stream_mask)))
		return false;

	if (drr_count > 0 && (pmo->options->disable_drr_var || !dcn5_all_timings_support_drr(stage, worksheet, display_cfg, drr_stream_mask)))
		return false;

	return dcn5_is_pstate_schedulable(pmo->ip_caps,
		worksheet->uclk_pstate.stream_pstate_meta,
		display_cfg,
		pstate_strategy->per_stream_pstate_method,
		&stage->func_locals->uclk_pstate_init.synchronized_timing_groups,
		&stage->func_locals->uclk_pstate_init.scheduling_check_locals);
}

void dcn5_insert_into_candidate_list(const struct dml2_pmo_pstate_strategy *pstate_strategy, int stream_count, struct dml2_optimization_worksheet *worksheet)
{
	(void)stream_count;
	worksheet->uclk_pstate.pstate_strategy_candidates[worksheet->uclk_pstate.num_pstate_candidates] = *pstate_strategy;
	worksheet->uclk_pstate.num_pstate_candidates++;
}


static void dml2_pmo_dcn5_stage_optimizer_uclk_pstate_init(
	struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	struct dml2_stage_optimizer_uclk_pstate_init_locals *s = &stage->func_locals->uclk_pstate_init;
	const struct dml2_display_cfg *display_config          = worksheet->orig_dispcfg;
	const struct dml2_pmo_instance *pmo                    = stage->pmo;
	struct dml2_pmo_pstate_strategy override_base_strategy = { 0 };
	const struct dml2_pmo_pstate_strategy *strategy_list   = NULL;
	unsigned int strategy_list_size                        = 0;
	bool build_override_strategy                           = true;
	unsigned int stream_index                              = 0;
	unsigned int plane_index;
	unsigned int i;

	DML_LOG_COMP_IF_ENTER();
	worksheet->uclk_pstate.num_pstate_candidates = 0;

	memset(s, 0, sizeof(struct dml2_stage_optimizer_uclk_pstate_init_locals));

	if (display_config->overrides.all_streams_blanked) {
		goto exit;
	}

	// First build the stream plane mask (array of bitfields indexed by stream, indicating plane mapping)
	for (plane_index = 0; plane_index < display_config->num_planes; plane_index++) {
		const struct dml2_plane_parameters *plane_descriptor = &display_config->plane_descriptors[plane_index];
		enum dml2_uclk_pstate_change_strategy uclk_pstate_change_strategy =
			plane_descriptor->overrides.uclk_pstate_change_strategy;
		unsigned int *stream_plane_mask;

		stream_index      = plane_descriptor->stream_index;
		stream_plane_mask = &worksheet->uclk_pstate.stream_plane_mask[stream_index];

		set_bit_in_bitfield(stream_plane_mask, plane_index);

		build_override_strategy &= uclk_pstate_change_strategy != dml2_uclk_pstate_change_strategy_auto;
		override_base_strategy.per_stream_pstate_method[stream_index] =
				uclk_pstate_strategy_override_to_pstate_method(uclk_pstate_change_strategy);

		/* Save initial reserved vblank time as pstate optimize may overwrite this value. But
		 * if validation or permissibility fails then we must restore to the original value.
		 */
		worksheet->uclk_pstate.init_reserved_vblank_time_ns[plane_index] =
			worksheet->cur.config.reserved_vblank_time_ns[plane_index];
		worksheet->uclk_pstate.init_max_vactive_det_fill_delay_us[plane_index] =
			worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][dml2_pstate_type_uclk];
	}

	// Figure out which streams can do vactive, and also build up implicit FAMS2 meta
	for (stream_index = 0; stream_index < display_config->num_streams; stream_index++) {
		unsigned int stream_plane_mask = worksheet->uclk_pstate.stream_plane_mask[stream_index];

		if (dcn5_get_vactive_pstate_margin(&worksheet->validation_result, stream_plane_mask) > 0)
			set_bit_in_bitfield(&s->stream_vactive_capability_mask, stream_index);

		/* FAMS2 meta */
		dcn5_build_pstate_meta_per_stream(worksheet->orig_dispcfg,
			pmo->ip_caps,
			pmo->utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us,
			(double) pmo->ip_caps->max_vactive_det_fill_delay_us,
			0.0, //extra_vactive_allow_time_us
			stream_index,
			&worksheet->uclk_pstate.stream_pstate_meta[stream_index]);
	}

	/* get synchronized timing groups */
	dcn5_build_synchronized_timing_groups(&stage->func_locals->uclk_pstate_init.synchronized_timing_groups, display_config);

	if (build_override_strategy) {
		/* build expanded override strategy list (no permutations) */
		override_base_strategy.allow_state_increase = true;
		s->num_expanded_override_strategies         = 0;

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
		strategy_list      = s->expanded_override_strategy_list;
		strategy_list_size = s->num_expanded_override_strategies;
	} else {
		/* use predefined strategy list */
		strategy_list      = dcn5_get_expanded_strategy_list(stage, display_config->num_streams);
		strategy_list_size = dcn5_get_num_expanded_strategies(stage, display_config->num_streams);
	}

	if (!strategy_list || strategy_list_size == 0)
		goto exit;

	for (i = 0; i < strategy_list_size && worksheet->uclk_pstate.num_pstate_candidates < DML2_PMO_PSTATE_CANDIDATE_LIST_SIZE; i++) {
		const struct dml2_pmo_pstate_strategy *current_strategy = &strategy_list[i];

		if (validate_pstate_support_strategy_cofunctionality(stage, worksheet, display_config, current_strategy))
			dcn5_insert_into_candidate_list(current_strategy, display_config->num_streams, worksheet);
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

void dcn5_reset_worksheet_for_uclk_pstate(struct dml2_optimization_worksheet *worksheet)
{
	unsigned int plane_index;

	for (plane_index = 0; plane_index < worksheet->orig_dispcfg->num_planes; plane_index++) {

		// Restore reserve time
		worksheet->cur.config.reserved_vblank_time_ns[plane_index] = worksheet->uclk_pstate.init_reserved_vblank_time_ns[plane_index];

		// Reset pstate switch mode
		worksheet->cur.config.uclk_pstate_switch_modes[plane_index] = dml2_pstate_method_na;

		// Restore DET fill delay for ppt
		worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][dml2_pstate_type_uclk] = worksheet->uclk_pstate.init_max_vactive_det_fill_delay_us[plane_index];
	}
	worksheet->cur.config.fams2_required = false;
}

void dcn5_setup_planes_for_vactive_by_mask(struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet, int plane_mask)
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
				worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][dml2_pstate_type_uclk] = (unsigned int)math_max2(
					math_floor(worksheet->uclk_pstate.stream_pstate_meta[stream_index].method_vactive.max_vactive_det_fill_delay_us),
					worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][dml2_pstate_type_uclk]);
			}
		}
	}
}

void dcn5_setup_planes_for_vblank_by_mask(struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet, int plane_mask)
{
	unsigned int plane_index;
	const struct dml2_display_cfg *display_config = worksheet->orig_dispcfg;
	const struct dml2_pmo_instance *pmo = stage->pmo;

	worksheet->cur.unvalidated_change.bits.reserved_vblank_time = true;
	for (plane_index = 0; plane_index < display_config->num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			worksheet->cur.config.reserved_vblank_time_ns[plane_index] = (long)math_max2(pmo->utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us * 1000.0,
					worksheet->cur.config.reserved_vblank_time_ns[plane_index]);

			worksheet->cur.config.uclk_pstate_switch_modes[plane_index] = dml2_pstate_method_vblank;
		}
	}
}

void dcn5_setup_planes_for_vactive_drr_by_mask(struct dml2_pmo_stage_optimizer *stage,
		struct dml2_optimization_worksheet *worksheet,
		int plane_mask)
{
	unsigned int plane_index;
	unsigned int stream_index;
	const struct dml2_display_cfg *display_config = worksheet->orig_dispcfg;
	const struct dml2_pmo_instance *pmo = stage->pmo;

	for (plane_index = 0; plane_index < display_config->num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			stream_index = display_config->plane_descriptors[plane_index].stream_index;

			worksheet->cur.config.uclk_pstate_switch_modes[plane_index] = dml2_pstate_method_fw_vactive_drr;

			if (!pmo->options->disable_vactive_det_fill_bw_pad) {
				worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][dml2_pstate_type_uclk] = (unsigned int) math_max2(
					math_floor(worksheet->uclk_pstate.stream_pstate_meta[stream_index].method_vactive.max_vactive_det_fill_delay_us),
					worksheet->cur.config.max_vactive_det_fill_delay_us[plane_index][dml2_pstate_type_uclk]);
			}
		}
	}
}

void dcn5_setup_planes_for_vblank_drr_by_mask(struct dml2_pmo_stage_optimizer *stage,
		struct dml2_optimization_worksheet *worksheet,
		int plane_mask)
{
	unsigned int plane_index;
	const struct dml2_display_cfg *display_config = worksheet->orig_dispcfg;
	const struct dml2_pmo_instance *pmo = stage->pmo;

	worksheet->cur.unvalidated_change.bits.reserved_vblank_time = true;
	for (plane_index = 0; plane_index < display_config->num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			worksheet->cur.config.reserved_vblank_time_ns[plane_index]  = (long)math_max2(pmo->utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us * 1000,
					worksheet->cur.config.reserved_vblank_time_ns[plane_index]);

			worksheet->cur.config.uclk_pstate_switch_modes[plane_index] = dml2_pstate_method_fw_vblank_drr;
		}
	}
}

void dcn5_setup_planes_for_drr_by_mask(struct dml2_pmo_stage_optimizer *stage,
		struct dml2_optimization_worksheet *worksheet,
		int plane_mask)
{
	(void)stage;
	unsigned int plane_index;
	const struct dml2_display_cfg *display_config = worksheet->orig_dispcfg;

	for (plane_index = 0; plane_index < display_config->num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			worksheet->cur.config.uclk_pstate_switch_modes[plane_index] = dml2_pstate_method_fw_drr;
		}
	}
}

static bool setup_optimized_worksheet_for_uclk_pstate(struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	bool fams2_required = false;
	bool success = true;
	unsigned int stream_index, plane_index;
	int strategy_index = worksheet->uclk_pstate.cur_pstate_candidate;
	const struct dml2_plane_parameters *plane_descriptor;

	for (plane_index = 0; plane_index < worksheet->orig_dispcfg->num_planes; plane_index++) {
		plane_descriptor = &worksheet->orig_dispcfg->plane_descriptors[plane_index];
		set_bit_in_bitfield(&worksheet->uclk_pstate.stream_plane_mask[plane_descriptor->stream_index], plane_index);
	}

	for (stream_index = 0; stream_index < worksheet->orig_dispcfg->num_streams; stream_index++) {
		enum dml2_pstate_method method = worksheet->uclk_pstate.pstate_strategy_candidates[strategy_index].per_stream_pstate_method[stream_index];
		unsigned int stream_plane_mask = worksheet->uclk_pstate.stream_plane_mask[stream_index];

		bool is_vactive_fw_drr = method == dml2_pstate_method_fw_vactive_drr;
		bool is_vblank_fw_drr  = method == dml2_pstate_method_fw_vblank_drr;
		bool is_vactive        = method == dml2_pstate_method_vactive;
		bool is_vblank         = method == dml2_pstate_method_vblank;
		bool is_fw_drr         = method == dml2_pstate_method_fw_drr;
		bool is_na             = method == dml2_pstate_method_na;

		if (is_na) {
			success = false;
			break;
		} else if (is_vactive) {
			dcn5_setup_planes_for_vactive_by_mask(stage, worksheet, stream_plane_mask);
		} else if (is_vblank) {
			dcn5_setup_planes_for_vblank_by_mask(stage, worksheet, stream_plane_mask);
		} else if (is_vactive_fw_drr) {
			fams2_required = true;
			dcn5_setup_planes_for_vactive_drr_by_mask(stage, worksheet, stream_plane_mask);
		} else if (is_vblank_fw_drr) {
			fams2_required = true;
			dcn5_setup_planes_for_vblank_drr_by_mask(stage, worksheet, stream_plane_mask);
		} else if (is_fw_drr) {
			fams2_required = true;
			dcn5_setup_planes_for_drr_by_mask(stage, worksheet, stream_plane_mask);
		}
	}

	/* Indicate if FAMS2 required */
	if (success) {
		worksheet->cur.config.fams2_required = fams2_required;

		// Copy FAMS2 meta unconditionally - we need for vactive as well
		memcpy(&worksheet->cur.config.stream_pstate_meta,
				&worksheet->uclk_pstate.stream_pstate_meta,
				sizeof(struct dml2_pstate_meta) * DML2_MAX_PLANES);
		worksheet->cur.config.uclk_pstate_support = true;
	}

	return success;
}

static bool dml2_pmo_dcn5_stage_optimizer_uclk_pstate_optimize_next(
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

int dcn5_get_vactive_det_fill_latency_delay_us(const struct dml2_validation_result *validation_res, int plane_mask)
{
	unsigned char i;
	int max_vactive_fill_us = 0;

	for (i = 0; i < DML2_MAX_PLANES; i++) {
		if (is_bit_set_in_bitfield(plane_mask, i)) {
			if (validation_res->mode_support.cfg_support_info.plane_support_info[i].vactive_det_fill_delay_us[dml2_pstate_type_uclk] > max_vactive_fill_us)
				max_vactive_fill_us = validation_res->mode_support.cfg_support_info.plane_support_info[i].vactive_det_fill_delay_us[dml2_pstate_type_uclk];
		}
	}

	return max_vactive_fill_us;
}

int dcn5_get_minimum_reserved_time_us_for_planes(const struct dml2_optimization_worksheet *worksheet, int plane_mask)
{
	int min_time_us = 0xFFFFFF;
	unsigned int plane_index = 0;

	for (plane_index = 0; plane_index < worksheet->orig_dispcfg->num_planes; plane_index++) {
		if (is_bit_set_in_bitfield(plane_mask, plane_index)) {
			if (min_time_us > (worksheet->cur.config.reserved_vblank_time_ns[plane_index] / 1000))
				min_time_us = worksheet->cur.config.reserved_vblank_time_ns[plane_index] / 1000;
		}
	}
	return min_time_us;
}

static enum dml2_status dml2_pmo_dcn5_stage_optimizer_uclk_pstate_test_permissibility(
	struct dml2_pmo_stage_optimizer *stage, const struct dml2_optimization_worksheet *worksheet)
{
	const struct dml2_validation_result *validation_result = &worksheet->validation_result;
	const struct dml2_pmo_instance *pmo                    = stage->pmo;
	enum dml2_status status                                = DML2_STATUS_OK;
	int REQUIRED_RESERVED_TIME                             = 0;
	unsigned int stream_index                              = 0;

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

	REQUIRED_RESERVED_TIME    = (int)pmo->utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us;

	for (stream_index = 0; stream_index < worksheet->orig_dispcfg->num_streams; stream_index++) {
		struct dml2_pmo_pstate_strategy strategy         = worksheet->uclk_pstate.pstate_strategy_candidates[worksheet->uclk_pstate.cur_pstate_candidate];
		const struct dml2_pstate_meta *stream_pstate_meta  = &worksheet->cur.config.stream_pstate_meta[stream_index];
		enum dml2_pstate_method perStreamMethod          = strategy.per_stream_pstate_method[stream_index];
		unsigned int stream_plane_mask                   = worksheet->uclk_pstate.stream_plane_mask[stream_index];

		int vactive_pstate_margin = dcn5_get_vactive_pstate_margin(validation_result, stream_plane_mask);

		bool is_vactive_fw_drr = perStreamMethod == dml2_pstate_method_fw_vactive_drr;
		bool is_vblank_fw_drr  = perStreamMethod == dml2_pstate_method_fw_vblank_drr;
		bool is_vactive        = perStreamMethod == dml2_pstate_method_vactive;
		bool is_vblank         = perStreamMethod == dml2_pstate_method_vblank;
		bool is_fw_drr         = perStreamMethod == dml2_pstate_method_fw_drr;
		bool is_na             = perStreamMethod == dml2_pstate_method_na;

		if (is_vactive || is_vactive_fw_drr) {
			double max_vactive_det_fill_delay_us = math_ceil(stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_us);

			unsigned int vactive_det_fill_latency_delay_us = dcn5_get_vactive_det_fill_latency_delay_us(validation_result, stream_plane_mask);

			if (vactive_pstate_margin < 0.0 ||
				vactive_det_fill_latency_delay_us > max_vactive_det_fill_delay_us) {
				status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
				break;
			}
		} else if (is_vblank || is_vblank_fw_drr) {
			int minimum_reserved_time_us_for_planes = dcn5_get_minimum_reserved_time_us_for_planes(worksheet, stream_plane_mask);

			if (minimum_reserved_time_us_for_planes < REQUIRED_RESERVED_TIME) {
				status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
				break;
			}
		} else if (is_fw_drr) {
			bool do_all_planes_match_method = all_planes_match_method(worksheet->orig_dispcfg, stream_plane_mask, dml2_pstate_method_fw_drr);

			if (!do_all_planes_match_method) {
				status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
				break;
			}
		} else if (is_na) {
			status = DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE;
			break;
		}
	}
exit:
	DML_LOG_DEBUG("%s exit with status = %s\n", __func__, dml2_status_str(status));
	DML_LOG_COMP_IF_EXIT();
	return status;
}

void dml2_pmo_dcn5_stage_optimizer_uclk_pstate_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage)
{
	stage->pmo = pmo;
	stage->func_locals = &pmo->scratch.pmo_dcn5.func_locals;
	stage->init = dml2_pmo_dcn5_stage_optimizer_uclk_pstate_init;
	stage->optimize_next = dml2_pmo_dcn5_stage_optimizer_uclk_pstate_optimize_next;
	stage->test_permissibility =
			dml2_pmo_dcn5_stage_optimizer_uclk_pstate_test_permissibility;
}

static void dml2_pmo_dcn5_stage_optimizer_qos_init(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	DML_LOG_COMP_IF_ENTER();
	worksheet->qos.passing_index = worksheet->cur.config.min_sop_index;
	DML_LOG_COMP_IF_EXIT();
}

static bool dml2_pmo_dcn5_stage_optimizer_qos_optimize_next(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	bool should_continue = true;

	DML_LOG_COMP_IF_ENTER();
	if (worksheet->cur.config.min_sop_index == 0)
		worksheet->qos.is_index0_tested = true;

	if (worksheet->validation_result.is_prefetch_valid)
		worksheet->qos.passing_index = worksheet->cur.config.min_sop_index;
	else
		worksheet->qos.failing_index = worksheet->cur.config.min_sop_index;

	if (!worksheet->qos.is_index0_tested)
		/* first test index 0 to find a failing index */
		worksheet->cur.config.min_sop_index = 0;
	else if (worksheet->qos.passing_index == 0)
		/* stop when index 0 passes */
		should_continue = false;
	else if (worksheet->qos.passing_index > worksheet->qos.failing_index + 1)
		/* perform binary search to detect a failing to passing index edge */
		worksheet->cur.config.min_sop_index = (worksheet->qos.passing_index + worksheet->qos.failing_index) / 2;
	else
		/* no more index to try, optimization is complete */
		should_continue = false;

	if (should_continue)
		worksheet->cur.unvalidated_change.bits.sop_index = true;

	DML_LOG_DEBUG("%s exit with should_continue = %s\n", __func__, should_continue ? "true" : "false");
	DML_LOG_COMP_IF_EXIT();
	return should_continue;
}

static enum dml2_status dml2_pmo_dcn5_stage_optimizer_qos_test_permissibility(
		struct dml2_pmo_stage_optimizer *stage, const struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	(void)worksheet;
	DML_LOG_COMP_IF_ENTER();
	DML_LOG_DEBUG("%s exit with status = %s\n", __func__, dml2_status_str(DML2_STATUS_OK));
	DML_LOG_COMP_IF_EXIT();
	return DML2_STATUS_OK;
}

void dml2_pmo_dcn5_stage_optimizer_qos_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage)
{
	stage->pmo = pmo;
	stage->func_locals = &pmo->scratch.pmo_dcn5.func_locals;
	stage->init = dml2_pmo_dcn5_stage_optimizer_qos_init;
	stage->optimize_next = dml2_pmo_dcn5_stage_optimizer_qos_optimize_next;
	stage->test_permissibility =
			dml2_pmo_dcn5_stage_optimizer_qos_test_permissibility;
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

static void dml2_pmo_dcn5_stage_optimizer_vmin_init(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	unsigned int i;
	const struct dml2_core_mode_support_result *mode_support_result =
			&worksheet->validation_result.mode_support;
	const struct dml2_display_cfg *display_config = worksheet->orig_dispcfg;
	struct dml2_pmo_options *options = stage->pmo->options;
	const struct dml2_utm_soc_bb *utm_soc_bb = stage->pmo->utm_soc_bb;

	DML_LOG_COMP_IF_ENTER();
	if (options->disable_dyn_odm
			|| (options->disable_dyn_odm_for_multi_stream && display_config->num_streams > 1)
			|| utm_soc_bb->vmin_limit.dispclk_khz == 0)
		goto exit;

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
			worksheet->vmin.unoptimizable_streams[display_config->plane_descriptors[i].stream_index] = true;

	for (i = 0; i < display_config->num_streams; i++) {
		if (display_config->stream_descriptors[i].overrides.disable_dynamic_odm)
			worksheet->vmin.unoptimizable_streams[i] = true;
		/*
		 * ODM Combine requires horizontal timing divisible by 2 so each
		 * ODM segment has the same size.
		 */
		else if (!is_h_timing_divisible_by(&display_config->stream_descriptors[i].timing, 2))
			worksheet->vmin.unoptimizable_streams[i] = true;
		/*
		 * Our hardware support seamless ODM transitions for DP encoders
		 * only.
		 */
		else if (!is_dp_encoder(display_config->stream_descriptors[i].output.output_encoder))
			worksheet->vmin.unoptimizable_streams[i] = true;
	}

	for (i = 0; i < display_config->num_streams; i++)
		worksheet->vmin.init_odms_used[i] = mode_support_result->cfg_support_info.stream_support_info[i].odms_used;

	worksheet->vmin.is_initialized = true;
exit:
	DML_LOG_COMP_IF_EXIT();
}

static int find_highest_odm_load_stream_index(struct dml2_optimization_worksheet *worksheet)
{
	unsigned int i;
	int odm_load, highest_odm_load = -1, highest_odm_load_index = -1;
	unsigned int odms_used;

	for (i = 0; i < worksheet->orig_dispcfg->num_streams; i++) {
		odms_used = worksheet->cur.config.odm_combine_overrides[i] > 0 ?
				worksheet->cur.config.odm_combine_overrides[i] :
				worksheet->vmin.init_odms_used[i];

		if (odms_used > 0)
			odm_load = worksheet->orig_dispcfg->stream_descriptors[i].timing.pixel_clock_khz / odms_used;
		else
			odm_load = 0;

		if (odm_load > highest_odm_load) {
			highest_odm_load_index = i;
			highest_odm_load = odm_load;
		}
	}

	return highest_odm_load_index;
}

static bool dml2_pmo_dcn5_stage_optimizer_vmin_optimize_next(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	bool should_continue = true;
	int stream_index;

	DML_LOG_COMP_IF_ENTER();
	if (!worksheet->vmin.is_initialized) {
		should_continue = false;
		goto exit;
	}

	if (worksheet->validation_result.is_mode_support_valid
			&& (worksheet->validation_result.mode_support.global.dispclk_khz
					<= stage->pmo->utm_soc_bb->vmin_limit.dispclk_khz)) {
		should_continue = false;
		goto exit;
	}
	/*
	 * highest odm load stream must be optimizable to continue as dispclk is
	 * bounded by it.
	 */
	stream_index = find_highest_odm_load_stream_index(worksheet);
	if (stream_index < 0 ||
			worksheet->vmin.unoptimizable_streams[stream_index]) {
		should_continue = false;
		goto exit;
	}

	if ((int) worksheet->cur.config.odm_combine_overrides[stream_index] >= stage->pmo->odm_combine_limit) {
		should_continue = false;
		goto exit;
	}

	// Do not optimize forced ODM Combine number
	if (worksheet->orig_dispcfg->stream_descriptors[stream_index].overrides.odm_mode != dml2_odm_mode_auto) {
		should_continue = false;
		goto exit;
	}

	if (worksheet->cur.config.odm_combine_overrides[stream_index] > 0)
		worksheet->cur.config.odm_combine_overrides[stream_index] = worksheet->cur.config.odm_combine_overrides[stream_index] + 1;
	else
		worksheet->cur.config.odm_combine_overrides[stream_index] = worksheet->vmin.init_odms_used[stream_index] + 1;

	worksheet->cur.unvalidated_change.bits.odm_combine_overrides = true;
exit:
	DML_LOG_DEBUG("%s exit with should_continue = %s\n", __func__, should_continue ? "true" : "false");
	DML_LOG_COMP_IF_EXIT();
	return should_continue;
}

static enum dml2_status dml2_pmo_dcn5_stage_optimizer_vmin_test_permissibility(
		struct dml2_pmo_stage_optimizer *stage, const struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	(void)worksheet;
	DML_LOG_COMP_IF_ENTER();
	DML_LOG_DEBUG("%s exit with status = %s\n", __func__, dml2_status_str(DML2_STATUS_OK));
	DML_LOG_COMP_IF_EXIT();
	return DML2_STATUS_OK;
}

void dml2_pmo_dcn5_stage_optimizer_vmin_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage)
{
	stage->pmo = pmo;
	stage->func_locals = &pmo->scratch.pmo_dcn5.func_locals;
	stage->init = dml2_pmo_dcn5_stage_optimizer_vmin_init;
	stage->optimize_next = dml2_pmo_dcn5_stage_optimizer_vmin_optimize_next;
	stage->test_permissibility =
			dml2_pmo_dcn5_stage_optimizer_vmin_test_permissibility;
}


static bool dml2_pmo_dcn5_stage_optimizer_should_optimize_z8_stutter(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	const struct dml2_soc_power_management_parameters *power_params =
			&stage->pmo->utm_soc_bb->power_management_parameters;
	unsigned int i;
	const struct core_plane_support_info *plane_support_info =
			worksheet->validation_result.mode_support.cfg_support_info.plane_support_info;
	double line_time_us;
	double vblank_nom_time_us;
	double pixel_clock_hz;

	if (power_params->z8_stutter_exit_latency_us <= 0)
		/* z8 stutter not supported */
		return false;

	for (i = 0; i < worksheet->orig_dispcfg->num_planes; i++)
		if (plane_support_info[i].active_latency_hiding_us <
				power_params->z8_stutter_exit_latency_us + power_params->z8_min_idle_time)
			/* stutter period doesn't meet z8 eco */
			return false;

	for (i = 0; i < worksheet->orig_dispcfg->num_streams; i++) {
		pixel_clock_hz = worksheet->orig_dispcfg->stream_descriptors[i].timing.pixel_clock_khz * 1000.0;
		line_time_us = worksheet->orig_dispcfg->stream_descriptors[i].timing.h_total / pixel_clock_hz * 1000000;
		vblank_nom_time_us = line_time_us * worksheet->orig_dispcfg->stream_descriptors[i].timing.vblank_nom;

		if (vblank_nom_time_us < power_params->z8_stutter_exit_latency_us)
			/* z8 stutter optimization is too expensive to accept */
			return false;
	}

	return true;
}

static void dml2_pmo_dcn5_stage_optimizer_stutter_init(
	struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	unsigned int i;
	const struct dml2_soc_power_management_parameters *power_params =
		&stage->pmo->utm_soc_bb->power_management_parameters;

	DML_LOG_COMP_IF_ENTER();
	if (power_params->z8_stutter_exit_latency_us > 0 &&
			power_params->stutter_enter_plus_exit_latency_us > 0)
		DML_ASSERT_MSG(power_params->z8_stutter_exit_latency_us > power_params->stutter_enter_plus_exit_latency_us,
				"z8 stutter is expected to have more strict latency requirement!\n");

	/* keep a copy of current reserved vblank time as the base */
	for (i = 0; i < worksheet->orig_dispcfg->num_planes; i++)
		worksheet->stutter.init_reserved_vblank_time_ns[i] = worksheet->cur.config.reserved_vblank_time_ns[i];

	/* calculate preconditions */
	worksheet->stutter.should_optimize_z8_stutter =
			dml2_pmo_dcn5_stage_optimizer_should_optimize_z8_stutter(stage, worksheet);
	worksheet->stutter.should_optimize_stutter = (power_params->stutter_enter_plus_exit_latency_us > 0);
	DML_LOG_COMP_IF_EXIT();
}

static void dml2_pmo_dcn5_stage_optimizer_reset_stutter(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	unsigned int i;

	/* reset the reserved vblank time */
	for (i = 0; i < worksheet->orig_dispcfg->num_planes; i++)
		worksheet->cur.config.reserved_vblank_time_ns[i] = worksheet->stutter.init_reserved_vblank_time_ns[i];
}

static void dml2_pmo_dcn5_stage_optimizer_optimize_z8_stutter(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	unsigned int i;
	const struct dml2_soc_power_management_parameters *power_params =
		&stage->pmo->utm_soc_bb->power_management_parameters;
	const long z8_stutter_exit_latency_ns = (const long) power_params->z8_stutter_exit_latency_us * 1000;
	bool reserved_vblank_time_changed = false;

	/* we assume z8 stutter is always the first optimization attempt so there is no need to reset */
	// dml2_pmo_dcn5_stage_optimizer_reset_stutter(stage, worksheet);

	for (i = 0; i < worksheet->orig_dispcfg->num_planes; i++) {
		if (worksheet->cur.config.reserved_vblank_time_ns[i] < z8_stutter_exit_latency_ns) {
			worksheet->cur.config.reserved_vblank_time_ns[i] = z8_stutter_exit_latency_ns;
			reserved_vblank_time_changed = true;
		}
	}

	worksheet->cur.config.stutter_support_in_vblank = worksheet->stutter.should_optimize_stutter;
	worksheet->cur.config.z8_stutter_support_in_vblank = true;
	worksheet->cur.unvalidated_change.bits.reserved_vblank_time = reserved_vblank_time_changed;
}

static void dml2_pmo_dcn5_stage_optimizer_optimize_stutter(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	unsigned int i;
	const struct dml2_soc_power_management_parameters *power_params =
			&stage->pmo->utm_soc_bb->power_management_parameters;
	const long stutter_enter_plus_exit_latency_ns = (const long) power_params->stutter_enter_plus_exit_latency_us * 1000;
	bool reserved_vblank_time_changed = false;

	dml2_pmo_dcn5_stage_optimizer_reset_stutter(stage, worksheet);

	for (i = 0; i < worksheet->orig_dispcfg->num_planes; i++) {
		if (worksheet->cur.config.reserved_vblank_time_ns[i] < stutter_enter_plus_exit_latency_ns) {
			worksheet->cur.config.reserved_vblank_time_ns[i] = stutter_enter_plus_exit_latency_ns;
			reserved_vblank_time_changed = true;
		}
	}

	worksheet->cur.config.stutter_support_in_vblank = true;
	worksheet->cur.config.z8_stutter_support_in_vblank = false;
	worksheet->cur.unvalidated_change.bits.reserved_vblank_time = reserved_vblank_time_changed;
}

static bool dml2_pmo_dcn5_stage_optimizer_stutter_optimize_next(
		struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet)
{
	bool should_continue = true;

	DML_LOG_COMP_IF_ENTER();
	if (worksheet->validation_result.is_mode_support_valid
			&& stage->test_permissibility(stage, worksheet) == DML2_STATUS_OK) {
		/* optimization has passed no need to optimize further, stop */
		should_continue = false;
		goto exit;
	}

	if (!worksheet->stutter.should_optimize_z8_stutter
			&& !worksheet->stutter.should_optimize_stutter) {
		/* stutter optimization isn't supported */
		should_continue = false;
		goto exit;
	}

	if (worksheet->stutter.should_optimize_z8_stutter && !worksheet->stutter.is_z8_stutter_attempted) {
		dml2_pmo_dcn5_stage_optimizer_optimize_z8_stutter(stage, worksheet);
		worksheet->stutter.is_z8_stutter_attempted = true;
	} else if (worksheet->stutter.should_optimize_stutter && !worksheet->stutter.is_stutter_attempted) {
		dml2_pmo_dcn5_stage_optimizer_optimize_stutter(stage, worksheet);
		worksheet->stutter.is_stutter_attempted = true;
	} else {
		worksheet->cur.config.stutter_support_in_vblank = false;
		worksheet->cur.config.z8_stutter_support_in_vblank = false;
	}

	worksheet->cur.unvalidated_change.bits.stutter_support = true;
exit:
	DML_LOG_DEBUG("%s exit with should_continue = %s\n", __func__, should_continue ? "true" : "false");
	DML_LOG_COMP_IF_EXIT();
	return should_continue;
}

static enum dml2_status dml2_pmo_dcn5_stage_optimizer_stutter_test_permissibility(
		struct dml2_pmo_stage_optimizer *stage, const struct dml2_optimization_worksheet *worksheet)
{
	(void)stage;
	enum dml2_status status = worksheet->cur.config.stutter_support_in_vblank
		|| worksheet->cur.config.z8_stutter_support_in_vblank ?
		DML2_STATUS_OK : DML2_STATUS_OPTIMIZE_FAIL_STUTTER;

	DML_LOG_COMP_IF_ENTER();
	DML_LOG_DEBUG("%s exit with status = %s\n", __func__, dml2_status_str(status));
	DML_LOG_COMP_IF_EXIT();

	return status;
}

void dml2_pmo_dcn5_stage_optimizer_stutter_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage)
{
	stage->pmo = pmo;
	stage->func_locals = &pmo->scratch.pmo_dcn5.func_locals;
	stage->init = dml2_pmo_dcn5_stage_optimizer_stutter_init;
	stage->optimize_next = dml2_pmo_dcn5_stage_optimizer_stutter_optimize_next;
	stage->test_permissibility =
			dml2_pmo_dcn5_stage_optimizer_stutter_test_permissibility;
}
