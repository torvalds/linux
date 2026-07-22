// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_PMO_DCN5_H__
#define __DML2_PMO_DCN5_H__
#include "dml2_internal_shared_types.h"
bool dml2_pmo_dcn5_initialize(struct dml2_pmo_initialize_in_out *in_out);
int dml2_pmo_dcn5_get_ordered_mandatory_stage_optimizers(struct dml2_pmo_instance *pmo, struct dml2_pmo_stage_optimizer **optimers);
int dml2_pmo_dcn5_get_ordered_optional_stages_optimizers(struct dml2_pmo_instance *pmo, struct dml2_pmo_stage_optimizer **optimers);
void dml2_pmo_dcn5_initialize_worksheet(struct dml2_pmo_instance *pmo,
		const struct dml2_display_cfg *dispcfg,
		struct dml2_optimization_worksheet *worksheet);
enum dml2_status dml2_pmo_dcn5_sanity_check(struct dml2_pmo_instance *pmo,
		const struct dml2_optimization_worksheet *worksheet);
void dml2_pmo_dcn5_convert_worksheet_to_solution(struct dml2_pmo_instance *pmo,
		const struct dml2_optimization_worksheet *worksheet,
		struct dml2_display_solution *solution);
void dml2_pmo_dcn5_clear_pre_validation_states(struct dml2_pmo_instance *pmo,
		struct dml2_optimization_worksheet *worksheet);
#endif /* __DML2_PMO_DCN5_H__ */
