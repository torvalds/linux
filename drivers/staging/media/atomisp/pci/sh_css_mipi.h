/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __SH_CSS_MIPI_H
#define __SH_CSS_MIPI_H

#include <ia_css_err.h>		  /* ia_css_err */
#include <ia_css_types.h>	  /* ia_css_pipe */
#include <ia_css_stream_public.h> /* ia_css_stream_config */

void
mipi_init(void);

int
allocate_mipi_frames(struct ia_css_pipe *pipe, struct ia_css_stream_info *info);

int
free_mipi_frames(struct ia_css_pipe *pipe);

int
send_mipi_frames(struct ia_css_pipe *pipe);

#endif /* __SH_CSS_MIPI_H */
