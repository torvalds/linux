/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __IA_CSS_FORMATS_TYPES_H
#define __IA_CSS_FORMATS_TYPES_H

/** @file
* CSS-API header file for output format parameters.
*/

#include "type_support.h"

/** Formats configuration.
 *
 *  ISP block: FORMATS
 *  ISP1: FORMATS is used.
 *  ISP2: FORMATS is used.
 */
struct ia_css_formats_config {
	uint32_t video_full_range_flag; /**< selects the range of YUV output.
				u8.0, [0,1],
				default 1, ineffective n/a\n
				1 - full range, luma 0-255, chroma 0-255\n
				0 - reduced range, luma 16-235, chroma 16-240 */
};

#endif /* __IA_CSS_FORMATS_TYPES_H */
