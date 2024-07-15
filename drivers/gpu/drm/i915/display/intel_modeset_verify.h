/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_MODESET_VERIFY_H__
#define __INTEL_MODESET_VERIFY_H__

struct intel_atomic_state;
struct intel_crtc;

void intel_modeset_verify_crtc(struct intel_atomic_state *state,
			       struct intel_crtc *crtc);
void intel_modeset_verify_disabled(struct intel_atomic_state *state);

#endif /* __INTEL_MODESET_VERIFY_H__ */
