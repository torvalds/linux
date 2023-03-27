/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __HSW_IPS_H__
#define __HSW_IPS_H__

#include <linux/types.h>

struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;

bool hsw_ips_disable(const struct intel_crtc_state *crtc_state);
bool hsw_ips_pre_update(struct intel_atomic_state *state,
			struct intel_crtc *crtc);
void hsw_ips_post_update(struct intel_atomic_state *state,
			 struct intel_crtc *crtc);
bool hsw_crtc_supports_ips(struct intel_crtc *crtc);
bool hsw_crtc_state_ips_capable(const struct intel_crtc_state *crtc_state);
int hsw_ips_compute_config(struct intel_atomic_state *state,
			   struct intel_crtc *crtc);
void hsw_ips_get_config(struct intel_crtc_state *crtc_state);
void hsw_ips_crtc_debugfs_add(struct intel_crtc *crtc);

#endif /* __HSW_IPS_H__ */
