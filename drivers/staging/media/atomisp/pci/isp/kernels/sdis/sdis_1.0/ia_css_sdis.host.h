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

#ifndef __IA_CSS_SDIS_HOST_H
#define __IA_CSS_SDIS_HOST_H

#include "ia_css_sdis_types.h"
#include "ia_css_binary.h"
#include "ia_css_stream.h"
#include "sh_css_params.h"

extern const struct ia_css_dvs_coefficients default_sdis_config;

/* Opaque here, since size is binary dependent. */
struct sh_css_isp_sdis_hori_coef_tbl;
struct sh_css_isp_sdis_vert_coef_tbl;
struct sh_css_isp_sdis_hori_proj_tbl;
struct sh_css_isp_sdis_vert_proj_tbl;

void ia_css_sdis_horicoef_vmem_encode(
    struct sh_css_isp_sdis_hori_coef_tbl *to,
    const struct ia_css_dvs_coefficients *from,
    unsigned int size);

void ia_css_sdis_vertcoef_vmem_encode(
    struct sh_css_isp_sdis_vert_coef_tbl *to,
    const struct ia_css_dvs_coefficients *from,
    unsigned int size);

void ia_css_sdis_horiproj_encode(
    struct sh_css_isp_sdis_hori_proj_tbl *to,
    const struct ia_css_dvs_coefficients *from,
    unsigned int size);

void ia_css_sdis_vertproj_encode(
    struct sh_css_isp_sdis_vert_proj_tbl *to,
    const struct ia_css_dvs_coefficients *from,
    unsigned int size);

void ia_css_get_isp_dis_coefficients(
    struct ia_css_stream *stream,
    short *horizontal_coefficients,
    short *vertical_coefficients);

int
ia_css_get_dvs_statistics(
    struct ia_css_dvs_statistics	       *host_stats,
    const struct ia_css_isp_dvs_statistics *isp_stats);

void
ia_css_translate_dvs_statistics(
    struct ia_css_dvs_statistics               *host_stats,
    const struct ia_css_isp_dvs_statistics_map *isp_stats);

struct ia_css_isp_dvs_statistics *
ia_css_isp_dvs_statistics_allocate(
    const struct ia_css_dvs_grid_info *grid);

void
ia_css_isp_dvs_statistics_free(
    struct ia_css_isp_dvs_statistics *me);

size_t ia_css_sdis_hor_coef_tbl_bytes(const struct ia_css_binary *binary);
size_t ia_css_sdis_ver_coef_tbl_bytes(const struct ia_css_binary *binary);

void
ia_css_sdis_init_info(
    struct ia_css_sdis_info *dis,
    unsigned int sc_3a_dis_width,
    unsigned int sc_3a_dis_padded_width,
    unsigned int sc_3a_dis_height,
    unsigned int isp_pipe_version,
    unsigned int enabled);

void ia_css_sdis_clear_coefficients(
    struct ia_css_dvs_coefficients *dvs_coefs);

void ia_css_sdis_horicoef_debug_dtrace(
    const struct ia_css_dvs_coefficients *config, unsigned int level);

void ia_css_sdis_vertcoef_debug_dtrace(
    const struct ia_css_dvs_coefficients *config, unsigned int level);

void ia_css_sdis_horiproj_debug_dtrace(
    const struct ia_css_dvs_coefficients *config, unsigned int level);

void ia_css_sdis_vertproj_debug_dtrace(
    const struct ia_css_dvs_coefficients *config, unsigned int level);

#endif /* __IA_CSS_SDIS_HOST_H */
