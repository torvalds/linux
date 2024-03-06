// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <drm/drm_vblank.h>

#include "lsdc_irq.h"

/*
 * For the DC in LS7A2000, clearing interrupt status is achieved by
 * write "1" to LSDC_INT_REG.
 *
 * For the DC in LS7A1000, clear interrupt status is achieved by write "0"
 * to LSDC_INT_REG.
 *
 * Two different hardware engineers modify it as their will.
 */

irqreturn_t ls7a2000_dc_irq_handler(int irq, void *arg)
{
	struct drm_device *ddev = arg;
	struct lsdc_device *ldev = to_lsdc(ddev);
	u32 val;

	/* Read the interrupt status */
	val = lsdc_rreg32(ldev, LSDC_INT_REG);
	if ((val & INT_STATUS_MASK) == 0) {
		drm_warn(ddev, "no interrupt occurs\n");
		return IRQ_NONE;
	}

	ldev->irq_status = val;

	/* write "1" to clear the interrupt status */
	lsdc_wreg32(ldev, LSDC_INT_REG, val);

	if (ldev->irq_status & INT_CRTC0_VSYNC)
		drm_handle_vblank(ddev, 0);

	if (ldev->irq_status & INT_CRTC1_VSYNC)
		drm_handle_vblank(ddev, 1);

	return IRQ_HANDLED;
}

/* For the DC in LS7A1000 and LS2K1000 */
irqreturn_t ls7a1000_dc_irq_handler(int irq, void *arg)
{
	struct drm_device *ddev = arg;
	struct lsdc_device *ldev = to_lsdc(ddev);
	u32 val;

	/* Read the interrupt status */
	val = lsdc_rreg32(ldev, LSDC_INT_REG);
	if ((val & INT_STATUS_MASK) == 0) {
		drm_warn(ddev, "no interrupt occurs\n");
		return IRQ_NONE;
	}

	ldev->irq_status = val;

	/* write "0" to clear the interrupt status */
	val &= ~(INT_CRTC0_VSYNC | INT_CRTC1_VSYNC);
	lsdc_wreg32(ldev, LSDC_INT_REG, val);

	if (ldev->irq_status & INT_CRTC0_VSYNC)
		drm_handle_vblank(ddev, 0);

	if (ldev->irq_status & INT_CRTC1_VSYNC)
		drm_handle_vblank(ddev, 1);

	return IRQ_HANDLED;
}
