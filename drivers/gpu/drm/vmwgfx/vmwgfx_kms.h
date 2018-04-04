/**************************************************************************
 *
 * Copyright © 2009-2015 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef VMWGFX_KMS_H_
#define VMWGFX_KMS_H_

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder.h>
#include "vmwgfx_drv.h"



/**
 * struct vmw_kms_dirty - closure structure for the vmw_kms_helper_dirty
 * function.
 *
 * @fifo_commit: Callback that is called once for each display unit after
 * all clip rects. This function must commit the fifo space reserved by the
 * helper. Set up by the caller.
 * @clip: Callback that is called for each cliprect on each display unit.
 * Set up by the caller.
 * @fifo_reserve_size: Fifo size that the helper should try to allocat for
 * each display unit. Set up by the caller.
 * @dev_priv: Pointer to the device private. Set up by the helper.
 * @unit: The current display unit. Set up by the helper before a call to @clip.
 * @cmd: The allocated fifo space. Set up by the helper before the first @clip
 * call.
 * @num_hits: Number of clip rect commands for this display unit.
 * Cleared by the helper before the first @clip call. Updated by the @clip
 * callback.
 * @fb_x: Clip rect left side in framebuffer coordinates.
 * @fb_y: Clip rect right side in framebuffer coordinates.
 * @unit_x1: Clip rect left side in crtc coordinates.
 * @unit_y1: Clip rect top side in crtc coordinates.
 * @unit_x2: Clip rect right side in crtc coordinates.
 * @unit_y2: Clip rect bottom side in crtc coordinates.
 *
 * The clip rect coordinates are updated by the helper for each @clip call.
 * Note that this may be derived from if more info needs to be passed between
 * helper caller and helper callbacks.
 */
struct vmw_kms_dirty {
	void (*fifo_commit)(struct vmw_kms_dirty *);
	void (*clip)(struct vmw_kms_dirty *);
	size_t fifo_reserve_size;
	struct vmw_private *dev_priv;
	struct vmw_display_unit *unit;
	void *cmd;
	u32 num_hits;
	s32 fb_x;
	s32 fb_y;
	s32 unit_x1;
	s32 unit_y1;
	s32 unit_x2;
	s32 unit_y2;
};

#define VMWGFX_NUM_DISPLAY_UNITS 8


#define vmw_framebuffer_to_vfb(x) \
	container_of(x, struct vmw_framebuffer, base)
#define vmw_framebuffer_to_vfbs(x) \
	container_of(x, struct vmw_framebuffer_surface, base.base)
#define vmw_framebuffer_to_vfbd(x) \
	container_of(x, struct vmw_framebuffer_dmabuf, base.base)

/**
 * Base class for framebuffers
 *
 * @pin is called the when ever a crtc uses this framebuffer
 * @unpin is called
 */
struct vmw_framebuffer {
	struct drm_framebuffer base;
	int (*pin)(struct vmw_framebuffer *fb);
	int (*unpin)(struct vmw_framebuffer *fb);
	bool dmabuf;
	struct ttm_base_object *user_obj;
	uint32_t user_handle;
};

/*
 * Clip rectangle
 */
struct vmw_clip_rect {
	int x1, x2, y1, y2;
};

struct vmw_framebuffer_surface {
	struct vmw_framebuffer base;
	struct vmw_surface *surface;
	struct vmw_dma_buffer *buffer;
	struct list_head head;
	bool is_dmabuf_proxy;  /* true if this is proxy surface for DMA buf */
};


struct vmw_framebuffer_dmabuf {
	struct vmw_framebuffer base;
	struct vmw_dma_buffer *buffer;
};


static const uint32_t vmw_primary_plane_formats[] = {
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const uint32_t vmw_cursor_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
};


#define vmw_crtc_state_to_vcs(x) container_of(x, struct vmw_crtc_state, base)
#define vmw_plane_state_to_vps(x) container_of(x, struct vmw_plane_state, base)
#define vmw_connector_state_to_vcs(x) \
		container_of(x, struct vmw_connector_state, base)

/**
 * Derived class for crtc state object
 *
 * @base DRM crtc object
 */
struct vmw_crtc_state {
	struct drm_crtc_state base;
};

/**
 * Derived class for plane state object
 *
 * @base DRM plane object
 * @surf Display surface for STDU
 * @dmabuf display dmabuf for SOU
 * @content_fb_type Used by STDU.
 * @dmabuf_size Size of the dmabuf, used by Screen Object Display Unit
 * @pinned pin count for STDU display surface
 */
struct vmw_plane_state {
	struct drm_plane_state base;
	struct vmw_surface *surf;
	struct vmw_dma_buffer *dmabuf;

	int content_fb_type;
	unsigned long dmabuf_size;

	int pinned;

	/* For CPU Blit */
	struct ttm_bo_kmap_obj host_map;
	unsigned int cpp;
};


/**
 * Derived class for connector state object
 *
 * @base DRM connector object
 * @is_implicit connector property
 *
 */
struct vmw_connector_state {
	struct drm_connector_state base;

	bool is_implicit;
};

/**
 * Base class display unit.
 *
 * Since the SVGA hw doesn't have a concept of a crtc, encoder or connector
 * so the display unit is all of them at the same time. This is true for both
 * legacy multimon and screen objects.
 */
struct vmw_display_unit {
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_plane primary;
	struct drm_plane cursor;

	struct vmw_surface *cursor_surface;
	struct vmw_dma_buffer *cursor_dmabuf;
	size_t cursor_age;

	int cursor_x;
	int cursor_y;

	int hotspot_x;
	int hotspot_y;
	s32 core_hotspot_x;
	s32 core_hotspot_y;

	unsigned unit;

	/*
	 * Prefered mode tracking.
	 */
	unsigned pref_width;
	unsigned pref_height;
	bool pref_active;
	struct drm_display_mode *pref_mode;

	/*
	 * Gui positioning
	 */
	int gui_x;
	int gui_y;
	bool is_implicit;
	bool active_implicit;
	int set_gui_x;
	int set_gui_y;
};

#define vmw_crtc_to_du(x) \
	container_of(x, struct vmw_display_unit, crtc)
#define vmw_connector_to_du(x) \
	container_of(x, struct vmw_display_unit, connector)


/*
 * Shared display unit functions - vmwgfx_kms.c
 */
void vmw_du_cleanup(struct vmw_display_unit *du);
void vmw_du_crtc_save(struct drm_crtc *crtc);
void vmw_du_crtc_restore(struct drm_crtc *crtc);
int vmw_du_crtc_gamma_set(struct drm_crtc *crtc,
			   u16 *r, u16 *g, u16 *b,
			   uint32_t size,
			   struct drm_modeset_acquire_ctx *ctx);
int vmw_du_connector_set_property(struct drm_connector *connector,
				  struct drm_property *property,
				  uint64_t val);
int vmw_du_connector_atomic_set_property(struct drm_connector *connector,
					 struct drm_connector_state *state,
					 struct drm_property *property,
					 uint64_t val);
int
vmw_du_connector_atomic_get_property(struct drm_connector *connector,
				     const struct drm_connector_state *state,
				     struct drm_property *property,
				     uint64_t *val);
int vmw_du_connector_dpms(struct drm_connector *connector, int mode);
void vmw_du_connector_save(struct drm_connector *connector);
void vmw_du_connector_restore(struct drm_connector *connector);
enum drm_connector_status
vmw_du_connector_detect(struct drm_connector *connector, bool force);
int vmw_du_connector_fill_modes(struct drm_connector *connector,
				uint32_t max_width, uint32_t max_height);
int vmw_kms_helper_dirty(struct vmw_private *dev_priv,
			 struct vmw_framebuffer *framebuffer,
			 const struct drm_clip_rect *clips,
			 const struct drm_vmw_rect *vclips,
			 s32 dest_x, s32 dest_y,
			 int num_clips,
			 int increment,
			 struct vmw_kms_dirty *dirty);

int vmw_kms_helper_buffer_prepare(struct vmw_private *dev_priv,
				  struct vmw_dma_buffer *buf,
				  bool interruptible,
				  bool validate_as_mob);
void vmw_kms_helper_buffer_revert(struct vmw_dma_buffer *buf);
void vmw_kms_helper_buffer_finish(struct vmw_private *dev_priv,
				  struct drm_file *file_priv,
				  struct vmw_dma_buffer *buf,
				  struct vmw_fence_obj **out_fence,
				  struct drm_vmw_fence_rep __user *
				  user_fence_rep);
int vmw_kms_helper_resource_prepare(struct vmw_resource *res,
				    bool interruptible);
void vmw_kms_helper_resource_revert(struct vmw_resource *res);
void vmw_kms_helper_resource_finish(struct vmw_resource *res,
				    struct vmw_fence_obj **out_fence);
int vmw_kms_readback(struct vmw_private *dev_priv,
		     struct drm_file *file_priv,
		     struct vmw_framebuffer *vfb,
		     struct drm_vmw_fence_rep __user *user_fence_rep,
		     struct drm_vmw_rect *vclips,
		     uint32_t num_clips);
struct vmw_framebuffer *
vmw_kms_new_framebuffer(struct vmw_private *dev_priv,
			struct vmw_dma_buffer *dmabuf,
			struct vmw_surface *surface,
			bool only_2d,
			const struct drm_mode_fb_cmd2 *mode_cmd);
int vmw_kms_fbdev_init_data(struct vmw_private *dev_priv,
			    unsigned unit,
			    u32 max_width,
			    u32 max_height,
			    struct drm_connector **p_con,
			    struct drm_crtc **p_crtc,
			    struct drm_display_mode **p_mode);
void vmw_guess_mode_timing(struct drm_display_mode *mode);
void vmw_kms_del_active(struct vmw_private *dev_priv,
			struct vmw_display_unit *du);
void vmw_kms_add_active(struct vmw_private *dev_priv,
			struct vmw_display_unit *du,
			struct vmw_framebuffer *vfb);
bool vmw_kms_crtc_flippable(struct vmw_private *dev_priv,
			    struct drm_crtc *crtc);
void vmw_kms_update_implicit_fb(struct vmw_private *dev_priv,
				struct drm_crtc *crtc);
void vmw_kms_create_implicit_placement_property(struct vmw_private *dev_priv,
						bool immutable);

/* Universal Plane Helpers */
void vmw_du_primary_plane_destroy(struct drm_plane *plane);
void vmw_du_cursor_plane_destroy(struct drm_plane *plane);

/* Atomic Helpers */
int vmw_du_primary_plane_atomic_check(struct drm_plane *plane,
				      struct drm_plane_state *state);
int vmw_du_cursor_plane_atomic_check(struct drm_plane *plane,
				     struct drm_plane_state *state);
void vmw_du_cursor_plane_atomic_update(struct drm_plane *plane,
				       struct drm_plane_state *old_state);
int vmw_du_cursor_plane_prepare_fb(struct drm_plane *plane,
				   struct drm_plane_state *new_state);
void vmw_du_plane_cleanup_fb(struct drm_plane *plane,
			     struct drm_plane_state *old_state);
void vmw_du_plane_reset(struct drm_plane *plane);
struct drm_plane_state *vmw_du_plane_duplicate_state(struct drm_plane *plane);
void vmw_du_plane_destroy_state(struct drm_plane *plane,
				struct drm_plane_state *state);
void vmw_du_plane_unpin_surf(struct vmw_plane_state *vps,
			     bool unreference);

int vmw_du_crtc_atomic_check(struct drm_crtc *crtc,
			     struct drm_crtc_state *state);
void vmw_du_crtc_atomic_begin(struct drm_crtc *crtc,
			      struct drm_crtc_state *old_crtc_state);
void vmw_du_crtc_atomic_flush(struct drm_crtc *crtc,
			      struct drm_crtc_state *old_crtc_state);
void vmw_du_crtc_reset(struct drm_crtc *crtc);
struct drm_crtc_state *vmw_du_crtc_duplicate_state(struct drm_crtc *crtc);
void vmw_du_crtc_destroy_state(struct drm_crtc *crtc,
				struct drm_crtc_state *state);
void vmw_du_connector_reset(struct drm_connector *connector);
struct drm_connector_state *
vmw_du_connector_duplicate_state(struct drm_connector *connector);

void vmw_du_connector_destroy_state(struct drm_connector *connector,
				    struct drm_connector_state *state);

/*
 * Legacy display unit functions - vmwgfx_ldu.c
 */
int vmw_kms_ldu_init_display(struct vmw_private *dev_priv);
int vmw_kms_ldu_close_display(struct vmw_private *dev_priv);
int vmw_kms_ldu_do_dmabuf_dirty(struct vmw_private *dev_priv,
				struct vmw_framebuffer *framebuffer,
				unsigned flags, unsigned color,
				struct drm_clip_rect *clips,
				unsigned num_clips, int increment);
int vmw_kms_update_proxy(struct vmw_resource *res,
			 const struct drm_clip_rect *clips,
			 unsigned num_clips,
			 int increment);

/*
 * Screen Objects display functions - vmwgfx_scrn.c
 */
int vmw_kms_sou_init_display(struct vmw_private *dev_priv);
int vmw_kms_sou_do_surface_dirty(struct vmw_private *dev_priv,
				 struct vmw_framebuffer *framebuffer,
				 struct drm_clip_rect *clips,
				 struct drm_vmw_rect *vclips,
				 struct vmw_resource *srf,
				 s32 dest_x,
				 s32 dest_y,
				 unsigned num_clips, int inc,
				 struct vmw_fence_obj **out_fence);
int vmw_kms_sou_do_dmabuf_dirty(struct vmw_private *dev_priv,
				struct vmw_framebuffer *framebuffer,
				struct drm_clip_rect *clips,
				struct drm_vmw_rect *vclips,
				unsigned num_clips, int increment,
				bool interruptible,
				struct vmw_fence_obj **out_fence);
int vmw_kms_sou_readback(struct vmw_private *dev_priv,
			 struct drm_file *file_priv,
			 struct vmw_framebuffer *vfb,
			 struct drm_vmw_fence_rep __user *user_fence_rep,
			 struct drm_vmw_rect *vclips,
			 uint32_t num_clips);

/*
 * Screen Target Display Unit functions - vmwgfx_stdu.c
 */
int vmw_kms_stdu_init_display(struct vmw_private *dev_priv);
int vmw_kms_stdu_surface_dirty(struct vmw_private *dev_priv,
			       struct vmw_framebuffer *framebuffer,
			       struct drm_clip_rect *clips,
			       struct drm_vmw_rect *vclips,
			       struct vmw_resource *srf,
			       s32 dest_x,
			       s32 dest_y,
			       unsigned num_clips, int inc,
			       struct vmw_fence_obj **out_fence);
int vmw_kms_stdu_dma(struct vmw_private *dev_priv,
		     struct drm_file *file_priv,
		     struct vmw_framebuffer *vfb,
		     struct drm_vmw_fence_rep __user *user_fence_rep,
		     struct drm_clip_rect *clips,
		     struct drm_vmw_rect *vclips,
		     uint32_t num_clips,
		     int increment,
		     bool to_surface,
		     bool interruptible);

int vmw_kms_set_config(struct drm_mode_set *set,
		       struct drm_modeset_acquire_ctx *ctx);

#endif
