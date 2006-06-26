/*
 * Miscellaneous procedures for dealing with the PowerMac hardware.
 * Contains support for the backlight.
 *
 *   Copyright (C) 2000 Benjamin Herrenschmidt
 *   Copyright (C) 2006 Michael Hanselmann <linux-kernel@hansmi.ch>
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <asm/prom.h>
#include <asm/backlight.h>

#define OLD_BACKLIGHT_MAX 15

/* Protect the pmac_backlight variable */
DEFINE_MUTEX(pmac_backlight_mutex);

/* Main backlight storage
 *
 * Backlight drivers in this variable are required to have the "props"
 * attribute set and to have an update_status function.
 *
 * We can only store one backlight here, but since Apple laptops have only one
 * internal display, it doesn't matter. Other backlight drivers can be used
 * independently.
 *
 * Lock ordering:
 * pmac_backlight_mutex (global, main backlight)
 *   pmac_backlight->sem (backlight class)
 */
struct backlight_device *pmac_backlight;

int pmac_has_backlight_type(const char *type)
{
	struct device_node* bk_node = find_devices("backlight");

	if (bk_node) {
		char *prop = get_property(bk_node, "backlight-control", NULL);
		if (prop && strncmp(prop, type, strlen(type)) == 0)
			return 1;
	}

	return 0;
}

int pmac_backlight_curve_lookup(struct fb_info *info, int value)
{
	int level = (FB_BACKLIGHT_LEVELS - 1);

	if (info && info->bl_dev) {
		int i, max = 0;

		/* Look for biggest value */
		for (i = 0; i < FB_BACKLIGHT_LEVELS; i++)
			max = max((int)info->bl_curve[i], max);

		/* Look for nearest value */
		for (i = 0; i < FB_BACKLIGHT_LEVELS; i++) {
			int diff = abs(info->bl_curve[i] - value);
			if (diff < max) {
				max = diff;
				level = i;
			}
		}

	}

	return level;
}

static void pmac_backlight_key(int direction)
{
	mutex_lock(&pmac_backlight_mutex);
	if (pmac_backlight) {
		struct backlight_properties *props;
		int brightness;

		down(&pmac_backlight->sem);
		props = pmac_backlight->props;

		brightness = props->brightness +
			((direction?-1:1) * (props->max_brightness / 15));

		if (brightness < 0)
			brightness = 0;
		else if (brightness > props->max_brightness)
			brightness = props->max_brightness;

		props->brightness = brightness;
		props->update_status(pmac_backlight);

		up(&pmac_backlight->sem);
	}
	mutex_unlock(&pmac_backlight_mutex);
}

void pmac_backlight_key_up()
{
	pmac_backlight_key(0);
}

void pmac_backlight_key_down()
{
	pmac_backlight_key(1);
}

int pmac_backlight_set_legacy_brightness(int brightness)
{
	int error = -ENXIO;

	mutex_lock(&pmac_backlight_mutex);
	if (pmac_backlight) {
		struct backlight_properties *props;

		down(&pmac_backlight->sem);
		props = pmac_backlight->props;
		props->brightness = brightness *
			props->max_brightness / OLD_BACKLIGHT_MAX;
		props->update_status(pmac_backlight);
		up(&pmac_backlight->sem);

		error = 0;
	}
	mutex_unlock(&pmac_backlight_mutex);

	return error;
}

int pmac_backlight_get_legacy_brightness()
{
	int result = -ENXIO;

	mutex_lock(&pmac_backlight_mutex);
	if (pmac_backlight) {
		struct backlight_properties *props;

		down(&pmac_backlight->sem);
		props = pmac_backlight->props;
		result = props->brightness *
			OLD_BACKLIGHT_MAX / props->max_brightness;
		up(&pmac_backlight->sem);
	}
	mutex_unlock(&pmac_backlight_mutex);

	return result;
}
