/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#ifndef __INTEL_DP_AUX_H__
#define __INTEL_DP_AUX_H__

#include <linux/types.h>

struct intel_dp;

u32 intel_dp_pack_aux(const u8 *src, int src_bytes);

void intel_dp_aux_fini(struct intel_dp *intel_dp);
void intel_dp_aux_init(struct intel_dp *intel_dp);

#endif /* __INTEL_DP_AUX_H__ */
