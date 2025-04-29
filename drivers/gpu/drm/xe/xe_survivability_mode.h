/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SURVIVABILITY_MODE_H_
#define _XE_SURVIVABILITY_MODE_H_

#include <linux/types.h>

struct xe_device;

int xe_survivability_mode_enable(struct xe_device *xe);
bool xe_survivability_mode_is_enabled(struct xe_device *xe);

#endif /* _XE_SURVIVABILITY_MODE_H_ */
