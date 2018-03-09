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
	u16 rotate;

	if (device_property_read_u16(dev, "rotate", &rotate))
		rotate = FB_ROTATE_UR;

	switch (rotate) {
	case 3:
		prop->rotate = FB_ROTATE_CCW;
		break;
	case 2:
		prop->rotate = FB_ROTATE_UD;
		break;
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
