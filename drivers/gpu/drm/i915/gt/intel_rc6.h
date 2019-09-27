/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_RC6_H
#define INTEL_RC6_H

#include "i915_reg.h"

struct intel_engine_cs;
struct intel_rc6;

void intel_rc6_init(struct intel_rc6 *rc6);
void intel_rc6_fini(struct intel_rc6 *rc6);

void intel_rc6_sanitize(struct intel_rc6 *rc6);
void intel_rc6_enable(struct intel_rc6 *rc6);
void intel_rc6_disable(struct intel_rc6 *rc6);

u64 intel_rc6_residency_ns(struct intel_rc6 *rc6, i915_reg_t reg);
u64 intel_rc6_residency_us(struct intel_rc6 *rc6, i915_reg_t reg);

#endif /* INTEL_RC6_H */
