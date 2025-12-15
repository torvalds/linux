/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

/* for soc/ */
#ifndef MISSING_CASE
#define MISSING_CASE(x) WARN(1, "Missing case (%s == %ld)\n", \
			     __stringify(x), (long)(x))
#endif

/* for a couple of users under i915/display */
#define i915_inject_probe_failure(unused) ((unused) && 0)
