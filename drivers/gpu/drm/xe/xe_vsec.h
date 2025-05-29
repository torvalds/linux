/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright © 2024 Intel Corporation */

#ifndef _XE_VSEC_H_
#define _XE_VSEC_H_

#include <linux/types.h>

struct pci_dev;
struct xe_device;

void xe_vsec_init(struct xe_device *xe);
int xe_pmt_telem_read(struct pci_dev *pdev, u32 guid, u64 *data, loff_t user_offset, u32 count);

#endif
