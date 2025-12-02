/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2025 Intel Corporation
 */

#ifndef __INTEL_LT_PHY_H__
#define __INTEL_LT_PHY_H__

#include <linux/types.h>

struct intel_atomic_state;
struct intel_display;
struct intel_encoder;
struct intel_crtc_state;
struct intel_crtc;
struct intel_lt_phy_pll_state;

void intel_lt_phy_pll_enable(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state);
void intel_lt_phy_pll_disable(struct intel_encoder *encoder);
int
intel_lt_phy_pll_calc_state(struct intel_crtc_state *crtc_state,
			    struct intel_encoder *encoder);
int intel_lt_phy_calc_port_clock(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state);
void intel_lt_phy_set_signal_levels(struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state);
void intel_lt_phy_dump_hw_state(struct intel_display *display,
				const struct intel_lt_phy_pll_state *hw_state);
bool
intel_lt_phy_pll_compare_hw_state(const struct intel_lt_phy_pll_state *a,
				  const struct intel_lt_phy_pll_state *b);
void intel_lt_phy_pll_readout_hw_state(struct intel_encoder *encoder,
				       const struct intel_crtc_state *crtc_state,
				       struct intel_lt_phy_pll_state *pll_state);
void intel_lt_phy_pll_state_verify(struct intel_atomic_state *state,
				   struct intel_crtc *crtc);
int
intel_lt_phy_calculate_hdmi_state(struct intel_lt_phy_pll_state *lt_state,
				  u32 frequency_khz);
void intel_xe3plpd_pll_enable(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state);
void intel_xe3plpd_pll_disable(struct intel_encoder *encoder);

#endif /* __INTEL_LT_PHY_H__ */
