/**************************************************************************
 *
 * Copyright © 2009-2014 VMware, Inc., Palo Alto, CA., USA
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
#include "vmwgfx_drv.h"



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
	struct drm_master *master;
	bool is_dmabuf_proxy;  /* true if this is proxy surface for DMA buf */
};


struct vmw_framebuffer_dmabuf {
	struct vmw_framebuffer base;
	struct vmw_dma_buffer *buffer;
};


/*
 * Basic clip rect manipulation
 */
void vmw_clip_cliprects(struct drm_clip_rect *rects,
			int num_rects,
			struct vmw_clip_rect clip,
			SVGASignedRect *out_rects,
			int *out_num);

/*
 * Basic cursor manipulation
 */
int vmw_cursor_update_image(struct vmw_private *dev_priv,
			    u32 *image, u32 width, u32 height,
			    u32 hotspotX, u32 hotspotY);
int vmw_cursor_update_dmabuf(struct vmw_private *dev_priv,
			     struct vmw_dma_buffer *dmabuf,
			     u32 width, u32 height,
			     u32 hotspotX, u32 hotspotY);
void vmw_cursor_update_position(struct vmw_private *dev_priv,
				bool show, int x, int y);


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

	struct vmw_surface *cursor_surface;
	struct vmw_dma_buffer *cursor_dmabuf;
	size_t cursor_age;

	int cursor_x;
	int cursor_y;

	int hotspot_x;
	int hotspot_y;

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
void vmw_du_crtc_gamma_set(struct drm_crtc *crtc,
			   u16 *r, u16 *g, u16 *b,
			   uint32_t start, uint32_t size);
int vmw_du_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
			   uint32_t handle, uint32_t width, uint32_t height);
int vmw_du_crtc_cursor_move(struct drm_crtc *crtc, int x, int y);
void vmw_du_connector_dpms(struct drm_connector *connector, int mode);
void vmw_du_connector_save(struct drm_connector *connector);
void vmw_du_connector_restore(struct drm_connector *connector);
enum drm_connector_status
vmw_du_connector_detect(struct drm_connector *connector, bool force);
int vmw_du_connector_fill_modes(struct drm_connector *connector,
				uint32_t max_width, uint32_t max_height);
int vmw_du_connector_set_property(struct drm_connector *connector,
				  struct drm_property *property,
				  uint64_t val);


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

/*
 * Screen Objects display functions - vmwgfx_scrn.c
 */
int vmw_kms_sou_init_display(struct vmw_private *dev_priv);
int vmw_kms_sou_close_display(struct vmw_private *dev_priv);
int vmw_kms_sou_do_surface_dirty(struct vmw_private *dev_priv,
				 struct drm_file *file_priv,
				 struct vmw_framebuffer *framebuffer,
				 unsigned flags, unsigned color,
				 struct drm_clip_rect *clips,
				 unsigned num_clips, int inc,
				 struct vmw_fence_obj **out_fence);
int vmw_kms_sou_do_dmabuf_dirty(struct drm_file *file_priv,
				struct vmw_private *dev_priv,
				struct vmw_framebuffer *framebuffer,
				unsigned flags, unsigned color,
				struct drm_clip_rect *clips,
				unsigned num_clips, int increment,
				struct vmw_fence_obj **out_fence);


/*
 * Screen Target Display Unit functions - vmwgfx_stdu.c
 */
int vmw_kms_stdu_init_display(struct vmw_private *dev_priv);
int vmw_kms_stdu_close_display(struct vmw_private *dev_priv);
int vmw_kms_stdu_do_surface_dirty(struct vmw_private *dev_priv,
				  struct drm_file *file_priv,
				  struct vmw_framebuffer *framebuffer,
				  struct drm_clip_rect *clips,
				  unsigned num_clips, int increment);
int vmw_kms_stdu_present(struct vmw_private *dev_priv,
			 struct drm_file *file_priv,
			 struct vmw_framebuffer *vfb,
			 uint32_t user_handle,
			 int32_t dest_x, int32_t dest_y,
			 struct drm_vmw_rect *clips,
			 uint32_t num_clips);

#endif
