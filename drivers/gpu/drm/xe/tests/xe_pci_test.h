/* SPDX-License-Identifier: GPL-2.0 AND MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PCI_TEST_H_
#define _XE_PCI_TEST_H_

#include <linux/types.h>
#include <kunit/test.h>

#include "xe_platform_types.h"
#include "xe_sriov_types.h"
#include "xe_step_types.h"

struct xe_device;

struct xe_pci_fake_data {
	enum xe_sriov_mode sriov_mode;
	enum xe_platform platform;
	enum xe_subplatform subplatform;
	struct xe_step_info step;
	u32 graphics_verx100;
	u32 media_verx100;
};

int xe_pci_fake_device_init(struct xe_device *xe);
const void *xe_pci_fake_data_gen_params(struct kunit *test, const void *prev, char *desc);
void xe_pci_fake_data_desc(const struct xe_pci_fake_data *param, char *desc);

const void *xe_pci_graphics_ip_gen_param(struct kunit *test, const void *prev, char *desc);
const void *xe_pci_media_ip_gen_param(struct kunit *test, const void *prev, char *desc);
const void *xe_pci_id_gen_param(struct kunit *test, const void *prev, char *desc);
const void *xe_pci_live_device_gen_param(struct kunit *test, const void *prev, char *desc);

#endif
