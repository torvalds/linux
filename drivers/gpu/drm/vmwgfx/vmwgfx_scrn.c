/**************************************************************************
 *
 * Copyright © 2011-2015 VMware, Inc., Palo Alto, CA., USA
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

#include "vmwgfx_kms.h"
#include <drm/drm_plane_helper.h>


#define vmw_crtc_to_sou(x) \
	container_of(x, struct vmw_screen_object_unit, base.crtc)
#define vmw_encoder_to_sou(x) \
	container_of(x, struct vmw_screen_object_unit, base.encoder)
#define vmw_connector_to_sou(x) \
	container_of(x, struct vmw_screen_object_unit, base.connector)

/**
 * struct vmw_kms_sou_surface_dirty - Closure structure for
 * blit surface to screen command.
 * @base: The base type we derive from. Used by vmw_kms_helper_dirty().
 * @left: Left side of bounding box.
 * @right: Right side of bounding box.
 * @top: Top side of bounding box.
 * @bottom: Bottom side of bounding box.
 * @dst_x: Difference between source clip rects and framebuffer coordinates.
 * @dst_y: Difference between source clip rects and framebuffer coordinates.
 * @sid: Surface id of surface to copy from.
 */
struct vmw_kms_sou_surface_dirty {
	struct vmw_kms_dirty base;
	s32 left, right, top, bottom;
	s32 dst_x, dst_y;
	u32 sid;
};

/*
 * SVGA commands that are used by this code. Please see the device headers
 * for explanation.
 */
struct vmw_kms_sou_readback_blit {
	uint32 header;
	SVGAFifoCmdBlitScreenToGMRFB body;
};

struct vmw_kms_sou_dmabuf_blit {
	uint32 header;
	SVGAFifoCmdBlitGMRFBToScreen body;
};

struct vmw_kms_sou_dirty_cmd {
	SVGA3dCmdHeader header;
	SVGA3dCmdBlitSurfaceToScreen body;
};


/*
 * Other structs.
 */

struct vmw_screen_object_display {
	unsigned num_implicit;

	struct vmw_framebuffer *implicit_fb;
	SVGAFifoCmdDefineGMRFB cur;
	struct vmw_dma_buffer *pinned_gmrfb;
};

/**
 * Display unit using screen objects.
 */
struct vmw_screen_object_unit {
	struct vmw_display_unit base;

	unsigned long buffer_size; /**< Size of allocated buffer */
	struct vmw_dma_buffer *buffer; /**< Backing store buffer */

	bool defined;
	bool active_implicit;
};

static void vmw_sou_destroy(struct vmw_screen_object_unit *sou)
{
	vmw_du_cleanup(&sou->base);
	kfree(sou);
}


/*
 * Screen Object Display Unit CRTC functions
 */

static void vmw_sou_crtc_destroy(struct drm_crtc *crtc)
{
	vmw_sou_destroy(vmw_crtc_to_sou(crtc));
}

static void vmw_sou_del_active(struct vmw_private *vmw_priv,
			       struct vmw_screen_object_unit *sou)
{
	struct vmw_screen_object_display *ld = vmw_priv->sou_priv;

	if (sou->active_implicit) {
		if (--(ld->num_implicit) == 0)
			ld->implicit_fb = NULL;
		sou->active_implicit = false;
	}
}

static void vmw_sou_add_active(struct vmw_private *vmw_priv,
			       struct vmw_screen_object_unit *sou,
			       struct vmw_framebuffer *vfb)
{
	struct vmw_screen_object_display *ld = vmw_priv->sou_priv;

	BUG_ON(!ld->num_implicit && ld->implicit_fb);

	if (!sou->active_implicit && sou->base.is_implicit) {
		ld->implicit_fb = vfb;
		sou->active_implicit = true;
		ld->num_implicit++;
	}
}

/**
 * Send the fifo command to create a screen.
 */
static int vmw_sou_fifo_create(struct vmw_private *dev_priv,
			       struct vmw_screen_object_unit *sou,
			       uint32_t x, uint32_t y,
			       struct drm_display_mode *mode)
{
	size_t fifo_size;

	struct {
		struct {
			uint32_t cmdType;
		} header;
		SVGAScreenObject obj;
	} *cmd;

	BUG_ON(!sou->buffer);

	fifo_size = sizeof(*cmd);
	cmd = vmw_fifo_reserve(dev_priv, fifo_size);
	/* The hardware has hung, nothing we can do about it here. */
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Fifo reserve failed.\n");
		return -ENOMEM;
	}

	memset(cmd, 0, fifo_size);
	cmd->header.cmdType = SVGA_CMD_DEFINE_SCREEN;
	cmd->obj.structSize = sizeof(SVGAScreenObject);
	cmd->obj.id = sou->base.unit;
	cmd->obj.flags = SVGA_SCREEN_HAS_ROOT |
		(sou->base.unit == 0 ? SVGA_SCREEN_IS_PRIMARY : 0);
	cmd->obj.size.width = mode->hdisplay;
	cmd->obj.size.height = mode->vdisplay;
	if (sou->base.is_implicit) {
		cmd->obj.root.x = x;
		cmd->obj.root.y = y;
	} else {
		cmd->obj.root.x = sou->base.gui_x;
		cmd->obj.root.y = sou->base.gui_y;
	}

	/* Ok to assume that buffer is pinned in vram */
	vmw_bo_get_guest_ptr(&sou->buffer->base, &cmd->obj.backingStore.ptr);
	cmd->obj.backingStore.pitch = mode->hdisplay * 4;

	vmw_fifo_commit(dev_priv, fifo_size);

	sou->defined = true;

	return 0;
}

/**
 * Send the fifo command to destroy a screen.
 */
static int vmw_sou_fifo_destroy(struct vmw_private *dev_priv,
				struct vmw_screen_object_unit *sou)
{
	size_t fifo_size;
	int ret;

	struct {
		struct {
			uint32_t cmdType;
		} header;
		SVGAFifoCmdDestroyScreen body;
	} *cmd;

	/* no need to do anything */
	if (unlikely(!sou->defined))
		return 0;

	fifo_size = sizeof(*cmd);
	cmd = vmw_fifo_reserve(dev_priv, fifo_size);
	/* the hardware has hung, nothing we can do about it here */
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Fifo reserve failed.\n");
		return -ENOMEM;
	}

	memset(cmd, 0, fifo_size);
	cmd->header.cmdType = SVGA_CMD_DESTROY_SCREEN;
	cmd->body.screenId = sou->base.unit;

	vmw_fifo_commit(dev_priv, fifo_size);

	/* Force sync */
	ret = vmw_fallback_wait(dev_priv, false, true, 0, false, 3*HZ);
	if (unlikely(ret != 0))
		DRM_ERROR("Failed to sync with HW");
	else
		sou->defined = false;

	return ret;
}

/**
 * Free the backing store.
 */
static void vmw_sou_backing_free(struct vmw_private *dev_priv,
				 struct vmw_screen_object_unit *sou)
{
	vmw_dmabuf_unreference(&sou->buffer);
	sou->buffer_size = 0;
}

/**
 * Allocate the backing store for the buffer.
 */
static int vmw_sou_backing_alloc(struct vmw_private *dev_priv,
				 struct vmw_screen_object_unit *sou,
				 unsigned long size)
{
	int ret;

	if (sou->buffer_size == size)
		return 0;

	if (sou->buffer)
		vmw_sou_backing_free(dev_priv, sou);

	sou->buffer = kzalloc(sizeof(*sou->buffer), GFP_KERNEL);
	if (unlikely(sou->buffer == NULL))
		return -ENOMEM;

	/* After we have alloced the backing store might not be able to
	 * resume the overlays, this is preferred to failing to alloc.
	 */
	vmw_overlay_pause_all(dev_priv);
	ret = vmw_dmabuf_init(dev_priv, sou->buffer, size,
			      &vmw_vram_ne_placement,
			      false, &vmw_dmabuf_bo_free);
	vmw_overlay_resume_all(dev_priv);

	if (unlikely(ret != 0))
		sou->buffer = NULL; /* vmw_dmabuf_init frees on error */
	else
		sou->buffer_size = size;

	return ret;
}

static int vmw_sou_crtc_set_config(struct drm_mode_set *set)
{
	struct vmw_private *dev_priv;
	struct vmw_screen_object_unit *sou;
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	struct drm_encoder *encoder;
	struct vmw_framebuffer *vfb;
	struct drm_framebuffer *fb;
	struct drm_crtc *crtc;
	int ret = 0;

	if (!set)
		return -EINVAL;

	if (!set->crtc)
		return -EINVAL;

	/* get the sou */
	crtc = set->crtc;
	sou = vmw_crtc_to_sou(crtc);
	vfb = set->fb ? vmw_framebuffer_to_vfb(set->fb) : NULL;
	dev_priv = vmw_priv(crtc->dev);

	if (set->num_connectors > 1) {
		DRM_ERROR("Too many connectors\n");
		return -EINVAL;
	}

	if (set->num_connectors == 1 &&
	    set->connectors[0] != &sou->base.connector) {
		DRM_ERROR("Connector doesn't match %p %p\n",
			set->connectors[0], &sou->base.connector);
		return -EINVAL;
	}

	/* sou only supports one fb active at the time */
	if (sou->base.is_implicit &&
	    dev_priv->sou_priv->implicit_fb && vfb &&
	    !(dev_priv->sou_priv->num_implicit == 1 &&
	      sou->active_implicit) &&
	    dev_priv->sou_priv->implicit_fb != vfb) {
		DRM_ERROR("Multiple framebuffers not supported\n");
		return -EINVAL;
	}

	/* since they always map one to one these are safe */
	connector = &sou->base.connector;
	encoder = &sou->base.encoder;

	/* should we turn the crtc off */
	if (set->num_connectors == 0 || !set->mode || !set->fb) {
		ret = vmw_sou_fifo_destroy(dev_priv, sou);
		/* the hardware has hung don't do anything more */
		if (unlikely(ret != 0))
			return ret;

		connector->encoder = NULL;
		encoder->crtc = NULL;
		crtc->primary->fb = NULL;
		crtc->x = 0;
		crtc->y = 0;
		crtc->enabled = false;

		vmw_sou_del_active(dev_priv, sou);

		vmw_sou_backing_free(dev_priv, sou);

		return 0;
	}


	/* we now know we want to set a mode */
	mode = set->mode;
	fb = set->fb;

	if (set->x + mode->hdisplay > fb->width ||
	    set->y + mode->vdisplay > fb->height) {
		DRM_ERROR("set outside of framebuffer\n");
		return -EINVAL;
	}

	vmw_svga_enable(dev_priv);

	if (mode->hdisplay != crtc->mode.hdisplay ||
	    mode->vdisplay != crtc->mode.vdisplay) {
		/* no need to check if depth is different, because backing
		 * store depth is forced to 4 by the device.
		 */

		ret = vmw_sou_fifo_destroy(dev_priv, sou);
		/* the hardware has hung don't do anything more */
		if (unlikely(ret != 0))
			return ret;

		vmw_sou_backing_free(dev_priv, sou);
	}

	if (!sou->buffer) {
		/* forced to depth 4 by the device */
		size_t size = mode->hdisplay * mode->vdisplay * 4;
		ret = vmw_sou_backing_alloc(dev_priv, sou, size);
		if (unlikely(ret != 0))
			return ret;
	}

	ret = vmw_sou_fifo_create(dev_priv, sou, set->x, set->y, mode);
	if (unlikely(ret != 0)) {
		/*
		 * We are in a bit of a situation here, the hardware has
		 * hung and we may or may not have a buffer hanging of
		 * the screen object, best thing to do is not do anything
		 * if we where defined, if not just turn the crtc of.
		 * Not what userspace wants but it needs to htfu.
		 */
		if (sou->defined)
			return ret;

		connector->encoder = NULL;
		encoder->crtc = NULL;
		crtc->primary->fb = NULL;
		crtc->x = 0;
		crtc->y = 0;
		crtc->enabled = false;

		return ret;
	}

	vmw_sou_add_active(dev_priv, sou, vfb);

	connector->encoder = encoder;
	encoder->crtc = crtc;
	crtc->mode = *mode;
	crtc->primary->fb = fb;
	crtc->x = set->x;
	crtc->y = set->y;
	crtc->enabled = true;

	return 0;
}

/**
 * Returns if this unit can be page flipped.
 * Must be called with the mode_config mutex held.
 */
static bool vmw_sou_screen_object_flippable(struct vmw_private *dev_priv,
					    struct drm_crtc *crtc)
{
	struct vmw_screen_object_unit *sou = vmw_crtc_to_sou(crtc);

	if (!sou->base.is_implicit)
		return true;

	if (dev_priv->sou_priv->num_implicit != 1)
		return false;

	return true;
}

/**
 * Update the implicit fb to the current fb of this crtc.
 * Must be called with the mode_config mutex held.
 */
static void vmw_sou_update_implicit_fb(struct vmw_private *dev_priv,
				       struct drm_crtc *crtc)
{
	struct vmw_screen_object_unit *sou = vmw_crtc_to_sou(crtc);

	BUG_ON(!sou->base.is_implicit);

	dev_priv->sou_priv->implicit_fb =
		vmw_framebuffer_to_vfb(sou->base.crtc.primary->fb);
}

static int vmw_sou_crtc_page_flip(struct drm_crtc *crtc,
				  struct drm_framebuffer *fb,
				  struct drm_pending_vblank_event *event,
				  uint32_t flags)
{
	struct vmw_private *dev_priv = vmw_priv(crtc->dev);
	struct drm_framebuffer *old_fb = crtc->primary->fb;
	struct vmw_framebuffer *vfb = vmw_framebuffer_to_vfb(fb);
	struct vmw_fence_obj *fence = NULL;
	struct drm_clip_rect clips;
	int ret;

	/* require ScreenObject support for page flipping */
	if (!dev_priv->sou_priv)
		return -ENOSYS;

	if (!vmw_sou_screen_object_flippable(dev_priv, crtc))
		return -EINVAL;

	crtc->primary->fb = fb;

	/* do a full screen dirty update */
	clips.x1 = clips.y1 = 0;
	clips.x2 = fb->width;
	clips.y2 = fb->height;

	if (vfb->dmabuf)
		ret = vmw_kms_sou_do_dmabuf_dirty(dev_priv, vfb,
						  &clips, 1, 1,
						  true, &fence);
	else
		ret = vmw_kms_sou_do_surface_dirty(dev_priv, vfb,
						   &clips, NULL, NULL,
						   0, 0, 1, 1, &fence);


	if (ret != 0)
		goto out_no_fence;
	if (!fence) {
		ret = -EINVAL;
		goto out_no_fence;
	}

	if (event) {
		struct drm_file *file_priv = event->base.file_priv;

		ret = vmw_event_fence_action_queue(file_priv, fence,
						   &event->base,
						   &event->event.tv_sec,
						   &event->event.tv_usec,
						   true);
	}

	/*
	 * No need to hold on to this now. The only cleanup
	 * we need to do if we fail is unref the fence.
	 */
	vmw_fence_obj_unreference(&fence);

	if (vmw_crtc_to_du(crtc)->is_implicit)
		vmw_sou_update_implicit_fb(dev_priv, crtc);

	return ret;

out_no_fence:
	crtc->primary->fb = old_fb;
	return ret;
}

static struct drm_crtc_funcs vmw_screen_object_crtc_funcs = {
	.save = vmw_du_crtc_save,
	.restore = vmw_du_crtc_restore,
	.cursor_set2 = vmw_du_crtc_cursor_set2,
	.cursor_move = vmw_du_crtc_cursor_move,
	.gamma_set = vmw_du_crtc_gamma_set,
	.destroy = vmw_sou_crtc_destroy,
	.set_config = vmw_sou_crtc_set_config,
	.page_flip = vmw_sou_crtc_page_flip,
};

/*
 * Screen Object Display Unit encoder functions
 */

static void vmw_sou_encoder_destroy(struct drm_encoder *encoder)
{
	vmw_sou_destroy(vmw_encoder_to_sou(encoder));
}

static struct drm_encoder_funcs vmw_screen_object_encoder_funcs = {
	.destroy = vmw_sou_encoder_destroy,
};

/*
 * Screen Object Display Unit connector functions
 */

static void vmw_sou_connector_destroy(struct drm_connector *connector)
{
	vmw_sou_destroy(vmw_connector_to_sou(connector));
}

static struct drm_connector_funcs vmw_sou_connector_funcs = {
	.dpms = vmw_du_connector_dpms,
	.save = vmw_du_connector_save,
	.restore = vmw_du_connector_restore,
	.detect = vmw_du_connector_detect,
	.fill_modes = vmw_du_connector_fill_modes,
	.set_property = vmw_du_connector_set_property,
	.destroy = vmw_sou_connector_destroy,
};

static int vmw_sou_init(struct vmw_private *dev_priv, unsigned unit)
{
	struct vmw_screen_object_unit *sou;
	struct drm_device *dev = dev_priv->dev;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;

	sou = kzalloc(sizeof(*sou), GFP_KERNEL);
	if (!sou)
		return -ENOMEM;

	sou->base.unit = unit;
	crtc = &sou->base.crtc;
	encoder = &sou->base.encoder;
	connector = &sou->base.connector;

	sou->active_implicit = false;

	sou->base.pref_active = (unit == 0);
	sou->base.pref_width = dev_priv->initial_width;
	sou->base.pref_height = dev_priv->initial_height;
	sou->base.pref_mode = NULL;
	sou->base.is_implicit = true;

	drm_connector_init(dev, connector, &vmw_sou_connector_funcs,
			   DRM_MODE_CONNECTOR_VIRTUAL);
	connector->status = vmw_du_connector_detect(connector, true);

	drm_encoder_init(dev, encoder, &vmw_screen_object_encoder_funcs,
			 DRM_MODE_ENCODER_VIRTUAL);
	drm_mode_connector_attach_encoder(connector, encoder);
	encoder->possible_crtcs = (1 << unit);
	encoder->possible_clones = 0;

	(void) drm_connector_register(connector);

	drm_crtc_init(dev, crtc, &vmw_screen_object_crtc_funcs);

	drm_mode_crtc_set_gamma_size(crtc, 256);

	drm_object_attach_property(&connector->base,
				      dev->mode_config.dirty_info_property,
				      1);

	return 0;
}

int vmw_kms_sou_init_display(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	int i, ret;

	if (dev_priv->sou_priv) {
		DRM_INFO("sou system already on\n");
		return -EINVAL;
	}

	if (!(dev_priv->capabilities & SVGA_CAP_SCREEN_OBJECT_2)) {
		DRM_INFO("Not using screen objects,"
			 " missing cap SCREEN_OBJECT_2\n");
		return -ENOSYS;
	}

	ret = -ENOMEM;
	dev_priv->sou_priv = kmalloc(sizeof(*dev_priv->sou_priv), GFP_KERNEL);
	if (unlikely(!dev_priv->sou_priv))
		goto err_no_mem;

	dev_priv->sou_priv->num_implicit = 0;
	dev_priv->sou_priv->implicit_fb = NULL;

	ret = drm_vblank_init(dev, VMWGFX_NUM_DISPLAY_UNITS);
	if (unlikely(ret != 0))
		goto err_free;

	ret = drm_mode_create_dirty_info_property(dev);
	if (unlikely(ret != 0))
		goto err_vblank_cleanup;

	for (i = 0; i < VMWGFX_NUM_DISPLAY_UNITS; ++i)
		vmw_sou_init(dev_priv, i);

	dev_priv->active_display_unit = vmw_du_screen_object;

	DRM_INFO("Screen Objects Display Unit initialized\n");

	return 0;

err_vblank_cleanup:
	drm_vblank_cleanup(dev);
err_free:
	kfree(dev_priv->sou_priv);
	dev_priv->sou_priv = NULL;
err_no_mem:
	return ret;
}

int vmw_kms_sou_close_display(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	if (!dev_priv->sou_priv)
		return -ENOSYS;

	drm_vblank_cleanup(dev);

	kfree(dev_priv->sou_priv);

	return 0;
}

static int do_dmabuf_define_gmrfb(struct vmw_private *dev_priv,
				  struct vmw_framebuffer *framebuffer)
{
	struct vmw_dma_buffer *buf =
		container_of(framebuffer, struct vmw_framebuffer_dmabuf,
			     base)->buffer;
	int depth = framebuffer->base.depth;
	struct {
		uint32_t header;
		SVGAFifoCmdDefineGMRFB body;
	} *cmd;

	/* Emulate RGBA support, contrary to svga_reg.h this is not
	 * supported by hosts. This is only a problem if we are reading
	 * this value later and expecting what we uploaded back.
	 */
	if (depth == 32)
		depth = 24;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (!cmd) {
		DRM_ERROR("Out of fifo space for dirty framebuffer command.\n");
		return -ENOMEM;
	}

	cmd->header = SVGA_CMD_DEFINE_GMRFB;
	cmd->body.format.bitsPerPixel = framebuffer->base.bits_per_pixel;
	cmd->body.format.colorDepth = depth;
	cmd->body.format.reserved = 0;
	cmd->body.bytesPerLine = framebuffer->base.pitches[0];
	/* Buffer is reserved in vram or GMR */
	vmw_bo_get_guest_ptr(&buf->base, &cmd->body.ptr);
	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_sou_surface_fifo_commit - Callback to fill in and submit a
 * blit surface to screen command.
 *
 * @dirty: The closure structure.
 *
 * Fills in the missing fields in the command, and translates the cliprects
 * to match the destination bounding box encoded.
 */
static void vmw_sou_surface_fifo_commit(struct vmw_kms_dirty *dirty)
{
	struct vmw_kms_sou_surface_dirty *sdirty =
		container_of(dirty, typeof(*sdirty), base);
	struct vmw_kms_sou_dirty_cmd *cmd = dirty->cmd;
	s32 trans_x = dirty->unit->crtc.x - sdirty->dst_x;
	s32 trans_y = dirty->unit->crtc.y - sdirty->dst_y;
	size_t region_size = dirty->num_hits * sizeof(SVGASignedRect);
	SVGASignedRect *blit = (SVGASignedRect *) &cmd[1];
	int i;

	cmd->header.id = SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN;
	cmd->header.size = sizeof(cmd->body) + region_size;

	/*
	 * Use the destination bounding box to specify destination - and
	 * source bounding regions.
	 */
	cmd->body.destRect.left = sdirty->left;
	cmd->body.destRect.right = sdirty->right;
	cmd->body.destRect.top = sdirty->top;
	cmd->body.destRect.bottom = sdirty->bottom;

	cmd->body.srcRect.left = sdirty->left + trans_x;
	cmd->body.srcRect.right = sdirty->right + trans_x;
	cmd->body.srcRect.top = sdirty->top + trans_y;
	cmd->body.srcRect.bottom = sdirty->bottom + trans_y;

	cmd->body.srcImage.sid = sdirty->sid;
	cmd->body.destScreenId = dirty->unit->unit;

	/* Blits are relative to the destination rect. Translate. */
	for (i = 0; i < dirty->num_hits; ++i, ++blit) {
		blit->left -= sdirty->left;
		blit->right -= sdirty->left;
		blit->top -= sdirty->top;
		blit->bottom -= sdirty->top;
	}

	vmw_fifo_commit(dirty->dev_priv, region_size + sizeof(*cmd));

	sdirty->left = sdirty->top = S32_MAX;
	sdirty->right = sdirty->bottom = S32_MIN;
}

/**
 * vmw_sou_surface_clip - Callback to encode a blit surface to screen cliprect.
 *
 * @dirty: The closure structure
 *
 * Encodes a SVGASignedRect cliprect and updates the bounding box of the
 * BLIT_SURFACE_TO_SCREEN command.
 */
static void vmw_sou_surface_clip(struct vmw_kms_dirty *dirty)
{
	struct vmw_kms_sou_surface_dirty *sdirty =
		container_of(dirty, typeof(*sdirty), base);
	struct vmw_kms_sou_dirty_cmd *cmd = dirty->cmd;
	SVGASignedRect *blit = (SVGASignedRect *) &cmd[1];

	/* Destination rect. */
	blit += dirty->num_hits;
	blit->left = dirty->unit_x1;
	blit->top = dirty->unit_y1;
	blit->right = dirty->unit_x2;
	blit->bottom = dirty->unit_y2;

	/* Destination bounding box */
	sdirty->left = min_t(s32, sdirty->left, dirty->unit_x1);
	sdirty->top = min_t(s32, sdirty->top, dirty->unit_y1);
	sdirty->right = max_t(s32, sdirty->right, dirty->unit_x2);
	sdirty->bottom = max_t(s32, sdirty->bottom, dirty->unit_y2);

	dirty->num_hits++;
}

/**
 * vmw_kms_sou_do_surface_dirty - Dirty part of a surface backed framebuffer
 *
 * @dev_priv: Pointer to the device private structure.
 * @framebuffer: Pointer to the surface-buffer backed framebuffer.
 * @clips: Array of clip rects. Either @clips or @vclips must be NULL.
 * @vclips: Alternate array of clip rects. Either @clips or @vclips must
 * be NULL.
 * @srf: Pointer to surface to blit from. If NULL, the surface attached
 * to @framebuffer will be used.
 * @dest_x: X coordinate offset to align @srf with framebuffer coordinates.
 * @dest_y: Y coordinate offset to align @srf with framebuffer coordinates.
 * @num_clips: Number of clip rects in @clips.
 * @inc: Increment to use when looping over @clips.
 * @out_fence: If non-NULL, will return a ref-counted pointer to a
 * struct vmw_fence_obj. The returned fence pointer may be NULL in which
 * case the device has already synchronized.
 *
 * Returns 0 on success, negative error code on failure. -ERESTARTSYS if
 * interrupted.
 */
int vmw_kms_sou_do_surface_dirty(struct vmw_private *dev_priv,
				 struct vmw_framebuffer *framebuffer,
				 struct drm_clip_rect *clips,
				 struct drm_vmw_rect *vclips,
				 struct vmw_resource *srf,
				 s32 dest_x,
				 s32 dest_y,
				 unsigned num_clips, int inc,
				 struct vmw_fence_obj **out_fence)
{
	struct vmw_framebuffer_surface *vfbs =
		container_of(framebuffer, typeof(*vfbs), base);
	struct vmw_kms_sou_surface_dirty sdirty;
	int ret;

	if (!srf)
		srf = &vfbs->surface->res;

	ret = vmw_kms_helper_resource_prepare(srf, true);
	if (ret)
		return ret;

	sdirty.base.fifo_commit = vmw_sou_surface_fifo_commit;
	sdirty.base.clip = vmw_sou_surface_clip;
	sdirty.base.dev_priv = dev_priv;
	sdirty.base.fifo_reserve_size = sizeof(struct vmw_kms_sou_dirty_cmd) +
	  sizeof(SVGASignedRect) * num_clips;

	sdirty.sid = srf->id;
	sdirty.left = sdirty.top = S32_MAX;
	sdirty.right = sdirty.bottom = S32_MIN;
	sdirty.dst_x = dest_x;
	sdirty.dst_y = dest_y;

	ret = vmw_kms_helper_dirty(dev_priv, framebuffer, clips, vclips,
				   dest_x, dest_y, num_clips, inc,
				   &sdirty.base);
	vmw_kms_helper_resource_finish(srf, out_fence);

	return ret;
}

/**
 * vmw_sou_dmabuf_fifo_commit - Callback to submit a set of readback clips.
 *
 * @dirty: The closure structure.
 *
 * Commits a previously built command buffer of readback clips.
 */
static void vmw_sou_dmabuf_fifo_commit(struct vmw_kms_dirty *dirty)
{
	vmw_fifo_commit(dirty->dev_priv,
			sizeof(struct vmw_kms_sou_dmabuf_blit) *
			dirty->num_hits);
}

/**
 * vmw_sou_dmabuf_clip - Callback to encode a readback cliprect.
 *
 * @dirty: The closure structure
 *
 * Encodes a BLIT_GMRFB_TO_SCREEN cliprect.
 */
static void vmw_sou_dmabuf_clip(struct vmw_kms_dirty *dirty)
{
	struct vmw_kms_sou_dmabuf_blit *blit = dirty->cmd;

	blit += dirty->num_hits;
	blit->header = SVGA_CMD_BLIT_GMRFB_TO_SCREEN;
	blit->body.destScreenId = dirty->unit->unit;
	blit->body.srcOrigin.x = dirty->fb_x;
	blit->body.srcOrigin.y = dirty->fb_y;
	blit->body.destRect.left = dirty->unit_x1;
	blit->body.destRect.top = dirty->unit_y1;
	blit->body.destRect.right = dirty->unit_x2;
	blit->body.destRect.bottom = dirty->unit_y2;
	dirty->num_hits++;
}

/**
 * vmw_kms_do_dmabuf_dirty - Dirty part of a dma-buffer backed framebuffer
 *
 * @dev_priv: Pointer to the device private structure.
 * @framebuffer: Pointer to the dma-buffer backed framebuffer.
 * @clips: Array of clip rects.
 * @num_clips: Number of clip rects in @clips.
 * @increment: Increment to use when looping over @clips.
 * @interruptible: Whether to perform waits interruptible if possible.
 * @out_fence: If non-NULL, will return a ref-counted pointer to a
 * struct vmw_fence_obj. The returned fence pointer may be NULL in which
 * case the device has already synchronized.
 *
 * Returns 0 on success, negative error code on failure. -ERESTARTSYS if
 * interrupted.
 */
int vmw_kms_sou_do_dmabuf_dirty(struct vmw_private *dev_priv,
				struct vmw_framebuffer *framebuffer,
				struct drm_clip_rect *clips,
				unsigned num_clips, int increment,
				bool interruptible,
				struct vmw_fence_obj **out_fence)
{
	struct vmw_dma_buffer *buf =
		container_of(framebuffer, struct vmw_framebuffer_dmabuf,
			     base)->buffer;
	struct vmw_kms_dirty dirty;
	int ret;

	ret = vmw_kms_helper_buffer_prepare(dev_priv, buf, interruptible,
					    false);
	if (ret)
		return ret;

	ret = do_dmabuf_define_gmrfb(dev_priv, framebuffer);
	if (unlikely(ret != 0))
		goto out_revert;

	dirty.fifo_commit = vmw_sou_dmabuf_fifo_commit;
	dirty.clip = vmw_sou_dmabuf_clip;
	dirty.fifo_reserve_size = sizeof(struct vmw_kms_sou_dmabuf_blit) *
		num_clips;
	ret = vmw_kms_helper_dirty(dev_priv, framebuffer, clips, NULL,
				   0, 0, num_clips, increment, &dirty);
	vmw_kms_helper_buffer_finish(dev_priv, NULL, buf, out_fence, NULL);

	return ret;

out_revert:
	vmw_kms_helper_buffer_revert(buf);

	return ret;
}


/**
 * vmw_sou_readback_fifo_commit - Callback to submit a set of readback clips.
 *
 * @dirty: The closure structure.
 *
 * Commits a previously built command buffer of readback clips.
 */
static void vmw_sou_readback_fifo_commit(struct vmw_kms_dirty *dirty)
{
	vmw_fifo_commit(dirty->dev_priv,
			sizeof(struct vmw_kms_sou_readback_blit) *
			dirty->num_hits);
}

/**
 * vmw_sou_readback_clip - Callback to encode a readback cliprect.
 *
 * @dirty: The closure structure
 *
 * Encodes a BLIT_SCREEN_TO_GMRFB cliprect.
 */
static void vmw_sou_readback_clip(struct vmw_kms_dirty *dirty)
{
	struct vmw_kms_sou_readback_blit *blit = dirty->cmd;

	blit += dirty->num_hits;
	blit->header = SVGA_CMD_BLIT_SCREEN_TO_GMRFB;
	blit->body.srcScreenId = dirty->unit->unit;
	blit->body.destOrigin.x = dirty->fb_x;
	blit->body.destOrigin.y = dirty->fb_y;
	blit->body.srcRect.left = dirty->unit_x1;
	blit->body.srcRect.top = dirty->unit_y1;
	blit->body.srcRect.right = dirty->unit_x2;
	blit->body.srcRect.bottom = dirty->unit_y2;
	dirty->num_hits++;
}

/**
 * vmw_kms_sou_readback - Perform a readback from the screen object system to
 * a dma-buffer backed framebuffer.
 *
 * @dev_priv: Pointer to the device private structure.
 * @file_priv: Pointer to a struct drm_file identifying the caller.
 * Must be set to NULL if @user_fence_rep is NULL.
 * @vfb: Pointer to the dma-buffer backed framebuffer.
 * @user_fence_rep: User-space provided structure for fence information.
 * Must be set to non-NULL if @file_priv is non-NULL.
 * @vclips: Array of clip rects.
 * @num_clips: Number of clip rects in @vclips.
 *
 * Returns 0 on success, negative error code on failure. -ERESTARTSYS if
 * interrupted.
 */
int vmw_kms_sou_readback(struct vmw_private *dev_priv,
			 struct drm_file *file_priv,
			 struct vmw_framebuffer *vfb,
			 struct drm_vmw_fence_rep __user *user_fence_rep,
			 struct drm_vmw_rect *vclips,
			 uint32_t num_clips)
{
	struct vmw_dma_buffer *buf =
		container_of(vfb, struct vmw_framebuffer_dmabuf, base)->buffer;
	struct vmw_kms_dirty dirty;
	int ret;

	ret = vmw_kms_helper_buffer_prepare(dev_priv, buf, true, false);
	if (ret)
		return ret;

	ret = do_dmabuf_define_gmrfb(dev_priv, vfb);
	if (unlikely(ret != 0))
		goto out_revert;

	dirty.fifo_commit = vmw_sou_readback_fifo_commit;
	dirty.clip = vmw_sou_readback_clip;
	dirty.fifo_reserve_size = sizeof(struct vmw_kms_sou_readback_blit) *
		num_clips;
	ret = vmw_kms_helper_dirty(dev_priv, vfb, NULL, vclips,
				   0, 0, num_clips, 1, &dirty);
	vmw_kms_helper_buffer_finish(dev_priv, file_priv, buf, NULL,
				     user_fence_rep);

	return ret;

out_revert:
	vmw_kms_helper_buffer_revert(buf);

	return ret;
}
