/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_SNPS_PHY_H__
#define __INTEL_SNPS_PHY_H__

struct intel_encoder;
struct intel_crtc_state;

int intel_mpllb_calc_state(struct intel_crtc_state *crtc_state,
			   struct intel_encoder *encoder);
void intel_mpllb_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state);
void intel_mpllb_disable(struct intel_encoder *encoder);

#endif /* __INTEL_SNPS_PHY_H__ */
