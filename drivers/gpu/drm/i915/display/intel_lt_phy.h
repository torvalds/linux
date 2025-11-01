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

#endif /* __INTEL_LT_PHY_H__ */
