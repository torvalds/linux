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

#ifndef __IA_CSS_STREAM_FORMAT_H
#define __IA_CSS_STREAM_FORMAT_H

/** @file
 * This file contains formats usable for ISP streaming input
 */

#include <type_support.h> /* bool */

/** The ISP streaming input interface supports the following formats.
 *  These match the corresponding MIPI formats.
 */
enum ia_css_stream_format {
	IA_CSS_STREAM_FORMAT_YUV420_8_LEGACY,    /**< 8 bits per subpixel */
	IA_CSS_STREAM_FORMAT_YUV420_8,  /**< 8 bits per subpixel */
	IA_CSS_STREAM_FORMAT_YUV420_10, /**< 10 bits per subpixel */
	IA_CSS_STREAM_FORMAT_YUV420_16, /**< 16 bits per subpixel */
	IA_CSS_STREAM_FORMAT_YUV422_8,  /**< UYVY..UYVY, 8 bits per subpixel */
	IA_CSS_STREAM_FORMAT_YUV422_10, /**< UYVY..UYVY, 10 bits per subpixel */
	IA_CSS_STREAM_FORMAT_YUV422_16, /**< UYVY..UYVY, 16 bits per subpixel */
	IA_CSS_STREAM_FORMAT_RGB_444,  /**< BGR..BGR, 4 bits per subpixel */
	IA_CSS_STREAM_FORMAT_RGB_555,  /**< BGR..BGR, 5 bits per subpixel */
	IA_CSS_STREAM_FORMAT_RGB_565,  /**< BGR..BGR, 5 bits B and R, 6 bits G */
	IA_CSS_STREAM_FORMAT_RGB_666,  /**< BGR..BGR, 6 bits per subpixel */
	IA_CSS_STREAM_FORMAT_RGB_888,  /**< BGR..BGR, 8 bits per subpixel */
	IA_CSS_STREAM_FORMAT_RAW_6,    /**< RAW data, 6 bits per pixel */
	IA_CSS_STREAM_FORMAT_RAW_7,    /**< RAW data, 7 bits per pixel */
	IA_CSS_STREAM_FORMAT_RAW_8,    /**< RAW data, 8 bits per pixel */
	IA_CSS_STREAM_FORMAT_RAW_10,   /**< RAW data, 10 bits per pixel */
	IA_CSS_STREAM_FORMAT_RAW_12,   /**< RAW data, 12 bits per pixel */
	IA_CSS_STREAM_FORMAT_RAW_14,   /**< RAW data, 14 bits per pixel */
	IA_CSS_STREAM_FORMAT_RAW_16,   /**< RAW data, 16 bits per pixel, which is
					    not specified in CSI-MIPI standard*/
	IA_CSS_STREAM_FORMAT_BINARY_8, /**< Binary byte stream, which is target at
					    JPEG. */

	/** CSI2-MIPI specific format: Generic short packet data. It is used to
	 *  keep the timing information for the opening/closing of shutters,
	 *  triggering of flashes and etc.
	 */
	IA_CSS_STREAM_FORMAT_GENERIC_SHORT1,  /**< Generic Short Packet Code 1 */
	IA_CSS_STREAM_FORMAT_GENERIC_SHORT2,  /**< Generic Short Packet Code 2 */
	IA_CSS_STREAM_FORMAT_GENERIC_SHORT3,  /**< Generic Short Packet Code 3 */
	IA_CSS_STREAM_FORMAT_GENERIC_SHORT4,  /**< Generic Short Packet Code 4 */
	IA_CSS_STREAM_FORMAT_GENERIC_SHORT5,  /**< Generic Short Packet Code 5 */
	IA_CSS_STREAM_FORMAT_GENERIC_SHORT6,  /**< Generic Short Packet Code 6 */
	IA_CSS_STREAM_FORMAT_GENERIC_SHORT7,  /**< Generic Short Packet Code 7 */
	IA_CSS_STREAM_FORMAT_GENERIC_SHORT8,  /**< Generic Short Packet Code 8 */

	/** CSI2-MIPI specific format: YUV data.
	 */
	IA_CSS_STREAM_FORMAT_YUV420_8_SHIFT,  /**< YUV420 8-bit (Chroma Shifted Pixel Sampling) */
	IA_CSS_STREAM_FORMAT_YUV420_10_SHIFT, /**< YUV420 8-bit (Chroma Shifted Pixel Sampling) */

	/** CSI2-MIPI specific format: Generic long packet data
	 */
	IA_CSS_STREAM_FORMAT_EMBEDDED, /**< Embedded 8-bit non Image Data */

	/** CSI2-MIPI specific format: User defined byte-based data. For example,
	 *  the data transmitter (e.g. the SoC sensor) can keep the JPEG data as
	 *  the User Defined Data Type 4 and the MPEG data as the
	 *  User Defined Data Type 7.
	 */
	IA_CSS_STREAM_FORMAT_USER_DEF1,  /**< User defined 8-bit data type 1 */
	IA_CSS_STREAM_FORMAT_USER_DEF2,  /**< User defined 8-bit data type 2 */
	IA_CSS_STREAM_FORMAT_USER_DEF3,  /**< User defined 8-bit data type 3 */
	IA_CSS_STREAM_FORMAT_USER_DEF4,  /**< User defined 8-bit data type 4 */
	IA_CSS_STREAM_FORMAT_USER_DEF5,  /**< User defined 8-bit data type 5 */
	IA_CSS_STREAM_FORMAT_USER_DEF6,  /**< User defined 8-bit data type 6 */
	IA_CSS_STREAM_FORMAT_USER_DEF7,  /**< User defined 8-bit data type 7 */
	IA_CSS_STREAM_FORMAT_USER_DEF8,  /**< User defined 8-bit data type 8 */
};

#define	IA_CSS_STREAM_FORMAT_NUM	IA_CSS_STREAM_FORMAT_USER_DEF8

unsigned int ia_css_util_input_format_bpp(
	enum ia_css_stream_format format,
	bool two_ppc);

#endif /* __IA_CSS_STREAM_FORMAT_H */
