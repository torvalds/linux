/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_SDIS2_HOST_H
#define __IA_CSS_SDIS2_HOST_H

#include "ia_css_sdis2_types.h"
#include "ia_css_binary.h"
#include "ia_css_stream.h"
#include "sh_css_params.h"

extern const struct ia_css_dvs2_coefficients default_sdis2_config;

/* Opaque here, since size is binary dependent. */
struct sh_css_isp_sdis_hori_coef_tbl;
struct sh_css_isp_sdis_vert_coef_tbl;
struct sh_css_isp_sdis_hori_proj_tbl;
struct sh_css_isp_sdis_vert_proj_tbl;

void ia_css_sdis2_horicoef_vmem_encode(
    struct sh_css_isp_sdis_hori_coef_tbl *to,
    const struct ia_css_dvs2_coefficients *from,
    unsigned int size);

void ia_css_sdis2_vertcoef_vmem_encode(
    struct sh_css_isp_sdis_vert_coef_tbl *to,
    const struct ia_css_dvs2_coefficients *from,
    unsigned int size);

void ia_css_sdis2_horiproj_encode(
    struct sh_css_isp_sdis_hori_proj_tbl *to,
    const struct ia_css_dvs2_coefficients *from,
    unsigned int size);

void ia_css_sdis2_vertproj_encode(
    struct sh_css_isp_sdis_vert_proj_tbl *to,
    const struct ia_css_dvs2_coefficients *from,
    unsigned int size);

void ia_css_get_isp_dvs2_coefficients(
    struct ia_css_stream *stream,
    short *hor_coefs_odd_real,
    short *hor_coefs_odd_imag,
    short *hor_coefs_even_real,
    short *hor_coefs_even_imag,
    short *ver_coefs_odd_real,
    short *ver_coefs_odd_imag,
    short *ver_coefs_even_real,
    short *ver_coefs_even_imag);

void ia_css_sdis2_clear_coefficients(
    struct ia_css_dvs2_coefficients *dvs2_coefs);

int
ia_css_get_dvs2_statistics(
    struct ia_css_dvs2_statistics	       *host_stats,
    const struct ia_css_isp_dvs_statistics *isp_stats);

void
ia_css_translate_dvs2_statistics(
    struct ia_css_dvs2_statistics              *host_stats,
    const struct ia_css_isp_dvs_statistics_map *isp_stats);

struct ia_css_isp_dvs_statistics *
ia_css_isp_dvs2_statistics_allocate(
    const struct ia_css_dvs_grid_info *grid);

void
ia_css_isp_dvs2_statistics_free(
    struct ia_css_isp_dvs_statistics *me);

void ia_css_sdis2_horicoef_debug_dtrace(
    const struct ia_css_dvs2_coefficients *config, unsigned int level);

void ia_css_sdis2_vertcoef_debug_dtrace(
    const struct ia_css_dvs2_coefficients *config, unsigned int level);

void ia_css_sdis2_horiproj_debug_dtrace(
    const struct ia_css_dvs2_coefficients *config, unsigned int level);

void ia_css_sdis2_vertproj_debug_dtrace(
    const struct ia_css_dvs2_coefficients *config, unsigned int level);

#endif /* __IA_CSS_SDIS2_HOST_H */
