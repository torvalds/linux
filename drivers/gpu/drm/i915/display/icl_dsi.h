/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __ICL_DSI_H__
#define __ICL_DSI_H__

struct drm_i915_private;
struct intel_bios_encoder_data;
struct intel_crtc_state;

void icl_dsi_init(struct drm_i915_private *dev_priv,
		  const struct intel_bios_encoder_data *devdata);
void icl_dsi_frame_update(struct intel_crtc_state *crtc_state);

#endif /* __ICL_DSI_H__ */
