/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, NVIDIA Corporation.
 */

#ifndef HOST1X_FENCE_H
#define HOST1X_FENCE_H

struct host1x_syncpt_fence;

void host1x_fence_signal(struct host1x_syncpt_fence *fence);

#endif
