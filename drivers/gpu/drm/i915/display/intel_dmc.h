/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DMC_H__
#define __INTEL_DMC_H__

#include <linux/types.h>

enum pipe;
struct drm_printer;
struct intel_display;
struct intel_dmc_snapshot;

void intel_dmc_init(struct intel_display *display);
void intel_dmc_load_program(struct intel_display *display);
void intel_dmc_disable_program(struct intel_display *display);
void intel_dmc_enable_pipe(struct intel_display *display, enum pipe pipe);
void intel_dmc_disable_pipe(struct intel_display *display, enum pipe pipe);
void intel_dmc_block_pkgc(struct intel_display *display, enum pipe pipe,
			  bool block);
void intel_dmc_start_pkgc_exit_at_start_of_undelayed_vblank(struct intel_display *display,
							    enum pipe pipe, bool enable);
void intel_dmc_fini(struct intel_display *display);
void intel_dmc_suspend(struct intel_display *display);
void intel_dmc_resume(struct intel_display *display);
bool intel_dmc_has_payload(struct intel_display *display);
void intel_dmc_debugfs_register(struct intel_display *display);

struct intel_dmc_snapshot *intel_dmc_snapshot_capture(struct intel_display *display);
void intel_dmc_snapshot_print(const struct intel_dmc_snapshot *snapshot, struct drm_printer *p);
void intel_dmc_update_dc6_allowed_count(struct intel_display *display, bool start_tracking);

void assert_dmc_loaded(struct intel_display *display);

#endif /* __INTEL_DMC_H__ */
