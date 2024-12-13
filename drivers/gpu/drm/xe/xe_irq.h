/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_IRQ_H_
#define _XE_IRQ_H_

#define XE_IRQ_DEFAULT_MSIX 1

struct xe_device;
struct xe_tile;
struct xe_gt;

int xe_irq_init(struct xe_device *xe);
int xe_irq_install(struct xe_device *xe);
void xe_irq_suspend(struct xe_device *xe);
void xe_irq_resume(struct xe_device *xe);
void xe_irq_enable_hwe(struct xe_gt *gt);

#endif
