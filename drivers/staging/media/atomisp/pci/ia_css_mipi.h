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

/* Backward compatible for CSS API 2.0 only
 * TO BE REMOVED when all drivers move to CSS API 2.1.
 */
/* @brief Specify a CSS MIPI frame buffer.
 *
 * @param[in]	size_mem_words	The frame size in memory words (32B).
 * @param[in]	contiguous	Allocate memory physically contiguously or not.
 * @return		The error code.
 *
 * \deprecated{Use ia_css_mipi_buffer_config instead.}
 *
 * Specifies a CSS MIPI frame buffer: size in memory words (32B).
 */
int
ia_css_mipi_frame_specify(const unsigned int	size_mem_words,
			  const bool contiguous);

/* @brief Register size of a CSS MIPI frame for check during capturing.
 *
 * @param[in]	port	CSI-2 port this check is registered.
 * @param[in]	size_mem_words	The frame size in memory words (32B).
 * @return		Return the error in case of failure. E.g. MAX_NOF_ENTRIES REACHED
 *
 * Register size of a CSS MIPI frame to check during capturing. Up to
 *		IA_CSS_MIPI_SIZE_CHECK_MAX_NOF_ENTRIES entries per port allowed. Entries are reset
 *		when stream is stopped.
 *
 *
 */
int
ia_css_mipi_frame_enable_check_on_size(const enum mipi_port_id port,
				       const unsigned int	size_mem_words);

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
