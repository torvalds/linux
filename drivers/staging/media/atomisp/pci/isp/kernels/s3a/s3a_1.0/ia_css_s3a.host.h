/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_S3A_HOST_H
#define __IA_CSS_S3A_HOST_H

#include "ia_css_s3a_types.h"
#include "ia_css_s3a_param.h"
#include "bh/bh_2/ia_css_bh.host.h"

extern const struct ia_css_3a_config default_3a_config;

void
ia_css_s3a_configure(
    unsigned int raw_bit_depth);

void
ia_css_s3a_encode(
    struct sh_css_isp_s3a_params *to,
    const struct ia_css_3a_config *from,
    unsigned int size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_ae_dump(
    const struct sh_css_isp_ae_params *ae,
    unsigned int level);

void
ia_css_awb_dump(
    const struct sh_css_isp_awb_params *awb,
    unsigned int level);

void
ia_css_af_dump(
    const struct sh_css_isp_af_params *af,
    unsigned int level);

void
ia_css_s3a_dump(
    const struct sh_css_isp_s3a_params *s3a,
    unsigned int level);

void
ia_css_s3a_debug_dtrace(
    const struct ia_css_3a_config *config,
    unsigned int level);
#endif

void
ia_css_s3a_hmem_decode(
    struct ia_css_3a_statistics *host_stats,
    const struct ia_css_bh_table *hmem_buf);

void
ia_css_s3a_dmem_decode(
    struct ia_css_3a_statistics *host_stats,
    const struct ia_css_3a_output *isp_stats);

void
ia_css_s3a_vmem_decode(
    struct ia_css_3a_statistics *host_stats,
    const u16 *isp_stats_hi,
    const uint16_t *isp_stats_lo);

#endif /* __IA_CSS_S3A_HOST_H */
