// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/module.h>

#include <drm/drm_print.h>

#include "drm_sysfb_helper.h"

int drm_sysfb_get_validated_int(struct drm_device *dev, const char *name,
				u64 value, u32 max)
{
	if (value > min(max, INT_MAX)) {
		drm_warn(dev, "%s of %llu exceeds maximum of %u\n", name, value, max);
		return -EINVAL;
	}
	return value;
}
EXPORT_SYMBOL(drm_sysfb_get_validated_int);

int drm_sysfb_get_validated_int0(struct drm_device *dev, const char *name,
				 u64 value, u32 max)
{
	if (!value) {
		drm_warn(dev, "%s of 0 not allowed\n", name);
		return -EINVAL;
	}
	return drm_sysfb_get_validated_int(dev, name, value, max);
}
EXPORT_SYMBOL(drm_sysfb_get_validated_int0);

MODULE_DESCRIPTION("Helpers for DRM sysfb drivers");
MODULE_LICENSE("GPL");
