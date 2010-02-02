/**************************************************************************
 *
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
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

#define vmw_crtc_to_ldu(x) \
	container_of(x, struct vmw_legacy_display_unit, base.crtc)
#define vmw_encoder_to_ldu(x) \
	container_of(x, struct vmw_legacy_display_unit, base.encoder)
#define vmw_connector_to_ldu(x) \
	container_of(x, struct vmw_legacy_display_unit, base.connector)

struct vmw_legacy_display {
	struct list_head active;

	unsigned num_active;

	struct vmw_framebuffer *fb;
};

/**
 * Display unit using the legacy register interface.
 */
struct vmw_legacy_display_unit {
	struct vmw_display_unit base;

	struct list_head active;

	unsigned unit;
};

static void vmw_ldu_destroy(struct vmw_legacy_display_unit *ldu)
{
	list_del_init(&ldu->active);
	vmw_display_unit_cleanup(&ldu->base);
	kfree(ldu);
}


/*
 * Legacy Display Unit CRTC functions
 */

static void vmw_ldu_crtc_save(struct drm_crtc *crtc)
{
}

static void vmw_ldu_crtc_restore(struct drm_crtc *crtc)
{
}

static void vmw_ldu_crtc_gamma_set(struct drm_crtc *crtc,
				   u16 *r, u16 *g, u16 *b,
				   uint32_t size)
{
}

static void vmw_ldu_crtc_destroy(struct drm_crtc *crtc)
{
	vmw_ldu_destroy(vmw_crtc_to_ldu(crtc));
}

static int vmw_ldu_commit_list(struct vmw_private *dev_priv)
{
	struct vmw_legacy_display *lds = dev_priv->ldu_priv;
	struct vmw_legacy_display_unit *entry;
	struct drm_crtc *crtc;
	int i = 0;

	/* to stop the screen from changing size on resize */
	vmw_write(dev_priv, SVGA_REG_NUM_GUEST_DISPLAYS, 0);
	for (i = 0; i < lds->num_active; i++) {
		vmw_write(dev_priv, SVGA_REG_DISPLAY_ID, i);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_IS_PRIMARY, !i);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_POSITION_X, 0);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_POSITION_Y, 0);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_WIDTH, 0);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_HEIGHT, 0);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_ID, SVGA_ID_INVALID);
	}

	/* Now set the mode */
	vmw_write(dev_priv, SVGA_REG_NUM_GUEST_DISPLAYS, lds->num_active);
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

	return 0;
}

static int vmw_ldu_del_active(struct vmw_private *vmw_priv,
			      struct vmw_legacy_display_unit *ldu)
{
	struct vmw_legacy_display *ld = vmw_priv->ldu_priv;
	if (list_empty(&ldu->active))
		return 0;

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

	if (!list_empty(&ldu->active))
		return 0;

	at = &ld->active;
	list_for_each_entry(entry, &ld->active, active) {
		if (entry->unit > ldu->unit)
			break;

		at = &entry->active;
	}

	list_add(&ldu->active, at);
	if (ld->num_active++ == 0) {
		BUG_ON(ld->fb);
		if (vfb->pin)
			vfb->pin(vfb);
		ld->fb = vfb;
	}

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
		crtc->fb = NULL;

		vmw_ldu_del_active(dev_priv, ldu);

		vmw_ldu_commit_list(dev_priv);

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

	crtc->fb = fb;
	encoder->crtc = crtc;
	connector->encoder = encoder;
	crtc->x = set->x;
	crtc->y = set->y;
	crtc->mode = *mode;

	vmw_ldu_add_active(dev_priv, ldu, vfb);

	vmw_ldu_commit_list(dev_priv);

	return 0;
}

static struct drm_crtc_funcs vmw_legacy_crtc_funcs = {
	.save = vmw_ldu_crtc_save,
	.restore = vmw_ldu_crtc_restore,
	.cursor_set = vmw_du_crtc_cursor_set,
	.cursor_move = vmw_du_crtc_cursor_move,
	.gamma_set = vmw_ldu_crtc_gamma_set,
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

static struct drm_encoder_funcs vmw_legacy_encoder_funcs = {
	.destroy = vmw_ldu_encoder_destroy,
};

/*
 * Legacy Display Unit connector functions
 */

static void vmw_ldu_connector_dpms(struct drm_connector *connector, int mode)
{
}

static void vmw_ldu_connector_save(struct drm_connector *connector)
{
}

static void vmw_ldu_connector_restore(struct drm_connector *connector)
{
}

static enum drm_connector_status
	vmw_ldu_connector_detect(struct drm_connector *connector)
{
	/* XXX vmwctrl should control connection status */
	if (vmw_connector_to_ldu(connector)->base.unit == 0)
		return connector_status_connected;
	return connector_status_disconnected;
}

static struct drm_display_mode vmw_ldu_connector_builtin[] = {
	/* 640x480@60Hz */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 25175, 640, 656,
		   752, 800, 0, 480, 489, 492, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 800x600@60Hz */
	{ DRM_MODE("800x600",
		   DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		   40000, 800, 840, 968, 1056, 0, 600, 601, 605, 628,
		   0, DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1024x768@60Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 65000, 1024, 1048,
		   1184, 1344, 0, 768, 771, 777, 806, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 1152x864@75Hz */
	{ DRM_MODE("1152x864", DRM_MODE_TYPE_DRIVER, 108000, 1152, 1216,
		   1344, 1600, 0, 864, 865, 868, 900, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x768@60Hz */
	{ DRM_MODE("1280x768", DRM_MODE_TYPE_DRIVER, 79500, 1280, 1344,
		   1472, 1664, 0, 768, 771, 778, 798, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x800@60Hz */
	{ DRM_MODE("1280x800", DRM_MODE_TYPE_DRIVER, 83500, 1280, 1352,
		   1480, 1680, 0, 800, 803, 809, 831, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 1280x960@60Hz */
	{ DRM_MODE("1280x960", DRM_MODE_TYPE_DRIVER, 108000, 1280, 1376,
		   1488, 1800, 0, 960, 961, 964, 1000, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x1024@60Hz */
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 108000, 1280, 1328,
		   1440, 1688, 0, 1024, 1025, 1028, 1066, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1360x768@60Hz */
	{ DRM_MODE("1360x768", DRM_MODE_TYPE_DRIVER, 85500, 1360, 1424,
		   1536, 1792, 0, 768, 771, 777, 795, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x1050@60Hz */
	{ DRM_MODE("1400x1050", DRM_MODE_TYPE_DRIVER, 121750, 1400, 1488,
		   1632, 1864, 0, 1050, 1053, 1057, 1089, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x900@60Hz */
	{ DRM_MODE("1440x900", DRM_MODE_TYPE_DRIVER, 106500, 1440, 1520,
		   1672, 1904, 0, 900, 903, 909, 934, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1600x1200@60Hz */
	{ DRM_MODE("1600x1200", DRM_MODE_TYPE_DRIVER, 162000, 1600, 1664,
		   1856, 2160, 0, 1200, 1201, 1204, 1250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1680x1050@60Hz */
	{ DRM_MODE("1680x1050", DRM_MODE_TYPE_DRIVER, 146250, 1680, 1784,
		   1960, 2240, 0, 1050, 1053, 1059, 1089, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1792x1344@60Hz */
	{ DRM_MODE("1792x1344", DRM_MODE_TYPE_DRIVER, 204750, 1792, 1920,
		   2120, 2448, 0, 1344, 1345, 1348, 1394, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1853x1392@60Hz */
	{ DRM_MODE("1856x1392", DRM_MODE_TYPE_DRIVER, 218250, 1856, 1952,
		   2176, 2528, 0, 1392, 1393, 1396, 1439, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1200@60Hz */
	{ DRM_MODE("1920x1200", DRM_MODE_TYPE_DRIVER, 193250, 1920, 2056,
		   2256, 2592, 0, 1200, 1203, 1209, 1245, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1440@60Hz */
	{ DRM_MODE("1920x1440", DRM_MODE_TYPE_DRIVER, 234000, 1920, 2048,
		   2256, 2600, 0, 1440, 1441, 1444, 1500, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2560x1600@60Hz */
	{ DRM_MODE("2560x1600", DRM_MODE_TYPE_DRIVER, 348500, 2560, 2752,
		   3032, 3504, 0, 1600, 1603, 1609, 1658, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* Terminate */
	{ DRM_MODE("", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) },
};

static int vmw_ldu_connector_fill_modes(struct drm_connector *connector,
					uint32_t max_width, uint32_t max_height)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode = NULL;
	int i;

	for (i = 0; vmw_ldu_connector_builtin[i].type != 0; i++) {
		if (vmw_ldu_connector_builtin[i].hdisplay > max_width ||
		    vmw_ldu_connector_builtin[i].vdisplay > max_height)
			continue;

		mode = drm_mode_duplicate(dev, &vmw_ldu_connector_builtin[i]);
		if (!mode)
			return 0;
		mode->vrefresh = drm_mode_vrefresh(mode);

		drm_mode_probed_add(connector, mode);
	}

	drm_mode_connector_list_update(connector);

	return 1;
}

static int vmw_ldu_connector_set_property(struct drm_connector *connector,
					  struct drm_property *property,
					  uint64_t val)
{
	return 0;
}

static void vmw_ldu_connector_destroy(struct drm_connector *connector)
{
	vmw_ldu_destroy(vmw_connector_to_ldu(connector));
}

static struct drm_connector_funcs vmw_legacy_connector_funcs = {
	.dpms = vmw_ldu_connector_dpms,
	.save = vmw_ldu_connector_save,
	.restore = vmw_ldu_connector_restore,
	.detect = vmw_ldu_connector_detect,
	.fill_modes = vmw_ldu_connector_fill_modes,
	.set_property = vmw_ldu_connector_set_property,
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

	ldu->unit = unit;
	crtc = &ldu->base.crtc;
	encoder = &ldu->base.encoder;
	connector = &ldu->base.connector;

	drm_connector_init(dev, connector, &vmw_legacy_connector_funcs,
			   DRM_MODE_CONNECTOR_LVDS);
	/* Initial status */
	if (unit == 0)
		connector->status = connector_status_connected;
	else
		connector->status = connector_status_disconnected;

	drm_encoder_init(dev, encoder, &vmw_legacy_encoder_funcs,
			 DRM_MODE_ENCODER_LVDS);
	drm_mode_connector_attach_encoder(connector, encoder);
	encoder->possible_crtcs = (1 << unit);
	encoder->possible_clones = 0;

	INIT_LIST_HEAD(&ldu->active);

	drm_crtc_init(dev, crtc, &vmw_legacy_crtc_funcs);

	drm_connector_attach_property(connector,
				      dev->mode_config.dirty_info_property,
				      1);

	return 0;
}

int vmw_kms_init_legacy_display_system(struct vmw_private *dev_priv)
{
	if (dev_priv->ldu_priv) {
		DRM_INFO("ldu system already on\n");
		return -EINVAL;
	}

	dev_priv->ldu_priv = kmalloc(GFP_KERNEL, sizeof(*dev_priv->ldu_priv));

	if (!dev_priv->ldu_priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&dev_priv->ldu_priv->active);
	dev_priv->ldu_priv->num_active = 0;
	dev_priv->ldu_priv->fb = NULL;

	drm_mode_create_dirty_info_property(dev_priv->dev);

	vmw_ldu_init(dev_priv, 0);
	vmw_ldu_init(dev_priv, 1);
	vmw_ldu_init(dev_priv, 2);
	vmw_ldu_init(dev_priv, 3);
	vmw_ldu_init(dev_priv, 4);
	vmw_ldu_init(dev_priv, 5);
	vmw_ldu_init(dev_priv, 6);
	vmw_ldu_init(dev_priv, 7);

	return 0;
}

int vmw_kms_close_legacy_display_system(struct vmw_private *dev_priv)
{
	if (!dev_priv->ldu_priv)
		return -ENOSYS;

	BUG_ON(!list_empty(&dev_priv->ldu_priv->active));

	kfree(dev_priv->ldu_priv);

	return 0;
}
