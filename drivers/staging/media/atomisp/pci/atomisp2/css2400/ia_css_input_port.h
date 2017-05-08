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

#ifndef __IA_CSS_INPUT_PORT_H
#define __IA_CSS_INPUT_PORT_H

/** @file
 * This file contains information about the possible input ports for CSS
 */

/** Enumeration of the physical input ports on the CSS hardware.
 *  There are 3 MIPI CSI-2 ports.
 */
enum ia_css_csi2_port {
	IA_CSS_CSI2_PORT0, /* Implicitly map to MIPI_PORT0_ID */
	IA_CSS_CSI2_PORT1, /* Implicitly map to MIPI_PORT1_ID */
	IA_CSS_CSI2_PORT2  /* Implicitly map to MIPI_PORT2_ID */
};

/** Backward compatible for CSS API 2.0 only
 *  TO BE REMOVED when all drivers move to CSS	API 2.1
 */
#define	IA_CSS_CSI2_PORT_4LANE IA_CSS_CSI2_PORT0
#define	IA_CSS_CSI2_PORT_1LANE IA_CSS_CSI2_PORT1
#define	IA_CSS_CSI2_PORT_2LANE IA_CSS_CSI2_PORT2

/** The CSI2 interface supports 2 types of compression or can
 *  be run without compression.
 */
enum ia_css_csi2_compression_type {
	IA_CSS_CSI2_COMPRESSION_TYPE_NONE, /**< No compression */
	IA_CSS_CSI2_COMPRESSION_TYPE_1,    /**< Compression scheme 1 */
	IA_CSS_CSI2_COMPRESSION_TYPE_2     /**< Compression scheme 2 */
};

struct ia_css_csi2_compression {
	enum ia_css_csi2_compression_type type;
	/**< Compression used */
	unsigned int                      compressed_bits_per_pixel;
	/**< Compressed bits per pixel (only when compression is enabled) */
	unsigned int                      uncompressed_bits_per_pixel;
	/**< Uncompressed bits per pixel (only when compression is enabled) */
};

/** Input port structure.
 */
struct ia_css_input_port {
	enum ia_css_csi2_port port; /**< Physical CSI-2 port */
	unsigned int num_lanes; /**< Number of lanes used (4-lane port only) */
	unsigned int timeout;   /**< Timeout value */
	unsigned int rxcount;   /**< Register value, should include all lanes */
	struct ia_css_csi2_compression compression; /**< Compression used */
};

#endif /* __IA_CSS_INPUT_PORT_H */
