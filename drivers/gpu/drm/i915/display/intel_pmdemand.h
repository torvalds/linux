/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_PMDEMAND_H__
#define __INTEL_PMDEMAND_H__

#include <linux/types.h>

enum pipe;
struct intel_atomic_state;
struct intel_crtc_state;
struct intel_display;
struct intel_encoder;
struct intel_global_state;
struct intel_plane_state;
struct intel_pmdemand_state;

struct intel_pmdemand_state *to_intel_pmdemand_state(struct intel_global_state *obj_state);

void intel_pmdemand_init_early(struct intel_display *display);
int intel_pmdemand_init(struct intel_display *display);
void intel_pmdemand_init_pmdemand_params(struct intel_display *display,
					 struct intel_pmdemand_state *pmdemand_state);
void intel_pmdemand_update_port_clock(struct intel_display *display,
				      struct intel_pmdemand_state *pmdemand_state,
				      enum pipe pipe, int port_clock);
void intel_pmdemand_update_phys_mask(struct intel_display *display,
				     struct intel_encoder *encoder,
				     struct intel_pmdemand_state *pmdemand_state,
				     bool clear_bit);
void intel_pmdemand_program_dbuf(struct intel_display *display,
				 u8 dbuf_slices);
void intel_pmdemand_pre_plane_update(struct intel_atomic_state *state);
void intel_pmdemand_post_plane_update(struct intel_atomic_state *state);
int intel_pmdemand_atomic_check(struct intel_atomic_state *state);

#endif /* __INTEL_PMDEMAND_H__ */
