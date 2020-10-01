/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DP_LINK_TRAINING_H__
#define __INTEL_DP_LINK_TRAINING_H__

#include <drm/drm_dp_helper.h>

struct intel_crtc_state;
struct intel_dp;

void intel_dp_get_adjust_train(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state,
			       const u8 link_status[DP_LINK_STATUS_SIZE]);
void intel_dp_start_link_train(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state);
void intel_dp_stop_link_train(struct intel_dp *intel_dp,
			      const struct intel_crtc_state *crtc_state);

#endif /* __INTEL_DP_LINK_TRAINING_H__ */
