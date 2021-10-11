/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_IRQ_H__
#define __INTEL_PXP_IRQ_H__

#include <linux/types.h>

struct intel_pxp;

#define GEN12_DISPLAY_PXP_STATE_TERMINATED_INTERRUPT BIT(1)
#define GEN12_DISPLAY_APP_TERMINATED_PER_FW_REQ_INTERRUPT BIT(2)
#define GEN12_DISPLAY_STATE_RESET_COMPLETE_INTERRUPT BIT(3)

#define GEN12_PXP_INTERRUPTS \
	(GEN12_DISPLAY_PXP_STATE_TERMINATED_INTERRUPT | \
	 GEN12_DISPLAY_APP_TERMINATED_PER_FW_REQ_INTERRUPT | \
	 GEN12_DISPLAY_STATE_RESET_COMPLETE_INTERRUPT)

#ifdef CONFIG_DRM_I915_PXP
void intel_pxp_irq_enable(struct intel_pxp *pxp);
void intel_pxp_irq_disable(struct intel_pxp *pxp);
void intel_pxp_irq_handler(struct intel_pxp *pxp, u16 iir);
#else
static inline void intel_pxp_irq_handler(struct intel_pxp *pxp, u16 iir)
{
}
#endif

#endif /* __INTEL_PXP_IRQ_H__ */
