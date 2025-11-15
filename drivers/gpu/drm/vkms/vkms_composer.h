/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_COMPOSER_H_
#define _VKMS_COMPOSER_H_

#include <kunit/visibility.h>
#include "vkms_drv.h"

/*
 * This enum is related to the positions of the variables inside
 * `struct drm_color_lut`, so the order of both needs to be the same.
 */
enum lut_channel {
	LUT_RED = 0,
	LUT_GREEN,
	LUT_BLUE,
	LUT_RESERVED
};

#if IS_ENABLED(CONFIG_KUNIT)
u16 lerp_u16(u16 a, u16 b, s64 t);
s64 get_lut_index(const struct vkms_color_lut *lut, u16 channel_value);
u16 apply_lut_to_channel_value(const struct vkms_color_lut *lut, u16 channel_value,
			       enum lut_channel channel);
void apply_3x4_matrix(struct pixel_argb_s32 *pixel, const struct drm_color_ctm_3x4 *matrix);
#endif

#endif /* _VKMS_COMPOSER_H_ */
