/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_CONTEXT_PARAM_H
#define INTEL_CONTEXT_PARAM_H

struct intel_context;

int intel_context_set_ring_size(struct intel_context *ce, long sz);
long intel_context_get_ring_size(struct intel_context *ce);

#endif /* INTEL_CONTEXT_PARAM_H */
