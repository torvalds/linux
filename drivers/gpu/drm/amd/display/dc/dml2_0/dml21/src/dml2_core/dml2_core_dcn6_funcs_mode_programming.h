// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_DCN6_FUNCS_MODE_PROGRAMMING_H__
#define __DML2_CORE_DCN6_FUNCS_MODE_PROGRAMMING_H__
#include "dml2_internal_shared_types.h"

enum dml2_status dml2_core_dcn6_funcs_populate_programming(struct dml2_core_instance *core,
		const struct dml2_display_solution *solution,
		struct dml2_display_cfg_programming *programming);

void dcn6_mode_programming(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states);

void dcn6_get_global_sync_programming(const struct dml2_core_internal_display_mode_lib *mode_lib,
		union dml2_global_sync_programming *out, int pipe_index);

void dcn6_get_stream_programming(const struct dml2_core_internal_display_mode_lib *mode_lib,
		struct dml2_per_stream_programming *out, int pipe_index);

void dcn6_populate_mode_programming(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_display_cfg_programming *programming,
		struct dml2_core_internal_scratch *s,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb);

void dcn6_mp_initialize_from_ms(struct dml2_core_internal_mode_program *outputs,
		const struct dml2_core_internal_mode_support *ms);

void dcn6_mp_initialize_from_solution(struct dml2_core_internal_mode_program *outputs,
		const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb);

void dcn6_mp_build_calculate_mp_context(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_instance *core,
		const struct dml2_display_solution *solution);

void dcn6_calculate_mode_programming(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states);
#endif /* __DML2_CORE_DCN6_FUNCS_MODE_PROGRAMMING_H__ */
