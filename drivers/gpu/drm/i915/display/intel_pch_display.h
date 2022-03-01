/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _INTEL_PCH_DISPLAY_H_
#define _INTEL_PCH_DISPLAY_H_

struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;

void ilk_pch_enable(struct intel_atomic_state *state,
		    struct intel_crtc *crtc);
void ilk_pch_disable(struct intel_atomic_state *state,
		     struct intel_crtc *crtc);
void ilk_pch_post_disable(struct intel_atomic_state *state,
			  struct intel_crtc *crtc);
void ilk_pch_get_config(struct intel_crtc_state *crtc_state);

void lpt_pch_enable(struct intel_atomic_state *state,
		    struct intel_crtc *crtc);
void lpt_pch_disable(struct intel_atomic_state *state,
		     struct intel_crtc *crtc);
void lpt_pch_get_config(struct intel_crtc_state *crtc_state);

#endif
