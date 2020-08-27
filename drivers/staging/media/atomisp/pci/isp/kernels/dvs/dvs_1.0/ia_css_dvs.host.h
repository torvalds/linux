/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_DVS_HOST_H
#define __IA_CSS_DVS_HOST_H

#include "ia_css_frame_public.h"
#include "ia_css_binary.h"
#include "sh_css_params.h"

#include "ia_css_types.h"
#include "ia_css_dvs_types.h"
#include "ia_css_dvs_param.h"

/* For bilinear interpolation, we need to add +1 to input block height calculation.
 * For bicubic interpolation, we will need to add +3 instaed */
#define DVS_GDC_BLI_INTERP_ENVELOPE 1
#define DVS_GDC_BCI_INTERP_ENVELOPE 3

void
ia_css_dvs_config(
    struct sh_css_isp_dvs_isp_config      *to,
    const struct ia_css_dvs_configuration *from,
    unsigned int size);

void
ia_css_dvs_configure(
    const struct ia_css_binary     *binary,
    const struct ia_css_frame_info *from);

void
convert_dvs_6axis_config(
    struct ia_css_isp_parameters *params,
    const struct ia_css_binary *binary);

struct ia_css_host_data *
convert_allocate_dvs_6axis_config(
    const struct ia_css_dvs_6axis_config *dvs_6axis_config,
    const struct ia_css_binary *binary,
    const struct ia_css_frame_info *dvs_in_frame_info);

int
store_dvs_6axis_config(
    const struct ia_css_dvs_6axis_config *dvs_6axis_config,
    const struct ia_css_binary *binary,
    const struct ia_css_frame_info *dvs_in_frame_info,
    ia_css_ptr ddr_addr_y);

#endif /* __IA_CSS_DVS_HOST_H */
