// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <linux/slab.h>

#include "intel_display_snapshot.h"
#include "intel_overlay.h"

struct intel_display_snapshot {
	struct intel_overlay_snapshot *overlay;
};

struct intel_display_snapshot *intel_display_snapshot_capture(struct intel_display *display)
{
	struct intel_display_snapshot *snapshot;

	snapshot = kzalloc(sizeof(*snapshot), GFP_ATOMIC);
	if (!snapshot)
		return NULL;

	snapshot->overlay = intel_overlay_snapshot_capture(display);

	return snapshot;
}

void intel_display_snapshot_print(const struct intel_display_snapshot *snapshot,
				  struct drm_printer *p)
{
	if (!snapshot)
		return;

	intel_overlay_snapshot_print(snapshot->overlay, p);
}

void intel_display_snapshot_free(struct intel_display_snapshot *snapshot)
{
	if (!snapshot)
		return;

	kfree(snapshot->overlay);
	kfree(snapshot);
}
