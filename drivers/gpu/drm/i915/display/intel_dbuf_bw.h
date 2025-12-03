/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef __INTEL_DBUF_BW_H__
#define __INTEL_DBUF_BW_H__

#include <drm/drm_atomic.h>

struct intel_atomic_state;
struct intel_dbuf_bw_state;
struct intel_crtc;
struct intel_display;
struct intel_global_state;

struct intel_dbuf_bw_state *
to_intel_dbuf_bw_state(struct intel_global_state *obj_state);

struct intel_dbuf_bw_state *
intel_atomic_get_old_dbuf_bw_state(struct intel_atomic_state *state);

struct intel_dbuf_bw_state *
intel_atomic_get_new_dbuf_bw_state(struct intel_atomic_state *state);

struct intel_dbuf_bw_state *
intel_atomic_get_dbuf_bw_state(struct intel_atomic_state *state);

int intel_dbuf_bw_init(struct intel_display *display);
int intel_dbuf_bw_calc_min_cdclk(struct intel_atomic_state *state,
				 bool *need_cdclk_calc);
int intel_dbuf_bw_min_cdclk(struct intel_display *display,
			    const struct intel_dbuf_bw_state *dbuf_bw_state);
void intel_dbuf_bw_update_hw_state(struct intel_display *display);
void intel_dbuf_bw_crtc_disable_noatomic(struct intel_crtc *crtc);

#endif /* __INTEL_DBUF_BW_H__ */
