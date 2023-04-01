/* SPDX-License-Identifier: GPL-2.0 AND MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PCI_TEST_H_
#define _XE_PCI_TEST_H_

#include "xe_platform_types.h"

struct xe_device;

/*
 * Some defines just for clarity: these mean the test doesn't care about what
 * platform it will get since it doesn't depend on any platform-specific bits
 */
#define XE_TEST_PLATFORM_ANY	XE_PLATFORM_UNINITIALIZED
#define XE_TEST_SUBPLATFORM_ANY	XE_SUBPLATFORM_UNINITIALIZED

typedef int (*xe_device_fn)(struct xe_device *);

int xe_call_for_each_device(xe_device_fn xe_fn);

int xe_pci_fake_device_init(struct xe_device *xe, enum xe_platform platform,
			    enum xe_subplatform subplatform);

#define xe_pci_fake_device_init_any(xe__)					\
	xe_pci_fake_device_init(xe__, XE_TEST_PLATFORM_ANY,			\
				XE_TEST_SUBPLATFORM_ANY)

#endif
