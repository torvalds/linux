/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_FORMATS_H_
#define _VKMS_FORMATS_H_

#include "vkms_drv.h"

void *get_pixel_conversion_function(u32 format);

void *get_line_to_frame_function(u32 format);

#endif /* _VKMS_FORMATS_H_ */
