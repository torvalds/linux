/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_DVS_TYPES_H
#define __IA_CSS_DVS_TYPES_H

/* DVS frame
 *
 *  ISP block: dvs frame
 */

#include "ia_css_frame_public.h"

struct ia_css_dvs_configuration {
	const struct ia_css_frame_info *info;
};

#endif /* __IA_CSS_DVS_TYPES_H */
