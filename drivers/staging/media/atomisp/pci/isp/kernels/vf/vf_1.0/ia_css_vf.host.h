/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_VF_HOST_H
#define __IA_CSS_VF_HOST_H

#include "ia_css_frame_public.h"
#include "ia_css_binary.h"

#include "ia_css_vf_types.h"
#include "ia_css_vf_param.h"

/* compute the log2 of the downscale factor needed to get closest
 * to the requested viewfinder resolution on the upper side. The output cannot
 * be smaller than the requested viewfinder resolution.
 */
int
sh_css_vf_downscale_log2(
    const struct ia_css_frame_info *out_info,
    const struct ia_css_frame_info *vf_info,
    unsigned int *downscale_log2);

int ia_css_vf_config(struct sh_css_isp_vf_isp_config *to,
		     const struct ia_css_vf_configuration *from,
		     unsigned int size);

int
ia_css_vf_configure(
    const struct ia_css_binary *binary,
    const struct ia_css_frame_info *out_info,
    struct ia_css_frame_info *vf_info,
    unsigned int *downscale_log2);

#endif /* __IA_CSS_VF_HOST_H */
