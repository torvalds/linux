// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_CX0_PHY_H__
#define __INTEL_CX0_PHY_H__

#include <linux/types.h>

enum icl_port_dpll_id;
struct intel_atomic_state;
struct intel_c10pll_state;
struct intel_c20pll_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_cx0pll_state;
struct intel_display;
struct intel_encoder;
struct intel_hdmi;

bool intel_encoder_is_c10phy(struct intel_encoder *encoder);
void intel_mtl_pll_enable(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state);
void intel_mtl_pll_disable(struct intel_encoder *encoder);
enum icl_port_dpll_id
intel_mtl_port_pll_type(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state);

int intel_cx0pll_calc_state(struct intel_crtc_state *crtc_state, struct intel_encoder *encoder);
void intel_cx0pll_readout_hw_state(struct intel_encoder *encoder,
				   struct intel_cx0pll_state *pll_state);
int intel_cx0pll_calc_port_clock(struct intel_encoder *encoder,
				 const struct intel_cx0pll_state *pll_state);

void intel_cx0pll_dump_hw_state(struct intel_display *display,
				const struct intel_cx0pll_state *hw_state);
void intel_cx0pll_state_verify(struct intel_atomic_state *state,
			       struct intel_crtc *crtc);
bool intel_cx0pll_compare_hw_state(const struct intel_cx0pll_state *a,
				   const struct intel_cx0pll_state *b);
void intel_cx0_phy_set_signal_levels(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state);
int intel_mtl_tbt_calc_port_clock(struct intel_encoder *encoder);
void intel_cx0_pll_power_save_wa(struct intel_display *display);

#endif /* __INTEL_CX0_PHY_H__ */
