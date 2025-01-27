/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_VF_TYPES_H
#define __IA_CSS_VF_TYPES_H

/* Viewfinder decimation
 *
 *  ISP block: vfeven_horizontal_downscale
 */

#include <ia_css_frame_public.h>
#include <type_support.h>

struct ia_css_vf_configuration {
	u32 vf_downscale_bits; /** Log VF downscale value */
	const struct ia_css_frame_info *info;
};

#endif /* __IA_CSS_VF_TYPES_H */
