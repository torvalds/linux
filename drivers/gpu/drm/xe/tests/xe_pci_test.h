/* SPDX-License-Identifier: GPL-2.0 AND MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PCI_TEST_H_
#define _XE_PCI_TEST_H_

struct xe_device;

typedef int (*xe_device_fn)(struct xe_device *);

int xe_call_for_each_device(xe_device_fn xe_fn);

#endif
