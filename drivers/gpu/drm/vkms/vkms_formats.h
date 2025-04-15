/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_FORMATS_H_
#define _VKMS_FORMATS_H_

#include "vkms_drv.h"

pixel_read_line_t get_pixel_read_line_function(u32 format);

pixel_write_t get_pixel_write_function(u32 format);

void get_conversion_matrix_to_argb_u16(u32 format, enum drm_color_encoding encoding,
				       enum drm_color_range range,
				       struct conversion_matrix *matrix);

#endif /* _VKMS_FORMATS_H_ */
