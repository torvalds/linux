/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _INTEL_PCH_REFCLK_H_
#define _INTEL_PCH_REFCLK_H_

#include <linux/types.h>

struct intel_crtc_state;
struct intel_display;

#ifdef I915
void lpt_program_iclkip(const struct intel_crtc_state *crtc_state);
void lpt_disable_iclkip(struct intel_display *display);
int lpt_get_iclkip(struct intel_display *display);
int lpt_iclkip(const struct intel_crtc_state *crtc_state);

void intel_init_pch_refclk(struct intel_display *display);
void lpt_disable_clkout_dp(struct intel_display *display);
#else
static inline void lpt_program_iclkip(const struct intel_crtc_state *crtc_state)
{
}
static inline void lpt_disable_iclkip(struct intel_display *display)
{
}
static inline int lpt_get_iclkip(struct intel_display *display)
{
	return 0;
}
static inline int lpt_iclkip(const struct intel_crtc_state *crtc_state)
{
	return 0;
}
static inline void intel_init_pch_refclk(struct intel_display *display)
{
}
static inline void lpt_disable_clkout_dp(struct intel_display *display)
{
}
#endif

#endif
