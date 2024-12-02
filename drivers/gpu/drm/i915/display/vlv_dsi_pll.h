/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __VLV_DSI_PLL_H__
#define __VLV_DSI_PLL_H__

#include <linux/types.h>

enum port;
struct drm_i915_private;
struct intel_crtc_state;
struct intel_encoder;

int vlv_dsi_pll_compute(struct intel_encoder *encoder,
			struct intel_crtc_state *config);
void vlv_dsi_pll_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *config);
void vlv_dsi_pll_disable(struct intel_encoder *encoder);
u32 vlv_dsi_get_pclk(struct intel_encoder *encoder,
		     struct intel_crtc_state *config);
void vlv_dsi_reset_clocks(struct intel_encoder *encoder, enum port port);

bool bxt_dsi_pll_is_enabled(struct drm_i915_private *dev_priv);
int bxt_dsi_pll_compute(struct intel_encoder *encoder,
			struct intel_crtc_state *config);
void bxt_dsi_pll_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *config);
void bxt_dsi_pll_disable(struct intel_encoder *encoder);
u32 bxt_dsi_get_pclk(struct intel_encoder *encoder,
		     struct intel_crtc_state *config);
void bxt_dsi_reset_clocks(struct intel_encoder *encoder, enum port port);

void assert_dsi_pll_enabled(struct drm_i915_private *i915);
void assert_dsi_pll_disabled(struct drm_i915_private *i915);

#endif /* __VLV_DSI_PLL_H__ */
