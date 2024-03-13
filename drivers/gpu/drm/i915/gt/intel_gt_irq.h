/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_GT_IRQ_H
#define INTEL_GT_IRQ_H

#include <linux/types.h>

#include "intel_engine_types.h"

struct intel_gt;

#define GEN8_GT_IRQS (GEN8_GT_RCS_IRQ | \
		      GEN8_GT_BCS_IRQ | \
		      GEN8_GT_VCS0_IRQ | \
		      GEN8_GT_VCS1_IRQ | \
		      GEN8_GT_VECS_IRQ | \
		      GEN8_GT_PM_IRQ | \
		      GEN8_GT_GUC_IRQ)

void gen11_gt_irq_reset(struct intel_gt *gt);
void gen11_gt_irq_postinstall(struct intel_gt *gt);
void gen11_gt_irq_handler(struct intel_gt *gt, const u32 master_ctl);

bool gen11_gt_reset_one_iir(struct intel_gt *gt,
			    const unsigned int bank,
			    const unsigned int bit);

void gen5_gt_irq_handler(struct intel_gt *gt, u32 gt_iir);

void gen5_gt_irq_postinstall(struct intel_gt *gt);
void gen5_gt_irq_reset(struct intel_gt *gt);
void gen5_gt_disable_irq(struct intel_gt *gt, u32 mask);
void gen5_gt_enable_irq(struct intel_gt *gt, u32 mask);

void gen6_gt_irq_handler(struct intel_gt *gt, u32 gt_iir);

void gen8_gt_irq_handler(struct intel_gt *gt, u32 master_ctl);
void gen8_gt_irq_reset(struct intel_gt *gt);
void gen8_gt_irq_postinstall(struct intel_gt *gt);

static inline void intel_engine_cs_irq(struct intel_engine_cs *engine, u16 iir)
{
	if (iir)
		engine->irq_handler(engine, iir);
}

static inline void
intel_engine_set_irq_handler(struct intel_engine_cs *engine,
			     void (*fn)(struct intel_engine_cs *engine,
					u16 iir))
{
	/*
	 * As the interrupt is live as allocate and setup the engines,
	 * err on the side of caution and apply barriers to updating
	 * the irq handler callback. This assures that when we do use
	 * the engine, we will receive interrupts only to ourselves,
	 * and not lose any.
	 */
	smp_store_mb(engine->irq_handler, fn);
}

#endif /* INTEL_GT_IRQ_H */
