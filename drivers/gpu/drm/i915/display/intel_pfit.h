/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef __INTEL_PFIT_H__
#define __INTEL_PFIT_H__

struct drm_connector_state;
struct intel_crtc_state;

int intel_pfit_compute_config(struct intel_crtc_state *crtc_state,
			      const struct drm_connector_state *conn_state);
void ilk_pfit_enable(const struct intel_crtc_state *crtc_state);
void ilk_pfit_disable(const struct intel_crtc_state *old_crtc_state);
void ilk_pfit_get_config(struct intel_crtc_state *crtc_state);
void i9xx_pfit_enable(const struct intel_crtc_state *crtc_state);
void i9xx_pfit_disable(const struct intel_crtc_state *old_crtc_state);
void i9xx_pfit_get_config(struct intel_crtc_state *crtc_state);

#endif /* __INTEL_PFIT_H__ */
