/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_WB_HOST_H
#define __IA_CSS_WB_HOST_H

#include "ia_css_wb_types.h"
#include "ia_css_wb_param.h"

extern const struct ia_css_wb_config default_wb_config;

void
ia_css_wb_encode(
    struct sh_css_isp_wb_params *to,
    const struct ia_css_wb_config *from,
    unsigned int size);

void
ia_css_wb_dump(
    const struct sh_css_isp_wb_params *wb,
    unsigned int level);

void
ia_css_wb_debug_dtrace(
    const struct ia_css_wb_config *wb,
    unsigned int level);

#endif /* __IA_CSS_WB_HOST_H */
