/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SURVIVABILITY_MODE_H_
#define _XE_SURVIVABILITY_MODE_H_

#include <linux/types.h>

struct xe_device;

void xe_survivability_mode_init(struct xe_device *xe);
void xe_survivability_mode_remove(struct xe_device *xe);
bool xe_survivability_mode_enabled(struct xe_device *xe);
bool xe_survivability_mode_required(struct xe_device *xe);

#endif /* _XE_SURVIVABILITY_MODE_H_ */
