// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 */

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>

#include "udl_connector.h"
#include "udl_drv.h"

static int udl_get_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct udl_device *udl = data;
	struct drm_device *dev = &udl->drm;
	struct usb_device *udev = udl_to_usb_device(udl);
	u8 *read_buff;
	int ret;
	size_t i;

	read_buff = kmalloc(2, GFP_KERNEL);
	if (!read_buff)
		return -ENOMEM;

	for (i = 0; i < len; i++) {
		int bval = (i + block * EDID_LENGTH) << 8;

		ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				      0x02, (0x80 | (0x02 << 5)), bval,
				      0xA1, read_buff, 2, USB_CTRL_GET_TIMEOUT);
		if (ret < 0) {
			drm_err(dev, "Read EDID byte %zu failed err %x\n", i, ret);
			goto err_kfree;
		} else if (ret < 1) {
			ret = -EIO;
			drm_err(dev, "Read EDID byte %zu failed\n", i);
			goto err_kfree;
		}

		buf[i] = read_buff[1];
	}

	kfree(read_buff);

	return 0;

err_kfree:
	kfree(read_buff);
	return ret;
}

static int udl_connector_helper_get_modes(struct drm_connector *connector)
{
	struct udl_connector *udl_connector = to_udl_connector(connector);

	drm_connector_update_edid_property(connector, udl_connector->edid);
	if (udl_connector->edid)
		return drm_add_edid_modes(connector, udl_connector->edid);

	return 0;
}

static enum drm_connector_status udl_connector_detect(struct drm_connector *connector, bool force)
{
	struct udl_device *udl = to_udl(connector->dev);
	struct udl_connector *udl_connector = to_udl_connector(connector);

	/* cleanup previous EDID */
	kfree(udl_connector->edid);

	udl_connector->edid = drm_do_get_edid(connector, udl_get_edid_block, udl);
	if (!udl_connector->edid)
		return connector_status_disconnected;

	return connector_status_connected;
}

static void udl_connector_destroy(struct drm_connector *connector)
{
	struct udl_connector *udl_connector = to_udl_connector(connector);

	drm_connector_cleanup(connector);
	kfree(udl_connector->edid);
	kfree(udl_connector);
}

static const struct drm_connector_helper_funcs udl_connector_helper_funcs = {
	.get_modes = udl_connector_helper_get_modes,
};

static const struct drm_connector_funcs udl_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = udl_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = udl_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

struct drm_connector *udl_connector_init(struct drm_device *dev)
{
	struct udl_connector *udl_connector;
	struct drm_connector *connector;
	int ret;

	udl_connector = kzalloc(sizeof(*udl_connector), GFP_KERNEL);
	if (!udl_connector)
		return ERR_PTR(-ENOMEM);

	connector = &udl_connector->connector;
	ret = drm_connector_init(dev, connector, &udl_connector_funcs, DRM_MODE_CONNECTOR_VGA);
	if (ret)
		goto err_kfree;

	drm_connector_helper_add(connector, &udl_connector_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_HPD |
			    DRM_CONNECTOR_POLL_CONNECT |
			    DRM_CONNECTOR_POLL_DISCONNECT;

	return connector;

err_kfree:
	kfree(udl_connector);
	return ERR_PTR(ret);
}
