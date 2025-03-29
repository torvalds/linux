// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_DCN4_H__
#define __DML2_CORE_DCN4_H__
bool core_dcn4_initialize(struct dml2_core_initialize_in_out *in_out);
bool core_dcn4_mode_support(struct dml2_core_mode_support_in_out *in_out);
bool core_dcn4_mode_programming(struct dml2_core_mode_programming_in_out *in_out);
bool core_dcn4_populate_informative(struct dml2_core_populate_informative_in_out *in_out);
bool core_dcn4_calculate_mcache_allocation(struct dml2_calculate_mcache_allocation_in_out *in_out);
#endif
