/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_CLOCK_GATING_H__
#define __INTEL_DISPLAY_CLOCK_GATING_H__

struct intel_display;

void intel_display_skl_init_clock_gating(struct intel_display *display);
void intel_display_kbl_init_clock_gating(struct intel_display *display);
void intel_display_cfl_init_clock_gating(struct intel_display *display);
void intel_display_bxt_init_clock_gating(struct intel_display *display);
void intel_display_glk_init_clock_gating(struct intel_display *display);
void intel_display_bdw_clock_gating_disable_fbcq(struct intel_display *display);
void intel_display_bdw_clock_gating_vblank_in_srd(struct intel_display *display);
void intel_display_bdw_clock_gating_kvm_notif(struct intel_display *display);
void intel_display_hsw_init_clock_gating(struct intel_display *display);
void intel_display_disable_trickle_feed(struct intel_display *display);
void intel_display_ilk_init_clock_gating(struct intel_display *display);
void intel_display_gen6_init_clock_gating(struct intel_display *display);
void intel_display_ivb_init_clock_gating(struct intel_display *display);
void intel_display_g4x_init_clock_gating(struct intel_display *display);
void intel_display_i965gm_init_clock_gating(struct intel_display *display);

#endif /* __INTEL_DISPLAY_CLOCK_GATING_H__ */
