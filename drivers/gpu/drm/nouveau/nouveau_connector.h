/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __NOUVEAU_CONNECTOR_H__
#define __NOUVEAU_CONNECTOR_H__

#include <drm/drm_edid.h>
#include "nouveau_crtc.h"

#include <core/event.h>

#include <subdev/bios.h>
#include <subdev/bios/gpio.h>

struct nouveau_i2c_port;

enum nouveau_underscan_type {
	UNDERSCAN_OFF,
	UNDERSCAN_ON,
	UNDERSCAN_AUTO,
};

/* the enum values specifically defined here match nv50/nvd0 hw values, and
 * the code relies on this
 */
enum nouveau_dithering_mode {
	DITHERING_MODE_OFF = 0x00,
	DITHERING_MODE_ON = 0x01,
	DITHERING_MODE_DYNAMIC2X2 = 0x10 | DITHERING_MODE_ON,
	DITHERING_MODE_STATIC2X2 = 0x18 | DITHERING_MODE_ON,
	DITHERING_MODE_TEMPORAL = 0x20 | DITHERING_MODE_ON,
	DITHERING_MODE_AUTO
};

enum nouveau_dithering_depth {
	DITHERING_DEPTH_6BPC = 0x00,
	DITHERING_DEPTH_8BPC = 0x02,
	DITHERING_DEPTH_AUTO
};

struct nouveau_connector {
	struct drm_connector base;
	enum dcb_connector_type type;
	u8 index;
	u8 *dcb;

	struct dcb_gpio_func hpd;
	struct work_struct hpd_work;
	struct nouveau_eventh *hpd_func;

	int dithering_mode;
	int dithering_depth;
	int scaling_mode;
	enum nouveau_underscan_type underscan;
	u32 underscan_hborder;
	u32 underscan_vborder;

	struct nouveau_encoder *detected_encoder;
	struct edid *edid;
	struct drm_display_mode *native_mode;
};

static inline struct nouveau_connector *nouveau_connector(
						struct drm_connector *con)
{
	return container_of(con, struct nouveau_connector, base);
}

static inline struct nouveau_connector *
nouveau_crtc_connector_get(struct nouveau_crtc *nv_crtc)
{
	struct drm_device *dev = nv_crtc->base.dev;
	struct drm_connector *connector;
	struct drm_crtc *crtc = to_drm_crtc(nv_crtc);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder && connector->encoder->crtc == crtc)
			return nouveau_connector(connector);
	}

	return NULL;
}

struct drm_connector *
nouveau_connector_create(struct drm_device *, int index);

#endif /* __NOUVEAU_CONNECTOR_H__ */
