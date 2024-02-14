/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_EXECLIST_H_
#define _XE_EXECLIST_H_

#include "xe_execlist_types.h"

struct xe_device;
struct xe_gt;

#define xe_execlist_port_assert_held(port) lockdep_assert_held(&(port)->lock)

int xe_execlist_init(struct xe_gt *gt);
struct xe_execlist_port *xe_execlist_port_create(struct xe_device *xe,
						 struct xe_hw_engine *hwe);
void xe_execlist_port_destroy(struct xe_execlist_port *port);

#endif
