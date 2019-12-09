// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_mode.c
 * Copyright 2012 Red Hat Inc.
 * Parts based on xf86-video-ast
 * Copyright (c) 2005 ASPEED Technology Inc.
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com,
 *          Hans de Goede <hdegoede@redhat.com>
 */
#include <linux/export.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "hgsmi_channels.h"
#include "vbox_drv.h"
#include "vboxvideo.h"

/*
 * Set a graphics mode.  Poke any required values into registers, do an HGSMI
 * mode set and tell the host we support advanced graphics functions.
 */
static void vbox_do_modeset(struct drm_crtc *crtc)
{
	struct drm_framebuffer *fb = crtc->primary->state->fb;
	struct vbox_crtc *vbox_crtc = to_vbox_crtc(crtc);
	struct vbox_private *vbox;
	int width, height, bpp, pitch;
	u16 flags;
	s32 x_offset, y_offset;

	vbox = crtc->dev->dev_private;
	width = vbox_crtc->width ? vbox_crtc->width : 640;
	height = vbox_crtc->height ? vbox_crtc->height : 480;
	bpp = fb ? fb->format->cpp[0] * 8 : 32;
	pitch = fb ? fb->pitches[0] : width * bpp / 8;
	x_offset = vbox->single_framebuffer ? vbox_crtc->x : vbox_crtc->x_hint;
	y_offset = vbox->single_framebuffer ? vbox_crtc->y : vbox_crtc->y_hint;

	/*
	 * This is the old way of setting graphics modes.  It assumed one screen
	 * and a frame-buffer at the start of video RAM.  On older versions of
	 * VirtualBox, certain parts of the code still assume that the first
	 * screen is programmed this way, so try to fake it.
	 */
	if (vbox_crtc->crtc_id == 0 && fb &&
	    vbox_crtc->fb_offset / pitch < 0xffff - crtc->y &&
	    vbox_crtc->fb_offset % (bpp / 8) == 0) {
		vbox_write_ioport(VBE_DISPI_INDEX_XRES, width);
		vbox_write_ioport(VBE_DISPI_INDEX_YRES, height);
		vbox_write_ioport(VBE_DISPI_INDEX_VIRT_WIDTH, pitch * 8 / bpp);
		vbox_write_ioport(VBE_DISPI_INDEX_BPP, bpp);
		vbox_write_ioport(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED);
		vbox_write_ioport(VBE_DISPI_INDEX_X_OFFSET,
			vbox_crtc->fb_offset % pitch / bpp * 8 + vbox_crtc->x);
		vbox_write_ioport(VBE_DISPI_INDEX_Y_OFFSET,
				  vbox_crtc->fb_offset / pitch + vbox_crtc->y);
	}

	flags = VBVA_SCREEN_F_ACTIVE;
	flags |= (fb && crtc->state->enable) ? 0 : VBVA_SCREEN_F_BLANK;
	flags |= vbox_crtc->disconnected ? VBVA_SCREEN_F_DISABLED : 0;
	hgsmi_process_display_info(vbox->guest_pool, vbox_crtc->crtc_id,
				   x_offset, y_offset,
				   vbox_crtc->x * bpp / 8 +
							vbox_crtc->y * pitch,
				   pitch, width, height, bpp, flags);
}

static int vbox_set_view(struct drm_crtc *crtc)
{
	struct vbox_crtc *vbox_crtc = to_vbox_crtc(crtc);
	struct vbox_private *vbox = crtc->dev->dev_private;
	struct vbva_infoview *p;

	/*
	 * Tell the host about the view.  This design originally targeted the
	 * Windows XP driver architecture and assumed that each screen would
	 * have a dedicated frame buffer with the command buffer following it,
	 * the whole being a "view".  The host works out which screen a command
	 * buffer belongs to by checking whether it is in the first view, then
	 * whether it is in the second and so on.  The first match wins.  We
	 * cheat around this by making the first view be the managed memory
	 * plus the first command buffer, the second the same plus the second
	 * buffer and so on.
	 */
	p = hgsmi_buffer_alloc(vbox->guest_pool, sizeof(*p),
			       HGSMI_CH_VBVA, VBVA_INFO_VIEW);
	if (!p)
		return -ENOMEM;

	p->view_index = vbox_crtc->crtc_id;
	p->view_offset = vbox_crtc->fb_offset;
	p->view_size = vbox->available_vram_size - vbox_crtc->fb_offset +
		       vbox_crtc->crtc_id * VBVA_MIN_BUFFER_SIZE;
	p->max_screen_size = vbox->available_vram_size - vbox_crtc->fb_offset;

	hgsmi_buffer_submit(vbox->guest_pool, p);
	hgsmi_buffer_free(vbox->guest_pool, p);

	return 0;
}

/*
 * Try to map the layout of virtual screens to the range of the input device.
 * Return true if we need to re-set the crtc modes due to screen offset
 * changes.
 */
static bool vbox_set_up_input_mapping(struct vbox_private *vbox)
{
	struct drm_crtc *crtci;
	struct drm_connector *connectori;
	struct drm_framebuffer *fb, *fb1 = NULL;
	bool single_framebuffer = true;
	bool old_single_framebuffer = vbox->single_framebuffer;
	u16 width = 0, height = 0;

	/*
	 * Are we using an X.Org-style single large frame-buffer for all crtcs?
	 * If so then screen layout can be deduced from the crtc offsets.
	 * Same fall-back if this is the fbdev frame-buffer.
	 */
	list_for_each_entry(crtci, &vbox->ddev.mode_config.crtc_list, head) {
		fb = crtci->primary->state->fb;
		if (!fb)
			continue;

		if (!fb1) {
			fb1 = fb;
			if (fb1 == vbox->ddev.fb_helper->fb)
				break;
		} else if (fb != fb1) {
			single_framebuffer = false;
		}
	}
	if (!fb1)
		return false;

	if (single_framebuffer) {
		vbox->single_framebuffer = true;
		vbox->input_mapping_width = fb1->width;
		vbox->input_mapping_height = fb1->height;
		return old_single_framebuffer != vbox->single_framebuffer;
	}
	/* Otherwise calculate the total span of all screens. */
	list_for_each_entry(connectori, &vbox->ddev.mode_config.connector_list,
			    head) {
		struct vbox_connector *vbox_connector =
		    to_vbox_connector(connectori);
		struct vbox_crtc *vbox_crtc = vbox_connector->vbox_crtc;

		width = max_t(u16, width, vbox_crtc->x_hint +
					  vbox_connector->mode_hint.width);
		height = max_t(u16, height, vbox_crtc->y_hint +
					    vbox_connector->mode_hint.height);
	}

	vbox->single_framebuffer = false;
	vbox->input_mapping_width = width;
	vbox->input_mapping_height = height;

	return old_single_framebuffer != vbox->single_framebuffer;
}

static void vbox_crtc_set_base_and_mode(struct drm_crtc *crtc,
					struct drm_framebuffer *fb,
					int x, int y)
{
	struct drm_gem_vram_object *gbo = drm_gem_vram_of_gem(fb->obj[0]);
	struct vbox_private *vbox = crtc->dev->dev_private;
	struct vbox_crtc *vbox_crtc = to_vbox_crtc(crtc);
	bool needs_modeset = drm_atomic_crtc_needs_modeset(crtc->state);

	mutex_lock(&vbox->hw_mutex);

	if (crtc->state->enable) {
		vbox_crtc->width = crtc->state->mode.hdisplay;
		vbox_crtc->height = crtc->state->mode.vdisplay;
	}

	vbox_crtc->x = x;
	vbox_crtc->y = y;
	vbox_crtc->fb_offset = drm_gem_vram_offset(gbo);

	/* vbox_do_modeset() checks vbox->single_framebuffer so update it now */
	if (needs_modeset && vbox_set_up_input_mapping(vbox)) {
		struct drm_crtc *crtci;

		list_for_each_entry(crtci, &vbox->ddev.mode_config.crtc_list,
				    head) {
			if (crtci == crtc)
				continue;
			vbox_do_modeset(crtci);
		}
	}

	vbox_set_view(crtc);
	vbox_do_modeset(crtc);

	if (needs_modeset)
		hgsmi_update_input_mapping(vbox->guest_pool, 0, 0,
					   vbox->input_mapping_width,
					   vbox->input_mapping_height);

	mutex_unlock(&vbox->hw_mutex);
}

static void vbox_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_crtc_state)
{
}

static void vbox_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_crtc_state)
{
}

static void vbox_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	if (crtc->state && crtc->state->event) {
		event = crtc->state->event;
		crtc->state->event = NULL;

		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
	}
}

static const struct drm_crtc_helper_funcs vbox_crtc_helper_funcs = {
	.atomic_enable = vbox_crtc_atomic_enable,
	.atomic_disable = vbox_crtc_atomic_disable,
	.atomic_flush = vbox_crtc_atomic_flush,
};

static void vbox_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static const struct drm_crtc_funcs vbox_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	/* .gamma_set = vbox_crtc_gamma_set, */
	.destroy = vbox_crtc_destroy,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static int vbox_primary_atomic_check(struct drm_plane *plane,
				     struct drm_plane_state *new_state)
{
	struct drm_crtc_state *crtc_state = NULL;

	if (new_state->crtc) {
		crtc_state = drm_atomic_get_existing_crtc_state(
					    new_state->state, new_state->crtc);
		if (WARN_ON(!crtc_state))
			return -EINVAL;
	}

	return drm_atomic_helper_check_plane_state(new_state, crtc_state,
						   DRM_PLANE_HELPER_NO_SCALING,
						   DRM_PLANE_HELPER_NO_SCALING,
						   false, true);
}

static void vbox_primary_atomic_update(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct drm_crtc *crtc = plane->state->crtc;
	struct drm_framebuffer *fb = plane->state->fb;
	struct vbox_private *vbox = fb->dev->dev_private;
	struct drm_mode_rect *clips;
	uint32_t num_clips, i;

	vbox_crtc_set_base_and_mode(crtc, fb,
				    plane->state->src_x >> 16,
				    plane->state->src_y >> 16);

	/* Send information about dirty rectangles to VBVA. */

	clips = drm_plane_get_damage_clips(plane->state);
	num_clips = drm_plane_get_damage_clips_count(plane->state);

	if (!num_clips)
		return;

	mutex_lock(&vbox->hw_mutex);

	for (i = 0; i < num_clips; ++i, ++clips) {
		struct vbva_cmd_hdr cmd_hdr;
		unsigned int crtc_id = to_vbox_crtc(crtc)->crtc_id;

		cmd_hdr.x = (s16)clips->x1;
		cmd_hdr.y = (s16)clips->y1;
		cmd_hdr.w = (u16)clips->x2 - clips->x1;
		cmd_hdr.h = (u16)clips->y2 - clips->y1;

		if (!vbva_buffer_begin_update(&vbox->vbva_info[crtc_id],
					      vbox->guest_pool))
			continue;

		vbva_write(&vbox->vbva_info[crtc_id], vbox->guest_pool,
			   &cmd_hdr, sizeof(cmd_hdr));
		vbva_buffer_end_update(&vbox->vbva_info[crtc_id]);
	}

	mutex_unlock(&vbox->hw_mutex);
}

static void vbox_primary_atomic_disable(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct drm_crtc *crtc = old_state->crtc;

	/* vbox_do_modeset checks plane->state->fb and will disable if NULL */
	vbox_crtc_set_base_and_mode(crtc, old_state->fb,
				    old_state->src_x >> 16,
				    old_state->src_y >> 16);
}

static int vbox_cursor_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *new_state)
{
	struct drm_crtc_state *crtc_state = NULL;
	u32 width = new_state->crtc_w;
	u32 height = new_state->crtc_h;
	int ret;

	if (new_state->crtc) {
		crtc_state = drm_atomic_get_existing_crtc_state(
					    new_state->state, new_state->crtc);
		if (WARN_ON(!crtc_state))
			return -EINVAL;
	}

	ret = drm_atomic_helper_check_plane_state(new_state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  true, true);
	if (ret)
		return ret;

	if (!new_state->fb)
		return 0;

	if (width > VBOX_MAX_CURSOR_WIDTH || height > VBOX_MAX_CURSOR_HEIGHT ||
	    width == 0 || height == 0)
		return -EINVAL;

	return 0;
}

/*
 * Copy the ARGB image and generate the mask, which is needed in case the host
 * does not support ARGB cursors.  The mask is a 1BPP bitmap with the bit set
 * if the corresponding alpha value in the ARGB image is greater than 0xF0.
 */
static void copy_cursor_image(u8 *src, u8 *dst, u32 width, u32 height,
			      size_t mask_size)
{
	size_t line_size = (width + 7) / 8;
	u32 i, j;

	memcpy(dst + mask_size, src, width * height * 4);
	for (i = 0; i < height; ++i)
		for (j = 0; j < width; ++j)
			if (((u32 *)src)[i * width + j] > 0xf0000000)
				dst[i * line_size + j / 8] |= (0x80 >> (j % 8));
}

static void vbox_cursor_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct vbox_private *vbox =
		container_of(plane->dev, struct vbox_private, ddev);
	struct vbox_crtc *vbox_crtc = to_vbox_crtc(plane->state->crtc);
	struct drm_framebuffer *fb = plane->state->fb;
	struct drm_gem_vram_object *gbo = drm_gem_vram_of_gem(fb->obj[0]);
	u32 width = plane->state->crtc_w;
	u32 height = plane->state->crtc_h;
	size_t data_size, mask_size;
	u32 flags;
	u8 *src;

	/*
	 * VirtualBox uses the host windowing system to draw the cursor so
	 * moves are a no-op, we only need to upload new cursor sprites.
	 */
	if (fb == old_state->fb)
		return;

	mutex_lock(&vbox->hw_mutex);

	vbox_crtc->cursor_enabled = true;

	/* pinning is done in prepare/cleanup framebuffer */
	src = drm_gem_vram_kmap(gbo, true, NULL);
	if (IS_ERR(src)) {
		mutex_unlock(&vbox->hw_mutex);
		DRM_WARN("Could not kmap cursor bo, skipping update\n");
		return;
	}

	/*
	 * The mask must be calculated based on the alpha
	 * channel, one bit per ARGB word, and must be 32-bit
	 * padded.
	 */
	mask_size = ((width + 7) / 8 * height + 3) & ~3;
	data_size = width * height * 4 + mask_size;

	copy_cursor_image(src, vbox->cursor_data, width, height, mask_size);
	drm_gem_vram_kunmap(gbo);

	flags = VBOX_MOUSE_POINTER_VISIBLE | VBOX_MOUSE_POINTER_SHAPE |
		VBOX_MOUSE_POINTER_ALPHA;
	hgsmi_update_pointer_shape(vbox->guest_pool, flags,
				   min_t(u32, max(fb->hot_x, 0), width),
				   min_t(u32, max(fb->hot_y, 0), height),
				   width, height, vbox->cursor_data, data_size);

	mutex_unlock(&vbox->hw_mutex);
}

static void vbox_cursor_atomic_disable(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct vbox_private *vbox =
		container_of(plane->dev, struct vbox_private, ddev);
	struct vbox_crtc *vbox_crtc = to_vbox_crtc(old_state->crtc);
	bool cursor_enabled = false;
	struct drm_crtc *crtci;

	mutex_lock(&vbox->hw_mutex);

	vbox_crtc->cursor_enabled = false;

	list_for_each_entry(crtci, &vbox->ddev.mode_config.crtc_list, head) {
		if (to_vbox_crtc(crtci)->cursor_enabled)
			cursor_enabled = true;
	}

	if (!cursor_enabled)
		hgsmi_update_pointer_shape(vbox->guest_pool, 0, 0, 0,
					   0, 0, NULL, 0);

	mutex_unlock(&vbox->hw_mutex);
}

static const u32 vbox_cursor_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
};

static const struct drm_plane_helper_funcs vbox_cursor_helper_funcs = {
	.atomic_check	= vbox_cursor_atomic_check,
	.atomic_update	= vbox_cursor_atomic_update,
	.atomic_disable	= vbox_cursor_atomic_disable,
	.prepare_fb	= drm_gem_vram_plane_helper_prepare_fb,
	.cleanup_fb	= drm_gem_vram_plane_helper_cleanup_fb,
};

static const struct drm_plane_funcs vbox_cursor_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_primary_helper_destroy,
	.reset		= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static const u32 vbox_primary_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const struct drm_plane_helper_funcs vbox_primary_helper_funcs = {
	.atomic_check = vbox_primary_atomic_check,
	.atomic_update = vbox_primary_atomic_update,
	.atomic_disable = vbox_primary_atomic_disable,
	.prepare_fb	= drm_gem_vram_plane_helper_prepare_fb,
	.cleanup_fb	= drm_gem_vram_plane_helper_cleanup_fb,
};

static const struct drm_plane_funcs vbox_primary_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_primary_helper_destroy,
	.reset		= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static struct drm_plane *vbox_create_plane(struct vbox_private *vbox,
					   unsigned int possible_crtcs,
					   enum drm_plane_type type)
{
	const struct drm_plane_helper_funcs *helper_funcs = NULL;
	const struct drm_plane_funcs *funcs;
	struct drm_plane *plane;
	const u32 *formats;
	int num_formats;
	int err;

	if (type == DRM_PLANE_TYPE_PRIMARY) {
		funcs = &vbox_primary_plane_funcs;
		formats = vbox_primary_plane_formats;
		helper_funcs = &vbox_primary_helper_funcs;
		num_formats = ARRAY_SIZE(vbox_primary_plane_formats);
	} else if (type == DRM_PLANE_TYPE_CURSOR) {
		funcs = &vbox_cursor_plane_funcs;
		formats = vbox_cursor_plane_formats;
		helper_funcs = &vbox_cursor_helper_funcs;
		num_formats = ARRAY_SIZE(vbox_cursor_plane_formats);
	} else {
		return ERR_PTR(-EINVAL);
	}

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	err = drm_universal_plane_init(&vbox->ddev, plane, possible_crtcs,
				       funcs, formats, num_formats,
				       NULL, type, NULL);
	if (err)
		goto free_plane;

	drm_plane_helper_add(plane, helper_funcs);

	return plane;

free_plane:
	kfree(plane);
	return ERR_PTR(-EINVAL);
}

static struct vbox_crtc *vbox_crtc_init(struct drm_device *dev, unsigned int i)
{
	struct vbox_private *vbox =
		container_of(dev, struct vbox_private, ddev);
	struct drm_plane *cursor = NULL;
	struct vbox_crtc *vbox_crtc;
	struct drm_plane *primary;
	u32 caps = 0;
	int ret;

	ret = hgsmi_query_conf(vbox->guest_pool,
			       VBOX_VBVA_CONF32_CURSOR_CAPABILITIES, &caps);
	if (ret)
		return ERR_PTR(ret);

	vbox_crtc = kzalloc(sizeof(*vbox_crtc), GFP_KERNEL);
	if (!vbox_crtc)
		return ERR_PTR(-ENOMEM);

	primary = vbox_create_plane(vbox, 1 << i, DRM_PLANE_TYPE_PRIMARY);
	if (IS_ERR(primary)) {
		ret = PTR_ERR(primary);
		goto free_mem;
	}

	if ((caps & VBOX_VBVA_CURSOR_CAPABILITY_HARDWARE)) {
		cursor = vbox_create_plane(vbox, 1 << i, DRM_PLANE_TYPE_CURSOR);
		if (IS_ERR(cursor)) {
			ret = PTR_ERR(cursor);
			goto clean_primary;
		}
	} else {
		DRM_WARN("VirtualBox host is too old, no cursor support\n");
	}

	vbox_crtc->crtc_id = i;

	ret = drm_crtc_init_with_planes(dev, &vbox_crtc->base, primary, cursor,
					&vbox_crtc_funcs, NULL);
	if (ret)
		goto clean_cursor;

	drm_mode_crtc_set_gamma_size(&vbox_crtc->base, 256);
	drm_crtc_helper_add(&vbox_crtc->base, &vbox_crtc_helper_funcs);

	return vbox_crtc;

clean_cursor:
	if (cursor) {
		drm_plane_cleanup(cursor);
		kfree(cursor);
	}
clean_primary:
	drm_plane_cleanup(primary);
	kfree(primary);
free_mem:
	kfree(vbox_crtc);
	return ERR_PTR(ret);
}

static void vbox_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs vbox_enc_funcs = {
	.destroy = vbox_encoder_destroy,
};

static struct drm_encoder *vbox_encoder_init(struct drm_device *dev,
					     unsigned int i)
{
	struct vbox_encoder *vbox_encoder;

	vbox_encoder = kzalloc(sizeof(*vbox_encoder), GFP_KERNEL);
	if (!vbox_encoder)
		return NULL;

	drm_encoder_init(dev, &vbox_encoder->base, &vbox_enc_funcs,
			 DRM_MODE_ENCODER_DAC, NULL);

	vbox_encoder->base.possible_crtcs = 1 << i;
	return &vbox_encoder->base;
}

/*
 * Generate EDID data with a mode-unique serial number for the virtual
 * monitor to try to persuade Unity that different modes correspond to
 * different monitors and it should not try to force the same resolution on
 * them.
 */
static void vbox_set_edid(struct drm_connector *connector, int width,
			  int height)
{
	enum { EDID_SIZE = 128 };
	unsigned char edid[EDID_SIZE] = {
		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,	/* header */
		0x58, 0x58,	/* manufacturer (VBX) */
		0x00, 0x00,	/* product code */
		0x00, 0x00, 0x00, 0x00,	/* serial number goes here */
		0x01,		/* week of manufacture */
		0x00,		/* year of manufacture */
		0x01, 0x03,	/* EDID version */
		0x80,		/* capabilities - digital */
		0x00,		/* horiz. res in cm, zero for projectors */
		0x00,		/* vert. res in cm */
		0x78,		/* display gamma (120 == 2.2). */
		0xEE,		/* features (standby, suspend, off, RGB, std */
				/* colour space, preferred timing mode) */
		0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54,
		/* chromaticity for standard colour space. */
		0x00, 0x00, 0x00,	/* no default timings */
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		    0x01, 0x01,
		0x01, 0x01, 0x01, 0x01,	/* no standard timings */
		0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x06, 0x00, 0x02, 0x02,
		    0x02, 0x02,
		/* descriptor block 1 goes below */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* descriptor block 2, monitor ranges */
		0x00, 0x00, 0x00, 0xFD, 0x00,
		0x00, 0xC8, 0x00, 0xC8, 0x64, 0x00, 0x0A, 0x20, 0x20, 0x20,
		    0x20, 0x20,
		/* 0-200Hz vertical, 0-200KHz horizontal, 1000MHz pixel clock */
		0x20,
		/* descriptor block 3, monitor name */
		0x00, 0x00, 0x00, 0xFC, 0x00,
		'V', 'B', 'O', 'X', ' ', 'm', 'o', 'n', 'i', 't', 'o', 'r',
		'\n',
		/* descriptor block 4: dummy data */
		0x00, 0x00, 0x00, 0x10, 0x00,
		0x0A, 0x20, 0x20, 0x20, 0x20, 0x20,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		0x20,
		0x00,		/* number of extensions */
		0x00		/* checksum goes here */
	};
	int clock = (width + 6) * (height + 6) * 60 / 10000;
	unsigned int i, sum = 0;

	edid[12] = width & 0xff;
	edid[13] = width >> 8;
	edid[14] = height & 0xff;
	edid[15] = height >> 8;
	edid[54] = clock & 0xff;
	edid[55] = clock >> 8;
	edid[56] = width & 0xff;
	edid[58] = (width >> 4) & 0xf0;
	edid[59] = height & 0xff;
	edid[61] = (height >> 4) & 0xf0;
	for (i = 0; i < EDID_SIZE - 1; ++i)
		sum += edid[i];
	edid[EDID_SIZE - 1] = (0x100 - (sum & 0xFF)) & 0xFF;
	drm_connector_update_edid_property(connector, (struct edid *)edid);
}

static int vbox_get_modes(struct drm_connector *connector)
{
	struct vbox_connector *vbox_connector = NULL;
	struct drm_display_mode *mode = NULL;
	struct vbox_private *vbox = NULL;
	unsigned int num_modes = 0;
	int preferred_width, preferred_height;

	vbox_connector = to_vbox_connector(connector);
	vbox = connector->dev->dev_private;

	hgsmi_report_flags_location(vbox->guest_pool, GUEST_HEAP_OFFSET(vbox) +
				    HOST_FLAGS_OFFSET);
	if (vbox_connector->vbox_crtc->crtc_id == 0)
		vbox_report_caps(vbox);

	num_modes = drm_add_modes_noedid(connector, 2560, 1600);
	preferred_width = vbox_connector->mode_hint.width ?
			  vbox_connector->mode_hint.width : 1024;
	preferred_height = vbox_connector->mode_hint.height ?
			   vbox_connector->mode_hint.height : 768;
	mode = drm_cvt_mode(connector->dev, preferred_width, preferred_height,
			    60, false, false, false);
	if (mode) {
		mode->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
		++num_modes;
	}
	vbox_set_edid(connector, preferred_width, preferred_height);

	if (vbox_connector->vbox_crtc->x_hint != -1)
		drm_object_property_set_value(&connector->base,
			vbox->ddev.mode_config.suggested_x_property,
			vbox_connector->vbox_crtc->x_hint);
	else
		drm_object_property_set_value(&connector->base,
			vbox->ddev.mode_config.suggested_x_property, 0);

	if (vbox_connector->vbox_crtc->y_hint != -1)
		drm_object_property_set_value(&connector->base,
			vbox->ddev.mode_config.suggested_y_property,
			vbox_connector->vbox_crtc->y_hint);
	else
		drm_object_property_set_value(&connector->base,
			vbox->ddev.mode_config.suggested_y_property, 0);

	return num_modes;
}

static void vbox_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static enum drm_connector_status
vbox_connector_detect(struct drm_connector *connector, bool force)
{
	struct vbox_connector *vbox_connector;

	vbox_connector = to_vbox_connector(connector);

	return vbox_connector->mode_hint.disconnected ?
	    connector_status_disconnected : connector_status_connected;
}

static int vbox_fill_modes(struct drm_connector *connector, u32 max_x,
			   u32 max_y)
{
	struct vbox_connector *vbox_connector;
	struct drm_device *dev;
	struct drm_display_mode *mode, *iterator;

	vbox_connector = to_vbox_connector(connector);
	dev = vbox_connector->base.dev;
	list_for_each_entry_safe(mode, iterator, &connector->modes, head) {
		list_del(&mode->head);
		drm_mode_destroy(dev, mode);
	}

	return drm_helper_probe_single_connector_modes(connector, max_x, max_y);
}

static const struct drm_connector_helper_funcs vbox_connector_helper_funcs = {
	.get_modes = vbox_get_modes,
};

static const struct drm_connector_funcs vbox_connector_funcs = {
	.detect = vbox_connector_detect,
	.fill_modes = vbox_fill_modes,
	.destroy = vbox_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int vbox_connector_init(struct drm_device *dev,
			       struct vbox_crtc *vbox_crtc,
			       struct drm_encoder *encoder)
{
	struct vbox_connector *vbox_connector;
	struct drm_connector *connector;

	vbox_connector = kzalloc(sizeof(*vbox_connector), GFP_KERNEL);
	if (!vbox_connector)
		return -ENOMEM;

	connector = &vbox_connector->base;
	vbox_connector->vbox_crtc = vbox_crtc;

	drm_connector_init(dev, connector, &vbox_connector_funcs,
			   DRM_MODE_CONNECTOR_VGA);
	drm_connector_helper_add(connector, &vbox_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_mode_create_suggested_offset_properties(dev);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_x_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_y_property, 0);

	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

static const struct drm_mode_config_funcs vbox_mode_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int vbox_mode_init(struct vbox_private *vbox)
{
	struct drm_device *dev = &vbox->ddev;
	struct drm_encoder *encoder;
	struct vbox_crtc *vbox_crtc;
	unsigned int i;
	int ret;

	drm_mode_config_init(dev);

	dev->mode_config.funcs = (void *)&vbox_mode_funcs;
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.max_width = VBE_DISPI_MAX_XRES;
	dev->mode_config.max_height = VBE_DISPI_MAX_YRES;

	for (i = 0; i < vbox->num_crtcs; ++i) {
		vbox_crtc = vbox_crtc_init(dev, i);
		if (IS_ERR(vbox_crtc)) {
			ret = PTR_ERR(vbox_crtc);
			goto err_drm_mode_cleanup;
		}
		encoder = vbox_encoder_init(dev, i);
		if (!encoder) {
			ret = -ENOMEM;
			goto err_drm_mode_cleanup;
		}
		ret = vbox_connector_init(dev, vbox_crtc, encoder);
		if (ret)
			goto err_drm_mode_cleanup;
	}

	drm_mode_config_reset(dev);
	return 0;

err_drm_mode_cleanup:
	drm_mode_config_cleanup(dev);
	return ret;
}

void vbox_mode_fini(struct vbox_private *vbox)
{
	drm_mode_config_cleanup(&vbox->ddev);
}
