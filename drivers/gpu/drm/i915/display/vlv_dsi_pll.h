/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __VLV_DSI_PLL_H__
#define __VLV_DSI_PLL_H__

#include <linux/types.h>

enum port;
struct intel_crtc_state;
struct intel_display;
struct intel_encoder;

int vlv_dsi_pll_compute(struct intel_encoder *encoder,
			struct intel_crtc_state *config);
void vlv_dsi_pll_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *config);
void vlv_dsi_pll_disable(struct intel_encoder *encoder);
u32 vlv_dsi_get_pclk(struct intel_encoder *encoder,
		     struct intel_crtc_state *config);
void vlv_dsi_reset_clocks(struct intel_encoder *encoder, enum port port);

int bxt_dsi_pll_compute(struct intel_encoder *encoder,
			struct intel_crtc_state *config);
void bxt_dsi_pll_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *config);
void bxt_dsi_pll_disable(struct intel_encoder *encoder);
u32 bxt_dsi_get_pclk(struct intel_encoder *encoder,
		     struct intel_crtc_state *config);
void bxt_dsi_reset_clocks(struct intel_encoder *encoder, enum port port);

#ifdef I915
bool bxt_dsi_pll_is_enabled(struct intel_display *display);
void assert_dsi_pll_enabled(struct intel_display *display);
void assert_dsi_pll_disabled(struct intel_display *display);
#else
static inline bool bxt_dsi_pll_is_enabled(struct intel_display *display)
{
	return false;
}
static inline void assert_dsi_pll_enabled(struct intel_display *display)
{
}

static inline void assert_dsi_pll_disabled(struct intel_display *display)
{
}
#endif

#endif /* __VLV_DSI_PLL_H__ */
