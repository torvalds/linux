/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_XNR_HOST_H
#define __IA_CSS_XNR_HOST_H

#include "sh_css_params.h"

#include "ia_css_xnr_param.h"
#include "ia_css_xnr_table.host.h"

extern const struct ia_css_xnr_config default_xnr_config;

void
ia_css_xnr_table_vamem_encode(
    struct sh_css_isp_xnr_vamem_params *to,
    const struct ia_css_xnr_table *from,
    unsigned int size);

void
ia_css_xnr_encode(
    struct sh_css_isp_xnr_params *to,
    const struct ia_css_xnr_config *from,
    unsigned int size);

void
ia_css_xnr_table_debug_dtrace(
    const struct ia_css_xnr_table *s3a,
    unsigned int level);

void
ia_css_xnr_debug_dtrace(
    const struct ia_css_xnr_config *config,
    unsigned int level);

#endif /* __IA_CSS_XNR_HOST_H */
