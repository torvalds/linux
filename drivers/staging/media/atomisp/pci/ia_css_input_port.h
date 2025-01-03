/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

/* For MIPI_PORT0_ID to MIPI_PORT2_ID */
#include "system_global.h"

#ifndef __IA_CSS_INPUT_PORT_H
#define __IA_CSS_INPUT_PORT_H

/* @file
 * This file contains information about the possible input ports for CSS
 */

/* Backward compatible for CSS API 2.0 only
 *  TO BE REMOVED when all drivers move to CSS	API 2.1
 */
#define	IA_CSS_CSI2_PORT_4LANE MIPI_PORT0_ID
#define	IA_CSS_CSI2_PORT_1LANE MIPI_PORT1_ID
#define	IA_CSS_CSI2_PORT_2LANE MIPI_PORT2_ID

/* The CSI2 interface supports 2 types of compression or can
 *  be run without compression.
 */
enum ia_css_csi2_compression_type {
	IA_CSS_CSI2_COMPRESSION_TYPE_NONE, /** No compression */
	IA_CSS_CSI2_COMPRESSION_TYPE_1,    /** Compression scheme 1 */
	IA_CSS_CSI2_COMPRESSION_TYPE_2     /** Compression scheme 2 */
};

struct ia_css_csi2_compression {
	enum ia_css_csi2_compression_type type;
	/** Compression used */
	unsigned int                      compressed_bits_per_pixel;
	/** Compressed bits per pixel (only when compression is enabled) */
	unsigned int                      uncompressed_bits_per_pixel;
	/** Uncompressed bits per pixel (only when compression is enabled) */
};

/* Input port structure.
 */
struct ia_css_input_port {
	enum mipi_port_id port; /** Physical CSI-2 port */
	unsigned int num_lanes; /** Number of lanes used (4-lane port only) */
	unsigned int timeout;   /** Timeout value */
	unsigned int rxcount;   /** Register value, should include all lanes */
	struct ia_css_csi2_compression compression; /** Compression used */
};

#endif /* __IA_CSS_INPUT_PORT_H */
