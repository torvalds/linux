// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <linux/slab.h>

#include <drm/drm_drv.h>

#include "intel_display_core.h"
#include "intel_display_device.h"
#include "intel_display_params.h"
#include "intel_display_snapshot.h"
#include "intel_dmc.h"
#include "intel_overlay.h"

struct intel_display_snapshot {
	struct intel_display *display;

	struct intel_display_device_info info;
	struct intel_display_runtime_info runtime_info;
	struct intel_display_params params;
	struct intel_overlay_snapshot *overlay;
	struct intel_dmc_snapshot *dmc;
};

struct intel_display_snapshot *intel_display_snapshot_capture(struct intel_display *display)
{
	struct intel_display_snapshot *snapshot;

	snapshot = kzalloc(sizeof(*snapshot), GFP_ATOMIC);
	if (!snapshot)
		return NULL;

	snapshot->display = display;

	memcpy(&snapshot->info, DISPLAY_INFO(display), sizeof(snapshot->info));
	memcpy(&snapshot->runtime_info, DISPLAY_RUNTIME_INFO(display),
	       sizeof(snapshot->runtime_info));

	intel_display_params_copy(&snapshot->params);

	snapshot->overlay = intel_overlay_snapshot_capture(display);
	snapshot->dmc = intel_dmc_snapshot_capture(display);

	return snapshot;
}

void intel_display_snapshot_print(const struct intel_display_snapshot *snapshot,
				  struct drm_printer *p)
{
	struct intel_display *display;

	if (!snapshot)
		return;

	display = snapshot->display;

	intel_display_device_info_print(&snapshot->info, &snapshot->runtime_info, p);
	intel_display_params_dump(&snapshot->params, display->drm->driver->name, p);

	intel_overlay_snapshot_print(snapshot->overlay, p);
	intel_dmc_snapshot_print(snapshot->dmc, p);
}

void intel_display_snapshot_free(struct intel_display_snapshot *snapshot)
{
	if (!snapshot)
		return;

	intel_display_params_free(&snapshot->params);

	kfree(snapshot->overlay);
	kfree(snapshot->dmc);
	kfree(snapshot);
}
