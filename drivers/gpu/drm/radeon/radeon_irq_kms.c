/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon_reg.h"
#include "radeon_microcode.h"
#include "radeon.h"
#include "atom.h"

static inline uint32_t r100_irq_ack(struct radeon_device *rdev)
{
	uint32_t irqs = RREG32(RADEON_GEN_INT_STATUS);
	uint32_t irq_mask = RADEON_SW_INT_TEST;

	if (irqs) {
		WREG32(RADEON_GEN_INT_STATUS, irqs);
	}
	return irqs & irq_mask;
}

int r100_irq_set(struct radeon_device *rdev)
{
	uint32_t tmp = 0;

	if (rdev->irq.sw_int) {
		tmp |= RADEON_SW_INT_ENABLE;
	}
	/* Todo go through CRTC and enable vblank int or not */
	WREG32(RADEON_GEN_INT_CNTL, tmp);
	return 0;
}

int r100_irq_process(struct radeon_device *rdev)
{
	uint32_t status;

	status = r100_irq_ack(rdev);
	if (!status) {
		return IRQ_NONE;
	}
	while (status) {
		/* SW interrupt */
		if (status & RADEON_SW_INT_TEST) {
			radeon_fence_process(rdev);
		}
		status = r100_irq_ack(rdev);
	}
	return IRQ_HANDLED;
}

int rs600_irq_set(struct radeon_device *rdev)
{
	uint32_t tmp = 0;

	if (rdev->irq.sw_int) {
		tmp |= RADEON_SW_INT_ENABLE;
	}
	WREG32(RADEON_GEN_INT_CNTL, tmp);
	/* Todo go through CRTC and enable vblank int or not */
	WREG32(R500_DxMODE_INT_MASK, 0);
	return 0;
}

irqreturn_t radeon_driver_irq_handler_kms(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct radeon_device *rdev = dev->dev_private;

	return radeon_irq_process(rdev);
}

void radeon_driver_irq_preinstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	unsigned i;

	/* Disable *all* interrupts */
	rdev->irq.sw_int = false;
	for (i = 0; i < 2; i++) {
		rdev->irq.crtc_vblank_int[i] = false;
	}
	radeon_irq_set(rdev);
	/* Clear bits */
	radeon_irq_process(rdev);
}

int radeon_driver_irq_postinstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;

	dev->max_vblank_count = 0x001fffff;
	rdev->irq.sw_int = true;
	radeon_irq_set(rdev);
	return 0;
}

void radeon_driver_irq_uninstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	unsigned i;

	if (rdev == NULL) {
		return;
	}
	/* Disable *all* interrupts */
	rdev->irq.sw_int = false;
	for (i = 0; i < 2; i++) {
		rdev->irq.crtc_vblank_int[i] = false;
	}
	radeon_irq_set(rdev);
}

int radeon_irq_kms_init(struct radeon_device *rdev)
{
	int r = 0;

	r = drm_vblank_init(rdev->ddev, 2);
	if (r) {
		return r;
	}
	drm_irq_install(rdev->ddev);
	rdev->irq.installed = true;
	DRM_INFO("radeon: irq initialized.\n");
	return 0;
}

void radeon_irq_kms_fini(struct radeon_device *rdev)
{
	if (rdev->irq.installed) {
		rdev->irq.installed = false;
		drm_irq_uninstall(rdev->ddev);
	}
}
