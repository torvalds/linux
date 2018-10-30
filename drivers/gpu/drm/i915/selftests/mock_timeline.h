/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017-2018 Intel Corporation
 */

#ifndef __MOCK_TIMELINE__
#define __MOCK_TIMELINE__

struct i915_timeline;

void mock_timeline_init(struct i915_timeline *timeline, u64 context);
void mock_timeline_fini(struct i915_timeline *timeline);

#endif /* !__MOCK_TIMELINE__ */
