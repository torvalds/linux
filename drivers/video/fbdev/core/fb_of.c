/*
 * Generic DT helper functions for fbdev devices
 *
 * Copyright (C) 2018 Olliver Schinagl
 *
 * Olliver Schinagl <oliver@schinagl.nl>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 *
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/fb.h>
#include <linux/property.h>
#include <uapi/linux/fb.h>

void fb_parse_properties(struct device *dev, struct fb_of_properties *prop)
{
	bool clear;
	u16 rotate;

	clear = device_property_read_bool(dev, "clear-on-probe");

	if (device_property_read_u16(dev, "rotate", &rotate))
		rotate = FB_ROTATE_UR;

	switch (rotate) {
	case 270: /* fall through */
	case 3:
		prop->rotate = FB_ROTATE_CCW;
		break;
	case 180: /* fall through */
	case 2:
		prop->rotate = FB_ROTATE_UD;
		break;
	case 90: /* fall through */
	case 1:
		prop->rotate = FB_ROTATE_CW;
		break;
	case 0: /* fall through */
	default:
		prop->rotate = FB_ROTATE_UR;
		break;
	}
}
EXPORT_SYMBOL_GPL(fb_parse_properties);
