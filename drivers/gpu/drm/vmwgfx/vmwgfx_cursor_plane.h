/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2024-2025 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 *
 **************************************************************************/

#ifndef VMWGFX_CURSOR_PLANE_H
#define VMWGFX_CURSOR_PLANE_H

#include "device_include/svga3d_cmd.h"
#include "drm/drm_file.h"
#include "drm/drm_fourcc.h"
#include "drm/drm_plane.h"

#include <linux/types.h>

struct SVGA3dCmdHeader;
struct ttm_buffer_object;
struct vmw_bo;
struct vmw_cursor;
struct vmw_private;
struct vmw_surface;
struct vmw_user_object;

#define vmw_plane_to_vcp(x) container_of(x, struct vmw_cursor_plane, base)

static const u32 __maybe_unused vmw_cursor_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
};

enum vmw_cursor_update_type {
	VMW_CURSOR_UPDATE_NONE = 0,
	VMW_CURSOR_UPDATE_LEGACY,
	VMW_CURSOR_UPDATE_GB_ONLY,
	VMW_CURSOR_UPDATE_MOB,
};

struct vmw_cursor_plane_state {
	enum vmw_cursor_update_type update_type;
	bool changed;
	bool surface_changed;
	struct vmw_bo *mob;
	struct {
		s32 hotspot_x;
		s32 hotspot_y;
		u32 id;
	} legacy;
};

/**
 * Derived class for cursor plane object
 *
 * @base DRM plane object
 * @cursor.cursor_mobs Cursor mobs available for re-use
 */
struct vmw_cursor_plane {
	struct drm_plane base;

	struct vmw_bo *cursor_mobs[3];
};

struct vmw_surface_metadata;
void *vmw_cursor_snooper_create(struct drm_file *file_priv,
				struct vmw_surface_metadata *metadata);
void vmw_cursor_cmd_dma_snoop(SVGA3dCmdHeader *header,
			      struct vmw_surface *srf,
			      struct ttm_buffer_object *bo);

void vmw_cursor_plane_destroy(struct drm_plane *plane);

int vmw_cursor_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state);
void vmw_cursor_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_state *state);
int vmw_cursor_plane_prepare_fb(struct drm_plane *plane,
				struct drm_plane_state *new_state);
void vmw_cursor_plane_cleanup_fb(struct drm_plane *plane,
				 struct drm_plane_state *old_state);

#endif /* VMWGFX_CURSOR_H */
