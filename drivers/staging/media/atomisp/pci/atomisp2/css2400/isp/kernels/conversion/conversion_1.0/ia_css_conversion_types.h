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

#ifndef __IA_CSS_CONVERSION_TYPES_H
#define __IA_CSS_CONVERSION_TYPES_H

/**
 *  Conversion Kernel parameters.
 *  Deinterleave bayer quad into isys format
 *
 *  ISP block: CONVERSION
 *
 */
struct ia_css_conversion_config {
	uint32_t en;     /**< en parameter */
	uint32_t dummy0; /**< dummy0 dummy parameter 0 */
	uint32_t dummy1; /**< dummy1 dummy parameter 1 */
	uint32_t dummy2; /**< dummy2 dummy parameter 2 */
};

#endif /* __IA_CSS_CONVERSION_TYPES_H */
