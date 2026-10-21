// SPDX-License-Identifier: MIT
//
// Copyright 2024-2025 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_DCN5_FUNCS_MODE_SUPPORT_H__
#define __DML2_CORE_DCN5_FUNCS_MODE_SUPPORT_H__
#include "dml2_internal_shared_types.h"
enum dml2_status dml2_core_dcn5_funcs_validate_solution(struct dml2_core_instance *core,
		const struct dml2_display_solution *solution,
		struct dml2_validation_result *result);
#endif /* __DML2_CORE_DCN5_FUNCS_MODE_SUPPORT_H__ */
