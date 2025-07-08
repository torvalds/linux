/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DMC_H__
#define __INTEL_DMC_H__

#include <linux/types.h>

enum pipe;
enum pipedmc_event_id;
struct drm_printer;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;
struct intel_dmc_snapshot;

void intel_dmc_init(struct intel_display *display);
void intel_dmc_load_program(struct intel_display *display);
void intel_dmc_wait_fw_load(struct intel_display *display);
void intel_dmc_disable_program(struct intel_display *display);
void intel_dmc_enable_pipe(const struct intel_crtc_state *crtc_state);
void intel_dmc_disable_pipe(const struct intel_crtc_state *crtc_state);
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

void assert_main_dmc_loaded(struct intel_display *display);

void intel_pipedmc_irq_handler(struct intel_display *display, enum pipe pipe);

u32 intel_pipedmc_start_mmioaddr(struct intel_crtc *crtc);
void intel_pipedmc_enable_event(struct intel_crtc *crtc,
				enum pipedmc_event_id event);
void intel_pipedmc_disable_event(struct intel_crtc *crtc,
				 enum pipedmc_event_id event);

void intel_pipedmc_irq_handler(struct intel_display *display, enum pipe pipe);

#endif /* __INTEL_DMC_H__ */
