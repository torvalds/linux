/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_FPN_HOST_H
#define __IA_CSS_FPN_HOST_H

#include "ia_css_binary.h"
#include "ia_css_fpn_types.h"
#include "ia_css_fpn_param.h"

void
ia_css_fpn_encode(
    struct sh_css_isp_fpn_params *to,
    const struct ia_css_fpn_table *from,
    unsigned int size);

void
ia_css_fpn_dump(
    const struct sh_css_isp_fpn_params *fpn,
    unsigned int level);

int ia_css_fpn_config(struct sh_css_isp_fpn_isp_config      *to,
		      const struct ia_css_fpn_configuration *from,
		      unsigned int size);

int ia_css_fpn_configure(const struct ia_css_binary     *binary,
			 const struct ia_css_frame_info *from);

#endif /* __IA_CSS_FPN_HOST_H */
