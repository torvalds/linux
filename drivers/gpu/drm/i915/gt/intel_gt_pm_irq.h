/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_GT_PM_IRQ_H
#define INTEL_GT_PM_IRQ_H

#include <linux/types.h>

struct intel_gt;

void gen6_gt_pm_unmask_irq(struct intel_gt *gt, u32 mask);
void gen6_gt_pm_mask_irq(struct intel_gt *gt, u32 mask);

void gen6_gt_pm_enable_irq(struct intel_gt *gt, u32 enable_mask);
void gen6_gt_pm_disable_irq(struct intel_gt *gt, u32 disable_mask);

void gen6_gt_pm_reset_iir(struct intel_gt *gt, u32 reset_mask);

#endif /* INTEL_GT_PM_IRQ_H */
