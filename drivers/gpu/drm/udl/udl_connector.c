// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 */

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>

#include "udl_connector.h"
#include "udl_drv.h"

static int udl_get_edid_block(void *data, u8 *buf, unsigned int block,
			       size_t len)
{
	int ret, i;
	u8 *read_buff;
	struct udl_device *udl = data;

	read_buff = kmalloc(2, GFP_KERNEL);
	if (!read_buff)
		return -1;

	for (i = 0; i < len; i++) {
		int bval = (i + block * EDID_LENGTH) << 8;
		ret = usb_control_msg(udl->udev,
				      usb_rcvctrlpipe(udl->udev, 0),
					  (0x02), (0x80 | (0x02 << 5)), bval,
					  0xA1, read_buff, 2, HZ);
		if (ret < 1) {
			DRM_ERROR("Read EDID byte %d failed err %x\n", i, ret);
			kfree(read_buff);
			return -1;
		}
		buf[i] = read_buff[1];
	}

	kfree(read_buff);
	return 0;
}

static int udl_get_modes(struct drm_connector *connector)
{
	struct udl_drm_connector *udl_connector =
					container_of(connector,
					struct udl_drm_connector,
					connector);

	drm_connector_update_edid_property(connector, udl_connector->edid);
	if (udl_connector->edid)
		return drm_add_edid_modes(connector, udl_connector->edid);
	return 0;
}

static enum drm_mode_status udl_mode_valid(struct drm_connector *connector,
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
	struct udl_device *udl = connector->dev->dev_private;
	struct udl_drm_connector *udl_connector =
					container_of(connector,
					struct udl_drm_connector,
					connector);

	/* cleanup previous edid */
	if (udl_connector->edid != NULL) {
		kfree(udl_connector->edid);
		udl_connector->edid = NULL;
	}

	udl_connector->edid = drm_do_get_edid(connector, udl_get_edid_block, udl);
	if (!udl_connector->edid)
		return connector_status_disconnected;

	return connector_status_connected;
}

static void udl_connector_destroy(struct drm_connector *connector)
{
	struct udl_drm_connector *udl_connector =
					container_of(connector,
					struct udl_drm_connector,
					connector);

	drm_connector_cleanup(connector);
	kfree(udl_connector->edid);
	kfree(connector);
}

static const struct drm_connector_helper_funcs udl_connector_helper_funcs = {
	.get_modes = udl_get_modes,
	.mode_valid = udl_mode_valid,
};

static const struct drm_connector_funcs udl_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = udl_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = udl_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_connector_destroy_state,
};

struct drm_connector *udl_connector_init(struct drm_device *dev)
{
	struct udl_drm_connector *udl_connector;
	struct drm_connector *connector;

	udl_connector = kzalloc(sizeof(struct udl_drm_connector), GFP_KERNEL);
	if (!udl_connector)
		return ERR_PTR(-ENOMEM);

	connector = &udl_connector->connector;
	drm_connector_init(dev, connector, &udl_connector_funcs,
			   DRM_MODE_CONNECTOR_DVII);
	drm_connector_helper_add(connector, &udl_connector_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_HPD |
		DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	return connector;
}
