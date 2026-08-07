// SPDX-License-Identifier: MIT
//
// Copyright 2024-2025 Advanced Micro Devices, Inc.

#ifndef __DML2_PMO_DCN5_STAGE_OPTIMIZERS_H__
#define __DML2_PMO_DCN5_STAGE_OPTIMIZERS_H__
#include "dml2_internal_shared_types.h"

void set_bit_in_bitfield(unsigned int *bit_field, unsigned int bit_offset);
bool is_bit_set_in_bitfield(unsigned int bit_field, unsigned int bit_offset);
int dcn5_get_vactive_pstate_margin(const struct dml2_validation_result *validation_res, int plane_mask);
void dcn5_build_method_scheduling_params(
	struct dml2_pstate_per_method_common_meta *stream_method_pstate_meta,
	const struct dml2_pstate_meta *stream_pstate_meta);
void dcn5_build_synchronized_timing_groups(
	// Output
	struct dml2_pmo_synchronized_timing_groups *s,
	// Input
	const struct dml2_display_cfg *display_config);
void dcn5_insert_strategy_into_expanded_list(
	const struct dml2_pmo_pstate_strategy *per_stream_pstate_strategy,
	const int stream_count,
	struct dml2_pmo_pstate_strategy *expanded_strategy_list,
	unsigned int *num_expanded_strategies);
bool dcn5_is_variant_method_valid(const struct dml2_pmo_pstate_strategy *base_strategy,
	const struct dml2_pmo_pstate_strategy *variant_strategy,
	const unsigned int num_streams_per_base_method[PMO_DCN4_MAX_DISPLAYS],
	const unsigned int num_streams_per_variant_method[PMO_DCN4_MAX_DISPLAYS],
	const unsigned int stream_count);
void dcn5_expand_base_strategy(
	const struct dml2_pmo_pstate_strategy *base_strategy,
	const unsigned int stream_count,
	struct dml2_pmo_pstate_strategy *expanded_strategy_list,
	unsigned int *num_expanded_strategies);
void dcn5_expand_variant_strategy(
	const struct dml2_pmo_pstate_strategy *base_strategy,
	const unsigned int stream_count,
	const bool should_permute,
	struct dml2_pmo_pstate_strategy *expanded_strategy_list,
	unsigned int *num_expanded_strategies);
const struct dml2_pmo_pstate_strategy *dcn5_get_expanded_strategy_list(struct dml2_pmo_stage_optimizer *stage, int stream_count);
unsigned int dcn5_get_num_expanded_strategies(
	struct dml2_pmo_stage_optimizer *stage,
	int stream_count);
bool dcn5_stream_matches_drr_policy(struct dml2_pmo_stage_optimizer *stage,
	const struct dml2_display_cfg *display_cfg,
	const enum dml2_pstate_method stream_pstate_method,
	unsigned int stream_index);
bool dcn5_all_timings_support_vactive(struct dml2_pmo_stage_optimizer *stage,
	const struct dml2_display_cfg *display_config,
	unsigned int mask);
bool dcn5_all_timings_support_vblank(struct dml2_pmo_stage_optimizer *stage,
	const struct dml2_display_cfg *display_config,
	unsigned int mask);
bool dcn5_all_timings_support_drr(struct dml2_pmo_stage_optimizer *stage,
	const struct dml2_optimization_worksheet *worksheet,
	const struct dml2_display_cfg *display_config,
	unsigned int mask);
void dcn5_insert_into_candidate_list(const struct dml2_pmo_pstate_strategy *pstate_strategy, int stream_count, struct dml2_optimization_worksheet *worksheet);
void dcn5_reset_worksheet_for_uclk_pstate(struct dml2_optimization_worksheet *worksheet);
void dcn5_setup_planes_for_vactive_by_mask(struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet, int plane_mask);
void dcn5_setup_planes_for_vblank_by_mask(struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet, int plane_mask);
void dcn5_setup_planes_for_vactive_drr_by_mask(struct dml2_pmo_stage_optimizer *stage,
	struct dml2_optimization_worksheet *worksheet,
	int plane_mask);
void dcn5_setup_planes_for_vblank_drr_by_mask(struct dml2_pmo_stage_optimizer *stage,
	struct dml2_optimization_worksheet *worksheet,
	int plane_mask);
void dcn5_setup_planes_for_drr_by_mask(struct dml2_pmo_stage_optimizer *stage,
	struct dml2_optimization_worksheet *worksheet,
	int plane_mask);
int dcn5_get_vactive_det_fill_latency_delay_us(const struct dml2_validation_result *validation_res, int plane_mask);
int dcn5_get_minimum_reserved_time_us_for_planes(const struct dml2_optimization_worksheet *worksheet, int plane_mask);

/* Public DCN5 PMO optimizers */
void dml2_pmo_dcn5_stage_optimizer_qos_create(struct dml2_pmo_instance *pmo_inst,
		struct dml2_pmo_stage_optimizer *optimizer);
void dml2_pmo_dcn5_stage_optimizer_mcache_create(struct dml2_pmo_instance *pmo_inst,
		struct dml2_pmo_stage_optimizer *optimizer);
void dml2_pmo_dcn5_stage_optimizer_uclk_pstate_create(struct dml2_pmo_instance *pmo_inst,
		struct dml2_pmo_stage_optimizer *optimizer);
void dml2_pmo_dcn5_stage_optimizer_vmin_create(struct dml2_pmo_instance *pmo_inst,
		struct dml2_pmo_stage_optimizer *optimizer);
void dml2_pmo_dcn5_stage_optimizer_stutter_create(struct dml2_pmo_instance *pmo_inst,
		struct dml2_pmo_stage_optimizer *optimizer);
void dml2_pmo_dcn5_stage_optimizer_mcache_init(
	struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet);
bool dml2_pmo_dcn5_stage_optimizer_mcache_test_total_mcache_limit(struct dml2_pmo_stage_optimizer *stage,
	const struct dml2_optimization_worksheet *worksheet);
bool dml2_pmo_dcn5_stage_optimizer_mcache_test_mcache_status(struct dml2_pmo_stage_optimizer *stage,
	const struct dml2_optimization_worksheet *worksheet);
bool dml2_pmo_dcn5_stage_optimizer_mcache_increment_pipe_usage(struct dml2_pmo_stage_optimizer *stage,
	struct dml2_optimization_worksheet *worksheet);
void dml2_pmo_dcn5_stage_optimizer_mcache_apply_default_pipe_usage(struct dml2_pmo_stage_optimizer *stage,
	struct dml2_optimization_worksheet *worksheet);

#endif /* __DML2_PMO_DCN5_STAGE_OPTIMIZERS_H__ */
