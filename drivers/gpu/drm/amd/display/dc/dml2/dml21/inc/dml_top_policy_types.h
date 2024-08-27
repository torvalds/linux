// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML_TOP_POLICY_TYPES_H__
#define __DML_TOP_POLICY_TYPES_H__

struct dml2_policy_parameters {
	unsigned long odm_combine_dispclk_threshold_khz;
	unsigned int max_immediate_flip_latency;
};

#endif
