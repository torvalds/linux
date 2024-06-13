/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_CRTC_STATE_DUMP_H__
#define __INTEL_CRTC_STATE_DUMP_H__

struct intel_crtc_state;
struct intel_atomic_state;

void intel_crtc_state_dump(const struct intel_crtc_state *crtc_state,
			   struct intel_atomic_state *state,
			   const char *context);

#endif /* __INTEL_CRTC_STATE_H__ */
