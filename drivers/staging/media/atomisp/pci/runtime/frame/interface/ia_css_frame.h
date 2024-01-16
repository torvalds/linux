/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
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

#ifndef __IA_CSS_FRAME_H__
#define __IA_CSS_FRAME_H__

/* ISP2401 */
#include <ia_css_types.h>

#include <ia_css_frame_format.h>
#include <ia_css_frame_public.h>
#include "dma.h"

/*********************************************************************
****	Frame INFO APIs
**********************************************************************/
/* @brief Sets the given width and alignment to the frame info
 *
 * @param
 * @param[in]	info        The info to which parameters would set
 * @param[in]	width       The width to be set to info
 * @param[in]	aligned     The aligned to be set to info
 * @return
 */
void ia_css_frame_info_set_width(struct ia_css_frame_info *info,
				 unsigned int width,
				 unsigned int min_padded_width);

/* @brief Sets the given format to the frame info
 *
 * @param
 * @param[in]	info        The info to which parameters would set
 * @param[in]	format      The format to be set to info
 * @return
 */
void ia_css_frame_info_set_format(struct ia_css_frame_info *info,
				  enum ia_css_frame_format format);

/* @brief Sets the frame info with the given parameters
 *
 * @param
 * @param[in]	info        The info to which parameters would set
 * @param[in]	width       The width to be set to info
 * @param[in]	height      The height to be set to info
 * @param[in]	format      The format to be set to info
 * @param[in]	aligned     The aligned to be set to info
 * @return
 */
void ia_css_frame_info_init(struct ia_css_frame_info *info,
			    unsigned int width,
			    unsigned int height,
			    enum ia_css_frame_format format,
			    unsigned int aligned);

/* @brief Checks whether 2 frame infos has the same resolution
 *
 * @param
 * @param[in]	frame_a         The first frame to be compared
 * @param[in]	frame_b         The second frame to be compared
 * @return      Returns true if the frames are equal
 */
bool ia_css_frame_info_is_same_resolution(
    const struct ia_css_frame_info *info_a,
    const struct ia_css_frame_info *info_b);

/* @brief Check the frame info is valid
 *
 * @param
 * @param[in]	info       The frame attributes to be initialized
 * @return	The error code.
 */
int ia_css_frame_check_info(const struct ia_css_frame_info *info);

/*********************************************************************
****	Frame APIs
**********************************************************************/

/* @brief Initialize the plane depending on the frame type
 *
 * @param
 * @param[in]	frame           The frame attributes to be initialized
 * @return	The error code.
 */
int ia_css_frame_init_planes(struct ia_css_frame *frame);

/* @brief Free an array of frames
 *
 * @param
 * @param[in]	num_frames      The number of frames to be freed in the array
 * @param[in]   **frames_array  The array of frames to be removed
 * @return
 */
void ia_css_frame_free_multiple(unsigned int num_frames,
				struct ia_css_frame **frames_array);

/* @brief Allocate a CSS frame structure of given size in bytes..
 *
 * @param	frame	The allocated frame.
 * @param[in]	size_bytes	The frame size in bytes.
 * @return	The error code.
 *
 * Allocate a frame using the given size in bytes.
 * The frame structure is partially null initialized.
 */
int ia_css_frame_allocate_with_buffer_size(struct ia_css_frame **frame,
					   const unsigned int size_bytes);

/* @brief Check whether 2 frames are same type
 *
 * @param
 * @param[in]	frame_a         The first frame to be compared
 * @param[in]	frame_b         The second frame to be compared
 * @return      Returns true if the frames are equal
 */
bool ia_css_frame_is_same_type(
    const struct ia_css_frame *frame_a,
    const struct ia_css_frame *frame_b);

/* @brief Configure a dma port from frame info
 *
 * @param
 * @param[in]	config         The DAM port configuration
 * @param[in]	info           The frame info
 * @return
 */
int ia_css_dma_configure_from_info(struct dma_port_config *config,
				   const struct ia_css_frame_info *info);

#endif /* __IA_CSS_FRAME_H__ */
