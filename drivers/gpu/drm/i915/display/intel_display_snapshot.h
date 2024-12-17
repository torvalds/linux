/* SPDX-License-Identifier: MIT */
/* Copyright © 2024 Intel Corporation */

#ifndef __INTEL_DISPLAY_SNAPSHOT_H__
#define __INTEL_DISPLAY_SNAPSHOT_H__

struct drm_printer;
struct intel_display;
struct intel_display_snapshot;

struct intel_display_snapshot *intel_display_snapshot_capture(struct intel_display *display);
void intel_display_snapshot_print(const struct intel_display_snapshot *snapshot,
				  struct drm_printer *p);
void intel_display_snapshot_free(struct intel_display_snapshot *snapshot);

#endif /* __INTEL_DISPLAY_SNAPSHOT_H__ */
