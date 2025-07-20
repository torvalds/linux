/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_IRQ_H_
#define _XE_IRQ_H_

#include <linux/interrupt.h>

#define XE_IRQ_DEFAULT_MSIX 1

struct xe_device;
struct xe_tile;
struct xe_gt;

int xe_irq_init(struct xe_device *xe);
int xe_irq_install(struct xe_device *xe);
void xe_irq_suspend(struct xe_device *xe);
void xe_irq_resume(struct xe_device *xe);
void xe_irq_enable_hwe(struct xe_gt *gt);
int xe_irq_msix_request_irq(struct xe_device *xe, irq_handler_t handler, void *irq_buf,
			    const char *name, bool dynamic_msix, u16 *msix);
void xe_irq_msix_free_irq(struct xe_device *xe, u16 msix);

#endif
