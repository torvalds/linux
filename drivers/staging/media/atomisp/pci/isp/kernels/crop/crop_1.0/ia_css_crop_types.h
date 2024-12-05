/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_CROP_TYPES_H
#define __IA_CSS_CROP_TYPES_H

/* Crop frame
 *
 *  ISP block: crop frame
 */

#include <ia_css_frame_public.h>
#include "sh_css_uds.h" /* sh_css_crop_pos */

struct ia_css_crop_config {
	struct sh_css_crop_pos crop_pos;
};

struct ia_css_crop_configuration {
	const struct ia_css_frame_info *info;
};

#endif /* __IA_CSS_CROP_TYPES_H */
