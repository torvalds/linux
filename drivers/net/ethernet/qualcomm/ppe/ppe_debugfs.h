/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/* PPE debugfs counters setup. */

#ifndef __PPE_DEBUGFS_H__
#define __PPE_DEBUGFS_H__

#include "ppe.h"

void ppe_debugfs_setup(struct ppe_device *ppe_dev);
void ppe_debugfs_teardown(struct ppe_device *ppe_dev);

#endif
