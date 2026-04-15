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

const struct drm_format_info *drm_sysfb_get_format(struct drm_device *dev,
						   const struct drm_sysfb_format *formats,
						   size_t nformats,
						   const struct pixel_format *pixel)
{
	const struct drm_format_info *format = NULL;
	size_t i;

	for (i = 0; i < nformats; ++i) {
		const struct drm_sysfb_format *f = &formats[i];

		if (pixel_format_equal(pixel, &f->pixel)) {
			format = drm_format_info(f->fourcc);
			break;
		}
	}

	if (!format)
		drm_warn(dev, "No compatible color format found\n");

	return format;
}
EXPORT_SYMBOL(drm_sysfb_get_format);

MODULE_DESCRIPTION("Helpers for DRM sysfb drivers");
MODULE_LICENSE("GPL");
