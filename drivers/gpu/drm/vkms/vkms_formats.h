/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_FORMATS_H_
#define _VKMS_FORMATS_H_

#include "vkms_drv.h"

pixel_read_line_t get_pixel_read_line_function(u32 format);

pixel_write_t get_pixel_write_function(u32 format);

#endif /* _VKMS_FORMATS_H_ */
