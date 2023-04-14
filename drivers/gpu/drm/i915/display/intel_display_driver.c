// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022-2023 Intel Corporation
 *
 * High level display driver entry points. This is a layer between top level
 * driver code and low level display functionality; no low level display code or
 * details here.
 */

#include <linux/vga_switcheroo.h>
#include <acpi/video.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_privacy_screen_consumer.h>
#include <drm/drm_probe_helper.h>

#include "i915_drv.h"
#include "intel_acpi.h"
#include "intel_audio.h"
#include "intel_display_debugfs.h"
#include "intel_display_driver.h"
#include "intel_fbdev.h"
#include "intel_opregion.h"

bool intel_modeset_probe_defer(struct pci_dev *pdev)
{
	struct drm_privacy_screen *privacy_screen;

	/*
	 * apple-gmux is needed on dual GPU MacBook Pro
	 * to probe the panel if we're the inactive GPU.
	 */
	if (vga_switcheroo_client_probe_defer(pdev))
		return true;

	/* If the LCD panel has a privacy-screen, wait for it */
	privacy_screen = drm_privacy_screen_get(&pdev->dev, NULL);
	if (IS_ERR(privacy_screen) && PTR_ERR(privacy_screen) == -EPROBE_DEFER)
		return true;

	drm_privacy_screen_put(privacy_screen);

	return false;
}

void intel_display_driver_register(struct drm_i915_private *i915)
{
	if (!HAS_DISPLAY(i915))
		return;

	/* Must be done after probing outputs */
	intel_opregion_register(i915);
	intel_acpi_video_register(i915);

	intel_audio_init(i915);

	intel_display_debugfs_register(i915);

	/*
	 * Some ports require correctly set-up hpd registers for
	 * detection to work properly (leading to ghost connected
	 * connector status), e.g. VGA on gm45.  Hence we can only set
	 * up the initial fbdev config after hpd irqs are fully
	 * enabled. We do it last so that the async config cannot run
	 * before the connectors are registered.
	 */
	intel_fbdev_initial_config_async(i915);

	/*
	 * We need to coordinate the hotplugs with the asynchronous
	 * fbdev configuration, for which we use the
	 * fbdev->async_cookie.
	 */
	drm_kms_helper_poll_init(&i915->drm);
}

void intel_display_driver_unregister(struct drm_i915_private *i915)
{
	if (!HAS_DISPLAY(i915))
		return;

	intel_fbdev_unregister(i915);
	intel_audio_deinit(i915);

	/*
	 * After flushing the fbdev (incl. a late async config which
	 * will have delayed queuing of a hotplug event), then flush
	 * the hotplug events.
	 */
	drm_kms_helper_poll_fini(&i915->drm);
	drm_atomic_helper_shutdown(&i915->drm);

	acpi_video_unregister();
	intel_opregion_unregister(i915);
}
