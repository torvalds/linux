/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_FORMATS_H_
#define _VKMS_FORMATS_H_

#include "vkms_drv.h"

pixel_read_line_t get_pixel_read_line_function(u32 format);

pixel_write_t get_pixel_write_function(u32 format);

void get_conversion_matrix_to_argb_u16(u32 format, enum drm_color_encoding encoding,
				       enum drm_color_range range,
				       struct conversion_matrix *matrix);

#if IS_ENABLED(CONFIG_KUNIT)
struct pixel_argb_u16 argb_u16_from_yuv161616(const struct conversion_matrix *matrix,
					      u16 y, u16 channel_1, u16 channel_2);
#endif

#endif /* _VKMS_FORMATS_H_ */
