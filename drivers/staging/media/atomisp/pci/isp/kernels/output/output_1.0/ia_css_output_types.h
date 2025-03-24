/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_OUTPUT_TYPES_H
#define __IA_CSS_OUTPUT_TYPES_H

/* @file
* CSS-API header file for parameters of output frames.
*/

/* Output frame
 *
 *  ISP block: output frame
 */

//#include "ia_css_frame_public.h"
struct ia_css_frame_info;

struct ia_css_output_configuration {
	const struct ia_css_frame_info *info;
};

struct ia_css_output0_configuration {
	const struct ia_css_frame_info *info;
};

struct ia_css_output1_configuration {
	const struct ia_css_frame_info *info;
};

struct ia_css_output_config {
	u8 enable_hflip;  /** enable horizontal output mirroring */
	u8 enable_vflip;  /** enable vertical output mirroring */
};

#endif /* __IA_CSS_OUTPUT_TYPES_H */
