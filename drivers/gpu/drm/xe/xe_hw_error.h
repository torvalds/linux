/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */
#ifndef _XE_HW_ERROR_H_
#define _XE_HW_ERROR_H_

#include <linux/types.h>

struct xe_tile;
struct xe_device;

void xe_hw_error_irq_handler(struct xe_tile *tile, const u32 master_ctl);
void xe_hw_error_init(struct xe_device *xe);
#endif
