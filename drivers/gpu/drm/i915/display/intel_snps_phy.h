/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_SNPS_PHY_H__
#define __INTEL_SNPS_PHY_H__

#include <linux/types.h>

enum phy;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;
struct intel_encoder;
struct intel_mpllb_state;

void intel_snps_phy_wait_for_calibration(struct intel_display *display);
void intel_snps_phy_update_psr_power_state(struct intel_encoder *encoder,
					   bool enable);

int intel_mpllb_calc_state(struct intel_crtc_state *crtc_state,
			   struct intel_encoder *encoder);
void intel_mpllb_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state);
void intel_mpllb_disable(struct intel_encoder *encoder);
void intel_mpllb_readout_hw_state(struct intel_encoder *encoder,
				  struct intel_mpllb_state *pll_state);
int intel_mpllb_calc_port_clock(struct intel_encoder *encoder,
				const struct intel_mpllb_state *pll_state);

void intel_snps_phy_set_signal_levels(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state);
void intel_mpllb_state_verify(struct intel_atomic_state *state,
			      struct intel_crtc *crtc);

#endif /* __INTEL_SNPS_PHY_H__ */
