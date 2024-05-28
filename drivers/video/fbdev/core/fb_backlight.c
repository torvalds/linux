// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/export.h>
#include <linux/fb.h>
#include <linux/mutex.h>

#if IS_ENABLED(CONFIG_FB_BACKLIGHT)
/*
 * This function generates a linear backlight curve
 *
 *     0: off
 *   1-7: min
 * 8-127: linear from min to max
 */
void fb_bl_default_curve(struct fb_info *fb_info, u8 off, u8 min, u8 max)
{
	unsigned int i, flat, count, range = (max - min);

	mutex_lock(&fb_info->bl_curve_mutex);

	fb_info->bl_curve[0] = off;

	for (flat = 1; flat < (FB_BACKLIGHT_LEVELS / 16); ++flat)
		fb_info->bl_curve[flat] = min;

	count = FB_BACKLIGHT_LEVELS * 15 / 16;
	for (i = 0; i < count; ++i)
		fb_info->bl_curve[flat + i] = min + (range * (i + 1) / count);

	mutex_unlock(&fb_info->bl_curve_mutex);
}
EXPORT_SYMBOL_GPL(fb_bl_default_curve);
#endif
