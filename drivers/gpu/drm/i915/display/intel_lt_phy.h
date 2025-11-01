/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2025 Intel Corporation
 */

#ifndef __INTEL_LT_PHY_H__
#define __INTEL_LT_PHY_H__

#include <linux/types.h>

struct intel_encoder;
struct intel_crtc_state;

void intel_lt_phy_pll_enable(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state);
int
intel_lt_phy_pll_calc_state(struct intel_crtc_state *crtc_state,
			    struct intel_encoder *encoder);

#define HAS_LT_PHY(display) (DISPLAY_VER(display) >= 35)

#endif /* __INTEL_LT_PHY_H__ */
