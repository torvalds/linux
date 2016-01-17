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

#include "vmwgfx_kms.h"
#include <drm/drm_plane_helper.h>


#define vmw_crtc_to_ldu(x) \
	container_of(x, struct vmw_legacy_display_unit, base.crtc)
#define vmw_encoder_to_ldu(x) \
	container_of(x, struct vmw_legacy_display_unit, base.encoder)
#define vmw_connector_to_ldu(x) \
	container_of(x, struct vmw_legacy_display_unit, base.connector)

struct vmw_legacy_display {
	struct list_head active;

	unsigned num_active;
	unsigned last_num_active;

	struct vmw_framebuffer *fb;
};

/**
 * Display unit using the legacy register interface.
 */
struct vmw_legacy_display_unit {
	struct vmw_display_unit base;

	struct list_head active;
};

static void vmw_ldu_destroy(struct vmw_legacy_display_unit *ldu)
{
	list_del_init(&ldu->active);
	vmw_du_cleanup(&ldu->base);
	kfree(ldu);
}


/*
 * Legacy Display Unit CRTC functions
 */

static void vmw_ldu_crtc_destroy(struct drm_crtc *crtc)
{
	vmw_ldu_destroy(vmw_crtc_to_ldu(crtc));
}

static int vmw_ldu_commit_list(struct vmw_private *dev_priv)
{
	struct vmw_legacy_display *lds = dev_priv->ldu_priv;
	struct vmw_legacy_display_unit *entry;
	struct vmw_display_unit *du = NULL;
	struct drm_framebuffer *fb = NULL;
	struct drm_crtc *crtc = NULL;
	int i = 0, ret;

	/* If there is no display topology the host just assumes
	 * that the guest will set the same layout as the host.
	 */
	if (!(dev_priv->capabilities & SVGA_CAP_DISPLAY_TOPOLOGY)) {
		int w = 0, h = 0;
		list_for_each_entry(entry, &lds->active, active) {
			crtc = &entry->base.crtc;
			w = max(w, crtc->x + crtc->mode.hdisplay);
			h = max(h, crtc->y + crtc->mode.vdisplay);
			i++;
		}

		if (crtc == NULL)
			return 0;
		fb = entry->base.crtc.primary->fb;

		return vmw_kms_write_svga(dev_priv, w, h, fb->pitches[0],
					  fb->bits_per_pixel, fb->depth);
	}

	if (!list_empty(&lds->active)) {
		entry = list_entry(lds->active.next, typeof(*entry), active);
		fb = entry->base.crtc.primary->fb;

		vmw_kms_write_svga(dev_priv, fb->width, fb->height, fb->pitches[0],
				   fb->bits_per_pixel, fb->depth);
	}

	/* Make sure we always show something. */
	vmw_write(dev_priv, SVGA_REG_NUM_GUEST_DISPLAYS,
		  lds->num_active ? lds->num_active : 1);

	i = 0;
	list_for_each_entry(entry, &lds->active, active) {
		crtc = &entry->base.crtc;

		vmw_write(dev_priv, SVGA_REG_DISPLAY_ID, i);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_IS_PRIMARY, !i);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_POSITION_X, crtc->x);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_POSITION_Y, crtc->y);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_WIDTH, crtc->mode.hdisplay);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_HEIGHT, crtc->mode.vdisplay);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_ID, SVGA_ID_INVALID);

		i++;
	}

	BUG_ON(i != lds->num_active);

	lds->last_num_active = lds->num_active;


	/* Find the first du with a cursor. */
	list_for_each_entry(entry, &lds->active, active) {
		du = &entry->base;

		if (!du->cursor_dmabuf)
			continue;

		ret = vmw_cursor_update_dmabuf(dev_priv,
					       du->cursor_dmabuf,
					       64, 64,
					       du->hotspot_x,
					       du->hotspot_y);
		if (ret == 0)
			break;

		DRM_ERROR("Could not update cursor image\n");
	}

	return 0;
}

static int vmw_ldu_del_active(struct vmw_private *vmw_priv,
			      struct vmw_legacy_display_unit *ldu)
{
	struct vmw_legacy_display *ld = vmw_priv->ldu_priv;
	if (list_empty(&ldu->active))
		return 0;

	/* Must init otherwise list_empty(&ldu->active) will not work. */
	list_del_init(&ldu->active);
	if (--(ld->num_active) == 0) {
		BUG_ON(!ld->fb);
		if (ld->fb->unpin)
			ld->fb->unpin(ld->fb);
		ld->fb = NULL;
	}

	return 0;
}

static int vmw_ldu_add_active(struct vmw_private *vmw_priv,
			      struct vmw_legacy_display_unit *ldu,
			      struct vmw_framebuffer *vfb)
{
	struct vmw_legacy_display *ld = vmw_priv->ldu_priv;
	struct vmw_legacy_display_unit *entry;
	struct list_head *at;

	BUG_ON(!ld->num_active && ld->fb);
	if (vfb != ld->fb) {
		if (ld->fb && ld->fb->unpin)
			ld->fb->unpin(ld->fb);
		if (vfb->pin)
			vfb->pin(vfb);
		ld->fb = vfb;
	}

	if (!list_empty(&ldu->active))
		return 0;

	at = &ld->active;
	list_for_each_entry(entry, &ld->active, active) {
		if (entry->base.unit > ldu->base.unit)
			break;

		at = &entry->active;
	}

	list_add(&ldu->active, at);

	ld->num_active++;

	return 0;
}

static int vmw_ldu_crtc_set_config(struct drm_mode_set *set)
{
	struct vmw_private *dev_priv;
	struct vmw_legacy_display_unit *ldu;
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	struct drm_encoder *encoder;
	struct vmw_framebuffer *vfb;
	struct drm_framebuffer *fb;
	struct drm_crtc *crtc;

	if (!set)
		return -EINVAL;

	if (!set->crtc)
		return -EINVAL;

	/* get the ldu */
	crtc = set->crtc;
	ldu = vmw_crtc_to_ldu(crtc);
	vfb = set->fb ? vmw_framebuffer_to_vfb(set->fb) : NULL;
	dev_priv = vmw_priv(crtc->dev);

	if (set->num_connectors > 1) {
		DRM_ERROR("to many connectors\n");
		return -EINVAL;
	}

	if (set->num_connectors == 1 &&
	    set->connectors[0] != &ldu->base.connector) {
		DRM_ERROR("connector doesn't match %p %p\n",
			set->connectors[0], &ldu->base.connector);
		return -EINVAL;
	}

	/* ldu only supports one fb active at the time */
	if (dev_priv->ldu_priv->fb && vfb &&
	    !(dev_priv->ldu_priv->num_active == 1 &&
	      !list_empty(&ldu->active)) &&
	    dev_priv->ldu_priv->fb != vfb) {
		DRM_ERROR("Multiple framebuffers not supported\n");
		return -EINVAL;
	}

	/* since they always map one to one these are safe */
	connector = &ldu->base.connector;
	encoder = &ldu->base.encoder;

	/* should we turn the crtc off? */
	if (set->num_connectors == 0 || !set->mode || !set->fb) {

		connector->encoder = NULL;
		encoder->crtc = NULL;
		crtc->primary->fb = NULL;
		crtc->enabled = false;

		vmw_ldu_del_active(dev_priv, ldu);

		return vmw_ldu_commit_list(dev_priv);
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

	crtc->primary->fb = fb;
	encoder->crtc = crtc;
	connector->encoder = encoder;
	crtc->x = set->x;
	crtc->y = set->y;
	crtc->mode = *mode;
	crtc->enabled = true;

	vmw_ldu_add_active(dev_priv, ldu, vfb);

	return vmw_ldu_commit_list(dev_priv);
}

static const struct drm_crtc_funcs vmw_legacy_crtc_funcs = {
	.cursor_set2 = vmw_du_crtc_cursor_set2,
	.cursor_move = vmw_du_crtc_cursor_move,
	.gamma_set = vmw_du_crtc_gamma_set,
	.destroy = vmw_ldu_crtc_destroy,
	.set_config = vmw_ldu_crtc_set_config,
};


/*
 * Legacy Display Unit encoder functions
 */

static void vmw_ldu_encoder_destroy(struct drm_encoder *encoder)
{
	vmw_ldu_destroy(vmw_encoder_to_ldu(encoder));
}

static const struct drm_encoder_funcs vmw_legacy_encoder_funcs = {
	.destroy = vmw_ldu_encoder_destroy,
};

/*
 * Legacy Display Unit connector functions
 */

static void vmw_ldu_connector_destroy(struct drm_connector *connector)
{
	vmw_ldu_destroy(vmw_connector_to_ldu(connector));
}

static const struct drm_connector_funcs vmw_legacy_connector_funcs = {
	.dpms = vmw_du_connector_dpms,
	.detect = vmw_du_connector_detect,
	.fill_modes = vmw_du_connector_fill_modes,
	.set_property = vmw_du_connector_set_property,
	.destroy = vmw_ldu_connector_destroy,
};

static int vmw_ldu_init(struct vmw_private *dev_priv, unsigned unit)
{
	struct vmw_legacy_display_unit *ldu;
	struct drm_device *dev = dev_priv->dev;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;

	ldu = kzalloc(sizeof(*ldu), GFP_KERNEL);
	if (!ldu)
		return -ENOMEM;

	ldu->base.unit = unit;
	crtc = &ldu->base.crtc;
	encoder = &ldu->base.encoder;
	connector = &ldu->base.connector;

	INIT_LIST_HEAD(&ldu->active);

	ldu->base.pref_active = (unit == 0);
	ldu->base.pref_width = dev_priv->initial_width;
	ldu->base.pref_height = dev_priv->initial_height;
	ldu->base.pref_mode = NULL;
	ldu->base.is_implicit = true;

	drm_connector_init(dev, connector, &vmw_legacy_connector_funcs,
			   DRM_MODE_CONNECTOR_VIRTUAL);
	connector->status = vmw_du_connector_detect(connector, true);

	drm_encoder_init(dev, encoder, &vmw_legacy_encoder_funcs,
			 DRM_MODE_ENCODER_VIRTUAL, NULL);
	drm_mode_connector_attach_encoder(connector, encoder);
	encoder->possible_crtcs = (1 << unit);
	encoder->possible_clones = 0;

	(void) drm_connector_register(connector);

	drm_crtc_init(dev, crtc, &vmw_legacy_crtc_funcs);

	drm_mode_crtc_set_gamma_size(crtc, 256);

	drm_object_attach_property(&connector->base,
				      dev->mode_config.dirty_info_property,
				      1);

	return 0;
}

int vmw_kms_ldu_init_display(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	int i, ret;

	if (dev_priv->ldu_priv) {
		DRM_INFO("ldu system already on\n");
		return -EINVAL;
	}

	dev_priv->ldu_priv = kmalloc(sizeof(*dev_priv->ldu_priv), GFP_KERNEL);
	if (!dev_priv->ldu_priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&dev_priv->ldu_priv->active);
	dev_priv->ldu_priv->num_active = 0;
	dev_priv->ldu_priv->last_num_active = 0;
	dev_priv->ldu_priv->fb = NULL;

	/* for old hardware without multimon only enable one display */
	if (dev_priv->capabilities & SVGA_CAP_MULTIMON)
		ret = drm_vblank_init(dev, VMWGFX_NUM_DISPLAY_UNITS);
	else
		ret = drm_vblank_init(dev, 1);
	if (ret != 0)
		goto err_free;

	ret = drm_mode_create_dirty_info_property(dev);
	if (ret != 0)
		goto err_vblank_cleanup;

	if (dev_priv->capabilities & SVGA_CAP_MULTIMON)
		for (i = 0; i < VMWGFX_NUM_DISPLAY_UNITS; ++i)
			vmw_ldu_init(dev_priv, i);
	else
		vmw_ldu_init(dev_priv, 0);

	dev_priv->active_display_unit = vmw_du_legacy;

	DRM_INFO("Legacy Display Unit initialized\n");

	return 0;

err_vblank_cleanup:
	drm_vblank_cleanup(dev);
err_free:
	kfree(dev_priv->ldu_priv);
	dev_priv->ldu_priv = NULL;
	return ret;
}

int vmw_kms_ldu_close_display(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	if (!dev_priv->ldu_priv)
		return -ENOSYS;

	drm_vblank_cleanup(dev);

	BUG_ON(!list_empty(&dev_priv->ldu_priv->active));

	kfree(dev_priv->ldu_priv);

	return 0;
}


int vmw_kms_ldu_do_dmabuf_dirty(struct vmw_private *dev_priv,
				struct vmw_framebuffer *framebuffer,
				unsigned flags, unsigned color,
				struct drm_clip_rect *clips,
				unsigned num_clips, int increment)
{
	size_t fifo_size;
	int i;

	struct {
		uint32_t header;
		SVGAFifoCmdUpdate body;
	} *cmd;

	fifo_size = sizeof(*cmd) * num_clips;
	cmd = vmw_fifo_reserve(dev_priv, fifo_size);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Fifo reserve failed.\n");
		return -ENOMEM;
	}

	memset(cmd, 0, fifo_size);
	for (i = 0; i < num_clips; i++, clips += increment) {
		cmd[i].header = SVGA_CMD_UPDATE;
		cmd[i].body.x = clips->x1;
		cmd[i].body.y = clips->y1;
		cmd[i].body.width = clips->x2 - clips->x1;
		cmd[i].body.height = clips->y2 - clips->y1;
	}

	vmw_fifo_commit(dev_priv, fifo_size);
	return 0;
}
