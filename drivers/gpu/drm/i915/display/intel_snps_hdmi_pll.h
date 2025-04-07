/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Synopsys, Inc., Intel Corporation
 */

#ifndef __INTEL_SNPS_HDMI_PLL_H__
#define __INTEL_SNPS_HDMI_PLL_H__

#include <linux/types.h>

struct intel_c10pll_state;
struct intel_mpllb_state;

void intel_snps_hdmi_pll_compute_mpllb(struct intel_mpllb_state *pll_state, u64 pixel_clock);
void intel_snps_hdmi_pll_compute_c10pll(struct intel_c10pll_state *pll_state, u64 pixel_clock);

#endif /* __INTEL_SNPS_HDMI_PLL_H__ */
