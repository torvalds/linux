/**************************************************************************
 *
 * Copyright © 2011 VMware, Inc., Palo Alto, CA., USA
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

struct vmw_screen_object_display {
	unsigned num_implicit;

	struct vmw_framebuffer *implicit_fb;
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
	vmw_display_unit_cleanup(&sou->base);
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
	struct ttm_buffer_object *bo;

	if (unlikely(sou->buffer == NULL))
		return;

	bo = &sou->buffer->base;
	ttm_bo_unref(&bo);
	sou->buffer = NULL;
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
		DRM_ERROR("to many connectors\n");
		return -EINVAL;
	}

	if (set->num_connectors == 1 &&
	    set->connectors[0] != &sou->base.connector) {
		DRM_ERROR("connector doesn't match %p %p\n",
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

	vmw_fb_off(dev_priv);

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

static struct drm_crtc_funcs vmw_screen_object_crtc_funcs = {
	.save = vmw_du_crtc_save,
	.restore = vmw_du_crtc_restore,
	.cursor_set = vmw_du_crtc_cursor_set,
	.cursor_move = vmw_du_crtc_cursor_move,
	.gamma_set = vmw_du_crtc_gamma_set,
	.destroy = vmw_sou_crtc_destroy,
	.set_config = vmw_sou_crtc_set_config,
	.page_flip = vmw_du_page_flip,
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

static struct drm_connector_funcs vmw_legacy_connector_funcs = {
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

	drm_connector_init(dev, connector, &vmw_legacy_connector_funcs,
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

int vmw_kms_init_screen_object_display(struct vmw_private *dev_priv)
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

	DRM_INFO("Screen objects system initialized\n");

	return 0;

err_vblank_cleanup:
	drm_vblank_cleanup(dev);
err_free:
	kfree(dev_priv->sou_priv);
	dev_priv->sou_priv = NULL;
err_no_mem:
	return ret;
}

int vmw_kms_close_screen_object_display(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	if (!dev_priv->sou_priv)
		return -ENOSYS;

	drm_vblank_cleanup(dev);

	kfree(dev_priv->sou_priv);

	return 0;
}

/**
 * Returns if this unit can be page flipped.
 * Must be called with the mode_config mutex held.
 */
bool vmw_kms_screen_object_flippable(struct vmw_private *dev_priv,
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
void vmw_kms_screen_object_update_implicit_fb(struct vmw_private *dev_priv,
					      struct drm_crtc *crtc)
{
	struct vmw_screen_object_unit *sou = vmw_crtc_to_sou(crtc);

	BUG_ON(!sou->base.is_implicit);

	dev_priv->sou_priv->implicit_fb =
		vmw_framebuffer_to_vfb(sou->base.crtc.primary->fb);
}
