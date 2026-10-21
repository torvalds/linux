// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DML2_PMO_DCN6_STAGE_OPTIMIZERS_H__
#define __DML2_PMO_DCN6_STAGE_OPTIMIZERS_H__
#include "dml2_internal_shared_types.h"

void dml2_pmo_dcn6_stage_optimizer_uclk_pstate_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage);
void dml2_pmo_dcn6_stage_optimizer_vmin_dcfclk_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage);
void dml2_pmo_dcn6_stage_optimizer_mcache_create(struct dml2_pmo_instance *pmo,
	struct dml2_pmo_stage_optimizer *stage);
void dml2_pmo_dcn6_stage_optimizer_fclk_ppt_temp_read_pstate_create(struct dml2_pmo_instance *pmo,
		struct dml2_pmo_stage_optimizer *stage);
#endif /* __DML2_PMO_DCN6_STAGE_OPTIMIZERS_H__ */
