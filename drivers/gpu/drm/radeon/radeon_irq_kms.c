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

#include <linux/pci.h>
#include <linux/pm_runtime.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/radeon_drm.h>

#include "atom.h"
#include "radeon.h"
#include "radeon_kms.h"
#include "radeon_reg.h"


#define RADEON_WAIT_IDLE_TIMEOUT 200

/*
 * radeon_driver_irq_handler_kms - irq handler for KMS
 *
 * This is the irq handler for the radeon KMS driver (all asics).
 * radeon_irq_process is a macro that points to the per-asic
 * irq handler callback.
 */
static irqreturn_t radeon_driver_irq_handler_kms(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct radeon_device *rdev = dev->dev_private;
	irqreturn_t ret;

	ret = radeon_irq_process(rdev);
	if (ret == IRQ_HANDLED)
		pm_runtime_mark_last_busy(dev->dev);
	return ret;
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
/**
 * radeon_hotplug_work_func - display hotplug work handler
 *
 * @work: work struct
 *
 * This is the hot plug event work handler (all asics).
 * The work gets scheduled from the irq handler if there
 * was a hot plug interrupt.  It walks the connector table
 * and calls the hotplug handler for each one, then sends
 * a drm hotplug event to alert userspace.
 */
static void radeon_hotplug_work_func(struct work_struct *work)
{
	struct radeon_device *rdev = container_of(work, struct radeon_device,
						  hotplug_work.work);
	struct drm_device *dev = rdev->ddev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;

	/* we can race here at startup, some boards seem to trigger
	 * hotplug irqs when they shouldn't. */
	if (!rdev->mode_info.mode_config_initialized)
		return;

	mutex_lock(&mode_config->mutex);
	list_for_each_entry(connector, &mode_config->connector_list, head)
		radeon_connector_hotplug(connector);
	mutex_unlock(&mode_config->mutex);
	/* Just fire off a uevent and let userspace tell us what to do */
	drm_helper_hpd_irq_event(dev);
}

static void radeon_dp_work_func(struct work_struct *work)
{
}

/**
 * radeon_driver_irq_preinstall_kms - drm irq preinstall callback
 *
 * @dev: drm dev pointer
 *
 * Gets the hw ready to enable irqs (all asics).
 * This function disables all interrupt sources on the GPU.
 */
static void radeon_driver_irq_preinstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	unsigned long irqflags;
	unsigned i;

	spin_lock_irqsave(&rdev->irq.lock, irqflags);
	/* Disable *all* interrupts */
	for (i = 0; i < RADEON_NUM_RINGS; i++)
		atomic_set(&rdev->irq.ring_int[i], 0);
	rdev->irq.dpm_thermal = false;
	for (i = 0; i < RADEON_MAX_HPD_PINS; i++)
		rdev->irq.hpd[i] = false;
	for (i = 0; i < RADEON_MAX_CRTCS; i++) {
		rdev->irq.crtc_vblank_int[i] = false;
		atomic_set(&rdev->irq.pflip[i], 0);
		rdev->irq.afmt[i] = false;
	}
	radeon_irq_set(rdev);
	spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
	/* Clear bits */
	radeon_irq_process(rdev);
}

/**
 * radeon_driver_irq_postinstall_kms - drm irq preinstall callback
 *
 * @dev: drm dev pointer
 *
 * Handles stuff to be done after enabling irqs (all asics).
 * Returns 0 on success.
 */
static int radeon_driver_irq_postinstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;

	if (ASIC_IS_AVIVO(rdev))
		dev->max_vblank_count = 0x00ffffff;
	else
		dev->max_vblank_count = 0x001fffff;

	return 0;
}

/**
 * radeon_driver_irq_uninstall_kms - drm irq uninstall callback
 *
 * @dev: drm dev pointer
 *
 * This function disables all interrupt sources on the GPU (all asics).
 */
static void radeon_driver_irq_uninstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	unsigned long irqflags;
	unsigned i;

	if (rdev == NULL) {
		return;
	}
	spin_lock_irqsave(&rdev->irq.lock, irqflags);
	/* Disable *all* interrupts */
	for (i = 0; i < RADEON_NUM_RINGS; i++)
		atomic_set(&rdev->irq.ring_int[i], 0);
	rdev->irq.dpm_thermal = false;
	for (i = 0; i < RADEON_MAX_HPD_PINS; i++)
		rdev->irq.hpd[i] = false;
	for (i = 0; i < RADEON_MAX_CRTCS; i++) {
		rdev->irq.crtc_vblank_int[i] = false;
		atomic_set(&rdev->irq.pflip[i], 0);
		rdev->irq.afmt[i] = false;
	}
	radeon_irq_set(rdev);
	spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
}

static int radeon_irq_install(struct radeon_device *rdev, int irq)
{
	struct drm_device *dev = rdev->ddev;
	int ret;

	if (irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	radeon_driver_irq_preinstall_kms(dev);

	/* PCI devices require shared interrupts. */
	ret = request_irq(irq, radeon_driver_irq_handler_kms,
			  IRQF_SHARED, dev->driver->name, dev);
	if (ret)
		return ret;

	radeon_driver_irq_postinstall_kms(dev);

	return 0;
}

static void radeon_irq_uninstall(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	radeon_driver_irq_uninstall_kms(dev);
	free_irq(pdev->irq, dev);
}

/**
 * radeon_msi_ok - asic specific msi checks
 *
 * @rdev: radeon device pointer
 *
 * Handles asic specific MSI checks to determine if
 * MSIs should be enabled on a particular chip (all asics).
 * Returns true if MSIs should be enabled, false if MSIs
 * should not be enabled.
 */
static bool radeon_msi_ok(struct radeon_device *rdev)
{
	/* RV370/RV380 was first asic with MSI support */
	if (rdev->family < CHIP_RV380)
		return false;

	/* MSIs don't work on AGP */
	if (rdev->flags & RADEON_IS_AGP)
		return false;

	/*
	 * Older chips have a HW limitation, they can only generate 40 bits
	 * of address for "64-bit" MSIs which breaks on some platforms, notably
	 * IBM POWER servers, so we limit them
	 */
	if (rdev->family < CHIP_BONAIRE) {
		dev_info(rdev->dev, "radeon: MSI limited to 32-bit\n");
		rdev->pdev->no_64bit_msi = 1;
	}

	/* force MSI on */
	if (radeon_msi == 1)
		return true;
	else if (radeon_msi == 0)
		return false;

	/* Quirks */
	/* HP RS690 only seems to work with MSIs. */
	if ((rdev->pdev->device == 0x791f) &&
	    (rdev->pdev->subsystem_vendor == 0x103c) &&
	    (rdev->pdev->subsystem_device == 0x30c2))
		return true;

	/* Dell RS690 only seems to work with MSIs. */
	if ((rdev->pdev->device == 0x791f) &&
	    (rdev->pdev->subsystem_vendor == 0x1028) &&
	    (rdev->pdev->subsystem_device == 0x01fc))
		return true;

	/* Dell RS690 only seems to work with MSIs. */
	if ((rdev->pdev->device == 0x791f) &&
	    (rdev->pdev->subsystem_vendor == 0x1028) &&
	    (rdev->pdev->subsystem_device == 0x01fd))
		return true;

	/* Gateway RS690 only seems to work with MSIs. */
	if ((rdev->pdev->device == 0x791f) &&
	    (rdev->pdev->subsystem_vendor == 0x107b) &&
	    (rdev->pdev->subsystem_device == 0x0185))
		return true;

	/* try and enable MSIs by default on all RS690s */
	if (rdev->family == CHIP_RS690)
		return true;

	/* RV515 seems to have MSI issues where it loses
	 * MSI rearms occasionally. This leads to lockups and freezes.
	 * disable it by default.
	 */
	if (rdev->family == CHIP_RV515)
		return false;
	if (rdev->flags & RADEON_IS_IGP) {
		/* APUs work fine with MSIs */
		if (rdev->family >= CHIP_PALM)
			return true;
		/* lots of IGPs have problems with MSIs */
		return false;
	}

	return true;
}

/**
 * radeon_irq_kms_init - init driver interrupt info
 *
 * @rdev: radeon device pointer
 *
 * Sets up the work irq handlers, vblank init, MSIs, etc. (all asics).
 * Returns 0 for success, error for failure.
 */
int radeon_irq_kms_init(struct radeon_device *rdev)
{
	int r = 0;

	spin_lock_init(&rdev->irq.lock);

	/* Disable vblank irqs aggressively for power-saving */
	rdev->ddev->vblank_disable_immediate = true;

	r = drm_vblank_init(rdev->ddev, rdev->num_crtc);
	if (r) {
		return r;
	}

	/* enable msi */
	rdev->msi_enabled = 0;

	if (radeon_msi_ok(rdev)) {
		int ret = pci_enable_msi(rdev->pdev);
		if (!ret) {
			rdev->msi_enabled = 1;
			dev_info(rdev->dev, "radeon: using MSI.\n");
		}
	}

	INIT_DELAYED_WORK(&rdev->hotplug_work, radeon_hotplug_work_func);
	INIT_WORK(&rdev->dp_work, radeon_dp_work_func);
	INIT_WORK(&rdev->audio_work, r600_audio_update_hdmi);

	rdev->irq.installed = true;
	r = radeon_irq_install(rdev, rdev->pdev->irq);
	if (r) {
		rdev->irq.installed = false;
		flush_delayed_work(&rdev->hotplug_work);
		return r;
	}

	DRM_INFO("radeon: irq initialized.\n");
	return 0;
}

/**
 * radeon_irq_kms_fini - tear down driver interrupt info
 *
 * @rdev: radeon device pointer
 *
 * Tears down the work irq handlers, vblank handlers, MSIs, etc. (all asics).
 */
void radeon_irq_kms_fini(struct radeon_device *rdev)
{
	if (rdev->irq.installed) {
		radeon_irq_uninstall(rdev);
		rdev->irq.installed = false;
		if (rdev->msi_enabled)
			pci_disable_msi(rdev->pdev);
		flush_delayed_work(&rdev->hotplug_work);
	}
}

/**
 * radeon_irq_kms_sw_irq_get - enable software interrupt
 *
 * @rdev: radeon device pointer
 * @ring: ring whose interrupt you want to enable
 *
 * Enables the software interrupt for a specific ring (all asics).
 * The software interrupt is generally used to signal a fence on
 * a particular ring.
 */
void radeon_irq_kms_sw_irq_get(struct radeon_device *rdev, int ring)
{
	unsigned long irqflags;

	if (!rdev->irq.installed)
		return;

	if (atomic_inc_return(&rdev->irq.ring_int[ring]) == 1) {
		spin_lock_irqsave(&rdev->irq.lock, irqflags);
		radeon_irq_set(rdev);
		spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
	}
}

/**
 * radeon_irq_kms_sw_irq_get_delayed - enable software interrupt
 *
 * @rdev: radeon device pointer
 * @ring: ring whose interrupt you want to enable
 *
 * Enables the software interrupt for a specific ring (all asics).
 * The software interrupt is generally used to signal a fence on
 * a particular ring.
 */
bool radeon_irq_kms_sw_irq_get_delayed(struct radeon_device *rdev, int ring)
{
	return atomic_inc_return(&rdev->irq.ring_int[ring]) == 1;
}

/**
 * radeon_irq_kms_sw_irq_put - disable software interrupt
 *
 * @rdev: radeon device pointer
 * @ring: ring whose interrupt you want to disable
 *
 * Disables the software interrupt for a specific ring (all asics).
 * The software interrupt is generally used to signal a fence on
 * a particular ring.
 */
void radeon_irq_kms_sw_irq_put(struct radeon_device *rdev, int ring)
{
	unsigned long irqflags;

	if (!rdev->irq.installed)
		return;

	if (atomic_dec_and_test(&rdev->irq.ring_int[ring])) {
		spin_lock_irqsave(&rdev->irq.lock, irqflags);
		radeon_irq_set(rdev);
		spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
	}
}

/**
 * radeon_irq_kms_pflip_irq_get - enable pageflip interrupt
 *
 * @rdev: radeon device pointer
 * @crtc: crtc whose interrupt you want to enable
 *
 * Enables the pageflip interrupt for a specific crtc (all asics).
 * For pageflips we use the vblank interrupt source.
 */
void radeon_irq_kms_pflip_irq_get(struct radeon_device *rdev, int crtc)
{
	unsigned long irqflags;

	if (crtc < 0 || crtc >= rdev->num_crtc)
		return;

	if (!rdev->irq.installed)
		return;

	if (atomic_inc_return(&rdev->irq.pflip[crtc]) == 1) {
		spin_lock_irqsave(&rdev->irq.lock, irqflags);
		radeon_irq_set(rdev);
		spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
	}
}

/**
 * radeon_irq_kms_pflip_irq_put - disable pageflip interrupt
 *
 * @rdev: radeon device pointer
 * @crtc: crtc whose interrupt you want to disable
 *
 * Disables the pageflip interrupt for a specific crtc (all asics).
 * For pageflips we use the vblank interrupt source.
 */
void radeon_irq_kms_pflip_irq_put(struct radeon_device *rdev, int crtc)
{
	unsigned long irqflags;

	if (crtc < 0 || crtc >= rdev->num_crtc)
		return;

	if (!rdev->irq.installed)
		return;

	if (atomic_dec_and_test(&rdev->irq.pflip[crtc])) {
		spin_lock_irqsave(&rdev->irq.lock, irqflags);
		radeon_irq_set(rdev);
		spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
	}
}

/**
 * radeon_irq_kms_enable_afmt - enable audio format change interrupt
 *
 * @rdev: radeon device pointer
 * @block: afmt block whose interrupt you want to enable
 *
 * Enables the afmt change interrupt for a specific afmt block (all asics).
 */
void radeon_irq_kms_enable_afmt(struct radeon_device *rdev, int block)
{
	unsigned long irqflags;

	if (!rdev->irq.installed)
		return;

	spin_lock_irqsave(&rdev->irq.lock, irqflags);
	rdev->irq.afmt[block] = true;
	radeon_irq_set(rdev);
	spin_unlock_irqrestore(&rdev->irq.lock, irqflags);

}

/**
 * radeon_irq_kms_disable_afmt - disable audio format change interrupt
 *
 * @rdev: radeon device pointer
 * @block: afmt block whose interrupt you want to disable
 *
 * Disables the afmt change interrupt for a specific afmt block (all asics).
 */
void radeon_irq_kms_disable_afmt(struct radeon_device *rdev, int block)
{
	unsigned long irqflags;

	if (!rdev->irq.installed)
		return;

	spin_lock_irqsave(&rdev->irq.lock, irqflags);
	rdev->irq.afmt[block] = false;
	radeon_irq_set(rdev);
	spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
}

/**
 * radeon_irq_kms_enable_hpd - enable hotplug detect interrupt
 *
 * @rdev: radeon device pointer
 * @hpd_mask: mask of hpd pins you want to enable.
 *
 * Enables the hotplug detect interrupt for a specific hpd pin (all asics).
 */
void radeon_irq_kms_enable_hpd(struct radeon_device *rdev, unsigned hpd_mask)
{
	unsigned long irqflags;
	int i;

	if (!rdev->irq.installed)
		return;

	spin_lock_irqsave(&rdev->irq.lock, irqflags);
	for (i = 0; i < RADEON_MAX_HPD_PINS; ++i)
		rdev->irq.hpd[i] |= !!(hpd_mask & (1 << i));
	radeon_irq_set(rdev);
	spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
}

/**
 * radeon_irq_kms_disable_hpd - disable hotplug detect interrupt
 *
 * @rdev: radeon device pointer
 * @hpd_mask: mask of hpd pins you want to disable.
 *
 * Disables the hotplug detect interrupt for a specific hpd pin (all asics).
 */
void radeon_irq_kms_disable_hpd(struct radeon_device *rdev, unsigned hpd_mask)
{
	unsigned long irqflags;
	int i;

	if (!rdev->irq.installed)
		return;

	spin_lock_irqsave(&rdev->irq.lock, irqflags);
	for (i = 0; i < RADEON_MAX_HPD_PINS; ++i)
		rdev->irq.hpd[i] &= !(hpd_mask & (1 << i));
	radeon_irq_set(rdev);
	spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
}

/**
 * radeon_irq_kms_set_irq_n_enabled - helper for updating interrupt enable registers
 *
 * @rdev: radeon device pointer
 * @reg: the register to write to enable/disable interrupts
 * @mask: the mask that enables the interrupts
 * @enable: whether to enable or disable the interrupt register
 * @name: the name of the interrupt register to print to the kernel log
 * @n: the number of the interrupt register to print to the kernel log
 *
 * Helper for updating the enable state of interrupt registers. Checks whether
 * or not the interrupt matches the enable state we want. If it doesn't, then
 * we update it and print a debugging message to the kernel log indicating the
 * new state of the interrupt register.
 *
 * Used for updating sequences of interrupts registers like HPD1, HPD2, etc.
 */
void radeon_irq_kms_set_irq_n_enabled(struct radeon_device *rdev,
				      u32 reg, u32 mask,
				      bool enable, const char *name, unsigned n)
{
	u32 tmp = RREG32(reg);

	/* Interrupt state didn't change */
	if (!!(tmp & mask) == enable)
		return;

	if (enable) {
		DRM_DEBUG("%s%d interrupts enabled\n", name, n);
		WREG32(reg, tmp |= mask);
	} else {
		DRM_DEBUG("%s%d interrupts disabled\n", name, n);
		WREG32(reg, tmp & ~mask);
	}
}
