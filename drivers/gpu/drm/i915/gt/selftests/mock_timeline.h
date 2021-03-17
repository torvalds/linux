/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017-2018 Intel Corporation
 */

#ifndef __MOCK_TIMELINE__
#define __MOCK_TIMELINE__

#include <linux/types.h>

struct intel_timeline;

void mock_timeline_init(struct intel_timeline *timeline, u64 context);
void mock_timeline_fini(struct intel_timeline *timeline);

#endif /* !__MOCK_TIMELINE__ */
