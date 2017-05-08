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

#ifndef __SH_CSS_MIPI_H
#define __SH_CSS_MIPI_H

#include <ia_css_err.h>		  /* ia_css_err */
#include <ia_css_types.h>	  /* ia_css_pipe */
#include <ia_css_stream_public.h> /* ia_css_stream_config */

void
mipi_init(void);

enum ia_css_err
allocate_mipi_frames(struct ia_css_pipe *pipe, struct ia_css_stream_info *info);

enum ia_css_err
free_mipi_frames(struct ia_css_pipe *pipe);

enum ia_css_err
send_mipi_frames(struct ia_css_pipe *pipe);

/**
 * @brief Calculate the required MIPI buffer sizes.
 * Based on the stream configuration, calculate the
 * required MIPI buffer sizes (in DDR words).
 *
 * @param[in]	stream_cfg		Point to the target stream configuration
 * @param[out]	size_mem_words	MIPI buffer size in DDR words.
 *
 * @return
 */
enum ia_css_err
calculate_mipi_buff_size(
		struct ia_css_stream_config *stream_cfg,
		unsigned int *size_mem_words);

#endif /* __SH_CSS_MIPI_H */
