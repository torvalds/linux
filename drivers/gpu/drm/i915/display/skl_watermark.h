/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __SKL_WATERMARK_H__
#define __SKL_WATERMARK_H__

#include <linux/types.h>

enum plane_id;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_dbuf_state;
struct intel_display;
struct intel_plane;
struct intel_plane_state;
struct skl_ddb_entry;
struct skl_pipe_wm;
struct skl_wm_level;

u8 intel_enabled_dbuf_slices_mask(struct intel_display *display);

void intel_sagv_pre_plane_update(struct intel_atomic_state *state);
void intel_sagv_post_plane_update(struct intel_atomic_state *state);
bool intel_crtc_can_enable_sagv(const struct intel_crtc_state *crtc_state);
bool intel_has_sagv(struct intel_display *display);

u32 skl_ddb_dbuf_slice_mask(struct intel_display *display,
			    const struct skl_ddb_entry *entry);

bool skl_ddb_allocation_overlaps(const struct skl_ddb_entry *ddb,
				 const struct skl_ddb_entry *entries,
				 int num_entries, int ignore_idx);

void intel_wm_state_verify(struct intel_atomic_state *state,
			   struct intel_crtc *crtc);

void skl_wm_crtc_disable_noatomic(struct intel_crtc *crtc);
void skl_wm_plane_disable_noatomic(struct intel_crtc *crtc,
				   struct intel_plane *plane);

void skl_watermark_ipc_init(struct intel_display *display);
void skl_watermark_ipc_update(struct intel_display *display);
bool skl_watermark_ipc_enabled(struct intel_display *display);
void skl_watermark_debugfs_register(struct intel_display *display);

unsigned int skl_watermark_max_latency(struct intel_display *display,
				       int initial_wm_level);
void skl_wm_init(struct intel_display *display);

const struct skl_wm_level *skl_plane_wm_level(const struct skl_pipe_wm *pipe_wm,
					      enum plane_id plane_id,
					      int level);
const struct skl_wm_level *skl_plane_trans_wm(const struct skl_pipe_wm *pipe_wm,
					      enum plane_id plane_id);
unsigned int skl_plane_relative_data_rate(const struct intel_crtc_state *crtc_state,
					  struct intel_plane *plane, int width,
					  int height, int cpp);

struct intel_dbuf_state *
intel_atomic_get_dbuf_state(struct intel_atomic_state *state);

int intel_dbuf_num_enabled_slices(const struct intel_dbuf_state *dbuf_state);
int intel_dbuf_num_active_pipes(const struct intel_dbuf_state *dbuf_state);

int intel_dbuf_init(struct intel_display *display);
int intel_dbuf_state_set_mdclk_cdclk_ratio(struct intel_atomic_state *state,
					   int ratio);

void intel_dbuf_pre_plane_update(struct intel_atomic_state *state);
void intel_dbuf_post_plane_update(struct intel_atomic_state *state);
void intel_dbuf_mdclk_cdclk_ratio_update(struct intel_display *display,
					 int ratio, bool joined_mbus);
void intel_dbuf_mbus_pre_ddb_update(struct intel_atomic_state *state);
void intel_dbuf_mbus_post_ddb_update(struct intel_atomic_state *state);
void intel_program_dpkgc_latency(struct intel_atomic_state *state);

bool intel_dbuf_pmdemand_needs_update(struct intel_atomic_state *state);

#endif /* __SKL_WATERMARK_H__ */

