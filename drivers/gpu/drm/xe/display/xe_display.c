// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_display.h"
#include "regs/xe_regs.h"

#include <linux/fb.h>

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/xe_drm.h>

#include "soc/intel_dram.h"
#include "i915_drv.h"		/* FIXME: HAS_DISPLAY() depends on this */
#include "intel_acpi.h"
#include "intel_audio.h"
#include "intel_bw.h"
#include "intel_display.h"
#include "intel_display_driver.h"
#include "intel_display_irq.h"
#include "intel_display_types.h"
#include "intel_dmc.h"
#include "intel_dp.h"
#include "intel_encoder.h"
#include "intel_fbdev.h"
#include "intel_hdcp.h"
#include "intel_hotplug.h"
#include "intel_opregion.h"
#include "xe_module.h"

/* Xe device functions */

static bool has_display(struct xe_device *xe)
{
	return HAS_DISPLAY(xe);
}

/**
 * xe_display_driver_probe_defer - Detect if we need to wait for other drivers
 *				   early on
 * @pdev: PCI device
 *
 * Returns: true if probe needs to be deferred, false otherwise
 */
bool xe_display_driver_probe_defer(struct pci_dev *pdev)
{
	if (!xe_modparam.enable_display)
		return 0;

	return intel_display_driver_probe_defer(pdev);
}

/**
 * xe_display_driver_set_hooks - Add driver flags and hooks for display
 * @driver: DRM device driver
 *
 * Set features and function hooks in @driver that are needed for driving the
 * display IP. This sets the driver's capability of driving display, regardless
 * if the device has it enabled
 */
void xe_display_driver_set_hooks(struct drm_driver *driver)
{
	if (!xe_modparam.enable_display)
		return;

	driver->driver_features |= DRIVER_MODESET | DRIVER_ATOMIC;
}

static void unset_display_features(struct xe_device *xe)
{
	xe->drm.driver_features &= ~(DRIVER_MODESET | DRIVER_ATOMIC);
}

static void display_destroy(struct drm_device *dev, void *dummy)
{
	struct xe_device *xe = to_xe_device(dev);

	destroy_workqueue(xe->display.hotplug.dp_wq);
}

/**
 * xe_display_create - create display struct
 * @xe: XE device instance
 *
 * Initialize all fields used by the display part.
 *
 * TODO: once everything can be inside a single struct, make the struct opaque
 * to the rest of xe and return it to be xe->display.
 *
 * Returns: 0 on success
 */
int xe_display_create(struct xe_device *xe)
{
	spin_lock_init(&xe->display.fb_tracking.lock);

	xe->display.hotplug.dp_wq = alloc_ordered_workqueue("xe-dp", 0);

	return drmm_add_action_or_reset(&xe->drm, display_destroy, NULL);
}

static void xe_display_fini_nommio(struct drm_device *dev, void *dummy)
{
	struct xe_device *xe = to_xe_device(dev);

	if (!xe->info.enable_display)
		return;

	intel_power_domains_cleanup(xe);
}

int xe_display_init_nommio(struct xe_device *xe)
{
	if (!xe->info.enable_display)
		return 0;

	/* Fake uncore lock */
	spin_lock_init(&xe->uncore.lock);

	/* This must be called before any calls to HAS_PCH_* */
	intel_detect_pch(xe);

	return drmm_add_action_or_reset(&xe->drm, xe_display_fini_nommio, xe);
}

static void xe_display_fini_noirq(void *arg)
{
	struct xe_device *xe = arg;

	if (!xe->info.enable_display)
		return;

	intel_display_driver_remove_noirq(xe);
}

int xe_display_init_noirq(struct xe_device *xe)
{
	int err;

	if (!xe->info.enable_display)
		return 0;

	intel_display_driver_early_probe(xe);

	/* Early display init.. */
	intel_opregion_setup(xe);

	/*
	 * Fill the dram structure to get the system dram info. This will be
	 * used for memory latency calculation.
	 */
	intel_dram_detect(xe);

	intel_bw_init_hw(xe);

	intel_display_device_info_runtime_init(xe);

	err = intel_display_driver_probe_noirq(xe);
	if (err)
		return err;

	return devm_add_action_or_reset(xe->drm.dev, xe_display_fini_noirq, xe);
}

static void xe_display_fini_noaccel(void *arg)
{
	struct xe_device *xe = arg;

	if (!xe->info.enable_display)
		return;

	intel_display_driver_remove_nogem(xe);
}

int xe_display_init_noaccel(struct xe_device *xe)
{
	int err;

	if (!xe->info.enable_display)
		return 0;

	err = intel_display_driver_probe_nogem(xe);
	if (err)
		return err;

	return devm_add_action_or_reset(xe->drm.dev, xe_display_fini_noaccel, xe);
}

int xe_display_init(struct xe_device *xe)
{
	if (!xe->info.enable_display)
		return 0;

	return intel_display_driver_probe(xe);
}

void xe_display_fini(struct xe_device *xe)
{
	if (!xe->info.enable_display)
		return;

	intel_hpd_poll_fini(xe);

	intel_hdcp_component_fini(xe);
	intel_audio_deinit(xe);
}

void xe_display_register(struct xe_device *xe)
{
	if (!xe->info.enable_display)
		return;

	intel_display_driver_register(xe);
	intel_register_dsm_handler();
	intel_power_domains_enable(xe);
}

void xe_display_unregister(struct xe_device *xe)
{
	if (!xe->info.enable_display)
		return;

	intel_unregister_dsm_handler();
	intel_power_domains_disable(xe);
	intel_display_driver_unregister(xe);
}

void xe_display_driver_remove(struct xe_device *xe)
{
	if (!xe->info.enable_display)
		return;

	intel_display_driver_remove(xe);
}

/* IRQ-related functions */

void xe_display_irq_handler(struct xe_device *xe, u32 master_ctl)
{
	if (!xe->info.enable_display)
		return;

	if (master_ctl & DISPLAY_IRQ)
		gen11_display_irq_handler(xe);
}

void xe_display_irq_enable(struct xe_device *xe, u32 gu_misc_iir)
{
	if (!xe->info.enable_display)
		return;

	if (gu_misc_iir & GU_MISC_GSE)
		intel_opregion_asle_intr(xe);
}

void xe_display_irq_reset(struct xe_device *xe)
{
	if (!xe->info.enable_display)
		return;

	gen11_display_irq_reset(xe);
}

void xe_display_irq_postinstall(struct xe_device *xe, struct xe_gt *gt)
{
	if (!xe->info.enable_display)
		return;

	if (gt->info.id == XE_GT0)
		gen11_de_irq_postinstall(xe);
}

static bool suspend_to_idle(void)
{
#if IS_ENABLED(CONFIG_ACPI_SLEEP)
	if (acpi_target_system_state() < ACPI_STATE_S3)
		return true;
#endif
	return false;
}

void xe_display_pm_suspend(struct xe_device *xe, bool runtime)
{
	bool s2idle = suspend_to_idle();
	if (!xe->info.enable_display)
		return;

	/*
	 * We do a lot of poking in a lot of registers, make sure they work
	 * properly.
	 */
	intel_power_domains_disable(xe);
	if (has_display(xe))
		drm_kms_helper_poll_disable(&xe->drm);

	if (!runtime)
		intel_display_driver_suspend(xe);

	intel_dp_mst_suspend(xe);

	intel_hpd_cancel_work(xe);

	intel_encoder_suspend_all(&xe->display);

	intel_opregion_suspend(xe, s2idle ? PCI_D1 : PCI_D3cold);

	intel_fbdev_set_suspend(&xe->drm, FBINFO_STATE_SUSPENDED, true);

	intel_dmc_suspend(xe);
}

void xe_display_pm_suspend_late(struct xe_device *xe)
{
	bool s2idle = suspend_to_idle();
	if (!xe->info.enable_display)
		return;

	intel_power_domains_suspend(xe, s2idle);

	intel_display_power_suspend_late(xe);
}

void xe_display_pm_resume_early(struct xe_device *xe)
{
	if (!xe->info.enable_display)
		return;

	intel_display_power_resume_early(xe);

	intel_power_domains_resume(xe);
}

void xe_display_pm_resume(struct xe_device *xe, bool runtime)
{
	if (!xe->info.enable_display)
		return;

	intel_dmc_resume(xe);

	if (has_display(xe))
		drm_mode_config_reset(&xe->drm);

	intel_display_driver_init_hw(xe);
	intel_hpd_init(xe);

	/* MST sideband requires HPD interrupts enabled */
	intel_dp_mst_resume(xe);
	if (!runtime)
		intel_display_driver_resume(xe);

	intel_hpd_poll_disable(xe);
	if (has_display(xe))
		drm_kms_helper_poll_enable(&xe->drm);

	intel_opregion_resume(xe);

	intel_fbdev_set_suspend(&xe->drm, FBINFO_STATE_RUNNING, false);

	intel_power_domains_enable(xe);
}

static void display_device_remove(struct drm_device *dev, void *arg)
{
	struct xe_device *xe = arg;

	intel_display_device_remove(xe);
}

int xe_display_probe(struct xe_device *xe)
{
	int err;

	if (!xe->info.enable_display)
		goto no_display;

	intel_display_device_probe(xe);

	err = drmm_add_action_or_reset(&xe->drm, display_device_remove, xe);
	if (err)
		return err;

	if (has_display(xe))
		return 0;

no_display:
	xe->info.enable_display = false;
	unset_display_features(xe);
	return 0;
}
