/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __INPUT_SYSTEM_LOCAL_H_INCLUDED__
#define __INPUT_SYSTEM_LOCAL_H_INCLUDED__

#include "csi_rx.h"
#include "pixelgen.h"
#include "isys_stream2mmio.h"
#include "isys_irq.h"

typedef enum {
	MIPI_FORMAT_SHORT1 = 0x08,
	MIPI_FORMAT_SHORT2,
	MIPI_FORMAT_SHORT3,
	MIPI_FORMAT_SHORT4,
	MIPI_FORMAT_SHORT5,
	MIPI_FORMAT_SHORT6,
	MIPI_FORMAT_SHORT7,
	MIPI_FORMAT_SHORT8,
	MIPI_FORMAT_EMBEDDED = 0x12,
	MIPI_FORMAT_YUV420_8 = 0x18,
	MIPI_FORMAT_YUV420_10,
	MIPI_FORMAT_YUV420_8_LEGACY,
	MIPI_FORMAT_YUV420_8_SHIFT = 0x1C,
	MIPI_FORMAT_YUV420_10_SHIFT,
	MIPI_FORMAT_YUV422_8 = 0x1E,
	MIPI_FORMAT_YUV422_10,
	MIPI_FORMAT_RGB444 = 0x20,
	MIPI_FORMAT_RGB555,
	MIPI_FORMAT_RGB565,
	MIPI_FORMAT_RGB666,
	MIPI_FORMAT_RGB888,
	MIPI_FORMAT_RAW6 = 0x28,
	MIPI_FORMAT_RAW7,
	MIPI_FORMAT_RAW8,
	MIPI_FORMAT_RAW10,
	MIPI_FORMAT_RAW12,
	MIPI_FORMAT_RAW14,
	MIPI_FORMAT_CUSTOM0 = 0x30,
	MIPI_FORMAT_CUSTOM1,
	MIPI_FORMAT_CUSTOM2,
	MIPI_FORMAT_CUSTOM3,
	MIPI_FORMAT_CUSTOM4,
	MIPI_FORMAT_CUSTOM5,
	MIPI_FORMAT_CUSTOM6,
	MIPI_FORMAT_CUSTOM7,
	//MIPI_FORMAT_RAW16, /*not supported by 2401*/
	//MIPI_FORMAT_RAW18,
	N_MIPI_FORMAT
} mipi_format_t;

#define N_MIPI_FORMAT_CUSTOM	8

/* The number of stores for compressed format types */
#define	N_MIPI_COMPRESSOR_CONTEXT	(N_RX_CHANNEL_ID * N_MIPI_FORMAT_CUSTOM)

#endif /* __INPUT_SYSTEM_LOCAL_H_INCLUDED__ */
