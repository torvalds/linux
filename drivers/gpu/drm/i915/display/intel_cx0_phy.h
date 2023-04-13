// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_CX0_PHY_H__
#define __INTEL_CX0_PHY_H__

#include <linux/types.h>
#include <linux/bitfield.h>
#include <linux/bits.h>

#include "i915_drv.h"
#include "intel_display_types.h"

struct drm_i915_private;
struct intel_encoder;
struct intel_crtc_state;
enum phy;

bool intel_is_c10phy(struct drm_i915_private *dev_priv, enum phy phy);
void intel_cx0pll_enable(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state);
void intel_cx0pll_disable(struct intel_encoder *encoder);
void intel_c10pll_readout_hw_state(struct intel_encoder *encoder, struct intel_c10pll_state *pll_state);
int intel_cx0pll_calc_state(struct intel_crtc_state *crtc_state, struct intel_encoder *encoder);
void intel_c10pll_dump_hw_state(struct drm_i915_private *dev_priv,
				const struct intel_c10pll_state *hw_state);
int intel_c10pll_calc_port_clock(struct intel_encoder *encoder,
				 const struct intel_c10pll_state *pll_state);
void intel_c10pll_state_verify(struct intel_atomic_state *state,
			       struct intel_crtc_state *new_crtc_state);
void intel_cx0_phy_set_signal_levels(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state);
int intel_c10_phy_check_hdmi_link_rate(int clock);

#endif /* __INTEL_CX0_PHY_H__ */
