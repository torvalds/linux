/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_CROP_HOST_H
#define __IA_CSS_CROP_HOST_H

#include <ia_css_frame_public.h>
#include <ia_css_binary.h>

#include "ia_css_crop_types.h"
#include "ia_css_crop_param.h"

void
ia_css_crop_encode(
    struct sh_css_isp_crop_isp_params *to,
    const struct ia_css_crop_config *from,
    unsigned int size);

int ia_css_crop_config(struct sh_css_isp_crop_isp_config      *to,
		       const struct ia_css_crop_configuration *from,
		       unsigned int size);

int ia_css_crop_configure(const struct ia_css_binary     *binary,
			  const struct ia_css_frame_info *from);

#endif /* __IA_CSS_CROP_HOST_H */
