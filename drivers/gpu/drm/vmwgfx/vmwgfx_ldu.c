// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2009-2015 VMware, Inc., Palo Alto, CA., USA
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
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>


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
	struct drm_framebuffer *fb = NULL;
	struct drm_crtc *crtc = NULL;
	int i = 0;

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
		fb = entry->base.crtc.primary->state->fb;

		return vmw_kms_write_svga(dev_priv, w, h, fb->pitches[0],
					  fb->format->cpp[0] * 8,
					  fb->format->depth);
	}

	if (!list_empty(&lds->active)) {
		entry = list_entry(lds->active.next, typeof(*entry), active);
		fb = entry->base.crtc.primary->state->fb;

		vmw_kms_write_svga(dev_priv, fb->width, fb->height, fb->pitches[0],
				   fb->format->cpp[0] * 8, fb->format->depth);
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
		vmw_svga_enable(vmw_priv);
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

/**
 * vmw_ldu_crtc_mode_set_nofb - Enable svga
 *
 * @crtc: CRTC associated with the new screen
 *
 * For LDU, just enable the svga
 */
static void vmw_ldu_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
}

/**
 * vmw_ldu_crtc_atomic_enable - Noop
 *
 * @crtc: CRTC associated with the new screen
 *
 * This is called after a mode set has been completed.  Here's
 * usually a good place to call vmw_ldu_add_active/vmw_ldu_del_active
 * but since for LDU the display plane is closely tied to the
 * CRTC, it makes more sense to do those at plane update time.
 */
static void vmw_ldu_crtc_atomic_enable(struct drm_crtc *crtc,
				       struct drm_crtc_state *old_state)
{
}

/**
 * vmw_ldu_crtc_atomic_disable - Turns off CRTC
 *
 * @crtc: CRTC to be turned off
 */
static void vmw_ldu_crtc_atomic_disable(struct drm_crtc *crtc,
					struct drm_crtc_state *old_state)
{
}

static const struct drm_crtc_funcs vmw_legacy_crtc_funcs = {
	.gamma_set = vmw_du_crtc_gamma_set,
	.destroy = vmw_ldu_crtc_destroy,
	.reset = vmw_du_crtc_reset,
	.atomic_duplicate_state = vmw_du_crtc_duplicate_state,
	.atomic_destroy_state = vmw_du_crtc_destroy_state,
	.set_config = vmw_kms_set_config,
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
	.reset = vmw_du_connector_reset,
	.atomic_duplicate_state = vmw_du_connector_duplicate_state,
	.atomic_destroy_state = vmw_du_connector_destroy_state,
	.atomic_set_property = vmw_du_connector_atomic_set_property,
	.atomic_get_property = vmw_du_connector_atomic_get_property,
};

static const struct
drm_connector_helper_funcs vmw_ldu_connector_helper_funcs = {
};

/*
 * Legacy Display Plane Functions
 */

static void
vmw_ldu_primary_plane_atomic_update(struct drm_plane *plane,
				    struct drm_plane_state *old_state)
{
	struct vmw_private *dev_priv;
	struct vmw_legacy_display_unit *ldu;
	struct vmw_framebuffer *vfb;
	struct drm_framebuffer *fb;
	struct drm_crtc *crtc = plane->state->crtc ?: old_state->crtc;


	ldu = vmw_crtc_to_ldu(crtc);
	dev_priv = vmw_priv(plane->dev);
	fb       = plane->state->fb;

	vfb = (fb) ? vmw_framebuffer_to_vfb(fb) : NULL;

	if (vfb)
		vmw_ldu_add_active(dev_priv, ldu, vfb);
	else
		vmw_ldu_del_active(dev_priv, ldu);

	vmw_ldu_commit_list(dev_priv);
}


static const struct drm_plane_funcs vmw_ldu_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = vmw_du_primary_plane_destroy,
	.reset = vmw_du_plane_reset,
	.atomic_duplicate_state = vmw_du_plane_duplicate_state,
	.atomic_destroy_state = vmw_du_plane_destroy_state,
};

static const struct drm_plane_funcs vmw_ldu_cursor_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = vmw_du_cursor_plane_destroy,
	.reset = vmw_du_plane_reset,
	.atomic_duplicate_state = vmw_du_plane_duplicate_state,
	.atomic_destroy_state = vmw_du_plane_destroy_state,
};

/*
 * Atomic Helpers
 */
static const struct
drm_plane_helper_funcs vmw_ldu_cursor_plane_helper_funcs = {
	.atomic_check = vmw_du_cursor_plane_atomic_check,
	.atomic_update = vmw_du_cursor_plane_atomic_update,
	.prepare_fb = vmw_du_cursor_plane_prepare_fb,
	.cleanup_fb = vmw_du_plane_cleanup_fb,
};

static const struct
drm_plane_helper_funcs vmw_ldu_primary_plane_helper_funcs = {
	.atomic_check = vmw_du_primary_plane_atomic_check,
	.atomic_update = vmw_ldu_primary_plane_atomic_update,
};

static const struct drm_crtc_helper_funcs vmw_ldu_crtc_helper_funcs = {
	.mode_set_nofb = vmw_ldu_crtc_mode_set_nofb,
	.atomic_check = vmw_du_crtc_atomic_check,
	.atomic_begin = vmw_du_crtc_atomic_begin,
	.atomic_flush = vmw_du_crtc_atomic_flush,
	.atomic_enable = vmw_ldu_crtc_atomic_enable,
	.atomic_disable = vmw_ldu_crtc_atomic_disable,
};


static int vmw_ldu_init(struct vmw_private *dev_priv, unsigned unit)
{
	struct vmw_legacy_display_unit *ldu;
	struct drm_device *dev = dev_priv->dev;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_plane *primary, *cursor;
	struct drm_crtc *crtc;
	int ret;

	ldu = kzalloc(sizeof(*ldu), GFP_KERNEL);
	if (!ldu)
		return -ENOMEM;

	ldu->base.unit = unit;
	crtc = &ldu->base.crtc;
	encoder = &ldu->base.encoder;
	connector = &ldu->base.connector;
	primary = &ldu->base.primary;
	cursor = &ldu->base.cursor;

	INIT_LIST_HEAD(&ldu->active);

	ldu->base.pref_active = (unit == 0);
	ldu->base.pref_width = dev_priv->initial_width;
	ldu->base.pref_height = dev_priv->initial_height;
	ldu->base.pref_mode = NULL;

	/*
	 * Remove this after enabling atomic because property values can
	 * only exist in a state object
	 */
	ldu->base.is_implicit = true;

	/* Initialize primary plane */
	vmw_du_plane_reset(primary);

	ret = drm_universal_plane_init(dev, &ldu->base.primary,
				       0, &vmw_ldu_plane_funcs,
				       vmw_primary_plane_formats,
				       ARRAY_SIZE(vmw_primary_plane_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize primary plane");
		goto err_free;
	}

	drm_plane_helper_add(primary, &vmw_ldu_primary_plane_helper_funcs);

	/* Initialize cursor plane */
	vmw_du_plane_reset(cursor);

	ret = drm_universal_plane_init(dev, &ldu->base.cursor,
			0, &vmw_ldu_cursor_funcs,
			vmw_cursor_plane_formats,
			ARRAY_SIZE(vmw_cursor_plane_formats),
			NULL, DRM_PLANE_TYPE_CURSOR, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize cursor plane");
		drm_plane_cleanup(&ldu->base.primary);
		goto err_free;
	}

	drm_plane_helper_add(cursor, &vmw_ldu_cursor_plane_helper_funcs);


	vmw_du_connector_reset(connector);
	ret = drm_connector_init(dev, connector, &vmw_legacy_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		goto err_free;
	}

	drm_connector_helper_add(connector, &vmw_ldu_connector_helper_funcs);
	connector->status = vmw_du_connector_detect(connector, true);
	vmw_connector_state_to_vcs(connector->state)->is_implicit = true;


	ret = drm_encoder_init(dev, encoder, &vmw_legacy_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize encoder\n");
		goto err_free_connector;
	}

	(void) drm_connector_attach_encoder(connector, encoder);
	encoder->possible_crtcs = (1 << unit);
	encoder->possible_clones = 0;

	ret = drm_connector_register(connector);
	if (ret) {
		DRM_ERROR("Failed to register connector\n");
		goto err_free_encoder;
	}


	vmw_du_crtc_reset(crtc);
	ret = drm_crtc_init_with_planes(dev, crtc, &ldu->base.primary,
					&ldu->base.cursor,
					&vmw_legacy_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize CRTC\n");
		goto err_free_unregister;
	}

	drm_crtc_helper_add(crtc, &vmw_ldu_crtc_helper_funcs);

	drm_mode_crtc_set_gamma_size(crtc, 256);

	drm_object_attach_property(&connector->base,
				   dev_priv->hotplug_mode_update_property, 1);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_x_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_y_property, 0);
	if (dev_priv->implicit_placement_property)
		drm_object_attach_property
			(&connector->base,
			 dev_priv->implicit_placement_property,
			 1);

	return 0;

err_free_unregister:
	drm_connector_unregister(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
err_free_connector:
	drm_connector_cleanup(connector);
err_free:
	kfree(ldu);
	return ret;
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

	vmw_kms_create_implicit_placement_property(dev_priv, true);

	if (dev_priv->capabilities & SVGA_CAP_MULTIMON)
		for (i = 0; i < VMWGFX_NUM_DISPLAY_UNITS; ++i)
			vmw_ldu_init(dev_priv, i);
	else
		vmw_ldu_init(dev_priv, 0);

	dev_priv->active_display_unit = vmw_du_legacy;

	DRM_INFO("Legacy Display Unit initialized\n");

	return 0;

err_free:
	kfree(dev_priv->ldu_priv);
	dev_priv->ldu_priv = NULL;
	return ret;
}

int vmw_kms_ldu_close_display(struct vmw_private *dev_priv)
{
	if (!dev_priv->ldu_priv)
		return -ENOSYS;

	BUG_ON(!list_empty(&dev_priv->ldu_priv->active));

	kfree(dev_priv->ldu_priv);

	return 0;
}


int vmw_kms_ldu_do_bo_dirty(struct vmw_private *dev_priv,
			    struct vmw_framebuffer *framebuffer,
			    unsigned int flags, unsigned int color,
			    struct drm_clip_rect *clips,
			    unsigned int num_clips, int increment)
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
