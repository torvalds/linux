// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.
#ifndef __DML2_TOP_SOC15_H__
#define __DML2_TOP_SOC15_H__
#include "dml2_internal_shared_types.h"
bool dml2_top_soc15_initialize_instance(struct dml2_initialize_instance_in_out *in_out);

bool dml2_top_mcache_calc_mcache_count_and_offsets(struct top_mcache_calc_mcache_count_and_offsets_in_out *params);
void dml2_top_mcache_assign_global_mcache_ids(struct top_mcache_assign_global_mcache_ids_in_out *params);
bool dml2_top_mcache_validate_admissability(struct top_mcache_validate_admissability_in_out *params);
bool dml2_top_soc15_build_mcache_programming(struct dml2_build_mcache_programming_in_out *params);
#endif /* __DML2_TOP_SOC15_H__ */
