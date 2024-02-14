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

#ifndef __IA_CSS_MIPI_H
#define __IA_CSS_MIPI_H

/* @file
 * This file contains MIPI support functionality
 */

#include <type_support.h>
#include "ia_css_err.h"
#include "ia_css_stream_format.h"
#include "ia_css_input_port.h"

/* @brief Calculate the size of a mipi frame.
 *
 * @param[in]	width		The width (in pixels) of the frame.
 * @param[in]	height		The height (in lines) of the frame.
 * @param[in]	format		The frame (MIPI) format.
 * @param[in]	hasSOLandEOL	Whether frame (MIPI) contains (optional) SOL and EOF packets.
 * @param[in]	embedded_data_size_words		Embedded data size in memory words.
 * @param		size_mem_words					The mipi frame size in memory words (32B).
 * @return		The error code.
 *
 * Calculate the size of a mipi frame, based on the resolution and format.
 */
int
ia_css_mipi_frame_calculate_size(const unsigned int width,
				 const unsigned int height,
				 const enum atomisp_input_format format,
				 const bool hasSOLandEOL,
				 const unsigned int embedded_data_size_words,
				 unsigned int *size_mem_words);

#endif /* __IA_CSS_MIPI_H */
