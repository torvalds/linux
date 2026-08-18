// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_PMO_FAMS2_DCN4_H__
#define __DML2_PMO_FAMS2_DCN4_H__

#include "dml2_internal_shared_types.h"

struct display_configuation_with_meta;

bool pmo_dcn4_fams2_initialize(struct dml2_pmo_initialize_in_out *in_out);

bool pmo_dcn4_fams2_optimize_dcc_mcache(struct dml2_pmo_optimize_dcc_mcache_in_out *in_out);

bool pmo_dcn4_fams2_init_for_vmin(struct dml2_pmo_init_for_vmin_in_out *in_out);
bool pmo_dcn4_fams2_test_for_vmin(struct dml2_pmo_test_for_vmin_in_out *in_out);
bool pmo_dcn4_fams2_optimize_for_vmin(struct dml2_pmo_optimize_for_vmin_in_out *in_out);

bool pmo_dcn4_fams2_init_for_pstate_support(struct dml2_pmo_init_for_pstate_support_in_out *in_out);
bool pmo_dcn4_fams2_test_for_pstate_support(struct dml2_pmo_test_for_pstate_support_in_out *in_out);
bool pmo_dcn4_fams2_optimize_for_pstate_support(struct dml2_pmo_optimize_for_pstate_support_in_out *in_out);

bool pmo_dcn4_fams2_init_for_stutter(struct dml2_pmo_init_for_stutter_in_out *in_out);
bool pmo_dcn4_fams2_test_for_stutter(struct dml2_pmo_test_for_stutter_in_out *in_out);
bool pmo_dcn4_fams2_optimize_for_stutter(struct dml2_pmo_optimize_for_stutter_in_out *in_out);

void pmo_dcn4_fams2_expand_base_pstate_strategies(
	const struct dml2_pmo_pstate_strategy *base_strategies_list,
	const unsigned int num_base_strategies,
	const unsigned int stream_count,
	struct dml2_pmo_pstate_strategy *expanded_strategy_list,
	unsigned int *num_expanded_strategies);

/* Helpers shared with derived PMO implementations (e.g. DCN42). */
int dcn4_get_vactive_pstate_margin(
	const struct display_configuation_with_meta *display_cfg,
	int plane_mask);

int dcn4_get_minimum_reserved_time_us_for_planes(
	const struct display_configuation_with_meta *display_config,
	int plane_mask);

enum dml2_pstate_method dcn4_uclk_pstate_strategy_override_to_pstate_method(
	const enum dml2_uclk_pstate_change_strategy override_strategy);

struct dml2_pmo_pstate_strategy *dcn4_get_expanded_strategy_list(
	struct dml2_pmo_init_data *init_data,
	int stream_count);

unsigned int dcn4_get_num_expanded_strategies(
	struct dml2_pmo_init_data *init_data,
	int stream_count);

void dcn4_insert_strategy_into_expanded_list(
	const struct dml2_pmo_pstate_strategy *per_stream_pstate_strategy,
	const int stream_count,
	struct dml2_pmo_pstate_strategy *expanded_strategy_list,
	unsigned int *num_expanded_strategies);

void dcn4_expand_variant_strategy(
	const struct dml2_pmo_pstate_strategy *base_strategy,
	const unsigned int stream_count,
	const bool should_permute,
	struct dml2_pmo_pstate_strategy *expanded_strategy_list,
	unsigned int *num_expanded_strategies);

void dcn4_insert_into_candidate_list(
	const struct dml2_pmo_pstate_strategy *pstate_strategy,
	int stream_count,
	struct dml2_pmo_scratch *scratch);

#endif
