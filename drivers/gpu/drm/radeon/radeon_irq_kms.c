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
#include "radeon.h"
#include "atom.h"

irqreturn_t radeon_driver_irq_handler_kms(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct radeon_device *rdev = dev->dev_private;

	return radeon_irq_process(rdev);
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
static void radeon_hotplug_work_func(struct work_struct *work)
{
	struct radeon_device *rdev = container_of(work, struct radeon_device,
						  hotplug_work);
	struct drm_device *dev = rdev->ddev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;

	if (mode_config->num_connector) {
		list_for_each_entry(connector, &mode_config->connector_list, head)
			radeon_connector_hotplug(connector);
	}
	/* Just fire off a uevent and let userspace tell us what to do */
	drm_sysfs_hotplug_event(dev);
}

void radeon_driver_irq_preinstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	unsigned i;

	INIT_WORK(&rdev->hotplug_work, radeon_hotplug_work_func);

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
		rdev->irq.hpd[i] = false;
	}
	radeon_irq_set(rdev);
}

int radeon_irq_kms_init(struct radeon_device *rdev)
{
	int r = 0;
	int num_crtc = 2;

	if (rdev->flags & RADEON_SINGLE_CRTC)
		num_crtc = 1;
	spin_lock_init(&rdev->irq.sw_lock);
	r = drm_vblank_init(rdev->ddev, num_crtc);
	if (r) {
		return r;
	}
	/* enable msi */
	rdev->msi_enabled = 0;
	/* MSIs don't seem to work on my rs780;
	 * not sure about rs880 or other rs780s.
	 * Needs more investigation.
	 */
	if ((rdev->family >= CHIP_RV380) &&
	    (rdev->family != CHIP_RS780) &&
	    (rdev->family != CHIP_RS880)) {
		int ret = pci_enable_msi(rdev->pdev);
		if (!ret) {
			rdev->msi_enabled = 1;
			DRM_INFO("radeon: using MSI.\n");
		}
	}
	rdev->irq.installed = true;
	r = drm_irq_install(rdev->ddev);
	if (r) {
		rdev->irq.installed = false;
		return r;
	}
	DRM_INFO("radeon: irq initialized.\n");
	return 0;
}

void radeon_irq_kms_fini(struct radeon_device *rdev)
{
	drm_vblank_cleanup(rdev->ddev);
	if (rdev->irq.installed) {
		drm_irq_uninstall(rdev->ddev);
		rdev->irq.installed = false;
		if (rdev->msi_enabled)
			pci_disable_msi(rdev->pdev);
	}
}

void radeon_irq_kms_sw_irq_get(struct radeon_device *rdev)
{
	unsigned long irqflags;

	spin_lock_irqsave(&rdev->irq.sw_lock, irqflags);
	if (rdev->ddev->irq_enabled && (++rdev->irq.sw_refcount == 1)) {
		rdev->irq.sw_int = true;
		radeon_irq_set(rdev);
	}
	spin_unlock_irqrestore(&rdev->irq.sw_lock, irqflags);
}

void radeon_irq_kms_sw_irq_put(struct radeon_device *rdev)
{
	unsigned long irqflags;

	spin_lock_irqsave(&rdev->irq.sw_lock, irqflags);
	BUG_ON(rdev->ddev->irq_enabled && rdev->irq.sw_refcount <= 0);
	if (rdev->ddev->irq_enabled && (--rdev->irq.sw_refcount == 0)) {
		rdev->irq.sw_int = false;
		radeon_irq_set(rdev);
	}
	spin_unlock_irqrestore(&rdev->irq.sw_lock, irqflags);
}

