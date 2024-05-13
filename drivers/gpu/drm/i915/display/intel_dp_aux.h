/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#ifndef __INTEL_DP_AUX_H__
#define __INTEL_DP_AUX_H__

#include <linux/types.h>

enum aux_ch;
struct drm_i915_private;
struct intel_dp;
struct intel_encoder;

void intel_dp_aux_fini(struct intel_dp *intel_dp);
void intel_dp_aux_init(struct intel_dp *intel_dp);

enum aux_ch intel_dp_aux_ch(struct intel_encoder *encoder);

void intel_dp_aux_irq_handler(struct drm_i915_private *i915);
u32 intel_dp_aux_pack(const u8 *src, int src_bytes);

#endif /* __INTEL_DP_AUX_H__ */
