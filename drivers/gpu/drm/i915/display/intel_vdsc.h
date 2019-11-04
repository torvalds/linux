/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_VDSC_H__
#define __INTEL_VDSC_H__

struct intel_encoder;
struct intel_crtc_state;

void intel_dsc_enable(struct intel_encoder *encoder,
		      const struct intel_crtc_state *crtc_state);
void intel_dsc_disable(const struct intel_crtc_state *crtc_state);
int intel_dsc_compute_params(struct intel_encoder *encoder,
			     struct intel_crtc_state *pipe_config);
enum intel_display_power_domain
intel_dsc_power_domain(const struct intel_crtc_state *crtc_state);

#endif /* __INTEL_VDSC_H__ */
