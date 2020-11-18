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

#ifndef __IA_CSS_SC_HOST_H
#define __IA_CSS_SC_HOST_H

#include "sh_css_params.h"

#include "ia_css_sc_types.h"
#include "ia_css_sc_param.h"

void
ia_css_sc_encode(
    struct sh_css_isp_sc_params *to,
    struct ia_css_shading_table **from,
    unsigned int size);

void
ia_css_sc_dump(
    const struct sh_css_isp_sc_params *sc,
    unsigned int level);

/* @brief Configure the shading correction.
 * @param[out]	to	Parameters used in the shading correction kernel in the isp.
 * @param[in]	from	Parameters passed from the host.
 * @param[in]	size	Size of the sh_css_isp_sc_isp_config structure.
 *
 * This function passes the parameters for the shading correction from the host to the isp.
 */
/* ISP2401 */
void
ia_css_sc_config(
    struct sh_css_isp_sc_isp_config *to,
    const struct ia_css_sc_configuration *from,
    unsigned int size);

/* @brief Configure the shading correction.
 * @param[in]	binary	The binary, which has the shading correction.
 * @param[in]	internal_frame_origin_x_bqs_on_sctbl
 *			X coordinate (in bqs) of the origin of the internal frame on the shading table.
 * @param[in]	internal_frame_origin_y_bqs_on_sctbl
 *			Y coordinate (in bqs) of the origin of the internal frame on the shading table.
 *
 * This function calls the ia_css_configure_sc() function.
 * (The ia_css_configure_sc() function is automatically generated in ia_css_isp.configs.c.)
 * The ia_css_configure_sc() function calls the ia_css_sc_config() function
 * to pass the parameters for the shading correction from the host to the isp.
 */
/* ISP2401 */
void
ia_css_sc_configure(
    const struct ia_css_binary *binary,
    u32 internal_frame_origin_x_bqs_on_sctbl,
    uint32_t internal_frame_origin_y_bqs_on_sctbl);

/* ------ deprecated(bz675) : from ------ */
void
sh_css_get_shading_settings(const struct ia_css_isp_parameters *params,
			    struct ia_css_shading_settings *settings);

void
sh_css_set_shading_settings(struct ia_css_isp_parameters *params,
			    const struct ia_css_shading_settings *settings);
/* ------ deprecated(bz675) : to ------ */

#endif /* __IA_CSS_SC_HOST_H */
