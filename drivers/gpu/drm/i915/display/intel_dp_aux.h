/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#ifndef __INTEL_DP_AUX_H__
#define __INTEL_DP_AUX_H__

struct intel_dp;

void intel_dp_aux_fini(struct intel_dp *intel_dp);
void intel_dp_aux_init(struct intel_dp *intel_dp);

#endif /* __INTEL_DP_AUX_H__ */
