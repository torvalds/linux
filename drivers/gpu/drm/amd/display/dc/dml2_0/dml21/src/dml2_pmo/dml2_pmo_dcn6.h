// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DML2_PMO_DCN6_H__
#define __DML2_PMO_DCN6_H__
#include "dml2_internal_shared_types.h"

bool dml2_pmo_dcn6a_initialize(struct dml2_pmo_initialize_in_out *in_out);
int dml2_pmo_dcn6a_get_ordered_mandatory_stage_optimizers(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer **optimers);
int dml2_pmo_dcn6a_get_ordered_optional_stages_optimizers(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer **optimers);
bool dml2_pmo_dcn6b_initialize(struct dml2_pmo_initialize_in_out *in_out);
int dml2_pmo_dcn6b_get_ordered_mandatory_stage_optimizers(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer **optimers);
int dml2_pmo_dcn6b_get_ordered_optional_stages_optimizers(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer **optimers);
void dml2_pmo_dcn6_convert_worksheet_to_solution(struct dml2_pmo_instance *pmo,
		const struct dml2_optimization_worksheet *worksheet,
		struct dml2_display_solution *solution);
#endif /* __DML2_PMO_DCN6_H__ */
