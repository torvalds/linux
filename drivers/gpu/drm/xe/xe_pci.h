/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_PCI_H_
#define _XE_PCI_H_

#include "tests/xe_test.h"

int xe_register_pci_driver(void);
void xe_unregister_pci_driver(void);

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
struct xe_device;

typedef int (*xe_device_fn)(struct xe_device *);

int xe_call_for_each_device(xe_device_fn xe_fn);
#endif
#endif
