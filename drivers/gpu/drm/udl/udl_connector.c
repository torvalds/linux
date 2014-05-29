/*
 * Copyright (C) 2012 Red Hat
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_crtc_helper.h>
#include "udl_drv.h"

/* dummy connector to just get EDID,
   all UDL appear to have a DVI-D */

static u8 *udl_get_edid(struct udl_device *udl)
{
	u8 *block;
	char *rbuf;
	int ret, i;

	block = kmalloc(EDID_LENGTH, GFP_KERNEL);
	if (block == NULL)
		return NULL;

	rbuf = kmalloc(2, GFP_KERNEL);
	if (rbuf == NULL)
		goto error;

	for (i = 0; i < EDID_LENGTH; i++) {
		ret = usb_control_msg(udl->ddev->usbdev,
				      usb_rcvctrlpipe(udl->ddev->usbdev, 0), (0x02),
				      (0x80 | (0x02 << 5)), i << 8, 0xA1, rbuf, 2,
				      HZ);
		if (ret < 1) {
			DRM_ERROR("Read EDID byte %d failed err %x\n", i, ret);
			goto error;
		}
		block[i] = rbuf[1];
	}

	kfree(rbuf);
	return block;

error:
	kfree(block);
	kfree(rbuf);
	return NULL;
}

static int udl_get_modes(struct drm_connector *connector)
{
	struct udl_device *udl = connector->dev->dev_private;
	struct edid *edid;
	int ret;

	edid = (struct edid *)udl_get_edid(udl);
	if (!edid) {
		drm_mode_connector_update_edid_property(connector, NULL);
		return 0;
	}

	/*
	 * We only read the main block, but if the monitor reports extension
	 * blocks then the drm edid code expects them to be present, so patch
	 * the extension count to 0.
	 */
	edid->checksum += edid->extensions;
	edid->extensions = 0;

	drm_mode_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);
	return ret;
}

static int udl_mode_valid(struct drm_connector *connector,
			  struct drm_display_mode *mode)
{
	struct udl_device *udl = connector->dev->dev_private;
	if (!udl->sku_pixel_limit)
		return 0;

	if (mode->vdisplay * mode->hdisplay > udl->sku_pixel_limit)
		return MODE_VIRTUAL_Y;

	return 0;
}

static enum drm_connector_status
udl_detect(struct drm_connector *connector, bool force)
{
	if (drm_device_is_unplugged(connector->dev))
		return connector_status_disconnected;
	return connector_status_connected;
}

static struct drm_encoder*
udl_best_single_encoder(struct drm_connector *connector)
{
	int enc_id = connector->encoder_ids[0];
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;

	obj = drm_mode_object_find(connector->dev, enc_id, DRM_MODE_OBJECT_ENCODER);
	if (!obj)
		return NULL;
	encoder = obj_to_encoder(obj);
	return encoder;
}

static int udl_connector_set_property(struct drm_connector *connector,
				      struct drm_property *property,
				      uint64_t val)
{
	return 0;
}

static void udl_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static struct drm_connector_helper_funcs udl_connector_helper_funcs = {
	.get_modes = udl_get_modes,
	.mode_valid = udl_mode_valid,
	.best_encoder = udl_best_single_encoder,
};

static struct drm_connector_funcs udl_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = udl_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = udl_connector_destroy,
	.set_property = udl_connector_set_property,
};

int udl_connector_init(struct drm_device *dev, struct drm_encoder *encoder)
{
	struct drm_connector *connector;

	connector = kzalloc(sizeof(struct drm_connector), GFP_KERNEL);
	if (!connector)
		return -ENOMEM;

	drm_connector_init(dev, connector, &udl_connector_funcs, DRM_MODE_CONNECTOR_DVII);
	drm_connector_helper_add(connector, &udl_connector_helper_funcs);

	drm_connector_register(connector);
	drm_mode_connector_attach_encoder(connector, encoder);

	drm_object_attach_property(&connector->base,
				      dev->mode_config.dirty_info_property,
				      1);
	return 0;
}
