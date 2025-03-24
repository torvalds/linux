/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_TDF_HOST_H
#define __IA_CSS_TDF_HOST_H

#include "ia_css_tdf_types.h"
#include "ia_css_tdf_param.h"

void
ia_css_tdf_vmem_encode(
    struct ia_css_isp_tdf_vmem_params *to,
    const struct ia_css_tdf_config *from,
    size_t size);

void
ia_css_tdf_encode(
    struct ia_css_isp_tdf_dmem_params *to,
    const struct ia_css_tdf_config *from,
    size_t size);

void
ia_css_tdf_debug_dtrace(
    const struct ia_css_tdf_config *config, unsigned int level)
;

#endif /* __IA_CSS_TDF_HOST_H */
