/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_SNPS_PHY_H__
#define __INTEL_SNPS_PHY_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_encoder;
struct intel_crtc_state;
struct intel_mpllb_state;
enum phy;

void intel_snps_phy_wait_for_calibration(struct drm_i915_private *dev_priv);
void intel_snps_phy_update_psr_power_state(struct drm_i915_private *dev_priv,
					   enum phy phy, bool enable);

int intel_mpllb_calc_state(struct intel_crtc_state *crtc_state,
			   struct intel_encoder *encoder);
void intel_mpllb_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state);
void intel_mpllb_disable(struct intel_encoder *encoder);
void intel_mpllb_readout_hw_state(struct intel_encoder *encoder,
				  struct intel_mpllb_state *pll_state);
int intel_mpllb_calc_port_clock(struct intel_encoder *encoder,
				const struct intel_mpllb_state *pll_state);

int intel_snps_phy_check_hdmi_link_rate(int clock);
void intel_snps_phy_set_signal_levels(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state);

#endif /* __INTEL_SNPS_PHY_H__ */
