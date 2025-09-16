/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef __INTEL_FLIPQ_H__
#define __INTEL_FLIPQ_H__

#include <linux/types.h>

enum intel_dsb_id;
enum intel_flipq_id;
enum pipe;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;
struct intel_dsb;

bool intel_flipq_supported(struct intel_display *display);
void intel_flipq_init(struct intel_display *display);
void intel_flipq_reset(struct intel_display *display, enum pipe pipe);

void intel_flipq_enable(const struct intel_crtc_state *crtc_state);
void intel_flipq_disable(const struct intel_crtc_state *old_crtc_state);

void intel_flipq_add(struct intel_crtc *crtc,
		     enum intel_flipq_id flip_queue_id,
		     unsigned int pts,
		     enum intel_dsb_id dsb_id,
		     struct intel_dsb *dsb);
int intel_flipq_exec_time_us(struct intel_display *display);
void intel_flipq_wait_dmc_halt(struct intel_dsb *dsb, struct intel_crtc *crtc);
void intel_flipq_unhalt_dmc(struct intel_dsb *dsb, struct intel_crtc *crtc);
void intel_flipq_dump(struct intel_crtc *crtc,
		      enum intel_flipq_id flip_queue_id);

#endif /* __INTEL_FLIPQ_H__ */
