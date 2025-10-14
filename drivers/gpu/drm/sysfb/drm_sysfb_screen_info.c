// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/screen_info.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>

#include "drm_sysfb_helper.h"

static s64 drm_sysfb_get_validated_size0(struct drm_device *dev, const char *name,
					 u64 value, u64 max)
{
	if (!value) {
		drm_warn(dev, "%s of 0 not allowed\n", name);
		return -EINVAL;
	} else if (value > min(max, S64_MAX)) {
		drm_warn(dev, "%s of %llu exceeds maximum of %llu\n", name, value, max);
		return -EINVAL;
	}
	return value;
}

int drm_sysfb_get_width_si(struct drm_device *dev, const struct screen_info *si)
{
	return drm_sysfb_get_validated_int0(dev, "width", si->lfb_width, U16_MAX);
}
EXPORT_SYMBOL(drm_sysfb_get_width_si);

int drm_sysfb_get_height_si(struct drm_device *dev, const struct screen_info *si)
{
	return drm_sysfb_get_validated_int0(dev, "height", si->lfb_height, U16_MAX);
}
EXPORT_SYMBOL(drm_sysfb_get_height_si);

struct resource *drm_sysfb_get_memory_si(struct drm_device *dev,
					 const struct screen_info *si,
					 struct resource *res)
{
	ssize_t	num;

	num = screen_info_resources(si, res, 1);
	if (!num) {
		drm_warn(dev, "memory resource not found\n");
		return NULL;
	}

	return res;
}
EXPORT_SYMBOL(drm_sysfb_get_memory_si);

int drm_sysfb_get_stride_si(struct drm_device *dev, const struct screen_info *si,
			    const struct drm_format_info *format,
			    unsigned int width, unsigned int height, u64 size)
{
	u64 lfb_linelength = si->lfb_linelength;

	if (!lfb_linelength)
		lfb_linelength = drm_format_info_min_pitch(format, 0, width);

	return drm_sysfb_get_validated_int0(dev, "stride", lfb_linelength, div64_u64(size, height));
}
EXPORT_SYMBOL(drm_sysfb_get_stride_si);

u64 drm_sysfb_get_visible_size_si(struct drm_device *dev, const struct screen_info *si,
				  unsigned int height, unsigned int stride, u64 size)
{
	u64 vsize = PAGE_ALIGN(height * stride);

	return drm_sysfb_get_validated_size0(dev, "visible size", vsize, size);
}
EXPORT_SYMBOL(drm_sysfb_get_visible_size_si);

const struct drm_format_info *drm_sysfb_get_format_si(struct drm_device *dev,
						      const struct drm_sysfb_format *formats,
						      size_t nformats,
						      const struct screen_info *si)
{
	const struct drm_format_info *format = NULL;
	struct pixel_format pixel;
	size_t i;
	int ret;

	ret = screen_info_pixel_format(si, &pixel);
	if (ret)
		return NULL;

	for (i = 0; i < nformats; ++i) {
		const struct drm_sysfb_format *f = &formats[i];

		if (pixel_format_equal(&pixel, &f->pixel)) {
			format = drm_format_info(f->fourcc);
			break;
		}
	}

	if (!format)
		drm_warn(dev, "No compatible color format found\n");

	return format;
}
EXPORT_SYMBOL(drm_sysfb_get_format_si);
